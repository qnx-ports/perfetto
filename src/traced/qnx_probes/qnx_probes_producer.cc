/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an "AS
 * IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 */
#include "src/traced/qnx_probes/qnx_probes_producer.h"

#include <stdio.h>
#include <sys/stat.h>

#include <memory>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/watchdog.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/ipc/producer_ipc_client.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/forward_decls.h"

#include "src/traced/qnx_probes/kernel/kernel_data_source.h"

namespace perfetto {
namespace {

constexpr uint32_t kInitialConnectionBackoffMs = 100;
constexpr uint32_t kMaxConnectionBackoffMs = 30 * 1000;

// Should be larger than FtraceController::kControllerFlushTimeoutMs.
constexpr uint32_t kFlushTimeoutMs = 1000;

constexpr size_t kTracingSharedMemSizeHintBytes = 1024 * 1024;
constexpr size_t kTracingSharedMemPageSizeHintBytes = 32 * 1024;

}  // namespace

// State transition diagram:
//                    +----------------------------+
//                    v                            +
// NotStarted -> NotConnected -> Connecting -> Connected
//                    ^              +
//                    +--------------+
//

QnxProbesProducer* QnxProbesProducer::instance_ = nullptr;

QnxProbesProducer* QnxProbesProducer::GetInstance() {
  return instance_;
}

QnxProbesProducer::QnxProbesProducer() : weak_factory_(this) {
  PERFETTO_CHECK(instance_ == nullptr);
  instance_ = this;
}

QnxProbesProducer::~QnxProbesProducer() {
  instance_ = nullptr;
  data_sources_.clear();
}

void QnxProbesProducer::Restart() {
  // We lost the connection with the tracing service. At this point we need
  // to reset all the data sources. Trying to handle that manually is going to
  // be error prone. What we do here is simply destroying the instance and
  // recreating it again.

  base::TaskRunner* task_runner = task_runner_;
  const char* socket_name = socket_name_;

  // Invoke destructor and then the constructor again.
  this->~QnxProbesProducer();
  new (this) QnxProbesProducer();

  ConnectWithRetries(socket_name, task_runner);
}

template <>
std::unique_ptr<QnxProbesDataSource>
QnxProbesProducer::CreateDSInstance<qnx::KernelDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::unique_ptr<qnx::KernelDataSource>(new qnx::KernelDataSource(
      session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall),
      config));
}

// Another anonymous namespace. This cannot be moved into the anonymous
// namespace on top (it would fail to compile), because the CreateDSInstance
// methods need to be fully declared before.
namespace {

using ProbesDataSourceFactoryFunc = std::unique_ptr<QnxProbesDataSource> (
    QnxProbesProducer::*)(TracingSessionID, const DataSourceConfig&);

struct DataSourceTraits {
  const QnxProbesDataSource::Descriptor* descriptor;
  ProbesDataSourceFactoryFunc factory_func;
};

template <typename T>
constexpr DataSourceTraits Ds() {
  return DataSourceTraits{&T::descriptor,
                          &QnxProbesProducer::CreateDSInstance<T>};
}

constexpr const DataSourceTraits kAllDataSources[] = {
    Ds<qnx::KernelDataSource>(),
};

}  // namespace

void QnxProbesProducer::OnConnect() {
  PERFETTO_DCHECK(state_ == kConnecting);
  state_ = kConnected;
  ResetConnectionBackoff();
  PERFETTO_LOG("Connected to the service");

  std::array<DataSourceDescriptor, base::ArraySize(kAllDataSources)>
      proto_descs;

  // Generate all data source descriptors.
  for (size_t i = 0; i < proto_descs.size(); i++) {
    DataSourceDescriptor& proto_desc = proto_descs[i];
    const QnxProbesDataSource::Descriptor* desc = kAllDataSources[i].descriptor;
    for (size_t j = i + 1; j < proto_descs.size(); j++) {
      if (kAllDataSources[i].descriptor == kAllDataSources[j].descriptor) {
        PERFETTO_FATAL("Duplicate descriptor name %s",
                       kAllDataSources[i].descriptor->name);
      }
    }

    proto_desc.set_name(desc->name);
    proto_desc.set_will_notify_on_start(true);
    proto_desc.set_will_notify_on_stop(true);
    using Flags = QnxProbesDataSource::Descriptor::Flags;
    if (desc->flags & Flags::kHandlesIncrementalState)
      proto_desc.set_handles_incremental_state_clear(true);
    if (desc->fill_descriptor_func) {
      desc->fill_descriptor_func(&proto_desc);
    }
  }

  // Register all the data sources. Separate from the above loop because, if
  // generating a data source descriptor takes too long, we don't want to be in
  // a state where only some data sources are registered.
  for (const DataSourceDescriptor& proto_desc : proto_descs) {
    endpoint_->RegisterDataSource(proto_desc);
  }

  // Used by tracebox to synchronize with traced_probes being registered.
  if (all_data_sources_registered_cb_) {
    endpoint_->Sync(all_data_sources_registered_cb_);
  }
}

