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

#include "src/traced/qnx_probes/kernel/trace_handler.h"

extern "C" {
#include <sys/kercalls.h>
#include <sys/neutrino.h>
#include <sys/states.h>
#include <sys/syspage.h>
#include <sys/tracelog.h>
#include <sys/uio.h>
}

#include <cinttypes>
#include <iostream>
#include <numeric>
#include <sstream>

#include "perfetto/base/logging.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#include "src/traced/qnx_probes/kernel/parser/trace_print.h"

using TaskState =
    perfetto::protos::pbzero::GenericKernelTaskStateEvent_TaskStateEnum;

namespace perfetto {
namespace qnx {

/**
 * Pushes data from tracelog into the parser. TraceHandler registers this
 * C-style method with the tracelog system such that it is called anytime the
 * kernel tracing system has a buffer of events available to process.
 *
 * @param cookie The pointer to the TraceParser instance that should handle the
 *               trace data.
 * @param data The pointer to the available block of trace data.
 * @param sz The number of bytes of available trace data.
 *
 * @returns The number of bytes the function handled. NOTE: This always returns
 *          the full size of the data passed in.
 */
ssize_t tlcb_write(void* cookie, const char* data, size_t sz) {
  if (cookie == nullptr) {
    return -1;
  }

  ssize_t rc = 0;
  if (sz > 0) {
    TraceParser* parser = reinterpret_cast<TraceParser*>(cookie);
    rc = parser->Parse(sz, (void*)data);
  }

  return rc;
}

TraceHandler::TraceHandler(std::shared_ptr<TraceWriter> writer)
    : TraceHandler(writer, {}) {}
TraceHandler::TraceHandler(std::shared_ptr<TraceWriter> writer,
                           const KernelTraceConfig& config)
    : process_info_cache_(),
      tracelogger_(nullptr),
      writer_(writer),
      writer_mutex_(),
      parser_(config.cache_pages_,
              config.cache_max_pages_,
              config.trace_buffer_init_bytes_) {
  int persist_kbuffers = 0;
  int reuse_kbuffers = 0;
  paddr_t kernel_buffers_paddr = ((paddr_t)~0);

  // Register callbacks for the trace events we want to handle.
  // PROCESS EVENTS
  auto proc_ctor_cb = std::bind(&TraceHandler::HandleProcessCreated, this,
                                std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3, std::placeholders::_4);
  parser_.Register(proc_ctor_cb, _NTO_TRACE_PROCESS, _NTO_TRACE_PROCCREATE);

  auto proc_name_cb = std::bind(&TraceHandler::HandleProcessNamed, this,
                                std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3, std::placeholders::_4);
  parser_.Register(proc_name_cb, _NTO_TRACE_PROCESS,
                   _NTO_TRACE_PROCCREATE_NAME);

  auto proc_dtor_cb = std::bind(&TraceHandler::HandleProcessDestroy, this,
                                std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3, std::placeholders::_4);
  parser_.Register(proc_dtor_cb, _NTO_TRACE_PROCESS, _NTO_TRACE_PROCDESTROY);

  // THREAD EVENTS
  auto thd_ctor_cb = std::bind(&TraceHandler::HandleThreadCreated, this,
                               std::placeholders::_1, std::placeholders::_2,
                               std::placeholders::_3, std::placeholders::_4);
  parser_.Register(thd_ctor_cb, _NTO_TRACE_THREAD, _NTO_TRACE_THCREATE);

  auto thd_update_cb = std::bind(&TraceHandler::HandleThreadStatusUpdated, this,
                                 std::placeholders::_1, std::placeholders::_2,
                                 std::placeholders::_3, std::placeholders::_4);
  parser_.Register(thd_update_cb, _NTO_TRACE_THREAD, _NTO_TRACE_THRUNNING,
                   _NTO_TRACE_THNET_REPLY);

  auto thd_name_cb = std::bind(&TraceHandler::HandleThreadNamed, this,
                               std::placeholders::_1, std::placeholders::_2,
                               std::placeholders::_3, std::placeholders::_4);
  parser_.Register(thd_name_cb, _NTO_TRACE_PROCESS, _NTO_TRACE_PROCTHREAD_NAME);

  auto thd_dead_cb = std::bind(&TraceHandler::HandleThreadDead, this,
                               std::placeholders::_1, std::placeholders::_2,
                               std::placeholders::_3, std::placeholders::_4);
  parser_.Register(thd_dead_cb, _NTO_TRACE_THREAD, _NTO_TRACE_THDEAD);

  auto thd_dtor_cb = std::bind(&TraceHandler::HandleThreadDestroy, this,
                               std::placeholders::_1, std::placeholders::_2,
                               std::placeholders::_3, std::placeholders::_4);
  parser_.Register(thd_dtor_cb, _NTO_TRACE_THREAD, _NTO_TRACE_THDESTROY);

  // On Start callback
  auto sys_page_cb = std::bind(&TraceHandler::HandleSysPageFinished, this,
                               std::placeholders::_1, std::placeholders::_2,
                               std::placeholders::_3, std::placeholders::_4);
  parser_.Register(sys_page_cb, _NTO_TRACE_EMPTY, _NTO_TRACE_EMPTYEVENT);

  /* This while loop is used as a means to easily break on some error and
   * return from single place. */
  int rc = 0;
  do {
    rc = tracelog_instance_init(&tracelogger_);
    if (rc) {
      PERFETTO_LOG("tracelog_instance_init failed(rc=%d): %s", rc,
                   strerror(rc));
      break;
    }

    rc = tracelog_acquire();
    if (rc) {
      PERFETTO_LOG("tracelog_acquire failed(rc=%d): %s", rc, strerror(rc));
      break;
    }

    // Configure the tracelogger and assign the tlcb_write callback to be
    // invoked when trace data is ready.
    rc = tracelog_setkbuffers(tracelogger_, kernel_buffers_paddr,
                              config.num_kbuffers_, persist_kbuffers,
                              reuse_kbuffers);
    if (rc) {
      PERFETTO_LOG("tracelog_setkbuffers failed(rc=%d): %s", rc, strerror(rc));
      break;
    }

    rc = tracelog_setbuffers(tracelogger_, nullptr, config.num_buffers_);
    if (rc) {
      PERFETTO_LOG("tracelog_setbuffers failed(rc=%d): %s", rc, strerror(rc));
      break;
    }

    rc = tracelog_setwriter(tracelogger_, tlcb_write, NULL, (void*)(&parser_));
    if (rc) {
      PERFETTO_LOG("tracelog_setwriter failed(rc=%d): %s", rc, strerror(rc));
      break;
    }

    // Clear existing events.
    TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_KERCALL);
    TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_KERCALL);
    TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_THREAD);
    TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_THREAD);
    TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_VTHREAD);
    TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_VTHREAD);
    TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_COMM);
    TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_COMM);
    TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_PROCESS);
    TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_PROCESS);

    // Configure event filters. We only add the bareminimum event classes to
    // reduce spam of unwanted events.
    TraceEvent(_NTO_TRACE_DELALLCLASSES);
    TraceEvent(_NTO_TRACE_ADDCLASS, _NTO_TRACE_THREAD);
    TraceEvent(_NTO_TRACE_ADDCLASS, _NTO_TRACE_PROCESS);
    TraceEvent(_NTO_TRACE_ADDEVENT, _NTO_TRACE_CONTROL, _NTO_TRACE_CONTROLTIME);

    if (config.wide_events_) {
      PERFETTO_LOG("Enabled wide tracelog events");
      TraceEvent(_NTO_TRACE_SETALLCLASSESWIDE);
    } else {
      PERFETTO_LOG("Disabled wide tracelog events");
      TraceEvent(_NTO_TRACE_SETALLCLASSESFAST);
    }
  } while (false);
  if (rc) {
    PERFETTO_LOG("Initialization failed");
  }

  // Clock Skew is only needed for QNX7.1 as QNX8.0 MONOTONIC_CLOCK is based on
  // ClockCycles().
  CalculateCyclesToMonoClockSkew();

  PERFETTO_LOG("TraceHandler INITIALIZED");
}

