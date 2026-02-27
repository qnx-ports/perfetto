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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PROCESS_INFO_CACHE_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PROCESS_INFO_CACHE_H_

#include <cstdint>
#include <string>
#include <unordered_map>

#include "protos/perfetto/trace/generic_kernel/generic_task.pbzero.h"

using ThreadStateEnum =
    perfetto::protos::pbzero::GenericKernelTaskStateEvent_TaskStateEnum;

namespace perfetto {
namespace qnx {

constexpr std::int32_t kInvalidId = -1;
const std::string kInvalidName = "";

/**
 * Caches process and thread info such that we have a complete record of the
 * available information about the processes and thread from which to create
 * events to be sent to Perfetto. This prevents sending partial information to
 * Perfetto by allowing the information to coalesce in the cache before we send
 * it.
 */
class ThreadInfo {
 public:
  ThreadInfo() = delete;
  explicit ThreadInfo(
      std::int32_t tid = kInvalidId,
      ThreadStateEnum state = ThreadStateEnum::TASK_STATE_UNKNOWN,
      const std::string& name = kInvalidName);

  std::int32_t GetId() const;
  void SetState(ThreadStateEnum state);
  ThreadStateEnum GetState() const;
  void SetName(const std::string& name);
  std::string GetName() const;
  bool IsDead() const;
  void Dump() const;

  static const ThreadInfo kInvalid;

 private:
  std::int32_t tid_;
  ThreadStateEnum state_;
  std::string name_;
};

class ProcessInfo {
 public:
  explicit ProcessInfo(std::int32_t pid = kInvalidId,
                       std::int32_t parent_pid = kInvalidId,
                       const std::string& name = kInvalidName);

  std::int32_t GetId() const;
  void SetParentId(std::int32_t parent_pid);
  std::int32_t GetParentId() const;
  void SetName(const std::string& name);
  std::string GetName() const;

  /// Updates thread information returning true if the process tree requires an
  /// update.
  bool UpdateThread(const ThreadInfo& thread_info);
  const ThreadInfo& GetThread(std::int32_t tid) const;
  const std::unordered_map<std::int32_t, ThreadInfo>& GetThreads();
  void Dump() const;

  static const ProcessInfo kInvalid;

 private:
  std::int32_t pid_;
  std::int32_t parent_pid_;
  std::string name_;
  std::unordered_map<std::int32_t, ThreadInfo> threads_;
};

class ProcessCache {
 public:
  ProcessCache();
  ProcessCache(const ProcessCache&) = delete;
  ProcessCache(ProcessCache&&) = delete;
  ~ProcessCache() = default;
  ProcessCache& operator=(const ProcessCache&) = delete;
  ProcessCache& operator=(ProcessCache&&) = delete;

  /// Caches information about a process and returns true if the process tree
  /// requires an update.
  bool CacheProcess(std::int32_t pid,
                    std::int32_t parent_pid = kInvalidId,
                    const std::string& name = kInvalidName);
  const ProcessInfo& GetProcess(std::int32_t) const;

  /// Caches information about a thread and returns true if the process tree
  /// requires an update.
  bool CacheThread(std::int32_t pid,
                   std::int32_t tid,
                   ThreadStateEnum state = ThreadStateEnum::TASK_STATE_UNKNOWN,
                   const std::string& name = kInvalidName);
  const ThreadInfo& GetThread(std::int32_t pid, std::int32_t tid) const;

  void UncacheProcess(std::int32_t pid);
  void Dump() const;

 private:
  std::unordered_map<std::int32_t, ProcessInfo> process_map_;
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PROCESS_INFO_CACHE_H_
