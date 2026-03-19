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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_EVENT_QUEUE_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_EVENT_QUEUE_H_

#include <sys/trace.h>

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

#include "src/traced/qnx_probes/kernel/parser/cpu_context.h"
#include "src/traced/qnx_probes/kernel/parser/multi_part_event.h"

namespace perfetto {
namespace qnx {

/**
 * A dynamic size queue used to assemble multi-part events. When traceevent_t
 * are inserted into the queue they are inspected to see if they are the start
 * of a new multi-part event or a part of an existing event. If they are part of
 * an existing event the queue is searched linearly for the matching event.
 *
 * The queue must be sorted to ensure that events are processed in the correct
 * order.
 *
 * When events are released they are released from the head of the queue (FIFO)
 */
class TraceEventQueue {
 public:
  TraceEventQueue();
  TraceEventQueue(const TraceEventQueue&) = delete;
  TraceEventQueue(TraceEventQueue&&) = delete;
  ~TraceEventQueue();

  TraceEventQueue& operator=(const TraceEventQueue&) = delete;
  TraceEventQueue& operator=(TraceEventQueue&&) = delete;

  int Init(const std::unique_ptr<CpuContext>& cpu_ctx);
  int InsertEvent(const traceevent_t* event, std::uint64_t timestamp);
  const MultiPartEvent* Front() const;
  int ReleaseEvent();
  size_t GetNumEvents() const;

  /**
   * @brief Checks if the first event can be safely emitted.
   *
   * @return Returns true if the first event can be dispatched safely
   */
  bool CanDispatch() const;

 private:
  // multimap is needed as multiple events can have the same ts
  std::multimap<std::uint64_t, MultiPartEvent> event_buffer_{};
  std::vector<std::uint64_t> cpus_latest_events_{};
  std::uint64_t oldest_latest_event_ =
      0;  // TODO We need a better name for this :)
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_EVENT_QUEUE_H_