TraceHandler::~TraceHandler() {
  int rc = EOK;

  TraceEvent(_NTO_TRACE_DELALLCLASSES);
  if (tracelogger_) {
    rc = tracelog_instance_destroy(tracelogger_);
    if (rc) {
      PERFETTO_LOG("tracelog_instance_destroy failed(rc=%d): %s", rc,
                   strerror(rc));
    }
  }

  rc = tracelog_release();
  if (rc) {
    PERFETTO_LOG("tracelog_release failed(rc=%d): %s", rc, strerror(rc));
  }
  PERFETTO_LOG("TraceHandler DESTROYED");
}

void TraceHandler::Start() {
  if (tracelogger_ == nullptr) {
    PERFETTO_LOG("Failed to start TraceHandler: tracelogger is NULL");
    return;
  }
  PERFETTO_LOG("TraceHandler STARTED");
  tracelog_start(tracelogger_, 1);
}

void TraceHandler::Stop() {
  if (tracelogger_ == nullptr) {
    PERFETTO_LOG("Failed to stop TraceHandler: tracelogger is NULL");
    return;
  }

  // Stop trace logger and wait for it to flush all it's buffers into the event
  // reader.
  tracelog_stop(tracelogger_);
  tracelog_wait();

  // Now flush the parser for any cached events.
  parser_.Finish();

  const auto& stats = parser_.getTraceStats();
  PERFETTO_LOG("TraceHandler STOPPED");
  PERFETTO_LOG(
      "Parser Stats {kernel_events_decoded: %lu, callback_hits: %lu, "
      "callback_miss: %lu, writer_dropped_packets: %lu}",
      stats.event_parsed_, stats.callback_call_count_,
      stats.callback_miss_count_, writer_->drop_count());
}

