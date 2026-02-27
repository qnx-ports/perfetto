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

#include "src/traced/qnx_probes/kernel/parser/trace_decoder.h"

#include <errno.h>
#include <string.h>

#include <charconv>
#include <cstdint>
#include <iostream>

#include "perfetto/base/logging.h"

#include "src/traced/qnx_probes/kernel/parser/trace_print.h"

// tracefile header
#if defined(_TRACE_MK_HK)
#undef _TRACE_MK_HK
#endif
#define _TRACE_MK_HK(k) _TRACE_HEADER_PREFIX #k _TRACE_HEADER_POSTFIX

namespace perfetto {
namespace qnx {

// TODO consider merging TraceParser and Decoder
// TODO consider adding kNumChunksPerPage equivalent to the config - if someone
//      configures a different kernel buffer size then this will likely need to
//      change as well
TraceDecoder::TraceDecoder(EventCallback event_callback,
                           std::size_t pages,
                           int max_pages,
                           std::size_t trace_buffer_init_bytes)
    : state_(State::Header),
      buffer_(trace_buffer_init_bytes),
      page_cache_(kNumChunksPerPage, pages, max_pages),
      page_cache_processor_thread_(),
      assembly_queue_(),
      num_events_decoded_(0),
      header_start_offset_(-1),
      header_end_offset_(-1),
      header_(),
      syspage_(nullptr),
      syspage_bytes_(0),
      cpu_ctx_{},
      event_callback_(event_callback) {}

TraceDecoder::~TraceDecoder() = default;

/**
 * Decodes the event data in one of three ways.
 *   1) Directly from the data passed in (DecodeCpuContext and DecodeEvents)
 *   2) From the buffer DecodeHeader, DecodeSyspage, DecodeCpuContext
 * NOTE: DecodeCpuContext can work either from the raw data or the buffer
 * depending on how the data is passed in.
 * NOTE: When working from the buffer the decoder may have enough data to
 * process multiple states.
 * If more data is expected than the function returns EMORE.
 *
 * @param: data_size The number of bytes of data to decode.
 * @param: data The pointer to the start of the data to decode.
 * @returns 0 if successful, 1 if more data required, -1 if an error occurs.
 */
int TraceDecoder::Decode(std::size_t data_size, void* data) {
  // clang-format off
  int rc = 0;
  if (data_size > 0 && data != nullptr) {
    switch(state_) {
      case State::Syspage: { rc = DecodeSyspage(data, data_size); break; }
      case State::CpuContext: { rc = DecodeCpuContext(data, data_size); break; }
      case State::Events: { rc = DecodeEvents(data, data_size); break;}
      default: { rc = buffer_.Append(data, data_size);}
    }
  }

  // Process any buffered data.
  while (!buffer_.IsEmpty() && rc == 0) {
    switch (state_) {
      case State::Header:     { rc = DecodeHeader(); break; }
      case State::Syspage:    { rc = DecodeSyspage(nullptr, 0); break; }
      case State::CpuContext: { rc = DecodeCpuContext(nullptr, 0); break; }
      default: { return -1; }
    }
  }
  return rc;
  // clang-format on
}

/**
 * Moves any outstanding write pages to the read queue in the page_cache_ and
 * cleans up the PageCacheProcessor thread.
 */
void TraceDecoder::Finish() {
  state_ = State::Finishing;
  page_cache_.Finish();
  if (page_cache_processor_thread_.joinable()) {
    page_cache_processor_thread_.join();
  }
  state_ = State::Done;
}

/**
 * Checks if all the header data has been cached by looking for the start and
 * end markers. Once the header is fully cached it parses the attributes and
 * moves the decoder state to SysPage.
 *
 * @returns 0 is successful, EMORE if more data required, -1 if an error occurs.
 */
int TraceDecoder::DecodeHeader() {
  if (state_ != State::Header) {
    return -1;
  }

  if (buffer_.IsEmpty()) {
    return EMORE;
  }

  // Check for the header start marker.
  char* header_start = nullptr;
  if (header_start_offset_ < 0) {
    header_start = strstr(buffer_.Start(), _TRACE_MK_HK(HEADER_BEGIN));
    if (!header_start) {
      return EMORE;
    }
    header_start_offset_ = header_start - buffer_.Start();
  }
  header_start = buffer_.Start() + header_start_offset_;

  // We already found the header start marker (header_start_ is set) so check
  // for the header end marker
  char* header_end = (char*)memfind(
      header_start, buffer_.End() - header_start - 1, _TRACE_MK_HK(HEADER_END),
      strlen(_TRACE_MK_HK(HEADER_END)));
  if (!header_end) {
    return EMORE;
  }
  header_end_offset_ = header_end - buffer_.Start();

  // Check to see if the size of the buffer was sufficient if not then suggest
  // a better size. However this recommendation is likely only good for live
  // data from tracelog. --> Perfetto
  std::size_t header_size = header_end_offset_ - header_start_offset_ +
                            strlen(_TRACE_MK_HK(HEADER_END));
  if (buffer_.InitialSize() < header_size) {
    PERFETTO_LOG(
        "Recommend increasing config value qnx_trace_buffer_init_bytes to at "
        "least %ld bytes.",
        header_size);
  }

  // Cached the whole header - now parse the individual header attributes.
  int result = ParseHeaderAttributes(header_start, header_end);
  if (result) {
    return result;
  }

  // Clear the header parsing state and set the state to syspage.
  // Remove the header data from the cache.
  buffer_.Truncate(header_size);
  if (buffer_.IsEmpty()) {
    buffer_.Compact();
  }
  header_start_offset_ = -1;
  header_end_offset_ = -1;

  state_ = State::Syspage;

  return 0;
}

/**
 * Copies the syspage data and create the CPU context and global_clocks values
 * based on the syspage value. This method emits a marker event _NTO_TRACE_EMPTY
 * _NTO_TRACE_EMPTYEVENT to indicate that the header and syspage parsing is
 * complete.
 *
 * NOTE: This function will attempt to use any buffered data BEFORE it uses the
 *       data passed in as arguments. Similarly any left over data will be
 *       pushed into the buffer.
 *
 * NOTE: the syspage size is determined based on the trace header value --> see
 * TraceHeader.syspage_len_
 *
 * @returns 0 if successful, EMORE if more data required, or -1 on error.
 */
int TraceDecoder::DecodeSyspage(void* data, std::size_t data_size) {
  if (state_ != State::Syspage) {
    return -1;
  }

  if (!header_.syspage_len_) {
    return -1;
  }

  if (syspage_ == nullptr) {
    syspage_ = std::unique_ptr<struct syspage_entry>(
        (struct syspage_entry*)malloc(*header_.syspage_len_));
    syspage_bytes_ = 0;
    if (syspage_ == nullptr) {
      return -1;
    }
  }

  std::size_t new_syspage_bytes = 0;
  std::size_t syspage_bytes_remaining = *header_.syspage_len_ - syspage_bytes_;
  char* syspage_ptr = reinterpret_cast<char*>(syspage_.get()) + syspage_bytes_;

  // If the buffer isn't empty then copy from the buffer.
  if (!buffer_.IsEmpty()) {
    new_syspage_bytes = std::min(syspage_bytes_remaining, buffer_.Size());
    buffer_.Get(syspage_ptr, new_syspage_bytes);

    syspage_bytes_ += new_syspage_bytes;
    syspage_bytes_remaining -= new_syspage_bytes;
    syspage_ptr = reinterpret_cast<char*>(syspage_.get()) + syspage_bytes_;
  }

  // If there is still more data required take it from the data passed in.
  if (syspage_bytes_ < *header_.syspage_len_) {
    if (data != nullptr && data_size > 0) {
      new_syspage_bytes = std::min(syspage_bytes_remaining, data_size);
      memcpy(syspage_ptr, data, new_syspage_bytes);

      syspage_bytes_ += new_syspage_bytes;
      syspage_bytes_remaining -= new_syspage_bytes;
      syspage_ptr = reinterpret_cast<char*>(syspage_.get()) + syspage_bytes_;

      // Push any outstanding data into the buffer.
      if (new_syspage_bytes < data_size) {
        char* data_ptr = reinterpret_cast<char*>(data) + new_syspage_bytes;
        buffer_.Append(data_ptr, data_size - new_syspage_bytes);
      }
    }
  }

  // If the buffer has nothing left in it compact it (release it).
  if (buffer_.IsEmpty()) {
    buffer_.Compact();
  }

  // If we still need more then return and wait for more.
  if (syspage_bytes_ < *header_.syspage_len_) {
    return EMORE;
  }

  // We have the syspage so transition to State::CpuContext parsing.
  state_ = State::CpuContext;

  if (!header_.cpu_num_) {
    return -1;
  }

  // Create CPU Context based on num cpu and use_global_clock from syspage.
  bool use_global_clock = (_SYSPAGE_ENTRY(syspage_.get(), qtime)->flags &
                           QTIME_FLAG_GLOBAL_CLOCKCYCLES) != 0;
  cpu_ctx_ = std::make_unique<CpuContext>(*header_.cpu_num_, use_global_clock,
                                          *header_.cycles_per_sec_);

  // Now that we have created the sys page call the users _NTO_TRACE_EMPTYEVENT
  event_callback_((_NTO_TRACE_EMPTY << 10) | _NTO_TRACE_EMPTYEVENT, 0, nullptr,
                  0);
  return 0;
}

/**
 * DecodeCpuContext attempts to process the context data from the data passed in
 * unless there is already data in the buffer, in which case it adds the new
 * data to the buffer and processes that.
 * DecodeCpuContext migrates any data (passed or from buffer) into the page
 * cache.
 * DecodeCpuContext will start the processor thread when transitioning to
 * state::Events.
 *
 * @param data The pointer to the data to decode.
 * @param data_size The number of bytes of data available to decode.
 * @returns 0 if successful, EMORE if more data requried, -1 on error.
 */
int TraceDecoder::DecodeCpuContext(void* data, std::size_t data_size) {
  if (state_ != State::CpuContext || !cpu_ctx_) {
    return -1;
  }

  // If there is data in the buffer, append new data and process buffer.
  // Otherwise setup to process data directly.
  std::size_t num_bytes;
  std::byte* event_data;
  if (!buffer_.IsEmpty()) {
    if (data_size > 0 && data != nullptr) {
      buffer_.Append(data, data_size);
    }
    num_bytes = buffer_.Size();
    event_data = reinterpret_cast<std::byte*>(buffer_.Start());
  } else {
    num_bytes = data_size;
    event_data = reinterpret_cast<std::byte*>(data);
  }

  // Use the data to initialize the context.
  if (!cpu_ctx_->IsInitialized()) {
    cpu_ctx_->Initialize(num_bytes, event_data);
  }

  // Move the data to the page cache for event processing.
  page_cache_.Write(event_data, num_bytes);
  if (!buffer_.IsEmpty()) {
    buffer_.Truncate(buffer_.Size());
    buffer_.Compact();
  }

  // If the context is already initialized then transition to State::Events.
  if (cpu_ctx_->IsInitialized()) {
    state_ = State::Events;
    page_cache_processor_thread_ =
        std::thread(&TraceDecoder::ProcessPageCache, this);
  } else {
    return EMORE;
  }

  return 0;
}

/**
 * Pushes any available data onto the page cache to be processed by the
 * PageCacheProcessor thread.
 *
 * @param data The pointer to the data to decode.
 * @param data_size The number of bytes available to decode.
 * @returns 0 if successful, EMORE if more data requried, -1 on error.
 */
int TraceDecoder::DecodeEvents(void* data, std::size_t data_size) {
  if (state_ != State::Events && state_ != State::CpuContext) {
    return -1;
  }

  if (data_size > 0 && data != nullptr) {
    page_cache_.Write((std::byte*)data, data_size);
  } else {
    // Migrate data from buffer to the page cache.
    page_cache_.Write((std::byte*)buffer_.Start(), buffer_.Size());
    buffer_.Truncate(buffer_.Size());
    buffer_.Compact();
  }

  return 0;
}

/**
 * Pushes traceevent_t onto the queue for multi-part assembly and dispatches any
 * terminated/complete events from the front of the queue.
 *
 * NOTE: This is performed by the PageCacheProcessor thread.
 *
 * @param trace_event The pointer to the traceevent_t to decode.
 * @returns 0 if successful, EMORE if more data requried, -1 on error.
 */
int TraceDecoder::DecodeEvent(traceevent_t* trace_event) {
  num_events_decoded_++;

  // Attempt to insert the event into the assembly queue but dispatch an event
  // if the queue is too full to take a new event.
  int rc = assembly_queue_.InsertEvent(trace_event);
  if (rc == 1) {
    DispatchEvent();
    rc = assembly_queue_.InsertEvent(trace_event);
  }

  if (rc != 0) {
    return rc;
  }

  // Dispatch any terminated/complete multi-part events at the beginning of the
  // assembly queue.
  auto* event = assembly_queue_.GetEventAt(0);
  while (event != nullptr && event->IsTerminated()) {
    DispatchEvent();
    event = assembly_queue_.GetEventAt(0);
  }

  return 0;
}

/**
 * Parses the individual header attributes from the block of memory delimited by
 * the header_start and header_end arguments . This method expects the entire
 * header to be available in a contiguous block of memory between these two
 * addresses. It parses the values and adds them to TraceHeader.
 *
 *  HEADER_BEGIN<prefix><key><postfix><value><prefix><key><postfix><value>...HEADER_END
 *  - header_start points to the first character of HEADER_BEGIN
 *  - header_end points to the first character of HEADER_END
 *
 * NOTE: This method parses specific header attributes and needs to be updated
 *       it new attributes are added.
 *
 * @param header_start The pointer to the start of the header data.
 * @param header_end The pointer to the end of the headers data.
 * @returns 0 if successful, EMORE if more data requried, -1 on error.
 */
int TraceDecoder::ParseHeaderAttributes(char* header_start, char* header_end) {
  if (!header_start || !header_end) {
    return -1;
  }

  // We must NOT alter any cache data beyond the header end marker as it will be
  // used when parsing the syspage.
  size_t prefix_len = strlen(_TRACE_HEADER_PREFIX);

  // Position the cursor just after the header start marker.
  char* cursor = header_start + strlen(_TRACE_MK_HK(HEADER_BEGIN));

  bool done = false;
  while (!done) {
    // Iterate over each attribute looking for attribute name by the TRACE_
    // prefix until we hit the end marker.
    char* attr_name = strstr(cursor, _TRACE_HEADER_PREFIX);
    if (!attr_name) {
      return -1;
    }

    // Check if we are at the end of the header.
    if (attr_name == header_end) {
      done = true;
    } else {
      // Each attribute name is of the form TRACE_<name> so place the cursor
      // where the name portion starts.
      attr_name += prefix_len;

      // Each attribute form is TRACE_<name>::<value> so locate the start of the
      // value using the postfix '::'
      char* value = strstr(attr_name, _TRACE_HEADER_POSTFIX);
      if (!value) {
        return -1;
      }
      size_t name_len = value - attr_name;
      value += strlen(_TRACE_HEADER_POSTFIX);

      // Find the end of the value by searching for the next TRACE_ prefix
      // NOTE this moves the cursor forward
      // If this is the last value then you won't find TRACE_ and you assume the
      // value takes the whole rest of the string up to header_end
      // Use memfind since the value is not null terminated
      size_t value_len = header_end - value;
      cursor =
          (char*)memfind(value, value_len, _TRACE_HEADER_PREFIX, prefix_len);
      if (cursor) {
        value_len = cursor - value;
      } else {
        cursor = header_end;
      }

      if (strncmp(attr_name, "DATE", name_len) == 0) {
        header_.date_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "VER_MAJOR", name_len) == 0) {
        header_.ver_major_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "VER_MINOR", name_len) == 0) {
        header_.ver_minor_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "LITTLE_ENDIAN", name_len) == 0) {
        header_.little_endian_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "BIG_ENDIAN", name_len) == 0) {
        header_.big_endian_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "MIDDLE_ENDIAN", name_len) == 0) {
        header_.middle_endian_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "ENCODING", name_len) == 0) {
        header_.encoding_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "BOOT_DATE", name_len) == 0) {
        header_.boot_date_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "CYCLES_PER_SEC", name_len) == 0) {
        std::uint64_t cycles_per_sec = 0;
        std::from_chars_result result =
            std::from_chars(value, cursor, cycles_per_sec);
        if (result.ec == std::errc()) {
          header_.cycles_per_sec_ = cycles_per_sec;
        } else {
          std::cerr << "Conversion failed for cycles_per_sec" << std::endl;
        }
      } else if (strncmp(attr_name, "CPU_NUM", name_len) == 0) {
        std::size_t cpu_num = 0;
        std::from_chars_result result = std::from_chars(value, cursor, cpu_num);
        if (result.ec == std::errc()) {
          header_.cpu_num_ = cpu_num;
        } else {
          std::cerr << "Conversion failed for cpu_num" << std::endl;
        }
      } else if (strncmp(attr_name, "SYSNAME", name_len) == 0) {
        header_.sys_name_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "NODENAME", name_len) == 0) {
        header_.node_name_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "SYS_RELEASE", name_len) == 0) {
        header_.sys_release_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "SYS_VERSION", name_len) == 0) {
        header_.sys_version_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "MACHINE", name_len) == 0) {
        header_.machine_ = std::string(value, value_len);
      } else if (strncmp(attr_name, "SYSPAGE_LEN", name_len) == 0) {
        size_t syspage_len = 0;
        std::from_chars_result result =
            std::from_chars(value, cursor, syspage_len);
        if (result.ec == std::errc()) {
          header_.syspage_len_ = syspage_len;
        } else {
          std::cerr << "Conversion failed for syspage_len" << std::endl;
        }
      } else {
        // Error unknown attribute.
      }

      // Position cursor after value.
      if (cursor == header_end) {
        done = true;
      }
    }
  }

  return 0;
}

