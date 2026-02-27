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

#include "src/traced/qnx_probes/kernel/parser/trace_parser.h"

/*
 * some values from sys/trace.h
 * header event id is bits 0 - 9
 * header event class is bits 10 - 24
 * header cpu is bits 24 - 29
 * header struct bits 30 - 31
 *
 * _TRACE_TOT_CLASS_NUM = 11
 * _TRACE_MAX_EVENT_NUM = (0x00000400u) = 1024
 * _TRACE_MAX_KER_CALL_NUM = 128
 * _TRACE_MAX_TH_STATE_NUM = STATE_MAX + 2 = 26
 * == classes
 * _NTO_TRACE_PROCESS = 9
 * _NTO_TRACE_THREAD = 10
 * _NTO_TRACE_EMPTY = 0
 * _NTO_TRACE_EMPTYEVENT = 0
 * == event ids
 * #define _NTO_TRACE_PROCCREATE       (0x00000001u<<0) = 1
 * #define _NTO_TRACE_PROCDESTROY      (0x00000001u<<1) = 2
 * #define _NTO_TRACE_PROCCREATE_NAME  (0x00000001u<<2) = 4
 * #define _NTO_TRACE_PROCDESTROY_NAME (0x00000001u<<3) = 8
 * #define _NTO_TRACE_PROCTHREAD_NAME  (0x00000001u<<4) = 16
 */