void TraceHandler::Flush(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(writer_mutex_);
  writer_->Flush(callback);
}

int TraceHandler::HandleSysPageFinished(std::uint32_t header,
                                        std::uint64_t timestamp,
                                        const std::uint32_t* data,
                                        std::size_t data_size) {
  (void)header;
  (void)timestamp;
  (void)data;
  (void)data_size;

  bool use_global_clock = (_SYSPAGE_ENTRY(parser_.GetSysPage(), qtime)->flags &
                           QTIME_FLAG_GLOBAL_CLOCKCYCLES) != 0;

  PERFETTO_LOG("TraceHandler SYSPAGE_PARSED");
  PERFETTO_LOG(
      "%s", (use_global_clock) ? "Using global clock" : "Using per CPU clock");
  {
    std::stringstream ss;
    ss << parser_.GetTraceHeader();
    PERFETTO_LOG("%s", ss.str().c_str());
  }

  return 0;
}

int TraceHandler::HandleThreadStatusUpdated(std::uint32_t header,
                                            std::uint64_t timestamp,
                                            const std::uint32_t* data,
                                            std::size_t data_size) {
  if (data_size < 2) {
    return -1;
  }

  auto ev = _NTO_TRACE_GETEVENT(header);
  auto cpu = _NTO_TRACE_GETCPU(header);
  std::int32_t pid = data[0];
  std::int32_t tid = data[1];
  std::int32_t prio = (data_size >= 3) ? static_cast<int32_t>(data[2]) : -1;

  // map the event state to one of the TaskState enums
  using TState = TaskState;
  TState state;
  switch (ev) {
    case STATE_RUNNING:
      state = TState::TASK_STATE_RUNNING;
      break;
    case STATE_READY:
      state = TState::TASK_STATE_RUNNABLE;
      break;
    case STATE_STOPPED:
      state = TState::TASK_STATE_STOPPED;
      break;
    case STATE_SEND:
    case STATE_RECEIVE:
    case STATE_REPLY:
    case STATE_STACK:
    case STATE_WAITTHREAD:
    case STATE_SIGSUSPEND:
    case STATE_SIGWAITINFO:
    case STATE_NANOSLEEP:
    case STATE_MUTEX:
    case STATE_CONDVAR:
    case STATE_JOIN:
    case STATE_INTR:
    case STATE_SEM:
    case STATE_WAITCTX:
    case STATE_NET_SEND:
    case STATE_NET_REPLY:
      state = TState::TASK_STATE_INTERRUPTIBLE_SLEEP;
      break;
    case STATE_WAITPAGE:
      state = TState::TASK_STATE_UNINTERRUPTIBLE_SLEEP;
      break;
    default:
      return 0;
  }

  if (process_info_cache_.CacheThread(pid, tid, state)) {
    PERFETTO_DLOG(
        "Thread Updated cpu=%d pid=%d tid=%d ts=%lu, state=%s", cpu, pid, tid,
        timestamp,
        protos::pbzero::GenericKernelTaskStateEvent_TaskStateEnum_Name(state));
    WriteThreadStatusUpdate(pid, tid, timestamp, cpu, prio);
  }
  return 0;
}