/**
 * Find the first occurrence of a set of bytes in another set of bytes. This is
 * the equivalent of strstr but without the null terminated string limitation.
 *
 * @param b1 The pointer to the non null terminated string to search for the
 *           first occurrence of b2.
 * @param b1len The number of bytes in the b1 string.
 * @param b2 The pointer to the non-null terminated string to locate in b1.
 * @param b2len The number of bytes in the b2 string.
 * @returns A pointer to the first occurence of b2 in b1 or NULL if no match is
 *          found.
 */
void* TraceDecoder::memfind(const void* b1,
                            std::size_t b1len,
                            const void* b2,
                            std::size_t b2len) {
  unsigned offset = 0;
  unsigned char c;
  void* found;

  while (offset + b2len < b1len) {
    c = *((unsigned char*)b2);
    found = memchr(((unsigned char*)b1) + offset, c, b1len - offset);
    if (found == nullptr) {
      return nullptr;
    }
    if (memcmp(found, b2, b2len) == 0) {
      return found;
    }
    offset = ((uintptr_t)found - (uintptr_t)b1) + 1;
  }

  return nullptr;
}

/**
 * Provides the main loop of the page_cache_processor_thread. Grabs a
 * traceevent_t from the page cache and processes it by decoding (assemble and
 * dispatching) it. This loop exits when the page cache returns no more events
 * (nullptr) which happens when the PageCache::Finish() is called and
 * PageCache::GetChunk() has read all the available data.
 */
