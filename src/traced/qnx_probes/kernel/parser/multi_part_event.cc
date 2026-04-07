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

#include "src/traced/qnx_probes/kernel/parser/multi_part_event.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace qnx {

MultiPartEvent::MultiPartEvent()
    : event_(),
      is_terminated_(false),
      num_parts_(0),
      multi_part_data_(nullptr),
      data_capacity_(0),
      timestamp_(0) {}

MultiPartEvent::MultiPartEvent(const traceevent_t* event,
                               std::uint64_t timestamp)
    : event_(),
      is_terminated_(false),
      num_parts_(0),
      multi_part_data_(nullptr),
      data_capacity_(0),
      timestamp_(timestamp) {
  switch (_TRACE_GET_STRUCT(event->header)) {
    case _TRACE_STRUCT_S: {
      event_ = *event;
      is_terminated_ = true;
      num_parts_ = 0;
      multi_part_data_ = nullptr;
      break;
    }
    case _TRACE_STRUCT_CB: {
      event_ = *event;
      is_terminated_ = false;
      num_parts_ = 1;
      data_capacity_ = kPartsPerStep * kBytesPerPart;
      multi_part_data_ =
          static_cast<std::uint32_t*>(malloc(data_capacity_));
      if (multi_part_data_) {
        multi_part_data_[0] = event->data[1];
        multi_part_data_[1] = event->data[2];
      } else {
        data_capacity_ = 0;
        num_parts_ = 0;
        PERFETTO_ELOG("Unable to alloc multi_part_data for new MultiPartEvent");
      }
      break;
    }
  }
}

MultiPartEvent::MultiPartEvent(const MultiPartEvent& other)
    : event_(other.event_),
      is_terminated_(other.is_terminated_),
      num_parts_(other.num_parts_),
      multi_part_data_(nullptr),
      data_capacity_(other.data_capacity_),
      timestamp_(other.timestamp_) {

  if (data_capacity_ > 0 && other.multi_part_data_ != nullptr) {
    std::uint32_t bytes_size = other.num_parts_ * kBytesPerPart;
    multi_part_data_ = static_cast<std::uint32_t*>(malloc(data_capacity_));
    if (multi_part_data_) {
      memcpy(multi_part_data_, other.multi_part_data_, bytes_size);
    } else {
      data_capacity_ = 0;
      num_parts_ = 0;
      PERFETTO_ELOG("Unable to allocate multi_part_data from copy of MultiPartEvent");
    }
  }
}

MultiPartEvent::MultiPartEvent(MultiPartEvent&& other)
    : event_(std::move(other.event_)),
      is_terminated_(std::move(other.is_terminated_)),
      num_parts_(std::move(other.num_parts_)),
      multi_part_data_(std::move(other.multi_part_data_)),
      data_capacity_(std::move(other.data_capacity_)),
      timestamp_(other.timestamp_) {
  other.multi_part_data_ = nullptr;
  other.data_capacity_ = 0;
  other.num_parts_ = 0;
  other.is_terminated_ = false;
}

MultiPartEvent::~MultiPartEvent() {
  if (multi_part_data_ != nullptr) {
    free(multi_part_data_);
  }
  data_capacity_ = 0;
  num_parts_ = 0;
}

MultiPartEvent& MultiPartEvent::operator=(const MultiPartEvent& rhs) {
  if (this == &rhs) {
    return *this;
  }

  if (multi_part_data_ != nullptr) {
    free(multi_part_data_);
    multi_part_data_ = nullptr;
  }

  event_ = rhs.event_;
  is_terminated_ = rhs.is_terminated_;
  num_parts_ = rhs.num_parts_;
  timestamp_ = rhs.timestamp_;
  data_capacity_ = rhs.data_capacity_;

  if (data_capacity_ > 0 && rhs.multi_part_data_ != nullptr) {
    std::uint32_t bytes_size = rhs.num_parts_ * kBytesPerPart;
    multi_part_data_ = static_cast<std::uint32_t*>(malloc(data_capacity_));
    if (multi_part_data_) {
      memcpy(multi_part_data_, rhs.multi_part_data_, bytes_size);
    } else {
      data_capacity_ = 0;
      num_parts_ = 0;
      PERFETTO_ELOG("Unable to alloc multi_part_data for assignment of MultiPartEvent");
    }
  }

  return *this;
}