int TraceHandler::HandleProcessCreated(std::uint32_t header,
                                       std::uint64_t timestamp,
                                       const std::uint32_t* data,
                                       std::size_t data_size) {
  if (data_size < 2) {
    return -1;
  }

  auto cpu = _NTO_TRACE_GETCPU(header);
  std::int32_t ppid = data[0];
  std::int32_t pid = data[1];

  PERFETTO_DLOG("Process Created cpu=%d pid=%d ppid=%d ts=%" PRIu64, cpu, pid,
                ppid, timestamp);
  // Nothing to do on new process as we only care about thread events.
  process_info_cache_.CacheProcess(pid, ppid);
  return 0;
}

int TraceHandler::HandleThreadCreated(std::uint32_t header,
                                      std::uint64_t timestamp,
                                      const std::uint32_t* data,
                                      std::size_t data_size) {
  if (data_size < 2) {
    return -1;
  }

  auto cpu = _NTO_TRACE_GETCPU(header);
  std::int32_t pid = data[0];
  std::int32_t tid = data[1];
  std::int32_t prio =
      (data_size >= 3) ? static_cast<std::int32_t>(data[2]) : -1;

  // Verify that the thread didn't already exist. This can happen if the
  // priority got updated. if that happens just ignore it.
  if (process_info_cache_.CacheThread(pid, tid,
                                      TaskState::TASK_STATE_CREATED)) {
    PERFETTO_DLOG("Thread Created   cpu=%d pid=%d tid=%d prio=%d ts=%lu", cpu,
                  pid, tid, prio, timestamp);
    WriteThreadStatusUpdate(pid, tid, timestamp, cpu, prio);
    WriteProcessTree(pid, tid, timestamp);
  }
  return 0;
}

int TraceHandler::HandleProcessNamed(std::uint32_t header,
                                     std::uint64_t timestamp,
                                     const std::uint32_t* data,
                                     std::size_t data_size) {
  (void)header;
  if (data_size < 3) {
    return -1;
  }

  // Read the process name from the data note +2 skips the ppid and pid.
  std::int32_t ppid = data[0];
  std::int32_t pid = data[1];
  std::string proc_name(reinterpret_cast<const char*>(data + 2));

  PERFETTO_DLOG("Process Named pid=%d ppid=%d name=%s", pid, ppid,
                proc_name.c_str());
  process_info_cache_.CacheProcess(pid, ppid, proc_name);
  WriteProcessTree(pid, std::nullopt, timestamp);
  return 0;
}

