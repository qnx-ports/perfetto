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

#include "src/traced/qnx_probes/kernel/process_info_cache.h"

#include <iostream>

namespace perfetto {
namespace qnx {

const ThreadInfo ThreadInfo::kInvalid{
    kInvalidId, ThreadStateEnum::TASK_STATE_UNKNOWN, kInvalidName};

ThreadInfo::ThreadInfo(std::int32_t tid,
                       ThreadStateEnum state,
                       const std::string& name)
    : tid_(tid), state_(state), name_(name) {}

std::int32_t ThreadInfo::GetId() const {
  return tid_;
}

void ThreadInfo::SetState(ThreadStateEnum state) {
  if (state != ThreadStateEnum::TASK_STATE_UNKNOWN) {
    state_ = state;
  }
}

ThreadStateEnum ThreadInfo::GetState() const {
  return state_;
}

void ThreadInfo::SetName(const std::string& name) {
  if (name != kInvalidName) {
    name_ = name;
  }
}

bool ThreadInfo::IsDead() const {
  return (state_ == ThreadStateEnum::TASK_STATE_DEAD ||
          state_ == ThreadStateEnum::TASK_STATE_DESTROYED);
}

std::string ThreadInfo::GetName() const {
  return name_;
}

void ThreadInfo::Dump() const {
  std::cout << "ThreadInfo{tid=" << GetId() << ", state=";

  switch (GetState()) {
    case ThreadStateEnum::TASK_STATE_CREATED:
      std::cout << "CREATED";
      break;
    case ThreadStateEnum::TASK_STATE_DEAD:
      std::cout << "DEAD";
      break;
    case ThreadStateEnum::TASK_STATE_DESTROYED:
      std::cout << "DESTROYED";
      break;
    case ThreadStateEnum::TASK_STATE_INTERRUPTIBLE_SLEEP:
      std::cout << "INTERRUPTIBLE_SLEEP";
      break;
    case ThreadStateEnum::TASK_STATE_RUNNABLE:
      std::cout << "RUNNABLE";
      break;
    case ThreadStateEnum::TASK_STATE_RUNNING:
      std::cout << "RUNNING";
      break;
    case ThreadStateEnum::TASK_STATE_STOPPED:
      std::cout << "STOPPED";
      break;
    case ThreadStateEnum::TASK_STATE_UNINTERRUPTIBLE_SLEEP:
      std::cout << "UNINTERRUPTIBLE_SLEEP";
      break;
    default:
      std::cout << "UNKNOWN";
      break;
  }

  std::cout << ", name=" << GetName() << "}";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const ProcessInfo ProcessInfo::kInvalid{kInvalidId, kInvalidId, kInvalidName};

ProcessInfo::ProcessInfo(std::int32_t pid,
                         std::int32_t parent_pid,
                         const std::string& name)
    : pid_(pid), parent_pid_(parent_pid), name_(name) {}

std::int32_t ProcessInfo::GetId() const {
  return pid_;
}

void ProcessInfo::SetParentId(std::int32_t parent_pid) {
  if (parent_pid != kInvalidId) {
    parent_pid_ = parent_pid;
  }
}

std::int32_t ProcessInfo::GetParentId() const {
  return parent_pid_;
}

void ProcessInfo::SetName(const std::string& name) {
  if (name != kInvalidName) {
    name_ = name;
  }
}

std::string ProcessInfo::GetName() const {
  return name_;
}

bool ProcessInfo::UpdateThread(const ThreadInfo& thread_info) {
  if (thread_info.GetId() == kInvalidId) {
    return false;
  }

  auto iter = threads_.find(thread_info.GetId());
  if (iter == threads_.end()) {
    // New Thread. Always update
    threads_.insert_or_assign(thread_info.GetId(), thread_info);
    return true;
  }

  bool update_required = false;
  auto& cur_thread = iter->second;
  const auto old_state = cur_thread.GetState();
  const auto new_state = thread_info.GetState();
  if (cur_thread.IsDead()) {
    // QNX reuses tids so check if a dead thread is being recreated.
    update_required |= (new_state == ThreadStateEnum::TASK_STATE_CREATED &&
                        old_state == ThreadStateEnum::TASK_STATE_DESTROYED);

    // Thread Joined/destroyed.
    update_required |= (old_state == ThreadStateEnum::TASK_STATE_DEAD &&
                        new_state == ThreadStateEnum::TASK_STATE_DESTROYED);
  } else if (thread_info.IsDead()) {
    // Only transition into a dead state if it was previously alive
    // (ignore destroy events)
    update_required = (new_state == ThreadStateEnum::TASK_STATE_DEAD);
  } else {
    // If both threads are alive update as long as the state changed and the new
    // state isn't created.
    update_required = (new_state != old_state &&
                       new_state != ThreadStateEnum::TASK_STATE_CREATED);
  }

  // Update the thread
  if (update_required) {
    cur_thread.SetName(thread_info.GetName());
    cur_thread.SetState(thread_info.GetState());
  }
  return update_required;
}

const ThreadInfo& ProcessInfo::GetThread(std::int32_t tid) const {
  auto it = threads_.find(tid);
  if (it != threads_.end()) {
    return it->second;
  }
  return ThreadInfo::kInvalid;
}

void ProcessInfo::Dump() const {
  std::cout << "ProcessInfo{pid=" << GetId() << ", ppid=" << GetParentId()
            << ", name=" << GetName() << ", threads[";

  auto iter = threads_.begin();
  while (iter != threads_.end()) {
    std::cout << std::endl;
    std::cout << "\t";
    iter->second.Dump();
    iter++;
  }
  std::cout << "]" << std::endl;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ProcessCache::ProcessCache() : process_map_() {}

bool ProcessCache::CacheProcess(std::int32_t pid,
                                std::int32_t parent_pid,
                                const std::string& name) {
  auto iter = process_map_.find(pid);
  if (iter != process_map_.end()) {
    iter->second.SetParentId(parent_pid);
    iter->second.SetName(name);
  } else {
    // Insert new
    process_map_.emplace(pid, ProcessInfo(pid, parent_pid, name));
  }
  // Any update to a process requires an update to the process tree.
  return true;
}

const ProcessInfo& ProcessCache::GetProcess(std::int32_t pid) const {
  auto iter = process_map_.find(pid);
  if (iter != process_map_.end()) {
    return iter->second;
  }
  return ProcessInfo::kInvalid;
}

bool ProcessCache::CacheThread(std::int32_t pid,
                               std::int32_t tid,
                               ThreadStateEnum state,
                               const std::string& name) {
  if (pid == kInvalidId || tid == kInvalidId) {
    return false;  // Ignore invalid ids
  }

  auto iter = process_map_.find(pid);
  if (iter == process_map_.end()) {
    // Insert new process with default parent_pid and name (empty)
    process_map_.emplace(pid, ProcessInfo(pid));
    iter = process_map_.find(pid);
  }
  ThreadInfo threadInfo(tid, state, name);
  return iter->second.UpdateThread(threadInfo);
}

const ThreadInfo& ProcessCache::GetThread(std::int32_t pid,
                                          std::int32_t tid) const {
  auto proc_iter = process_map_.find(pid);
  if (proc_iter == process_map_.end()) {
    return ThreadInfo::kInvalid;
  }

  return proc_iter->second.GetThread(tid);
}

void ProcessCache::UncacheProcess(std::int32_t pid) {
  auto proc_iter = process_map_.find(pid);
  if (proc_iter != process_map_.end()) {
    process_map_.erase(proc_iter);
  }
}

void ProcessCache::Dump() const {
  auto iter = process_map_.begin();
  while (iter != process_map_.end()) {
    iter->second.Dump();
    iter++;
  }
}

}  // namespace qnx
}  // namespace perfetto
