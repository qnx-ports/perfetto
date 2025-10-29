/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/traced/qnx_probes/kernel/kernel_data_source.h"

extern "C" {
#include <sys/neutrino.h>
}

#include "perfetto/base/logging.h"

namespace perfetto {
namespace qnx {

// static
const QnxProbesDataSource::Descriptor KernelDataSource::descriptor = {
    /* name */ "qnx.kernel",
    /* flags */ Descriptor::kFlagsNone,
    /* fill_descriptor_func */ nullptr,
};

KernelDataSource::KernelDataSource(TracingSessionID session_id,
                                   std::unique_ptr<TraceWriter> writer,
                                   const DataSourceConfig& ds_config)
    : QnxProbesDataSource(session_id, &descriptor),
      writer_(std::move(writer)),
      config_(),
      trace_thread_(),
      trace_handler_() {
  PERFETTO_LOG("KernelDataSource CREATED");
  config_.LoadFromDataSourceConfig(ds_config);
}

KernelDataSource::~KernelDataSource() {
  PERFETTO_LOG("KernelDataSource DESTROYED");
  if (trace_thread_.joinable()) {
    if (std::shared_ptr<TraceHandler> trace_handler = trace_handler_.lock()) {
      trace_handler->Stop();
      PERFETTO_LOG("TraceHandler STOPPED");
    }
    trace_thread_.join();
    PERFETTO_LOG("KernelDataSource JOINED");
  }
}

void KernelDataSource::Start() {
  PERFETTO_LOG("KernelDataSource STARTED");
  trace_thread_ = std::thread([this]() { this->Trace(); });
}

void KernelDataSource::Flush(FlushRequestID, std::function<void()> callback) {
  PERFETTO_LOG("KernelDataSource FLUSHING");

  if (std::shared_ptr<TraceHandler> trace_handler = trace_handler_.lock()) {
    trace_handler->Flush(callback);
    PERFETTO_LOG("KernelDataSource FLUSHED");
  } else {
    PERFETTO_LOG("Unable to flush dead TraceHandler");
  }
}

void KernelDataSource::Trace() {
  PERFETTO_LOG("KernelDataSource TRACING");
  // tracelog uses spin locks which require elevated IO privedges
  ThreadCtl(_NTO_TCTL_IO_LEVEL, (void*)(_NTO_IO_LEVEL_1));

  std::shared_ptr<TraceHandler> trace_handler =
      std::make_shared<TraceHandler>(writer_, config_);
  trace_handler_ = trace_handler;
  trace_handler->Start();

  ThreadCtl(_NTO_TCTL_IO_LEVEL, (void*)_NTO_IO_LEVEL_NONE);
}

}  // namespace qnx
}  // namespace perfetto
