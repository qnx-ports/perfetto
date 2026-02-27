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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_DECODER_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_DECODER_H_

#include <sys/syspage.h>
#include <sys/trace.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <thread>

#include "src/traced/qnx_probes/kernel/parser/cpu_context.h"
#include "src/traced/qnx_probes/kernel/parser/dynamic_buffer.h"
#include "src/traced/qnx_probes/kernel/parser/page_cache.h"
#include "src/traced/qnx_probes/kernel/parser/trace_event_queue.h"
#include "src/traced/qnx_probes/kernel/parser/trace_header.h"

namespace perfetto {
namespace qnx {

/**
 * Parses data from the tracelog system and extracts:
 * - The trace header and header attributes (see TraceHeader)
 * - The syspage
 * - The CpuContext
 * - TraceEvents (see MultiPartEvent)
 *
 * The decoder takes data passed in the Decode() method and parses the relevant
 * trace information from it. When MultiPartEvents are ready, the decoder
 * dispatches them to the registered EventCallBack.
 *
 * NOTE: The decoder is multi-threaded. During MultiPartEvent parsing, the
 *       decoder will push data into a page cache with the tracelog callback
 *       thread and process data from the page cache (assemble and dispatch
 *       multi-part events) from a page cache processor thread.
 *
 * The page cache first decodes the header and syspage as that information is
 * required in order to properly decode the trace events.
 *
 * The decoder uses a dynamic buffer to collect the trace header and manage
 * scenarios where the data is delivered to Decode() in a way that is not
 * aligned to the header/syspage/events.
 *
 * NOTE: In the case of Perfetto integration, the data pushed to decode is
 *       is coming directly from tracelog and is properly aligned. So in this
 *       case the buffer is only used to collect the header for parsing.
 *       However, if a different application were to read data from a file, then
 *       it might deliver data in arbitrary chunks that did not align to the
 *       header/syspage/event boundary and the buffer would used to hold the
 *       data in a contiguous block.
 *
 * In order to stop processing events the Finish event will stop putting new
 * events in the queue and will finish dispatching the available events and join
 * the page cache processing thread.
 */
class TraceDecoder {
 public:
  using EventCallback = std::function<void(std::uint32_t header,
                                           std::uint64_t timestamp,
                                           const std::uint32_t* data,
                                           std::size_t data_size)>;
  TraceDecoder(EventCallback event_callback,
               std::size_t pages,
               int max_pages,
               std::size_t trace_buffer_init_bytes);
  TraceDecoder(const TraceDecoder&) = delete;
  TraceDecoder(TraceDecoder&&) = delete;
  ~TraceDecoder();

  TraceDecoder& operator=(const TraceDecoder&) = delete;
  TraceDecoder& operator=(TraceDecoder&&) = delete;

  int Decode(std::size_t data_size, void* data);
  void Finish();
  inline const struct syspage_entry* GetSysPage() const {
    return syspage_.get();
  }
  inline const TraceHeader& GetTraceHeader() const { return header_; }
  inline std::size_t GetEventsDecoded() const { return num_events_decoded_; }

 private:
  enum class State {
    Unknown,
    Header,
    Syspage,
    CpuContext,
    Events,
    Finishing,
    Done
  };

  // 708 * 16 byte chunk = 11328 bytes per page
  static const std::size_t kNumChunksPerPage = 708;

  State state_;
  DynamicBuffer buffer_;
  PageCache page_cache_;
  std::thread page_cache_processor_thread_;
  TraceEventQueue assembly_queue_;
  std::size_t num_events_decoded_;

  // We use offsets here because the buffer may be reallocated so the pointer
  // values would change
  int header_start_offset_;
  int header_end_offset_;
  TraceHeader header_;

  std::unique_ptr<struct syspage_entry> syspage_{};
  std::size_t syspage_bytes_;
  std::unique_ptr<CpuContext> cpu_ctx_{};

  EventCallback event_callback_;

  int DecodeHeader();
  int DecodeSyspage(void* data, std::size_t data_size);
  int DecodeCpuContext(void* data, std::size_t data_size);
  int DecodeEvents(void* data, std::size_t data_size);
  int DecodeEvent(traceevent_t* event);
  int ParseHeaderAttributes(char* header_start, char* header_end);
  static void* memfind(const void* b1,
                       std::size_t b1len,
                       const void* b2,
                       std::size_t b2len);
  void ProcessPageCache();
  void DispatchEvent();
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_DECODER_H_
