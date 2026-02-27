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

#include "src/traced/qnx_probes/kernel/parser/trace_header.h"

#include <iomanip>

namespace perfetto {
namespace qnx {

// Operator overload implementation
std::ostream& operator<<(std::ostream& os, const TraceHeader& header) {
  os << "TraceHeader {";
  if (header.date_) {
    os << "date: " << *header.date_;
  }
  if (header.ver_major_) {
    os << ", ver_major: " << *header.ver_major_;
  }
  if (header.ver_minor_) {
    os << ", ver_minor: " << *header.ver_minor_;
  }
  if (header.little_endian_) {
    os << ", little_endian: " << *header.little_endian_;
  }
  if (header.big_endian_) {
    os << ", big_endian: " << *header.big_endian_;
  }
  if (header.middle_endian_) {
    os << ", middle_endian: " << *header.middle_endian_;
  }
  if (header.encoding_) {
    os << ", encoding: " << *header.encoding_;
  }
  if (header.boot_date_) {
    os << ", boot_date: " << *header.boot_date_;
  }
  if (header.cycles_per_sec_) {
    os << ", cycles_per_sec: " << *header.cycles_per_sec_;
  }
  if (header.cpu_num_) {
    os << ", cpu_num: " << *header.cpu_num_;
  }
  if (header.sys_name_) {
    os << ", sys_name: " << *header.sys_name_;
  }
  if (header.node_name_) {
    os << ", node_name: " << *header.node_name_;
  }
  if (header.sys_release_) {
    os << ", sys_release: " << *header.sys_release_;
  }
  if (header.sys_version_) {
    os << ", sys_version: " << *header.sys_version_;
  }
  if (header.machine_) {
    os << ", machine: " << *header.machine_;
  }
  if (header.syspage_len_) {
    os << ", syspage_len: " << *header.syspage_len_;
  }

  os << "}";

  return os;
}

}  // namespace qnx
}  // namespace perfetto
