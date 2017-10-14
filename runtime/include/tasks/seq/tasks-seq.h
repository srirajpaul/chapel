/*
 * Copyright 2004-2016 Cray Inc.
 * Other additional copyright holders may be indicated within.
 * Copyright 2016 Rice University
 * 
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 * 
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _tasks_seq_h_
#define _tasks_seq_h_

#include "chpl-threads.h"

#ifdef __cplusplus
extern "C" {
#endif

// type (and default value) used to communicate task identifiers
// between C code and Chapel code in the runtime.
typedef unsigned int chpl_taskID_t;
#define chpl_nullTaskID 0
#ifndef CHPL_TASK_ID_STRING_MAX_LEN
#define CHPL_TASK_ID_STRING_MAX_LEN 21
#endif

//
// Task layer private area argument bundle header
//
typedef struct {
  chpl_bool serial_state;
  chpl_bool countRunning;
  chpl_bool is_executeOn;
  int lineno;
  int filename;
  c_sublocid_t requestedSubloc;
  chpl_fn_int_t requested_fid;
  chpl_fn_p requested_fn;
  chpl_taskID_t id;
} chpl_task_bundle_t;

//
// Sync variables
//
typedef struct {
  volatile chpl_bool  is_full;
} chpl_sync_aux_t;

#ifdef __cplusplus
} // end extern "C"
#endif

#endif
