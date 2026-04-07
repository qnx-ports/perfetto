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
    kInvalidId, kInvalidUpdateTime, kInvalidCpuId,
    ThreadStateEnum::TASK_STATE_UNKNOWN, kInvalidName};

ThreadInfo::ThreadInfo(std::int32_t tid,
                       std::uint64_t update_ts,
                       std::uint32_t cpu_id,
                       ThreadStateEnum state,
                       const std::string& name)
    : tid_(tid),
      update_ts_(update_ts),
      cpu_id_(cpu_id),
      state_(state),
      name_(name) {}

void ThreadInfo::SetState(ThreadStateEnum state) {
  if (state != ThreadStateEnum::TASK_STATE_UNKNOWN) {
    state_ = state;
  }
}

void ThreadInfo::SetName(const std::string& name) {
  if (name != kInvalidName) {
    name_ = name;
  }
}

void ThreadInfo::Dump(std::ostream& os) const {
  os << "ThreadInfo{tid=" << GetId() << ", state=";

  switch (GetState()) {
    case ThreadStateEnum::TASK_STATE_CREATED:
      os << "CREATED";
      break;
    case ThreadStateEnum::TASK_STATE_DEAD:
      os << "DEAD";
      break;
    case ThreadStateEnum::TASK_STATE_DESTROYED:
      os << "DESTROYED";
      break;
    case ThreadStateEnum::TASK_STATE_INTERRUPTIBLE_SLEEP:
      os << "INTERRUPTIBLE_SLEEP";
      break;
    case ThreadStateEnum::TASK_STATE_RUNNABLE:
      os << "RUNNABLE";
      break;
    case ThreadStateEnum::TASK_STATE_RUNNING:
      os << "RUNNING";
      break;
    case ThreadStateEnum::TASK_STATE_STOPPED:
      os << "STOPPED";
      break;
    case ThreadStateEnum::TASK_STATE_UNINTERRUPTIBLE_SLEEP:
      os << "UNINTERRUPTIBLE_SLEEP";
      break;
    default:
      os << "UNKNOWN";
      break;
  }

  os << ", name=" << GetName() << "}";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const ProcessInfo ProcessInfo::kInvalid{kInvalidId, kInvalidId, kInvalidName};

ProcessInfo::ProcessInfo(std::int32_t pid,
                         std::int32_t parent_pid,
                         const std::string& name)
    : pid_(pid), parent_pid_(parent_pid), name_(name) {}

void ProcessInfo::SetParentId(std::int32_t parent_pid) {
  if (parent_pid != kInvalidId) {
    parent_pid_ = parent_pid;
  }
}

void ProcessInfo::SetName(const std::string& name) {
  if (name != kInvalidName) {
    name_ = name;
  }
}

void ProcessInfo::RemoveThread(std::int32_t tid) {
  auto iter = threads_.find(tid);
  if (iter != threads_.end()) {
    threads_.erase(iter);
  }
}

bool ProcessInfo::UpdateThread(const ThreadInfo& thread_info) {
  if (thread_info.GetId() == kInvalidId) {
    return false;
  }

  // Check if we are creating a thread of if no thread exists.
  auto iter = threads_.find(thread_info.GetId());
  if (thread_info.GetState() == ThreadStateEnum::TASK_STATE_CREATED) {
    if (iter == threads_.end()) {
      threads_.insert_or_assign(thread_info.GetId(), thread_info);
      return true;
    } else {
      return false;  // duplicate create call
    }
  }

  if (iter == threads_.end()) {
    // No thread to associate the event with
    return false;
  }

  /**
   * QNX 7.1 and QNX8.0 have 2 different behaviours when a process is being
   * terminated
   *
   * When a process is terminated on QNX 7.1 multiple THDESTROY and THDEAD event
   * can be emitted. This means we only want to accept the first one.
   *
   * THREADY
   * THRUNNING
   * THDESTROY
   * THDEAD
   * THSTACK
   * THREADY
   * THRUNNING
   * THDESTROY
   * THDEAD
   * THDESTROY
   *
   * When a process is terminated on QNX8.0 we still see multiple THDEAD but
   * only a single destroy following a similar patter with the last 3 events
   * only occurring If the thread wasn't waiting to be joined on exit.
   *
   * THREADY
   * THRUNNING
   * THDESTROY
   * THDEAD
   * THREADY
   * THRUNNING
   * THDEAD
   *
   * The common usage between them is
   *
   * DESTROY  // Ignore as comes as the same time as dead.
   * DEAD
   * ...
   * DEAD (only shows up in QNX8.0 If thread wasn't waiting to be joined)
   * 
   * Here we look for two THDEAD events -- the second indicates the thread is 
   * destroyed. We ignore (don't publish states between the first THDEAD and the
   * second).
   */
  bool update_required = false;
  auto& cur_thread = iter->second;
  const auto old_state = cur_thread.GetState();
  auto new_state = thread_info.GetState();
  if (old_state == ThreadStateEnum::TASK_STATE_DEAD) {
    // Second time we got thread dead which means it was joined so send the
    // destroy state.
    if (new_state == ThreadStateEnum::TASK_STATE_DEAD) {
      update_required = true;
      new_state = ThreadStateEnum::TASK_STATE_DESTROYED;
    }
  } else {
    // If both threads are alive update as long as the state changed
    update_required = (new_state != old_state);
  }

  if (update_required) {
    cur_thread.SetUpdateTime(thread_info.GetUpdateTime());
    cur_thread.SetCpuId(thread_info.GetCpuId());
    cur_thread.SetName(thread_info.GetName());
    cur_thread.SetState(new_state);
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

void ProcessInfo::Dump(std::ostream& os) const {
  os << "ProcessInfo{pid=" << GetId() << ", ppid=" << GetParentId()
            << ", name=" << GetName() << ", threads[";

  auto iter = threads_.begin();
  while (iter != threads_.end()) {
    os << std::endl;
    os << "\t";
    iter->second.Dump(os);
    iter++;
  }
  os << "]" << std::endl;
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

const ProcessInfo& ProcessCache::GetProcess(std::int32_t pid) {
  auto iter = process_map_.find(pid);
  if (iter != process_map_.end()) {
    return iter->second;
  }
  return ProcessInfo::kInvalid;
}

bool ProcessCache::CacheThread(std::int32_t pid,
                               std::int32_t tid,
                               std::uint64_t update_ts,
                               std::uint32_t cpu_id,
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
  ThreadInfo thread_info(tid, update_ts, cpu_id, state, name);
  return iter->second.UpdateThread(thread_info);
}

void ProcessCache::UncacheThread(std::int32_t pid, std::int32_t tid) {
  auto iter = process_map_.find(pid);
  if (iter == process_map_.end()) {
    return;
  }
  iter->second.RemoveThread(tid);
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

void ProcessCache::Dump(std::ostream& os) const {
  auto iter = process_map_.begin();
  while (iter != process_map_.end()) {
    iter->second.Dump(os);
    iter++;
  }
}

}  // namespace qnx
}  // namespace perfetto