MultiPartEvent& MultiPartEvent::operator=(MultiPartEvent&& rhs) {
  if (this == &rhs) {
    return *this;
  }

  if (multi_part_data_ != nullptr) {
    free(multi_part_data_);
    multi_part_data_ = nullptr;
  }

  event_ = std::move(rhs.event_);
  is_terminated_ = std::move(rhs.is_terminated_);
  num_parts_ = std::move(rhs.num_parts_);
  multi_part_data_ = std::move(rhs.multi_part_data_);
  data_capacity_ = std::move(rhs.data_capacity_);
  timestamp_ = rhs.timestamp_;

  rhs.multi_part_data_ = nullptr;
  rhs.data_capacity_ = 0;
  rhs.num_parts_ = 0;
  rhs.is_terminated_ = false;

  return *this;
}

int MultiPartEvent::Append(const traceevent_t* part) {
  if (part == nullptr) {
    return -1;
  }
  if (!IsPart(part)) {
    return 1;
  }

  // Event matches so see if there is enough space for the data already
  // and realloc an additional step if needed.
  std::uint32_t bytes_used = num_parts_ * kBytesPerPart;
  if ((data_capacity_ - bytes_used) < kBytesPerPart) {
    std::uint32_t new_data_capacity = data_capacity_ + kBytesPerStep;
    std::uint32_t* new_data = (std::uint32_t*)
                              realloc(multi_part_data_, new_data_capacity);
    if (new_data == nullptr) {
      PERFETTO_ELOG("Unable to realloc multi_part_data for Append of part");
      return -1;
    }
    multi_part_data_ = new_data;
    data_capacity_ = new_data_capacity;
  }

  // Copy the data elements for the new part into the data buffer.
  multi_part_data_[num_parts_ * kDataPerPart] = part->data[1];
  multi_part_data_[num_parts_ * kDataPerPart + 1] = part->data[2];

  // Increment the number of parts in the buffer.
  num_parts_++;

  // Check if the part is the terminating part.
  if (_TRACE_GET_STRUCT(part->header) == _TRACE_STRUCT_CE) {
    is_terminated_ = true;
  }

  return 0;
}

std::uint32_t MultiPartEvent::GetTimestampLSB() const {
  return event_.data[0];
}

std::uint64_t MultiPartEvent::GetTimestamp() const {
  return timestamp_;
}

const std::uint32_t* MultiPartEvent::GetData() const {
  if (multi_part_data_ == nullptr && num_parts_ == 0) {
    return &(event_.data[1]);
  }
  return multi_part_data_;
}

std::size_t MultiPartEvent::GetDataSize() const {
  if (multi_part_data_ == nullptr && num_parts_ == 0) {
    return kDataPerPart;
  }
  return num_parts_ * kDataPerPart;
}

bool MultiPartEvent::IsTerminated() const {
  return is_terminated_;
}

bool MultiPartEvent::IsPart(const traceevent_t* part) const {
  if (is_terminated_) {
    return false;
  }

  // Check if timestamps match
  if (event_.data[0] != part->data[0]) {
    return false;
  }

  // Check that the event is from the same CPU
  if (_NTO_TRACE_GETCPU(event_.header) != _NTO_TRACE_GETCPU(part->header)) {
    return false;
  }

  // Check this this is a multi part event that is expecting parts
  if (_TRACE_GET_STRUCT(event_.header) != _TRACE_STRUCT_CB) {
    return false;
  }

  // Check that the part is a continuation or end of multi-part sequence
  if ((_TRACE_GET_STRUCT(part->header) != _TRACE_STRUCT_CC) &&
      (_TRACE_GET_STRUCT(part->header) != _TRACE_STRUCT_CE)) {
    return false;
  }

  // Ensure the event classes match
  if (_NTO_TRACE_GETEVENT_C(event_.header) !=
      _NTO_TRACE_GETEVENT_C(part->header)) {
    return false;
  }

  // Ensure event ids match
  if (_NTO_TRACE_GETEVENT(event_.header) != _NTO_TRACE_GETEVENT(part->header)) {
    return false;
  }

  return true;
}

std::uint32_t MultiPartEvent::GetHeader() const {
  return event_.header;
}

std::uint32_t MultiPartEvent::GetCpu() const {
  return _NTO_TRACE_GETCPU(event_.header);
}

}  // namespace qnx
}  // namespace perfetto
