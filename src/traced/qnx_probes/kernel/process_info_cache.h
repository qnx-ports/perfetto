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
constexpr std::uint64_t kInvalidUpdateTime = 0;
constexpr std::int32_t kInvalidCpuId = -1;

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
      std::uint64_t update_ts = kInvalidUpdateTime,
      std::int32_t cpu_id = kInvalidCpuId,
      ThreadStateEnum state = ThreadStateEnum::TASK_STATE_UNKNOWN,
      const std::string& name = kInvalidName);

  std::int32_t GetId() const { return tid_; }
  void SetState(ThreadStateEnum state);
  ThreadStateEnum GetState() const { return state_; }
  void SetName(const std::string& name);
  const std::string& GetName() const { return name_; }
  void SetUpdateTime(std::uint64_t time) { update_ts_ = time; }
  std::uint64_t GetUpdateTime() const { return update_ts_; }
  void SetCpuId(std::int32_t cpu_id) { cpu_id_ = cpu_id; }
  std::uint32_t GetCpuId() const { return cpu_id_; }

  void Dump() const;

  static const ThreadInfo kInvalid;

 private:
  std::int32_t tid_;
  std::uint64_t update_ts_;  // ts of last update
  std::int32_t cpu_id_;      // cpu of last event
  ThreadStateEnum state_;
  std::string name_;
};

class ProcessInfo {
 public:
  using ThreadMap = std::unordered_map<std::int32_t, ThreadInfo>;

 public:
  explicit ProcessInfo(std::int32_t pid = kInvalidId,
                       std::int32_t parent_pid = kInvalidId,
                       const std::string& name = kInvalidName);

  std::int32_t GetId() const { return pid_; }
  void SetParentId(std::int32_t parent_pid);
  std::int32_t GetParentId() const { return parent_pid_; }
  void SetName(const std::string& name);
  const std::string& GetName() const { return name_; }
  const ThreadMap& GetThreads() const { return threads_; }

  // Updates thread information returning true if the process tree requires an
  // update.
  const ThreadInfo& GetThread(std::int32_t tid) const;
  bool UpdateThread(const ThreadInfo& thread_info);
  void RemoveThread(std::int32_t tid);
  
  void Dump() const;

  static const ProcessInfo kInvalid;

 private:
  std::int32_t pid_;
  std::int32_t parent_pid_;
  std::string name_;
  ThreadMap threads_;
};

class ProcessCache {
 public:
  ProcessCache();
  ProcessCache(const ProcessCache&) = delete;
  ProcessCache(ProcessCache&&) = delete;
  ~ProcessCache() = default;
  ProcessCache& operator=(const ProcessCache&) = delete;
  ProcessCache& operator=(ProcessCache&&) = delete;

  // Caches information about a process and returns true if the process tree
  // requires an update.
  bool CacheProcess(std::int32_t pid,
                    std::int32_t parent_pid = kInvalidId,
                    const std::string& name = kInvalidName);
  const ProcessInfo& GetProcess(std::int32_t);

  // Caches information about a thread and returns true if the process tree
  // requires an update.
  bool CacheThread(std::int32_t pid,
                   std::int32_t tid,
                   std::uint64_t update_ts,
                   std::int32_t cpu_id,
                   ThreadStateEnum state = ThreadStateEnum::TASK_STATE_UNKNOWN,
                   const std::string& name = kInvalidName);
  const ThreadInfo& GetThread(std::int32_t pid, std::int32_t tid) const;

  void UncacheThread(std::int32_t pid, std::int32_t tid);
  void UncacheProcess(std::int32_t pid);
  void Dump() const;

 private:
  std::unordered_map<std::int32_t, ProcessInfo> process_map_;
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PROCESS_INFO_CACHE_H_
