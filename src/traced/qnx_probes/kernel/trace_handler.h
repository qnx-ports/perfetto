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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_TRACE_HANDLER_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_TRACE_HANDLER_H_

#include <time.h>
extern "C" {
#include <sys/tracelog.h>
}

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "perfetto/base/time.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/generic_kernel/generic_task.pbzero.h"

#include "src/traced/qnx_probes/kernel/kernel_trace_config.h"
#include "src/traced/qnx_probes/kernel/parser/trace_parser.h"
#include "src/traced/qnx_probes/kernel/process_info_cache.h"

namespace perfetto {
namespace qnx {

/**
 * Configures and manages the trace parser to receive trace event callbacks and
 * writes trace events to the Perfetto TraceWriter (so they are sent to traced).
 *
 * The trace handler configures, registers and receives callback from the
 * tracelog system with data that it processes by pushing into the TraceParser.
 * The handler also registers callbacks with the TraceParser for the different
 * events of interest.
 *
 * The handler receives event callbacks from the trace parser and caches the
 * information in the ProcessInfoCache before sending data to traced via the
 * TraceWriter.
 */
class TraceHandler {
 public:
  TraceHandler(std::shared_ptr<TraceWriter> writer);
  TraceHandler(std::shared_ptr<TraceWriter> writer,
               const KernelTraceConfig& config);
  TraceHandler(const TraceHandler&) = delete;
  TraceHandler(TraceHandler&&) = delete;
  ~TraceHandler();

  TraceHandler& operator=(const TraceHandler&) = delete;
  TraceHandler&& operator=(TraceHandler&&) = delete;

  void Start();
  void Stop();
  void Flush(std::function<void()> callback);

  int HandleSysPageFinished(std::uint32_t header,
                            std::uint64_t timestamp,
                            const std::uint32_t* data,
                            std::size_t data_size);

  int HandleProcessCreated(std::uint32_t header,
                           std::uint64_t timestamp,
                           const std::uint32_t* data,
                           std::size_t data_size);

  int HandleProcessNamed(std::uint32_t header,
                         std::uint64_t timestamp,
                         const std::uint32_t* data,
                         std::size_t data_size);

  int HandleProcessDestroy(std::uint32_t header,
                           std::uint64_t timestamp,
                           const std::uint32_t* data,
                           std::size_t data_size);

  int HandleThreadCreated(std::uint32_t header,
                          std::uint64_t timestamp,
                          const std::uint32_t* data,
                          std::size_t data_size);

  int HandleThreadNamed(std::uint32_t header,
                        std::uint64_t timestamp,
                        const std::uint32_t* data,
                        std::size_t data_size);

  int HandleThreadStatusUpdated(std::uint32_t header,
                                std::uint64_t timestamp,
                                const std::uint32_t* data,
                                std::size_t data_size);

  int HandleThreadDead(std::uint32_t header,
                       std::uint64_t timestamp,
                       const std::uint32_t* data,
                       std::size_t data_size);

  int HandleThreadDestroy(std::uint32_t header,
                          std::uint64_t timestamp,
                          const std::uint32_t* data,
                          std::size_t data_size);

 private:
  ProcessCache process_info_cache_;
  tracelog_instance_t* tracelogger_;
  std::shared_ptr<TraceWriter> writer_;
  std::mutex writer_mutex_;
  TraceParser parser_;
  std::int64_t boot_clock_skew_{};

  /**
   * The number of timestamp samples over which to calculate the skew between
   * the cycles and monotonic clocks. Note we use the median so prefer an odd
   * number.
   */
  static const std::uint32_t kBootClocKSkewSampleCount = 501;

  static constexpr protos::pbzero::BuiltinClock kTimestampClockId =
      protos::pbzero::BUILTIN_CLOCK_MONOTONIC;

  void WriteProcessTree(std::int32_t pid,
                        std::optional<std::int32_t> tid,
                        std::uint64_t timestamp);

  void WriteThreadStatusUpdate(std::int32_t pid,
                               std::int32_t tid,
                               std::uint64_t timestamp,
                               std::uint32_t cpu,
                               std::int32_t prio);

  /**
   * Creates a unique 64 bit tid by encoding the system 32 bit tid in the
   * lower bits and the 32 bit pid in upper bits. This extended tid is used as a
   * system wide unique value for the tid since QNX tids are only unique per
   * process and Perfetto needs system wide uniqueness (similar to linux).
   *
   * @param pid The 32 bit system wide unique process identifier
   * @param tid The 32 bit process wide unique thread identifier
   * @returns A 64 bit system wide unique extended thread identifier
   */
  inline uint64_t GetExtTid(std::int32_t pid, std::int32_t tid) {
    return ((static_cast<std::uint64_t>(pid)) << 32) +
           static_cast<std::uint64_t>(tid);
  }

  /**
   * @brief Sets the timestamp and clock of a trace packet
   *
   * @param packet packet to set timestamp
   * @param ts trace event timestamp.
   *
   */
  void SetPacketTimestamp(
      protozero::MessageHandle<protos::pbzero::TracePacket>& packet,
      std::uint64_t ts);

  /**
   * @brief Calculates the clock skew between QNX ClockCycles used by tracelog
   * and Perfetto monotonic clock.
   */
  void CalculateCyclesToMonoClockSkew();
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_TRACE_HANDLER_H_
