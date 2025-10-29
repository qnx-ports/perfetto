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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_DYNAMIC_BUFFER_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_DYNAMIC_BUFFER_H_

#include <cstddef>

namespace perfetto {
namespace qnx {
// TODO log suggested size values for the buffer based on the header size.
/**
 * A dynamically resizing buffer that allows data to be cached in a contiguous
 * block of memory for subsequent processing. The dynamic buffer allows access
 * to the underlying data but clients MUST NOT hold on to the pointer as between
 * calls the dynamic buffer may change the location of the data.
 *
 * The buffer is expected to be used in a FIFO manner where data is read from
 * the front of the buffer and added to the end/tail.
 *
 * When all the data has been read from the buffer the cursor will reset to the
 * beginning of the buffer so that the full block of memory is available.
 *
 * The buffer supports compaction such that it will be truncated/shrunk to a
 * block of memory that fits just the avaialble data (or null if emtpy).
 *
 * The buffer is allocated lazily as more data is required. It is resized in
 * increments based on the specified initial size.
 */
class DynamicBuffer {
 public:
  explicit DynamicBuffer(std::size_t initial_size);
  DynamicBuffer(const DynamicBuffer&) = delete;
  DynamicBuffer(DynamicBuffer&&) = delete;
  ~DynamicBuffer();
  DynamicBuffer& operator=(const DynamicBuffer& rhs) = delete;
  DynamicBuffer& operator=(DynamicBuffer&& rhs) = delete;

  int Append(void* data, std::size_t data_size);
  std::size_t Get(void* data, std::size_t data_size);
  int Truncate(std::size_t data_size);
  int Compact();

  char* Start();
  char* End();
  std::size_t Size();
  std::size_t Capacity();
  std::size_t InitialSize();
  bool IsEmpty() const;

 private:
  char* buffer_;
  char* start_;
  char* end_;
  std::size_t initial_size_;
  std::size_t capacity_;
  std::size_t size_;

  bool EnsureCapacity(std::size_t data_size);
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_DYNAMIC_BUFFER_H_
