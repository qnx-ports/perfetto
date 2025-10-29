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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_KERNEL_DATA_SOURCE_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_KERNEL_DATA_SOURCE_H_

#include <memory>
#include <thread>

#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/traced/qnx_probes/kernel/trace_handler.h"
#include "src/traced/qnx_probes/qnx_probes_data_source.h"

namespace perfetto {
namespace qnx {

/**
 * A Perfetto traced probe data source for tracking and reporting QNX kernel
 * events. The data source collects and parses kernel events and writes them
 * to traced via the TraceWrite.
 *
 * The data source relies on the TraceHandler to perform much of the heavy
 * lifting for processing, caching and writing kernel trace events. The data
 * source applies the configuration from perfetto and exposes the lifecycle
 * functions that are called by the probes producer in order to control the
 * data source collection.
 *
 * NOTE: The probe producer instance only lives while the trace is performed and
 *       is destructed by the probes producer when the trace ends. So multiple
 *       instances of the data source come and go over the life of the process.
 */
class KernelDataSource : public QnxProbesDataSource {
 public:
  static const QnxProbesDataSource::Descriptor descriptor;

  KernelDataSource(TracingSessionID,
                   std::unique_ptr<TraceWriter> writer,
                   const DataSourceConfig&);
  ~KernelDataSource() override;

  // ProbesDataSource implementation.
  void Start() override;
  void Flush(FlushRequestID, std::function<void()> callback) override;

 private:
  std::shared_ptr<TraceWriter> writer_;
  KernelTraceConfig config_;
  std::thread trace_thread_;
  std::weak_ptr<TraceHandler> trace_handler_;

  void Trace();
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_KERNEL_DATA_SOURCE_H_
