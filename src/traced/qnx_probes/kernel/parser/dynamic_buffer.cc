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

#include "src/traced/qnx_probes/kernel/parser/dynamic_buffer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

namespace perfetto {
namespace qnx {

DynamicBuffer::DynamicBuffer(std::size_t initial_size)
    : buffer_(nullptr),
      start_(nullptr),
      end_(nullptr),
      initial_size_(initial_size),
      capacity_(initial_size),
      size_(0) {}

// TODO use malloc free and realloc on buffer memory instead of new/delete
DynamicBuffer::~DynamicBuffer() {
  delete[] buffer_;
  buffer_ = nullptr;
  start_ = nullptr;
  end_ = nullptr;
  initial_size_ = 0;
  capacity_ = 0;
  size_ = 0;
}

int DynamicBuffer::Append(void* data, std::size_t data_size) {
  if (data_size == 0) {
    return 0;
  }

  if (!data) {
    return -1;
  }

  if (!EnsureCapacity(data_size)) {
    return -1;
  }

  std::memcpy(end_, data, data_size);
  end_ += data_size;
  size_ += data_size;

  return 0;
}

std::size_t DynamicBuffer::Get(void* data, std::size_t data_size) {
  if (size_ == 0 || data_size == 0) {
    return 0;
  }
  std::size_t bytes_to_read = std::min(data_size, size_);
  std::memcpy(data, start_, bytes_to_read);
  start_ += bytes_to_read;
  size_ -= bytes_to_read;

  if (size_ == 0) {
    start_ = buffer_;
    end_ = buffer_;
  }

  return bytes_to_read;
}

int DynamicBuffer::Truncate(std::size_t data_size) {
  if (!buffer_) {
    return -1;
  }

  if (data_size > size_) {
    return -1;
  }

  start_ += data_size;
  size_ -= data_size;

  if (size_ == 0) {
    start_ = buffer_;
    end_ = buffer_;
  }
  return 0;
}

int DynamicBuffer::Compact() {
  if (size_ == 0) {
    delete[] buffer_;
    buffer_ = nullptr;
    start_ = nullptr;
    end_ = nullptr;
    capacity_ = 0;
    return 0;
  }

  if (size_ == capacity_) {
    return 0;
  }

  char* new_buffer = new (std::nothrow) char[size_];
  if (!new_buffer) {
    return -1;
  }

  std::memcpy(new_buffer, start_, size_);
  delete[] buffer_;

  buffer_ = new_buffer;
  start_ = buffer_;
  end_ = buffer_ + size_;
  capacity_ = size_;
  return 0;
}

char* DynamicBuffer::Start() {
  return start_;
}

char* DynamicBuffer::End() {
  return end_;
}

std::size_t DynamicBuffer::Size() {
  return size_;
}

std::size_t DynamicBuffer::Capacity() {
  return capacity_;
}

std::size_t DynamicBuffer::InitialSize() {
  return initial_size_;
}

bool DynamicBuffer::IsEmpty() const {
  return size_ <= 0;
}

bool DynamicBuffer::EnsureCapacity(std::size_t data_size) {
  // If there is no buffer yet that lazily create it.
  if (!buffer_) {
    capacity_ = std::max(initial_size_, data_size);
    buffer_ = new (std::nothrow) char[capacity_];
    if (!buffer_) {
      return false;
    }
    start_ = buffer_;
    end_ = buffer_;

    return true;
  }

  // If the buffer exists check if there is enough space at the end for the new
  // data
  std::size_t free_at_end = buffer_ + capacity_ - end_;
  if (free_at_end >= data_size) {
    return true;
  }

  // If there is not enough space at the end of the buffer then check if there
  // is enough space if we shift everything back to the front of the buffer.
  std::size_t total_free = capacity_ - size_;
  if (total_free >= data_size) {
    // Move existing data to front
    std::memmove(buffer_, start_, size_);
    start_ = buffer_;
    end_ = buffer_ + size_;
    return true;
  }

  // Need more space so reallocate in block of initial_capacity_
  std::size_t new_capacity = capacity_;
  while (new_capacity < size_ + data_size) {
    new_capacity += initial_size_;
  }

  char* new_buffer = new (std::nothrow) char[new_capacity];
  if (!new_buffer) {
    return false;
  }

  std::memcpy(new_buffer, start_, size_);
  delete[] buffer_;

  buffer_ = new_buffer;
  start_ = buffer_;
  end_ = buffer_ + size_;
  capacity_ = new_capacity;

  return true;
}

}  // namespace qnx
}  // namespace perfetto
