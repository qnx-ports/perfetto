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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_CPU_CONTEXT_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_CPU_CONTEXT_H_

#include <cstdint>
#include <vector>

#include "src/traced/qnx_probes/kernel/parser/multi_part_event.h"

namespace perfetto {
namespace qnx {

// TODO in the update function check that we handle global flag

/**
 * CpuContext holds a vector of CpuInfo that track the initial time and time
 * most significant bits for each cpu on the system. Each tracevent_t includes
 * the cpu id of the cpu it was captured from and the least significant bits of
 * the timestamp (clock cycles) when it was captured. In order to get the time
 * an event occured we need to reconstitute the timestamp (clock cycles) by
 * merging the most significant bits for the corresponding cpu.
 *
 * NOTE: The number of cpus on the system and the cycles per second are taken
 *       from the trace header. While the global clocks flag is retrieved from
 *       the syspage.
 *
 * On some systems each cpu has it's own time/cycles on others they are sync'd
 * globally. The cpu context handles this by aligning all the CpuInfos.
 *
 * The initial timestamp is taken from the first event we get for a given cpu.
 * The most significant bits are delivered in the data of a _TRACE_CONTROL_TIME
 * event. This means initializing a cpu info takes two distinct traceevent_t and
 * happens in two stages.
 *
 * The CpuContext Initializes itself from a block of data passed to it that is
 * a contiguous set of traceevent_t. The CpuContext will scan the block for the
 * first event from each cpu and a _TRACE_CONTROL_TIME for each cpu / unless
 * global is set in which case it just takes the first event and
 * _TRACE_CONTROL_TIME. The CpuContext is considered initialized when all the
 * CpuInfos have been initialized with both initial timestamp and msb.
 *
 * The most significant bits of a CpuInfo are updated periodically if they roll
 * over. This happens when a new _TRACE_CONTROL_TIME event is provided. This is
 * handled as part of the event dispatch to ensure that the update gets process
 * BEFORE any subsequent events get dispatched because they will need the
 * updated CpuInfo msb in order for their time to be properly reconstituted.
 */
class CpuInfo {
 public:
  CpuInfo() = default;
  ~CpuInfo() = default;

  // The first timestamp seen from this CPU.
  std::uint64_t initial_timestamp;

  // The most significant bits of the CPU's time.
  std::uint64_t timestamp_msb;

  // Bitset indicating if the fields have been initialized.
  std::uint32_t flags;
};

class CpuContext {
 public:
  CpuContext(std::size_t num_cpus,
             bool use_global_clk,
             std::uint64_t cycles_per_sec);
  ~CpuContext() = default;

  std::size_t GetNumCpus() const;
  std::size_t Initialize(std::size_t data_size, void* data);
  bool IsInitialized() const;
  void Update(MultiPartEvent* event);
  std::uint64_t GetCpuInitialTimestamp(std::size_t cpu_id) const;
  std::uint64_t GetCpuTimestampMsb(std::size_t cpu_id) const;
  std::uint32_t GetCpuFlags(std::size_t cpu_id) const;

  std::uint64_t CalculateEpochNano(std::uint32_t timestamp_lsb,
                                   std::size_t cpu_id) const;

 private:
  // Bitmask for inspecting wether the cpu msb is set.
  static const std::uint32_t kClkMsbFlag = 1u << 0;

  // Bitmask for inspecting whether the cpu initial timestamp is set.
  static const std::uint32_t kClkInitialFlag = 1u << 1;

  static const std::uint64_t kNanoPerSec = 1000000000UL;

  std::size_t num_cpu_initialized_;
  std::vector<CpuInfo> cpu_set_;
  bool use_global_clk_;
  std::uint64_t cycles_per_sec_;
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_CPU_CONTEXT_H_
