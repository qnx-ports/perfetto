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

namespace perfetto {
namespace qnx {

MultiPartEvent::MultiPartEvent()
    : event_(),
      is_terminated_(false),
      num_parts_(0),
      multi_part_data_(nullptr) {}

MultiPartEvent::MultiPartEvent(const traceevent_t* event)
    : event_(),
      is_terminated_(false),
      num_parts_(0),
      multi_part_data_(nullptr) {
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
      multi_part_data_ =
          static_cast<std::uint32_t*>(malloc(kPartsPerStep * kBytesPerPart));
      if (multi_part_data_) {
        multi_part_data_[0] = event->data[1];
        multi_part_data_[1] = event->data[2];
      }
      break;
    }
  }
}

MultiPartEvent::MultiPartEvent(const MultiPartEvent& other)
    : event_(other.event_),
      is_terminated_(other.is_terminated_),
      num_parts_(other.num_parts_),
      multi_part_data_(nullptr) {
  std::uint32_t bytes_size =
      other.num_parts_ * sizeof(other.multi_part_data_[0]);
  multi_part_data_ = static_cast<std::uint32_t*>(malloc(bytes_size));
  if (multi_part_data_ == nullptr) {
    // throw
  }
  memcpy(multi_part_data_, other.multi_part_data_, bytes_size);
}

MultiPartEvent::MultiPartEvent(MultiPartEvent&& other)
    : event_(std::move(other.event_)),
      is_terminated_(std::move(other.is_terminated_)),
      num_parts_(std::move(other.num_parts_)),
      multi_part_data_(std::move(other.multi_part_data_)) {
  other.multi_part_data_ = nullptr;
  other.num_parts_ = 0;
  other.is_terminated_ = false;
}

MultiPartEvent::~MultiPartEvent() {
  if (multi_part_data_ != nullptr) {
    free(multi_part_data_);
  }
}

MultiPartEvent& MultiPartEvent::operator=(const MultiPartEvent& rhs) {
  if (this == &rhs) {
    return *this;
  }

  event_ = rhs.event_;
  is_terminated_ = rhs.is_terminated_;
  num_parts_ = rhs.num_parts_;
  std::uint32_t bytes_size = rhs.num_parts_ * sizeof(rhs.multi_part_data_[0]);
  multi_part_data_ = static_cast<std::uint32_t*>(malloc(bytes_size));
  if (multi_part_data_ == nullptr) {
    // throw
  }
  memcpy(multi_part_data_, rhs.multi_part_data_, bytes_size);

  return *this;
}

MultiPartEvent& MultiPartEvent::operator=(MultiPartEvent&& rhs) {
  event_ = std::move(rhs.event_);
  is_terminated_ = std::move(rhs.is_terminated_);
  num_parts_ = std::move(rhs.num_parts_);
  multi_part_data_ = std::move(rhs.multi_part_data_);
  return *this;
}

int MultiPartEvent::Append(const traceevent_t* part) {
  if (part == nullptr) {
    return -1;
  }
  if (!IsPart(part)) {
    return 1;
  }

  // Event matches so see if there is enough space for the data already.
  if (((num_parts_ + kReservedParts) % kPartsPerStep) == 0) {
    multi_part_data_ = (std::uint32_t*)realloc(
        multi_part_data_,
        ((num_parts_ + kReservedParts + kPartsPerStep) * kBytesPerPart));

    if (multi_part_data_ == nullptr) {
      return -1;
    }
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
  // Check if timestamps match
  if (event_.data[0] != part->data[0]) {
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

  if (is_terminated_) {
    return false;
  }

  return true;
}

std::uint32_t MultiPartEvent::GetHeader() const {
  return event_.header;
}

}  // namespace qnx
}  // namespace perfetto