namespace perfetto {
namespace qnx {

TraceParser::TraceParser(std::size_t pages,
                         int max_pages,
                         std::size_t trace_buffer_init_bytes)
    : event_callbacks_(),
      decoder_(std::bind(&TraceParser::OnEvent,
                         this,
                         std::placeholders::_1,
                         std::placeholders::_2,
                         std::placeholders::_3,
                         std::placeholders::_4),
               pages,
               max_pages,
               trace_buffer_init_bytes) {}

TraceParser::~TraceParser() {}

int TraceParser::Register(Callback callback,
                          unsigned event_class,
                          unsigned event_id) {
  if (event_class < kMaxClasses) {
    // Map the external class enum to the internally defined value - shift it by
    // ten bits to get the int value between 0 - _TRACE_TOT_CLASS_NUM (11) which
    // can be used as in index into the event callbacks matrix
    switch (event_class) {
      case _NTO_TRACE_EMPTY: {
        event_callbacks_[_TRACE_EMPTY_C >> 10][event_id] = callback;
        break;
      }
      case _NTO_TRACE_CONTROL: {
        event_callbacks_[_TRACE_CONTROL_C >> 10][event_id] = callback;
        break;
      }
      case _NTO_TRACE_KERCALL: {
        // register the callback for kernel call entry at index = id and exit at
        // index = id + _TRACE_MAX_KER_CALL_NUM
        event_callbacks_[_TRACE_KER_CALL_C >> 10][event_id] = callback;
        event_callbacks_[_TRACE_KER_CALL_C >> 10]
                        [event_id + _TRACE_MAX_KER_CALL_NUM] = callback;
        break;
      }
      case _NTO_TRACE_KERCALLENTER: {
        event_callbacks_[_TRACE_KER_CALL_C >> 10][event_id] = callback;
        break;
      }
      case _NTO_TRACE_KERCALLEXIT: {
        event_callbacks_[_TRACE_KER_CALL_C >> 10]
                        [event_id + _TRACE_MAX_KER_CALL_NUM] = callback;
        break;
      }
      case _NTO_TRACE_KERCALLINT: {
        // interrupt callback is at special index 2 * _TRACE_MAX_KER_CALL_NUM so
        // that it is beyond all the kernel call exit mappings
        event_callbacks_[_TRACE_KER_CALL_C >> 10]
                        [event_id + (2 * _TRACE_MAX_KER_CALL_NUM)] = callback;
        break;
      }
        //    case _NTO_TRACE_INT:
        //      {event_callbacks_[event_class][event_id] = callback;break;}
        //    case _NTO_TRACE_INTENTER:
        //      {event_callbacks_[event_class][event_id] = callback;break;}
        //    case _NTO_TRACE_INTEXIT:
        //      {event_callbacks_[event_class][event_id] = callback;break;}
      case _NTO_TRACE_PROCESS: {
        event_callbacks_[_TRACE_PR_TH_C >> 10]
                        [(GetRightmostBitIndex(event_id) + 1) << 6] = callback;
        break;
      }
      // Process and thread events are mapped based on the most significant bit
      // in the event id (GetRightmostBitIndex)
      case _NTO_TRACE_THREAD: {
        event_callbacks_[_TRACE_PR_TH_C >> 10][GetRightmostBitIndex(event_id)] =
            callback;
        break;
      }
      case _NTO_TRACE_VTHREAD: {
        event_callbacks_[_TRACE_PR_TH_C >> 10][GetRightmostBitIndex(event_id) +
                                               _TRACE_MAX_TH_STATE_NUM] =
            callback;
        break;
      }
      case _NTO_TRACE_USER: {
        event_callbacks_[_TRACE_USER_C >> 10][event_id] = callback;
        break;
      }
      case _NTO_TRACE_SYSTEM: {
        event_callbacks_[_TRACE_SYSTEM_C >> 10][event_id] = callback;
        break;
      }
      case _NTO_TRACE_COMM: {
        event_callbacks_[_TRACE_COMM_C >> 10][event_id] = callback;
        break;
      }
        //    case _NTO_TRACE_INT_HANDLER_ENTER:
        //      {event_callbacks_[event_class][event_id] = callback;break;}
        //    case _NTO_TRACE_INT_HANDLER_EXIT:
        //      {event_callbacks_[event_class][event_id] = callback;break;}
      case _NTO_TRACE_QUIP: {
        event_callbacks_[_TRACE_QUIP_C >> 10][event_id] = callback;
        break;
      }
      case _NTO_TRACE_SEC: {
        event_callbacks_[_TRACE_SEC_C >> 10][event_id] = callback;
        break;
      }
      case _NTO_TRACE_QVM: {
        event_callbacks_[_TRACE_QVM_C >> 10][event_id] = callback;
        break;
      }
      default: {
        return -1;
      }
    }
  }

  return 0;
}

void TraceParser::InsertCallbackForRange(Callback callback,
                                         unsigned class_value,
                                         unsigned event_begin,
                                         unsigned event_end) {
  for (unsigned event_value = event_begin; event_value <= event_end;
       event_value++) {
    event_callbacks_[class_value][event_value] = callback;
  }
}

int TraceParser::Register(Callback callback,
                          unsigned event_class,
                          unsigned event_id_start,
                          unsigned event_id_end) {
  if (event_class >= kMaxClasses || event_id_end < event_id_start) {
    return -1;
  }

  switch (event_class) {
    case _NTO_TRACE_EMPTY: {
      InsertCallbackForRange(callback, _TRACE_EMPTY_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
    case _NTO_TRACE_CONTROL: {
      InsertCallbackForRange(callback, _TRACE_CONTROL_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
    case _NTO_TRACE_KERCALL: {
      // Register the callback for kernel call entry at index = id and exit at
      // index = id + _TRACE_MAX_KER_CALL_NUM
      InsertCallbackForRange(callback, _TRACE_KER_CALL_C >> 10, event_id_start,
                             event_id_end);
      InsertCallbackForRange(callback, _TRACE_KER_CALL_C >> 10,
                             event_id_start + _TRACE_MAX_KER_CALL_NUM,
                             event_id_end + _TRACE_MAX_KER_CALL_NUM);
      break;
    }
    case _NTO_TRACE_KERCALLENTER: {
      InsertCallbackForRange(callback, _TRACE_KER_CALL_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
    case _NTO_TRACE_KERCALLEXIT: {
      InsertCallbackForRange(callback, _TRACE_KER_CALL_C >> 10,
                             event_id_start + _TRACE_MAX_KER_CALL_NUM,
                             event_id_end + _TRACE_MAX_KER_CALL_NUM);
      break;
    }
    case _NTO_TRACE_KERCALLINT: {
      // Interrupt callback is at special index 2 * _TRACE_MAX_KER_CALL_NUM so
      // that it is beyond all the kernel call exit mappings
      InsertCallbackForRange(callback, _TRACE_KER_CALL_C >> 10,
                             event_id_start + (2 * _TRACE_MAX_KER_CALL_NUM),
                             event_id_end + (2 * _TRACE_MAX_KER_CALL_NUM));
      break;
    }
    case _NTO_TRACE_PROCESS: {
      InsertCallbackForRange(callback, _TRACE_PR_TH_C >> 10,
                             (GetRightmostBitIndex(event_id_start) + 1) << 6,
                             (GetRightmostBitIndex(event_id_end) + 1) << 6);
      break;
    }
    case _NTO_TRACE_THREAD: {
      InsertCallbackForRange(callback, _TRACE_PR_TH_C >> 10,
                             GetRightmostBitIndex(event_id_start),
                             GetRightmostBitIndex(event_id_end));
      break;
    }
    case _NTO_TRACE_VTHREAD: {
      InsertCallbackForRange(
          callback, _TRACE_PR_TH_C >> 10,
          GetRightmostBitIndex(event_id_start) + _TRACE_MAX_TH_STATE_NUM,
          GetRightmostBitIndex(event_id_end) + _TRACE_MAX_TH_STATE_NUM);
      break;
    }
    case _NTO_TRACE_USER: {
      InsertCallbackForRange(callback, _TRACE_USER_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
    case _NTO_TRACE_SYSTEM: {
      InsertCallbackForRange(callback, _TRACE_SYSTEM_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
    case _NTO_TRACE_COMM: {
      InsertCallbackForRange(callback, _TRACE_COMM_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
      //    case _NTO_TRACE_INT_HANDLER_ENTER:
      //      {event_callbacks_[event_class][event_id] = callback;break;}
      //    case _NTO_TRACE_INT_HANDLER_EXIT:
      //      {event_callbacks_[event_class][event_id] = callback;break;}
    case _NTO_TRACE_QUIP: {
      InsertCallbackForRange(callback, _TRACE_QUIP_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
    case _NTO_TRACE_SEC: {
      InsertCallbackForRange(callback, _TRACE_SEC_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
    case _NTO_TRACE_QVM: {
      InsertCallbackForRange(callback, _TRACE_QVM_C >> 10, event_id_start,
                             event_id_end);
      break;
    }
    default: {
      return -1;
    }
  }

  return 0;
}

int TraceParser::Parse(size_t data_size, void* data) {
  int rc = decoder_.Decode(data_size, data);
  // An error occurred so return 0 bytes processed
  if (rc < 0) {
    return 0;
  }

  // Decoder has processed the data so return the full data size
  return static_cast<int>(data_size);
}

int TraceParser::Finish() {
  decoder_.Finish();
  stats_.event_parsed_ = decoder_.GetEventsDecoded();
  return 0;
}

void TraceParser::OnEvent(std::uint32_t header,
                          std::uint64_t timestamp,
                          const std::uint32_t* data,
                          std::size_t data_size) {
  // Find the callback associated with the event class/id and invoke it.
  // Ensure the event is a TIME CONTROL event.
  std::uint32_t event_class_idx = (_NTO_TRACE_GETEVENT_C(header) >> 10);
  std::uint32_t event_id = _NTO_TRACE_GETEVENT(header);

  if (event_class_idx >= kMaxClasses || event_id >= kMaxEvents) {
    return;
  }

  if (event_callbacks_[event_class_idx][event_id]) {
    event_callbacks_[event_class_idx][event_id](header, timestamp, data,
                                                data_size);
    stats_.callback_call_count_++;
  } else {  // No callback registered.
    stats_.callback_miss_count_++;
  }
}

/**
 *  Finds the index of the rightmost bit that is set in a 32 bit integer
 *
 * @param k The 32 bit interger in which to find the rightmost bit that is set
 * @returns the index of the rightmost bit or -1 if no bits are set!!!
 */
int TraceParser::GetRightmostBitIndex(std::uint32_t k) {
  if (k) {
    int s = 0U;

    while (!(k & 0x1)) {
      ++s;
      k >>= 1;
    }

    return (s);
  }

  return -1;
}

}  // namespace qnx
}  // namespace perfetto
