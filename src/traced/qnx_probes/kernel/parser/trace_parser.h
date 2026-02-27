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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_PARSER_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_PARSER_H_

#include <sys/trace.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>

#include "src/traced/qnx_probes/kernel/parser/trace_decoder.h"

namespace perfetto {
namespace qnx {

/**
 * Parses trace event data and dispatches to callback that have been registered
 * for the various trace event types. The parser allows clients to register
 * callback functions for various events/classes/ranges. It accepts trace data
 * via the Parse() method, parsing the data, assembling and dispatching trace
 * events.
 *
 * NOTE: The parser defers to TraceDecoder to perform the heavy lifting of the
 *       trace event parsing/decoding. The parser mostly performs the function
 *       of tracking and dispatching events to registered clients.
 */
class TraceParser {
 public:
  struct TraceParserStats {
    std::size_t callback_call_count_ = 0;
    std::size_t callback_miss_count_ = 0;
    std::size_t event_parsed_ = 0;
  };
  using Callback = std::function<int(std::uint32_t ev_header,
                                     std::uint64_t ev_time,
                                     const std::uint32_t* buffer,
                                     std::size_t buffer_len)>;
  static const int kMaxClasses = _TRACE_TOT_CLASS_NUM;
  static const int kMaxEvents = _TRACE_MAX_EVENT_NUM;

  TraceParser(std::size_t pages,
              int max_pages,
              std::size_t trace_buffer_init_bytes);
  TraceParser(const TraceParser&) = delete;
  TraceParser(TraceParser&&) = delete;
  ~TraceParser();

  TraceParser& operator=(const TraceParser&) = delete;
  TraceParser& operator=(TraceParser&&) = delete;

  int Register(Callback callback, unsigned event_class, unsigned event_id);
  int Register(Callback callback,
               unsigned event_class,
               unsigned event_id_start,
               unsigned event_id_end);
  int Parse(size_t data_size, void* data);
  int Finish();

  inline const struct syspage_entry* GetSysPage() const {
    return decoder_.GetSysPage();
  }

  inline const TraceHeader& GetTraceHeader() const {
    return decoder_.GetTraceHeader();
  }

  inline const TraceParserStats& getTraceStats() const { return stats_; }

 private:
  std::array<std::array<Callback, kMaxEvents>, kMaxClasses> event_callbacks_;
  TraceDecoder decoder_;
  TraceParserStats stats_{};

  void InsertCallbackForRange(Callback callback,
                              unsigned class_value,
                              unsigned event_begin,
                              unsigned event_end);

  void OnEvent(std::uint32_t header,
               std::uint64_t timestamp,
               const std::uint32_t* data,
               std::size_t data_size);

  static int GetRightmostBitIndex(std::uint32_t k);
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_PARSER_H_