int TraceHandler::HandleThreadNamed(std::uint32_t header,
                                    std::uint64_t timestamp,
                                    const std::uint32_t* data,
                                    std::size_t data_size) {
  (void)header;
  if (data_size < 3) {
    return -1;
  }

  // Read the thread name from the data note +2 skips the pid and tid.
  std::int32_t pid = data[0];
  std::int32_t tid = data[1];
  std::string thread_name(reinterpret_cast<const char*>(data + 2));

  PERFETTO_DLOG("Thread Named pid=%d tid=%d name=%s", pid, tid,
                thread_name.c_str());
  process_info_cache_.CacheThread(pid, tid, TaskState::TASK_STATE_UNKNOWN,
                                  thread_name);
  {
    std::lock_guard<std::mutex> lk(writer_mutex_);
    auto packet = writer_->NewTracePacket();
    SetPacketTimestamp(packet, timestamp);
    auto generic_rename = packet->set_generic_kernel_task_rename_event();
    generic_rename->set_tid(GetExtTid(pid, tid));
    generic_rename->set_comm(thread_name);
  }
  return 0;
}

int TraceHandler::HandleThreadDestroy(std::uint32_t header,
                                      std::uint64_t timestamp,
                                      const std::uint32_t* data,
                                      std::size_t data_size) {
  if (data_size < 2) {
    return -1;
  }

  auto cpu = _NTO_TRACE_GETCPU(header);
  std::int32_t pid = data[0];
  std::int32_t tid = data[1];
  std::int32_t prio =
      (data_size >= 3) ? static_cast<std::int32_t>(data[2]) : -1;

  // tid=1 is a special case and destroy is handled in process destroy.
  if (tid == 1) {
    return 0;
  }

  if (process_info_cache_.CacheThread(pid, tid,
                                      TaskState::TASK_STATE_DESTROYED)) {
    PERFETTO_DLOG("Thread Destroyed cpu=%d pid=%d tid=%d prio=%d ts=%lu", cpu,
                  pid, tid, prio, timestamp);
    WriteThreadStatusUpdate(pid, tid, timestamp, cpu, prio);
  }
  return 0;
}

int TraceHandler::HandleThreadDead(std::uint32_t header,
                                   std::uint64_t timestamp,
                                   const std::uint32_t* data,
                                   std::size_t data_size) {
  if (data_size < 2) {
    return -1;
  }

  auto cpu = _NTO_TRACE_GETCPU(header);
  std::int32_t pid = data[0];
  std::int32_t tid = data[1];
  std::int32_t prio =
      (data_size >= 3) ? static_cast<std::int32_t>(data[2]) : -1;

  // tid=1 is a special case and dead is handled in process destroy.
  // As we don't know what the state truly is consider it sleeping when we get a
  // dead state
  if (tid == 1) {
    if (process_info_cache_.CacheThread(
            pid, tid, TaskState::TASK_STATE_INTERRUPTIBLE_SLEEP)) {
      PERFETTO_DLOG("Thread Dead cpu=%d pid=%d tid=%d prio=%d ts=%lu", cpu, pid,
                    tid, prio, timestamp);
      WriteThreadStatusUpdate(pid, tid, timestamp, cpu, prio);
    }
    return 0;
  }

  if (process_info_cache_.CacheThread(pid, tid, TaskState::TASK_STATE_DEAD)) {
    PERFETTO_DLOG("Thread Dead cpu=%d pid=%d tid=%d prio=%d ts=%lu", cpu, pid,
                  tid, prio, timestamp);
    WriteThreadStatusUpdate(pid, tid, timestamp, cpu, prio);
  }
  return 0;
}

