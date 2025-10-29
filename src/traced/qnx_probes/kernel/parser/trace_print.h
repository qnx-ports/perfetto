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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_PRINT_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_PRINT_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>

extern "C" {
#include <sys/trace.h>
}

namespace perfetto {
namespace qnx {

/**
 * A helper class for pretty printing trace event data.
 */
class TracePrint {
 public:
  TracePrint(std::uint32_t header,
             std::uint32_t timestamp,
             std::size_t data_size,
             const std::uint32_t* data);
  TracePrint(traceevent_t* event);

  static const char* GetClassName(std::uint32_t header);
  static const char* GetEventName(std::uint32_t header);
  static const char* GetStructName(std::uint32_t header);

  std::uint32_t header_;
  std::uint32_t timestamp_;
  std::size_t data_size_;
  const std::uint32_t* data_;
};

std::ostream& operator<<(std::ostream& os, const TracePrint& event);

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_PRINT_H_
