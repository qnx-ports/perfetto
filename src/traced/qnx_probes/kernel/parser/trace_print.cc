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

#include "src/traced/qnx_probes/kernel/parser/trace_print.h"

#include <iostream>

namespace perfetto {
namespace qnx {

const char* EVENT_CLASS_NAMES[] = {
    "_TRACE_EMPTY_C", "_TRACE_CONTROL_C", "_TRACE_KER_CALL_C",  "_TRACE_INT_C",
    "_TRACE_PR_TH_C", "_TRACE_SYSTEM_C",  "_TRACE_CONTAINER_C", "_TRACE_USER_C",
    "_TRACE_COMM_C",  "_TRACE_QUIP_C",    "_TRACE_SEC_C",       "_TRACE_QVM_C",
    "Unknown"};

const char* PR_TH_C_EVENT_NAMES[] = {"Unknown",
                                     "_TRACE_PR_TH_CREATE_P",
                                     "_TRACE_PR_TH_DESTROY_P",
                                     "_TRACE_PR_TH_CREATE_P_NAME",
                                     "_TRACE_PR_TH_DESTROY_P_NAME",
                                     "_TRACE_PR_TH_NAME_T"};

const char* THREAD_STATE_NAMES[] = {
    "STATE_DEAD",        /* 0	0x00 */
    "STATE_RUNNING",     /* 1	0x01 */
    "STATE_READY",       /* 2	0x02 */
    "STATE_STOPPED",     /* 3	0x03 */
    "STATE_SEND",        /* 4	0x04 */
    "STATE_RECEIVE",     /* 5	0x05 */
    "STATE_REPLY",       /* 6	0x06 */
    "STATE_STACK",       /* 7	0x07 */
    "STATE_WAITTHREAD",  /* 8	0x08 */
    "STATE_WAITPAGE",    /* 9	0x09 */
    "STATE_SIGSUSPEND",  /* 10	0x0a */
    "STATE_SIGWAITINFO", /* 11	0x0b */
    "STATE_NANOSLEEP",   /* 12	0x0c */
    "STATE_MUTEX",       /* 13	0x0d */
    "STATE_CONDVAR",     /* 14	0x0e */
    "STATE_JOIN",        /* 15	0x0f */
    "STATE_INTR",        /* 16	0x10 */
    "STATE_SEM",         /* 17	0x11 */
    "STATE_WAITCTX",     /* 18	0x12 */
    "STATE_NET_SEND",    /* 19	0x13 */
    "STATE_NET_REPLY",   /* 20	0x14 */

    "EMPTY", /* 21 */
    "EMPTY", /* 22 */
    "EMPTY", /* 23 */

    "_TRACE_THREAD_CREATE", /*24*/
    "_TRACE_THREAD_DESTROY" /*25*/

    //_TRACE_MAX_TH_STATE_NUM
};

const char* CONTROL_EVENT_NAMES[] = {"Unknown", "_TRACE_CONTROL_TIME",
                                     "_TRACE_CONTROL_BUFFER"};

const char* SYSTEM_EVENT_NAMES[] = {
    "Unknown",
    "_NTO_TRACE_SYS_RESERVED",      //(0x00000001u)
    "_NTO_TRACE_SYS_PATHMGR",       //(0x00000002u)
    "_NTO_TRACE_SYS_APS_NAME",      //(0x00000003u)
    "_NTO_TRACE_SYS_APS_BUDGETS",   //(0x00000004u)
    "_NTO_TRACE_SYS_APS_BNKR",      //(0x00000005) /* when APS scheduler detects
                                    // bankruptcy */
    "_NTO_TRACE_SYS_MMAP",          //(0x00000006u)
    "_NTO_TRACE_SYS_MUNMAP",        //(0x00000007u)
    "_NTO_TRACE_SYS_MAPNAME",       //(0x00000008u)
    "_NTO_TRACE_SYS_ADDRESS",       //(0x00000009u)
    "_NTO_TRACE_SYS_FUNC_ENTER",    //(0x0000000au)
    "_NTO_TRACE_SYS_FUNC_EXIT",     //(0x0000000bu)
    "_NTO_TRACE_SYS_SLOG",          //(0x0000000cu)
    "_NTO_TRACE_SYS_DEFRAG_START",  //(0x0000000du)
    "_NTO_TRACE_SYS_RUNSTATE",      //(0x0000000eu)
    "_NTO_TRACE_SYS_POWER",         //(0x0000000fu)
    "_NTO_TRACE_SYS_IPI",           //(0x00000010u)
    "_NTO_TRACE_SYS_PAGEWAIT",      //(0x00000011u)
    "_NTO_TRACE_SYS_TIMER",         //(0x00000012u)
    "_NTO_TRACE_SYS_DEFRAG_END",    //(0x00000013u)
    "_NTO_TRACE_SYS_PROFILE",       //(0x00000014u)
    "_NTO_TRACE_SYS_MAPNAME_64",    //(0x00000015u)
    "_NTO_TRACE_SYS_APS_PSTATS",    //(0x00000016u)
    "_NTO_TRACE_SYS_APS_OSTATS",    //(0x00000017u)
    "_NTO_TRACE_SYS_APS_INFO",      //(0x00000018u)
    "_NTO_TRACE_SYS_APS_JOIN",      //(0x00000019u)
    "_NTO_TRACE_SYS_APS_THREAD",    //(0x0000001au)
    "_NTO_TRACE_SYS_APS_PROCESS",   //(0x0000001bu)
    "_NTO_TRACE_SYS_SCHED_CONF",    //(0x0000001cu)
    // "_NTO_TRACE_SYS_LAST",		      //_NTO_TRACE_SYS_SCHED_CONF
    // "_NTO_TRACE_SYS_IPI_64", //(_NTO_TRACE_SYS_IPI|_NTO_TRACE_KERCALL64)
    // "_NTO_TRACE_SYS_PROFILE_64",
    // //(_NTO_TRACE_SYS_PROFILE|_NTO_TRACE_KERCALL64)
    // "_NTO_TRACE_SYS_COMPACTION" 	  //_NTO_TRACE_SYS_DEFRAG_START
};

const char* MSG_STRUCT_TYPES[] = {
    "_TRACE_STRUCT_S",   //            (0x00000000u)
    "_TRACE_STRUCT_CB",  //            (0x00000001u<<30)
    "_TRACE_STRUCT_CC",  //            (0x00000002u<<30)
    "_TRACE_STRUCT_CE"   //            (0x00000003u<<30)
};

TracePrint::TracePrint(std::uint32_t header,
                       std::uint32_t timestamp,
                       std::size_t data_size,
                       const std::uint32_t* data)
    : header_(header),
      timestamp_(timestamp),
      data_size_(data_size),
      data_(data) {}

TracePrint::TracePrint(traceevent_t* event)
    : header_(event->header),
      timestamp_(event->data[0]),
      data_size_(2),
      data_(&event->data[1]) {}

const char* TracePrint::GetClassName(std::uint32_t header) {
  std::uint32_t event_class = _NTO_TRACE_GETEVENT_C(header);
  std::uint32_t event_index = event_class >> 10;
  if (event_index >= _TRACE_TOT_CLASS_NUM) {
    event_index = _TRACE_TOT_CLASS_NUM;
  }

  return EVENT_CLASS_NAMES[event_index];
}

const char* TracePrint::GetEventName(std::uint32_t header) {
  std::uint32_t event_class = _NTO_TRACE_GETEVENT_C(header);
  std::uint32_t event_id = _NTO_TRACE_GETEVENT(header);

  if (event_class == _TRACE_PR_TH_C) {
    if (event_id > _TRACE_MAX_TH_STATE_NUM) {
      if (event_id <= 320) {
        event_id = event_id >> 6;
        return PR_TH_C_EVENT_NAMES[event_id];
      } else {
        return THREAD_STATE_NAMES[0];
      }
    }
    return THREAD_STATE_NAMES[event_id];
  }

  if (event_class == _TRACE_CONTROL_C) {
    if (event_id > _TRACE_CONTROL_BUFFER) {
      return CONTROL_EVENT_NAMES[0];
    }
    return CONTROL_EVENT_NAMES[event_id];
  }

  if (event_class == _TRACE_SYSTEM_C) {
    if (event_id > _NTO_TRACE_SYS_SCHED_CONF) {
      if (event_id == (_NTO_TRACE_SYS_IPI | _NTO_TRACE_KERCALL64)) {
        return "_NTO_TRACE_SYS_IPI_64";
      } else if (event_id == (_NTO_TRACE_SYS_PROFILE | _NTO_TRACE_KERCALL64)) {
        return "_NTO_TRACE_SYS_PROFILE_64";
      }
      return SYSTEM_EVENT_NAMES[0];
    }
    return SYSTEM_EVENT_NAMES[event_id];
  }

  return PR_TH_C_EVENT_NAMES[0];
}

const char* TracePrint::GetStructName(std::uint32_t header) {
  std::uint32_t msg_type = _TRACE_GET_STRUCT(header);
  std::uint32_t msg_type_index = msg_type >> 30;

  if (msg_type_index > (_TRACE_STRUCT_CE >> 30)) {
    return PR_TH_C_EVENT_NAMES[0];
  }

  return MSG_STRUCT_TYPES[msg_type_index];
}

std::ostream& operator<<(std::ostream& os, const TracePrint& event) {
  std::uint32_t event_class = _NTO_TRACE_GETEVENT_C(event.header_);
  std::uint32_t event_class_index = event_class >> 10;
  os << "\"Event\": {"
     << "\"class\": " << TracePrint::GetClassName(event.header_) << ":"
     << event_class << ":" << event_class_index
     << ", \"id\": " << TracePrint::GetEventName(event.header_) << ":"
     << _NTO_TRACE_GETEVENT(event.header_) << ", \"ts\": " << event.timestamp_
     << ", \"data_size\": " << event.data_size_ << ", \"data\": [";

  for (size_t i = 0; i < event.data_size_; i++) {
    if (i != 0) {
      os << ", ";
    }
    os << ((std::uint32_t*)event.data_)[i];
  }
  os << "]"
     << ", \"struct\": " << TracePrint::GetStructName(event.header_)
     << ", \"header\": " << event.header_ << "}";

  return os;
}

}  // namespace qnx
}  // namespace perfetto
