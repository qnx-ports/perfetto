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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_MULTI_PART_EVENT_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_MULTI_PART_EVENT_H_

#include <sys/trace.h>

#include <cstddef>
#include <cstdint>

namespace perfetto {
namespace qnx {

/**
 * A trace event made from multiple traceevent_t payloads. Some trace events
 * have data that spans several traceevent_t. traceevent_t have a header value
 * that indicates:
 *  - _TRACE_STRUCT_S: they are single event no other parts expected
 *  - _TRACE_STRUCT_CB: It is the first event in a multi-part sequence
 *  - _TRACE_STRUCT_CC: It is a continuation of a multi-part sequence
 *  - _TRACE_STRUCT_CE: It terminates a multi-part sequence
 *
 * The parts of a multi-part event are correlated by the timestamp and other
 * header values (cpu, event class, event id, ...). MultiPartEvent keeps a flag
 * indicating whether the event is terminated/complete or not.
 *
 * The multi-part event gathers the data from all the parts into a single
 * contiguous block of memory.
 */
class MultiPartEvent {
 public:
  MultiPartEvent();
  MultiPartEvent(const MultiPartEvent& other);
  MultiPartEvent(MultiPartEvent&& other);
  MultiPartEvent(const traceevent_t* event);
  ~MultiPartEvent();

  MultiPartEvent& operator=(const MultiPartEvent& rhs);
  MultiPartEvent& operator=(MultiPartEvent&& rhs);

  int Append(const traceevent_t* part);

  std::uint32_t GetTimestampLSB() const;
  const std::uint32_t* GetData() const;
  std::size_t GetDataSize() const;
  bool IsTerminated() const;
  bool IsPart(const traceevent_t* event) const;
  std::uint32_t GetHeader() const;

 private:
  traceevent_t event_;
  bool is_terminated_;
  std::uint32_t num_parts_;
  std::uint32_t* multi_part_data_;

  // In a multi part event, each event is considered a part, each part has two
  // data elements, each data element is a uint32_t -> 4 bytes
  // When assembling the multi-part event data into a buffer we want to grow the
  // buffer in chunks to avoid reallocating/resizing the
  // buffer for each additional part. kBytesPerStep is the number of bytes each
  // realloc will add to the buffer.  kPartsPerStep is
  // the number of additional parts available after each step. However, it
  // should be noted that we reserve a number of parts at the
  // end of the buffer as a null terminator (enough for one part).
  // kReservedParts is the number of reserved parts to keep free at the end of
  // the buffer.

  // The number of bytes to increment the buffer on each realloc.
  static const std::uint32_t kBytesPerStep = 128;

  // The number of bytes per data element (unsigned 32 bit integer).
  static const std::uint32_t kBytesPerData = sizeof(uint32_t);

  // The number of data elements per part.
  static const std::uint32_t kDataPerPart = 2;

  // The number of bytes of data for each part.
  static const std::uint32_t kBytesPerPart = kBytesPerData * kDataPerPart;

  // The number of parts to be added with each incremental realloc step.
  static const std::uint32_t kPartsPerStep = kBytesPerStep / kBytesPerPart;

  // The number of parts to reserve as empty at the end of the buffer.
  static const std::uint32_t kReservedParts = 1;
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_MULTI_PART_EVENT_H_
