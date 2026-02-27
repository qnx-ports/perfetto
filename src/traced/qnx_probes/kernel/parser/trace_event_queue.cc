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

#include "src/traced/qnx_probes/kernel/parser/trace_event_queue.h"

namespace perfetto {
namespace qnx {

TraceEventQueue::TraceEventQueue()
    : event_buffer_(), head_(0), tail_(0), num_queued_events_(0) {}

TraceEventQueue::~TraceEventQueue() {
  while (head_ != tail_) {
    std::destroy_at(Slot(head_));
    head_ = Next(head_);
  }
}

int TraceEventQueue::InsertEvent(const traceevent_t* event) {
  if (!event) {
    return -1;
  }

  switch (_TRACE_GET_STRUCT(event->header)) {
    // If it is the first part (or only part) then it requires a new insertion
    // in the queue.
    case _TRACE_STRUCT_S:
    case _TRACE_STRUCT_CB: {
      // Check if the queue is full
      if (num_queued_events_ == kQueueDepth) {
        return 1;
      }

      size_t insert_index = tail_;

      new (Slot(insert_index)) MultiPartEvent(event);
      num_queued_events_++;
      tail_ = Next(insert_index);
      break;
    }

    // If it is a part of an existing event then no insertion in queue required,
    // instead append it to matching queue element.
    case _TRACE_STRUCT_CC:
    case _TRACE_STRUCT_CE: {
      // Iterate over the list and find matching entry to append the part to.
      for (size_t i = head_; i != tail_; i = Next(i)) {
        // MultiPartEvent::Append performs match check so call it on each entry
        // and check the result.
        int result = Slot(i)->Append(event);
        if (result == 0) {
          return 0;  // Event was appended so return success.
        }
        if (result < 0) {
          return -1;  // Append failed (likely no mem) fail.
        }
        // Append did not succeed due to mismatch so keep searching.
      }
      break;
    }
    default: {
      return -1;  // Invalid argument
    }
  }
  return 0;
}

MultiPartEvent* TraceEventQueue::GetEventAt(std::size_t index) {
  if (index >= num_queued_events_) {
    return nullptr;
  }
  size_t abs_index = (head_ + index) % kQueueDepth;
  return Slot(abs_index);
}

int TraceEventQueue::ReleaseEvent() {
  if (num_queued_events_ == 0) {
    return -1;
  }
  std::destroy_at(Slot(head_));
  head_ = Next(head_);
  num_queued_events_--;
  return 0;
}

size_t TraceEventQueue::GetNumEvents() const {
  return num_queued_events_;
}

inline size_t TraceEventQueue::Next(size_t pos) {
  return (pos + 1) % kQueueDepth;
}

inline size_t TraceEventQueue::Prev(size_t pos) {
  return (pos == 0 ? kQueueDepth - 1 : pos - 1);
}

inline size_t TraceEventQueue::Delta(size_t from, size_t to) {
  return (from <= to) ? (to - from) : (to + kQueueDepth - from);
}

}  // namespace qnx
}  // namespace perfetto
