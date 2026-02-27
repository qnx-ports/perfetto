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

#include <array>
#include <cstddef>
#include <memory>

#include "src/traced/qnx_probes/kernel/parser/multi_part_event.h"

namespace perfetto {
namespace qnx {

/**
 * A fixed size used to assemble multi-part events. When traceevent_t are
 * inserted into the queue they are inspected to see if they are the start of a
 * new multi-part event or a part of an existing event. If they are part of an
 * existing event the queue is searched linearly for the matching event.
 *
 * The queue should naturally be in order by timestamp as that is how we receive
 * events from tracelog.
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

  int InsertEvent(const traceevent_t* event);
  MultiPartEvent* GetEventAt(std::size_t index);
  int ReleaseEvent();
  size_t GetNumEvents() const;
  inline bool Full() const { return num_queued_events_ == kQueueDepth; }

 private:
  // 1024 sizing based on comment from original traceparser.c which describes
  // the empirical analysis of kev files in which 16 depth was max required so
  // 1024 was agreed to be safety.
  static constexpr size_t kQueueDepth = 1024;
  static constexpr size_t kBufferSize = kQueueDepth * sizeof(MultiPartEvent);

  static inline size_t Next(size_t pos);
  static inline size_t Prev(size_t pos);
  static inline size_t Delta(size_t from, size_t to);

  MultiPartEvent* Slot(size_t pos) {
    return reinterpret_cast<MultiPartEvent*>(
        &event_buffer_[pos * sizeof(MultiPartEvent)]);
  }

  std::array<std::byte, kBufferSize> event_buffer_{};
  size_t head_;
  size_t tail_;
  size_t num_queued_events_;
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_EVENT_QUEUE_H_
