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

#include "src/traced/qnx_probes/kernel/parser/cpu_context.h"

#include <errno.h>
#include <sys/trace.h>

#include <ctime>

namespace perfetto {
namespace qnx {

CpuContext::CpuContext(std::size_t num_cpus,
                       bool use_global_clk,
                       std::uint64_t cycles_per_sec)
    : num_cpu_initialized_(0),
      cpu_set_(),
      use_global_clk_(use_global_clk),
      cycles_per_sec_(cycles_per_sec) {
  cpu_set_.resize(num_cpus);
}

std::size_t CpuContext::GetNumCpus() const {
  return cpu_set_.size();
}

// Initializing the CPU context involves two key elements. First we use the 
// first event we receive for each given CPU in order to set the 
// initial_timestamp value. Then we use the first _TRACE_CONTROL_TIME event in 
// order to set the timestamp_msb. We need both for each CPU context to be 
// initialized. That means the data argument needs enough events to have both an
// initial event AND a _TRACE_CONTROL_TIME event for each CPU. 
// In addition, if use_global_clk is set (usually is) then we can initialize all
// the CPU contexts using the first event and _TRACE_CONTROL_TIME event.
// Lastly note that the trace event sequence from tracelog emperically seems to 
// start with the _TRACE_CONTROL_TIME events.
std::size_t CpuContext::Initialize(std::size_t data_size, void* data) {
  if (IsInitialized() || data_size <= 0 || data == nullptr) {
    return 0;
  }

  // Process the data one traceevent_t at a time and see if we can initialize
  // all the CPU contexts.
  std::size_t remaining_data = data_size;
  traceevent_t* current_event = static_cast<traceevent_t*>(data);
  std::size_t event_size_bytes = sizeof(traceevent_t);
  while (!IsInitialized() && remaining_data >= event_size_bytes) {

    // Retrieve the CPU id from the event header.
    std::uint32_t current_cpu = _NTO_TRACE_GETCPU(current_event->header);
    if (current_cpu < cpu_set_.size()) {
      // Check if the specified CPU is already initialized.
      if (!(cpu_set_[current_cpu].flags & kClkInitialFlag)) {
        // If we are just using the global clock values then initialize ALL the
        // CPU to the same initial time, otherwise just the specified CPU
        if (use_global_clk_) {
          for (size_t cpu_index = 0; cpu_index < cpu_set_.size(); cpu_index++) {
            cpu_set_[cpu_index].flags |= kClkInitialFlag;
            cpu_set_[cpu_index].initial_timestamp = current_event->data[0];
          }
        } else {
          cpu_set_[current_cpu].flags |= kClkInitialFlag;
          cpu_set_[current_cpu].initial_timestamp = current_event->data[0];
        }
      }

      // Now check if the event is a _TRACE_CONTROL_TIME event and initialize 
      // the CPU's timestamp_msb if it is.
      // Ensure the event is a TIME CONTROL event.
      std::uint32_t event_class = _NTO_TRACE_GETEVENT_C(current_event->header);
      std::uint32_t event_id = _NTO_TRACE_GETEVENT(current_event->header);
      if (event_class == _TRACE_CONTROL_C && event_id == _TRACE_CONTROL_TIME) {
        // If the most significant bits are not already set then assign them based
        // on the time control event.
        if (!(cpu_set_[current_cpu].flags & kClkMsbFlag)) {
          if (use_global_clk_) {
            // Since we are using the global clock value set ALL the clocks to the
            // same MSB.
            for (size_t cpu_index = 0; cpu_index < cpu_set_.size(); cpu_index++) {
              cpu_set_[cpu_index].flags |= kClkMsbFlag;
              cpu_set_[cpu_index].timestamp_msb =
                  ((uint64_t)current_event->data[1]) << 32u;
              cpu_set_[cpu_index].initial_timestamp |=
                  cpu_set_[current_cpu].timestamp_msb;
              num_cpu_initialized_++;
            }
          } else {
            // Set the specified CPU flags/msb/initial_clk
            cpu_set_[current_cpu].flags |= kClkMsbFlag;
            cpu_set_[current_cpu].timestamp_msb =
                ((uint64_t)current_event->data[1]) << 32u;
            cpu_set_[current_cpu].initial_timestamp |=
                cpu_set_[current_cpu].timestamp_msb;
            num_cpu_initialized_++;
          }
        }
      }
    }

    // Iterate to the next possible event.
    remaining_data -= event_size_bytes;
    current_event = current_event + 1;
  }

  return data_size - remaining_data;
}

bool CpuContext::IsInitialized() const {
  return cpu_set_.size() == num_cpu_initialized_;
}

std::uint64_t CpuContext::Update(const traceevent_t* event) {
  if (!event) {
    return 0;
  }

  // Ensure the event is a TIME CONTROL event
  std::uint32_t event_class = _NTO_TRACE_GETEVENT_C(event->header);
  std::uint32_t event_id = _NTO_TRACE_GETEVENT(event->header);
  std::uint32_t event_cpu = _NTO_TRACE_GETCPU(event->header);

  if (event_cpu >= cpu_set_.size()) {
    return 0;
  }

  if ((event_class == _TRACE_CONTROL_C) && (event_id == _TRACE_CONTROL_TIME)) {
    cpu_set_[event_cpu].timestamp_msb = ((uint64_t)event->data[1]) << 32u;
  }

  return (cpu_set_[event_cpu].timestamp_msb | event->data[0]);
}

std::uint64_t CpuContext::GetCpuInitialTimestamp(std::size_t cpu_id) const {
  if (cpu_id >= cpu_set_.size()) {
    return 0;
  }

  return cpu_set_[cpu_id].initial_timestamp;
}

std::uint64_t CpuContext::GetCpuTimestampMsb(std::size_t cpu_id) const {
  if (cpu_id >= cpu_set_.size()) {
    return 0;
  }

  return cpu_set_[cpu_id].timestamp_msb;
}

std::uint32_t CpuContext::GetCpuFlags(std::size_t cpu_id) const {
  if (cpu_id >= cpu_set_.size()) {
    return 0;
  }

  return cpu_set_[cpu_id].flags;
}

std::uint64_t CpuContext::CalculateEpochNano(
    std::uint64_t timestamp_cycles) const {
  // Ensure it is initialized
  if (!IsInitialized()) {
    return 0;
  }

  // uint64_t will overflow causing incorrect timestamps and cycles_per_sec_
  // isn't large enough to divide by kNanoPerSec So use __int128 to ensure the
  // timestamp doesn't overflow, giving us an exact value in ns since boot.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
  unsigned __int128 timestamp_ns =
      static_cast<unsigned __int128>(timestamp_cycles) *
      static_cast<unsigned __int128>(kNanoPerSec) /
      static_cast<unsigned __int128>(cycles_per_sec_);
#pragma GCC diagnostic pop
  return timestamp_ns;
}

}  // namespace qnx
}  // namespace perfetto