void TraceDecoder::ProcessPageCache() {
  // Loop is exited when GetChunk returns nullptr.
  while (1) {
    traceevent_t* event =
        reinterpret_cast<traceevent_t*>(page_cache_.GetChunk());
    if (event == nullptr) {
      return;
    }
    DecodeEvent(event);
    page_cache_.ReleaseChunk();
  }
}

/**
 * Dispatches an event from the assembly queue. The dispatch first updates the
 * CPU context (if necessary), calculates the absolute time of the event, and
 * then invokes the TraceDecoder::Callback method.
 *
 * @returns 0 on success or -1 on failture to release the event.
 */
void TraceDecoder::DispatchEvent() {
  auto* process_event = assembly_queue_.GetEventAt(0);

  // Update the CPU context based on this event if needed.
  cpu_ctx_->Update(process_event);

  // Dispatch this event
  if (event_callback_) {
    auto cpu_id = _NTO_TRACE_GETCPU(process_event->GetHeader());
    auto ts =
        cpu_ctx_->CalculateEpochNano(process_event->GetTimestampLSB(), cpu_id);

    event_callback_(process_event->GetHeader(), ts,
                    (unsigned*)process_event->GetData(),
                    process_event->GetDataSize());
  }

  // Make room in the queue for the new event.
  assembly_queue_.ReleaseEvent();
}

}  // namespace qnx
}  // namespace perfetto
