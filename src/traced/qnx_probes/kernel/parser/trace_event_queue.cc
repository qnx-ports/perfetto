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
#include <cinttypes>
#include <limits>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace qnx {

TraceEventQueue::TraceEventQueue() = default;
TraceEventQueue::~TraceEventQueue() = default;

int TraceEventQueue::Init(const std::unique_ptr<CpuContext>& cpu_ctx) {
  if (!cpu_ctx) {
    PERFETTO_ELOG("Attempt to initialize TraceEventQueue with null CPU context!");
    return -1;
  }

  std::size_t num_cpus = cpu_ctx->GetNumCpus();
  if (num_cpus == 0) {
    PERFETTO_ELOG("Attempt to initialize TraceEventQueue with 0 CPUs!");
    return -1;
  }

  cpus_latest_events_.resize(cpu_ctx->GetNumCpus());
  for (size_t i = 0; i < cpu_ctx->GetNumCpus(); i++) {
    cpus_latest_events_[i] = cpu_ctx->GetCpuInitialTimestamp(i);
  }
  min_latest_timestamp_ =
      *std::min_element(cpus_latest_events_.begin(), cpus_latest_events_.end());

  // Initialize the buffer start marker to true so that the first events is 
  // interpreted as a buffer start event. 
  is_buffer_start_ = true;
  return 0;
}

int TraceEventQueue::InsertEvent(traceevent_t* event,
                                 std::uint64_t event_ts) {
  if (!event) {
    return -1;
  }

  // Ensure the CPU of the event is valid.
  auto event_cpu = _NTO_TRACE_GETCPU(event->header);
  if (event_cpu >= cpus_latest_events_.size()) {
    PERFETTO_LOG("Ignoring event found for unknown CPU %" PRIu32, event_cpu);
    return -1;
  } 

  // The first event in each kernel trace buffer is a _TRACE_CONTROL_TIME event.
  // Use tha marker to assign the buffer cpu value.
  auto ev_class = _NTO_TRACE_GETEVENT_C(event->header);
  auto ev_id = _NTO_TRACE_GETEVENT(event->header);

// Check for erronious cpu assignment in event. Due to a QNX 8.0 kernel tracing 
// bug there can be events that are assigned to the wrong cpu.
#if (__QNX__ >= 800)
  // If this is the first event following a _TRACE_CONTROL_BUFFER_END event then
  // set the buffer cpu based on this event. 
  // If the event IS a _TRACE_CONTROL_BUFFER_END event then set the flag to 
  // indicate that the next event will be the first event in the next buffer.
  // NOTE: in practice buffers start with a CONTROL_TIME event not a
  // CONTROL_BUFFER event. So the sequence between buffers is 
  // BUFFER_END -> CONTROL_TIME -> BUFFER but we need to set the cpu for 
  // processing right away on whatever event follows the BUFFER_END.
  if (is_buffer_start_) {
    buffer_cpu_ = event_cpu;
    is_buffer_start_ = false;
  } else if (ev_class == _TRACE_CONTROL_C && ev_id == _TRACE_CONTROL_BUFFER_END) {
    is_buffer_start_= true;
  }

  if (event_cpu != buffer_cpu_) {
    // Heal the event cpu by assigning it the value of the buffer_cpu to align
    // with the rest of the events in this buffer and avoid event time
    // regressions. 
    event->header = 
      (((event->header) & ~0x3f000000) 
      | (((std::uint32_t)(buffer_cpu_) << 24) & 0x3f000000));
    event_cpu = _NTO_TRACE_GETCPU(event->header);
    num_events_healed_++;
  }
#endif

  switch (_TRACE_GET_STRUCT(event->header)) {
    // If it is the first part (or only part) then it requires a new insertion
    // in the queue.
    case _TRACE_STRUCT_S:
    case _TRACE_STRUCT_CB: {
      // If the event CPU is min latest we'll need to update min_latest based on
      // this new event (which should be later).
      bool update_latest =
          (min_latest_timestamp_ == cpus_latest_events_[event_cpu]);

      // Check for a time regression on the event. Events SHOULD be in order per
      // CPU.
      if (cpus_latest_events_[event_cpu] > event_ts) {
        PERFETTO_ILOG(
          "Regression on event.ts=0x%" PRIX32 
          " from cpu.tx=0x%" PRIX64
          " class=%" PRIu32 " id=%" PRIu32, 
          event->data[0], cpus_latest_events_[event_cpu], ev_class, ev_id);
      } 

      // Update the latest ts for the cpu which the event came on.
      //
      // We make a few assumptions for this.
      // 1. CPU timestamps for that CPU are monotonically increasing.
      // 2. The latest timestamp only needs to be updated if our current cpu ts
      //    is the same as the oldest. As all other timestamps in
      //    cpu_latest_event must be greater than the current oldest.
      cpus_latest_events_[event_cpu] = event_ts;
      if (update_latest) {
        min_latest_timestamp_ = *std::min_element(cpus_latest_events_.begin(),
                                                 cpus_latest_events_.end());
      }

      // We need to find the events position in the assembly queue by timestamp.
      // Multimap should take care this for us.
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
      PERFETTO_LOG("Ignoring event part with no matching event");
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
          event->GetTimestamp() < min_latest_timestamp_);
}

}  // namespace qnx
}  // namespace perfetto
