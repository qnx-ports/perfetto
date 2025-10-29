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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_KERNEL_TRACE_CONFIG_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_KERNEL_TRACE_CONFIG_H_

#include <cstdint>

#include "src/traced/qnx_probes/qnx_probes_data_source.h"

namespace perfetto {
namespace qnx {

/**
 * Holds the kernel data source configuration parameters and provides default
 * values.
 *
 * See protos/perfetto/config/qnx/qnx_config.proto for the corresponding
 * proto definition.
 */
struct KernelTraceConfig {
  unsigned num_buffers_ = kDefaultNumBuffers;
  unsigned num_kbuffers_ = kDefaultNumKBuffers;
  bool wide_events_ = kDefaultEnableWideEvents;
  std::uint32_t cache_pages_ = kDefaultCachePages;
  std::int32_t cache_max_pages_ = kDefaultCacheMaxPages;
  std::uint32_t trace_buffer_init_bytes_ = kDefaultTraceBufferInitBytes;

  void LoadFromDataSourceConfig(const DataSourceConfig& ds_config);

  // Defaults
  static constexpr unsigned kDefaultNumBuffers = 64;
  static constexpr unsigned kDefaultNumKBuffers = 32;
  static constexpr bool kDefaultEnableWideEvents = true;
  static constexpr std::uint32_t kDefaultCachePages = 4;
  static constexpr std::int32_t kDefaultCacheMaxPages = -1;
  static constexpr std::int32_t kDefaultTraceBufferInitBytes = 4096;
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_KERNEL_TRACE_CONFIG_H_
