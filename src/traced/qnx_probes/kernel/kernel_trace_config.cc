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

#include "src/traced/qnx_probes/kernel/kernel_trace_config.h"

#include "protos/perfetto/config/data_source_config.gen.h"
#include "protos/perfetto/config/qnx/qnx_config.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace qnx {

using protos::pbzero::QnxConfig;

void KernelTraceConfig::LoadFromDataSourceConfig(
    const DataSourceConfig& ds_config) {
  QnxConfig::Decoder qnx_cfg(ds_config.qnx_config_raw());
  if (qnx_cfg.has_qnx_kernel_buffers()) {
    num_buffers_ = qnx_cfg.qnx_kernel_buffers();
  }
  PERFETTO_LOG("Using config qnx_kernel_buffers=%u", num_buffers_);

  if (qnx_cfg.has_qnx_kernel_kbuffers()) {
    num_kbuffers_ = qnx_cfg.qnx_kernel_kbuffers();
  }
  PERFETTO_LOG("Using config qnx_kernel_kbuffers=%u", num_kbuffers_);

  if (qnx_cfg.has_qnx_kernel_wide_events()) {
    wide_events_ = qnx_cfg.qnx_kernel_wide_events();
  }
  PERFETTO_LOG("Using config qnx_kernel_wide_events=%s",
               (wide_events_) ? "true" : "false");

  if (qnx_cfg.has_qnx_cache_pages()) {
    cache_pages_ = qnx_cfg.qnx_cache_pages();
  }
  PERFETTO_LOG("Using config qnx_cache_pages=%u", cache_pages_);

  if (qnx_cfg.has_qnx_cache_max_pages()) {
    cache_max_pages_ = qnx_cfg.qnx_cache_max_pages();
  }
  PERFETTO_LOG("Using config qnx_cache_max_pages=%d", cache_max_pages_);

  if (qnx_cfg.has_qnx_trace_buffer_init_bytes()) {
    trace_buffer_init_bytes_ = qnx_cfg.qnx_trace_buffer_init_bytes();
  }
  PERFETTO_LOG("Using config qnx_trace_buffer_init_bytes=%d",
               trace_buffer_init_bytes_);
}
}  // namespace qnx
}  // namespace perfetto
