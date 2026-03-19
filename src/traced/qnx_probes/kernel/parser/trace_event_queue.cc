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
#include <algorithm>
#include <iostream>
#include <limits>

namespace perfetto {
namespace qnx {

TraceEventQueue::TraceEventQueue() = default;
TraceEventQueue::~TraceEventQueue() = default;

int TraceEventQueue::Init(const std::unique_ptr<CpuContext>& cpu_ctx) {
  cpus_latest_events_.resize(cpu_ctx->GetNumCpus());
  for (size_t i = 0; i < cpu_ctx->GetNumCpus(); i++) {
    cpus_latest_events_[i] = cpu_ctx->GetCpuInitialTimestamp(i);
  }
  oldest_latest_event_ =
      *std::min_element(cpus_latest_events_.begin(), cpus_latest_events_.end());
  return 0;
}

int TraceEventQueue::InsertEvent(const traceevent_t* event,
                                 std::uint64_t event_ts) {
  if (!event) {
    return -1;
  }

  switch (_TRACE_GET_STRUCT(event->header)) {
    // If it is the first part (or only part) then it requires a new insertion
    // in the queue.
    case _TRACE_STRUCT_S:
    case _TRACE_STRUCT_CB: {
      /**
       * Update the latest ts for the cpu which the event came on.
       *
       * We make a few assumptions for this.
       * 1. CPU timestamps for that CPU are monotonically increasing.
       * 2. The latest timestamp only needs to be updated if our current cpu ts
       *    is the same as the oldest. As all other timestamps in
       *    cpu_latest_event must be greater than the current oldest.
       */
      auto event_cpu = _NTO_TRACE_GETCPU(event->header);
      bool update_latest =
          (oldest_latest_event_ == cpus_latest_events_[event_cpu]);
      if (cpus_latest_events_[event_cpu] > event_ts) {
        std::cout << "Timestamp regress on cpu: " << event_cpu
                  << " from: " << cpus_latest_events_[event_cpu]
                  << " to: " << event_ts << std::endl;
      }
      cpus_latest_events_[event_cpu] = event_ts;
      if (update_latest) {
        oldest_latest_event_ = *std::min_element(cpus_latest_events_.begin(),
                                                 cpus_latest_events_.end());
      }

      /**
       * We need to find the events position in the assembly queue by timestamp.
       * Multimap should take care this for us.
       */
      event_buffer_.emplace(event_ts,
                            std::move(MultiPartEvent(event, event_ts)));
      break;
    }

    // If it is a part of an existing event then no insertion in queue required,
    // instead append it to matching queue element.
    case _TRACE_STRUCT_CC:
    case _TRACE_STRUCT_CE: {
      // Find all elements with the same timestamp to limit the search of events
      // which need to be checked.
      auto events = event_buffer_.equal_range(event_ts);
      for (auto itr = events.first; itr != events.second; ++itr) {
        int result = itr->second.Append(event);
        if (result <= 0) {
          return result;  // Event was appended so return success. or an error
                          // occurred.
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

int TraceEventQueue::ReleaseEvent() {
  if (event_buffer_.empty()) {
    return -1;
  }
  event_buffer_.erase(event_buffer_.cbegin());
  return 0;
}

const MultiPartEvent* TraceEventQueue::Front() const {
  if (event_buffer_.empty()) {
    return nullptr;
  }
  return &(event_buffer_.cbegin()->second);
}

size_t TraceEventQueue::GetNumEvents() const {
  return event_buffer_.size();
}

/**
 * For QNX7.1 and earlier events will come in order so we can safely dispatch
 * any terminated/complete multi-part events at the beginning of the queue.
 *
 * QNX8.0+ only maintains order on a per cpu basis so we need to verify that
 * every CPU has emitted an event earlier then the beginning of the queue.
 *
 * To simply the logic we just default to the 8.0 case as it will work for both.
 */
bool TraceEventQueue::CanDispatch() const {
  auto* event = Front();
  return (event != nullptr && event->IsTerminated() &&
          event->GetTimestamp() < oldest_latest_event_);
}

}  // namespace qnx
}  // namespace perfetto