void QnxProbesProducer::OnDisconnect() {
  PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
  PERFETTO_LOG("Disconnected from tracing service");

  if (state_ == kConnected) {
    return task_runner_->PostTask([this] { this->Restart(); });
  }

  state_ = kNotConnected;
  IncreaseConnectionBackoff();
  task_runner_->PostDelayedTask([this] { this->Connect(); },
                                connection_backoff_ms_);
}

void QnxProbesProducer::SetupDataSource(DataSourceInstanceID instance_id,
                                        const DataSourceConfig& config) {
  PERFETTO_DLOG("SetupDataSource(id=%" PRIu64 ", name=%s)", instance_id,
                config.name().c_str());
  PERFETTO_DCHECK(data_sources_.count(instance_id) == 0);
  TracingSessionID session_id = config.tracing_session_id();
  PERFETTO_CHECK(session_id > 0);

  std::unique_ptr<QnxProbesDataSource> data_source;

  for (const DataSourceTraits& rds : kAllDataSources) {
    if (rds.descriptor->name != config.name()) {
      continue;
    }
    data_source = (this->*(rds.factory_func))(session_id, config);
    break;
  }

  if (!data_source) {
    PERFETTO_ELOG("Failed to create data source '%s'", config.name().c_str());
    return;
  }

  session_data_sources_[session_id].emplace(data_source->descriptor,
                                            data_source.get());
  data_sources_[instance_id] = std::move(data_source);
}

void QnxProbesProducer::StartDataSource(DataSourceInstanceID instance_id,
                                        const DataSourceConfig& config) {
  PERFETTO_DLOG("StartDataSource(id=%" PRIu64 ", name=%s)", instance_id,
                config.name().c_str());
  auto it = data_sources_.find(instance_id);
  if (it == data_sources_.end()) {
    // Can happen if SetupDataSource() failed (e.g. ftrace was busy).
    PERFETTO_ELOG("Data source id=%" PRIu64 " not found", instance_id);
    return;
  }

  QnxProbesDataSource* data_source = it->second.get();
  if (data_source->started) {
    return;
  }

  if (config.trace_duration_ms() != 0) {
    // We need to ensure this timeout is worse than the worst case
    // time from us starting to traced managing to disable us.
    // See b/236814186#comment8 for context
    // Note: when using prefer_suspend_clock_for_duration the actual duration
    // might be < timeout measured in in wall time. But this is fine
    // because the resulting timeout will be conservative (it will be accurate
    // if the device never suspends, and will be more lax if it does).
    uint32_t timeout =
        2 * (kDefaultFlushTimeoutMs + config.trace_duration_ms() +
             config.stop_timeout_ms());
    watchdogs_.emplace(
        instance_id, base::Watchdog::GetInstance()->CreateFatalTimer(
                         timeout, base::WatchdogCrashReason::kTraceDidntStop));
  }
  data_source->started = true;
  data_source->Start();
  endpoint_->NotifyDataSourceStarted(instance_id);
}

void QnxProbesProducer::StopDataSource(DataSourceInstanceID id) {
  PERFETTO_LOG("Producer stop (id=%" PRIu64 ")", id);
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    // Can happen if SetupDataSource() failed (e.g. ftrace was busy).
    PERFETTO_ELOG("Cannot stop data source id=%" PRIu64 ", not found", id);
    return;
  }
  QnxProbesDataSource* data_source = it->second.get();

  TracingSessionID session_id = data_source->tracing_session_id;

  auto session_it = session_data_sources_.find(session_id);
  if (session_it != session_data_sources_.end()) {
    auto desc_range = session_it->second.equal_range(data_source->descriptor);
    for (auto ds_it = desc_range.first; ds_it != desc_range.second; ds_it++) {
      if (ds_it->second == data_source) {
        session_it->second.erase(ds_it);
        if (session_it->second.empty()) {
          session_data_sources_.erase(session_it);
        }
        break;
      }
    }
  }
  data_sources_.erase(it);
  watchdogs_.erase(id);

  // We could (and used to) acknowledge the stop before tearing the local state
  // down, allowing the tracing service and the consumer to carry on quicker.
  // However in the case of tracebox, the traced_probes subprocess gets killed
  // as soon as the trace is considered finished (i.e. all data source stops
  // were acked), and therefore the kill would race against the tracefs
  // cleanup.
  endpoint_->NotifyDataSourceStopped(id);
}

void QnxProbesProducer::OnTracingSetup() {
  // shared_memory() can be null in test environments when running in-process.
  if (endpoint_->shared_memory()) {
    base::Watchdog::GetInstance()->SetMemoryLimit(
        endpoint_->shared_memory()->size() + base::kWatchdogDefaultMemorySlack,
        base::kWatchdogDefaultMemoryWindow);
  }
}