int TraceHandler::HandleProcessDestroy(std::uint32_t header,
                                       std::uint64_t timestamp,
                                       const std::uint32_t* data,
                                       std::size_t data_size) {
  if (data_size < 1) {
    return -1;
  }
  auto cpu = _NTO_TRACE_GETCPU(header);
  std::int32_t pid = data[1];

  // QNX 7.1 has a quirk where thread shutdown doesn't correctly appear so
  // inject destroy and dead event
  for (auto state :
       {TaskState::TASK_STATE_DEAD, TaskState::TASK_STATE_DESTROYED}) {
    process_info_cache_.CacheThread(pid, 1, state);
    WriteThreadStatusUpdate(pid, 1, timestamp, cpu, -1);
    // We can't emit the events at the same time so increment by a ns to mimic a
    // dead destroy
    timestamp++;
  }
  process_info_cache_.UncacheProcess(pid);
  return 0;
}

void TraceHandler::WriteProcessTree(std::int32_t pid,
                                    std::optional<std::int32_t> tid,
                                    std::uint64_t timestamp) {
  std::lock_guard<std::mutex> lk(writer_mutex_);
  auto packet = writer_->NewTracePacket();
  SetPacketTimestamp(packet, timestamp);
  auto process_tree = packet->set_generic_kernel_process_tree();

  // set thread for process if it exists
  if (tid.has_value()) {
    auto* thread_info = process_tree->add_threads();
    thread_info->set_pid(pid);
    thread_info->set_tid(GetExtTid(pid, *tid));

    // In QNX it is considered the main thread if it has tid of 1.
    thread_info->set_is_main_thread(*tid == 1);
  }

  // Set the tree for the parents
  const auto& cur_process = process_info_cache_.GetProcess(pid);
  if (cur_process.GetId() == kInvalidId) {
    return;
  }
  auto* process = process_tree->add_processes();

  const auto& name = cur_process.GetName();
  auto ppid = cur_process.GetParentId();
  process->set_pid(cur_process.GetId());
  if (-1 != ppid) {
    process->set_ppid(ppid);
  }
  if (!name.empty()) {
    process->set_cmdline(name);
  }

  PERFETTO_DLOG("Update Process tree pid=%d tid=%d exttid=%lu name=%s", pid,
                (tid.has_value()) ? *tid : 0,
                (tid.has_value()) ? GetExtTid(pid, *tid) : 0, name.c_str());
}

void TraceHandler::WriteThreadStatusUpdate(std::int32_t pid,
                                           std::int32_t tid,
                                           std::uint64_t timestamp,
                                           std::uint32_t cpu,
                                           std::int32_t prio) {
  auto thread = process_info_cache_.GetThread(pid, tid);
  if (thread.GetId() == kInvalidId) {
    PERFETTO_DLOG("Failed too find cached thread with pid=%d tid=%d", pid, tid);
    return;
  }

  {
    std::lock_guard<std::mutex> lk(writer_mutex_);
    auto packet = writer_->NewTracePacket();
    SetPacketTimestamp(packet, timestamp);

    auto generic_state = packet->set_generic_kernel_task_state_event();
    generic_state->set_state(thread.GetState());
    generic_state->set_tid(GetExtTid(pid, tid));
    generic_state->set_cpu(cpu);
    if (prio != -1) {
      generic_state->set_prio(prio);
    }

    const auto& name = thread.GetName();
    if (name != kInvalidName) {
      generic_state->set_comm(name);
    }
    PERFETTO_DLOG(
        "WriteThreadStatusUpdate cpu=%u pid=%d tid=%d etid=%lu ts=%lu, "
        "state=%s",
        cpu, pid, tid, GetExtTid(pid, tid), timestamp,
        protos::pbzero::GenericKernelTaskStateEvent_TaskStateEnum_Name(
            thread.GetState()));
  }
}

void TraceHandler::SetPacketTimestamp(
    protozero::MessageHandle<protos::pbzero::TracePacket>& packet,
    std::uint64_t ts) {
  packet->set_timestamp(boot_clock_skew_ + ts);
  packet->set_timestamp_clock_id(kTimestampClockId);
}

