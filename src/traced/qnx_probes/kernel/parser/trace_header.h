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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_HEADER_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_HEADER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace perfetto {
namespace qnx {

/**
 * Represents the trace header attributes parsed at the begining of a trace
 * session.
 */
class TraceHeader {
 public:
  TraceHeader() = default;
  ~TraceHeader() = default;

  std::optional<std::string> date_;
  std::optional<std::string> ver_major_;
  std::optional<std::string> ver_minor_;
  std::optional<std::string> little_endian_;
  std::optional<std::string> big_endian_;
  std::optional<std::string> middle_endian_;
  std::optional<std::string> encoding_;
  std::optional<std::string> boot_date_;
  std::optional<std::uint64_t> cycles_per_sec_;
  std::optional<std::size_t> cpu_num_;
  std::optional<std::string> sys_name_;
  std::optional<std::string> node_name_;
  std::optional<std::string> sys_release_;
  std::optional<std::string> sys_version_;
  std::optional<std::string> machine_;
  std::optional<std::size_t> syspage_len_;
};

std::ostream& operator<<(std::ostream& os, const TraceHeader& header);

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_TRACE_HEADER_H_