void QnxProbesProducer::Flush(FlushRequestID flush_request_id,
                              const DataSourceInstanceID* data_source_ids,
                              size_t num_data_sources,
                              FlushFlags) {
  PERFETTO_DLOG("QnxProbesProducer::Flush(%" PRIu64 ") begin",
                flush_request_id);
  PERFETTO_DCHECK(flush_request_id);
  auto log_on_exit = base::OnScopeExit([&] {
    PERFETTO_DLOG("QnxProbesProducer::Flush(%" PRIu64 ") end",
                  flush_request_id);
  });

  // Issue a Flush() to all started data sources.
  std::vector<std::pair<DataSourceInstanceID, QnxProbesDataSource*>>
      ds_to_flush;
  for (size_t i = 0; i < num_data_sources; i++) {
    DataSourceInstanceID ds_id = data_source_ids[i];
    auto it = data_sources_.find(ds_id);
    if (it == data_sources_.end() || !it->second->started)
      continue;
    pending_flushes_.emplace(flush_request_id, ds_id);
    ds_to_flush.emplace_back(ds_id, it->second.get());
  }

  // If there is nothing to flush, ack immediately.
  if (ds_to_flush.empty()) {
    endpoint_->NotifyFlushComplete(flush_request_id);
    return;
  }

  // Otherwise post the timeout task and issue all flushes in order.
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, flush_request_id] {
        if (weak_this)
          weak_this->OnFlushTimeout(flush_request_id);
      },
      kFlushTimeoutMs);

  // Issue all the flushes in order. We do this in a separate loop to deal with
  // the case of data sources invoking the callback synchronously (b/295189870).
  for (const auto& kv : ds_to_flush) {
    const DataSourceInstanceID ds_id = kv.first;
    QnxProbesDataSource* const data_source = kv.second;
    auto flush_callback = [weak_this, flush_request_id, ds_id] {
      if (weak_this)
        weak_this->OnDataSourceFlushComplete(flush_request_id, ds_id);
    };
    PERFETTO_DLOG("Flushing data source %" PRIu64 " %s", ds_id,
                  data_source->descriptor->name);
    data_source->Flush(flush_request_id, flush_callback);
  }
}

void QnxProbesProducer::OnDataSourceFlushComplete(
    FlushRequestID flush_request_id,
    DataSourceInstanceID ds_id) {
  PERFETTO_DLOG("Flush %" PRIu64 " acked by data source %" PRIu64,
                flush_request_id, ds_id);
  auto range = pending_flushes_.equal_range(flush_request_id);
  for (auto it = range.first; it != range.second; it++) {
    if (it->second == ds_id) {
      pending_flushes_.erase(it);
      break;
    }
  }

  if (pending_flushes_.count(flush_request_id))
    return;  // Still waiting for other data sources to ack.

  PERFETTO_DLOG("All data sources acked to flush %" PRIu64, flush_request_id);
  endpoint_->NotifyFlushComplete(flush_request_id);
}

void QnxProbesProducer::OnFlushTimeout(FlushRequestID flush_request_id) {
  if (pending_flushes_.count(flush_request_id) == 0)
    return;  // All acked.
  PERFETTO_ELOG("Flush(%" PRIu64 ") timed out", flush_request_id);
  pending_flushes_.erase(flush_request_id);
  endpoint_->NotifyFlushComplete(flush_request_id);
}

void QnxProbesProducer::ClearIncrementalState(
    const DataSourceInstanceID* data_source_ids,
    size_t num_data_sources) {
  for (size_t i = 0; i < num_data_sources; i++) {
    DataSourceInstanceID ds_id = data_source_ids[i];
    auto it = data_sources_.find(ds_id);
    if (it == data_sources_.end() || !it->second->started)
      continue;

    it->second->ClearIncrementalState();
  }
}

void QnxProbesProducer::ConnectWithRetries(const char* socket_name,
                                           base::TaskRunner* task_runner) {
  PERFETTO_DCHECK(state_ == kNotStarted);
  state_ = kNotConnected;

  ResetConnectionBackoff();
  socket_name_ = socket_name;
  task_runner_ = task_runner;
  Connect();
}

void QnxProbesProducer::Connect() {
  PERFETTO_DCHECK(state_ == kNotConnected);
  state_ = kConnecting;
  endpoint_ = ProducerIPCClient::Connect(
      socket_name_, this, "perfetto.traced_probes", task_runner_,
      TracingService::ProducerSMBScrapingMode::kDisabled,
      kTracingSharedMemSizeHintBytes, kTracingSharedMemPageSizeHintBytes);
}

void QnxProbesProducer::IncreaseConnectionBackoff() {
  connection_backoff_ms_ *= 2;
  if (connection_backoff_ms_ > kMaxConnectionBackoffMs)
    connection_backoff_ms_ = kMaxConnectionBackoffMs;
}

void QnxProbesProducer::ResetConnectionBackoff() {
  connection_backoff_ms_ = kInitialConnectionBackoffMs;
}

void QnxProbesProducer::ActivateTrigger(std::string trigger) {
  task_runner_->PostTask(
      [this, trigger]() { endpoint_->ActivateTriggers({trigger}); });
}

}  // namespace perfetto