/**
 * Calculates the skew between the clock cycles time and the monotonic time by
 * determining the median delta in a set of timestamp samples and assigns the
 * skew value in ns to the boot_clock_skew_ member of TraceHandler.
 *
 * QNX kernel tracing uses ClockCycles() to timestamp events. Perfetto uses a
 * Monotonic clock which means there can be some skew in timing when
 * converting between the two timespaces.
 *
 * This method samples the two clocks in a tight loop kBootClockSkewSampleCount
 * times and then performs the time delta and median determination. The sampling
 * sequence swaps the order of time sampling in order to help even out any delay
 * caused by the cost of sampling.
 *
 * NOTE: The method also calculates the average skew but it is currently not
 *       used beyond logging it for comparison in order to determine if there is
 *       any advantage/disadvantage to using the median vs. average skew value.
 *
 * NOTE: We collect monotonic time using the same API as relay GetWallTimeNS().
 *       However we could also use
 *       clock_gettime(CLOCK_MONOTONIC, &monotonic_time); and convert to ns
 * using mono_ns[i] = (std::uint64_t)monotonic_time.tv_sec * 1000000000ULL
 *                  + monotonic_time.tv_nsec;
 *
 * NOTE: This calculation is only required on QNX 7.1 systems as QNX 8.0 aligns
 *       these two clocks. (monotonic based on cycles).
 */
void TraceHandler::CalculateCyclesToMonoClockSkew() {
  std::uint64_t mono_ns[kBootClocKSkewSampleCount];
  std::uint64_t cycles[kBootClocKSkewSampleCount];
  std::uint64_t cycles_ns[kBootClocKSkewSampleCount];
  std::int64_t delta_ns[kBootClocKSkewSampleCount];
  std::int64_t delta_ns_avg = 0;

  std::uint64_t cycles_per_sec = SYSPAGE_ENTRY(qtime)->cycles_per_sec;

  // Collect the clock samples.
  for (std::size_t i = 0; i < kBootClocKSkewSampleCount; i++) {
    // Alternate order of clock sampling to help even out delay cause by sample
    // order.
    if (i & 1) {
      cycles[i] = ClockCycles();
      mono_ns[i] = base::GetWallTimeNs().count();
    } else {
      mono_ns[i] = base::GetWallTimeNs().count();
      cycles[i] = ClockCycles();
    }
  }

  // Calculate the cycles ns and the delta.
  std::int64_t avg_count = 0;
  for (std::size_t i = 0; i < kBootClocKSkewSampleCount; i++) {
    // Calculate the cycles ns.
    // NOTE: We use 128 bits here to avoid truncation and roll over.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    __int128_t cycles_ns_128 = static_cast<unsigned __int128>(cycles[i]) *
                               static_cast<unsigned __int128>(1000000000UL) /
                               static_cast<unsigned __int128>(cycles_per_sec);
    cycles_ns[i] = cycles_ns_128;
#pragma GCC diagnostic pop

    // Calculate the delta between monotonic ns and cycles ns.
    delta_ns[i] = static_cast<std::int64_t>(mono_ns[i]) -
                  static_cast<std::int64_t>(cycles_ns[i]);

    // Update the average calculation with the newly calculated delta.
    delta_ns_avg += (delta_ns[i] - delta_ns_avg) / ++avg_count;
  }

  // Find the median value from the list.
  std::size_t median_index = kBootClocKSkewSampleCount / 2;
  std::nth_element(delta_ns, delta_ns + median_index,
                   delta_ns + kBootClocKSkewSampleCount);
  boot_clock_skew_ = delta_ns[median_index];

  PERFETTO_LOG(
      "Calculated cycles to monotonic clock skew as %ldns, (avg=%ld) over %u "
      "samples",
      boot_clock_skew_, delta_ns_avg, kBootClocKSkewSampleCount);
}

}  // namespace qnx
}  // namespace perfetto
