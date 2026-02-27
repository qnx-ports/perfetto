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
#include <iostream>

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
    if (current_cpu <= cpu_set_.size()) {
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
    }

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

    // Iterate to the next possible event.
    remaining_data -= event_size_bytes;
    current_event = current_event + 1;
  }

  return data_size - remaining_data;
}

bool CpuContext::IsInitialized() const {
  return cpu_set_.size() == num_cpu_initialized_;
}

void CpuContext::Update(MultiPartEvent* event) {
  if (!event) {
    return;
  }

  std::uint32_t header = event->GetHeader();

  // Ensure the event is a TIME CONTROL event
  std::uint32_t event_class = _NTO_TRACE_GETEVENT_C(header);
  if (event_class != _TRACE_CONTROL_C) {
    return;
  }
  std::uint32_t event_id = _NTO_TRACE_GETEVENT(header);
  if (event_id != _TRACE_CONTROL_TIME) {
    return;
  }

  cpu_set_[_NTO_TRACE_GETCPU(header)].timestamp_msb =
      ((uint64_t)event->GetData()[0]) << 32u;
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

std::uint64_t CpuContext::CalculateEpochNano(std::uint32_t timestamp_lsb,
                                             std::size_t cpu_id) const {
  // Ensure it is initialized
  if (!IsInitialized() || cpu_id >= GetNumCpus()) {
    return 0;
  }

  // Calculate the timestamp cycles since boot by merging the msb from the CPU
  // context with the lsb from the event.
  const CpuInfo& cpu = cpu_set_[cpu_id];
  std::uint64_t timestamp_cycles = (cpu.timestamp_msb | timestamp_lsb);

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
