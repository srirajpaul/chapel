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

#ifndef _tasks_hclib_h_
#define _tasks_hclib_h_

#include <hclib.h>

#include "chpl-threads.h"
#include "chpl-atomics.h"

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

#define U64_COUNT(size) ((size+(sizeof(u64)-1))/sizeof(u64))

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
// Condition variables
//
// typedef pthread_cond_t chpl_thread_condvar_t;

#define USE_TICKET_LOCK
// #define USE_SV_TICKET_LOCK 
//  * If defined, USE_TICKET_LOCK must be defined too
//  * TODO: currently this only supports regular access patterns, i.e., readFE and writeEF

#ifdef USE_TICKET_LOCK
// Machine parameters
#define TL_PAR 12        // Number of physical threads that run in parallel (# cores)
#define TL_LINE_SIZE 32  // Number of 4-byte int variables that fit in a cache line

// Finite count spin lock followed by sleep-and-awake synchronizations on timeout
#define TL_FINITE_SPIN
#define TL_SPIN_COUNT 10000  // Tuning parameters

typedef enum { TL_GENERAL, TL_READ, TL_WRITE } ticket_lock_type;
#ifdef USE_SV_TICKET_LOCK
typedef enum { SV_EMPTY, SV_FULL, SV_TRANSIT } sync_var_state;
#endif

typedef struct _sync_list_node_t {
    hclib_promise_t *prom;
    struct _sync_list_node_t *next;
    uint_least32_t ticket;
} sync_list_node_t;

// Sync variables
//
typedef struct {
    pthread_mutex_t lock;
    sync_list_node_t *wait_list;
    atomic_uint_least32_t ticket, minSleep;
    volatile uint_least32_t arrCurrent[TL_PAR*TL_LINE_SIZE];
    uint_least32_t current;
#ifdef USE_SV_TICKET_LOCK
    pthread_mutex_t lockR, lockW;
    sync_list_node_t *wait_listW, *wait_listR;
    atomic_uint_least32_t ticketR, ticketW, minSleepR, minSleepW;
    volatile uint_least32_t arrCurrentR[TL_PAR*TL_LINE_SIZE], arrCurrentW[TL_PAR*TL_LINE_SIZE];
    uint_least32_t currentR, currentW;
    volatile sync_var_state state;
#else
    volatile chpl_bool  is_full;
#endif
} chpl_sync_aux_t;

#else // USE_TICKET_LOCK
typedef struct _sync_list_node_t {
    hclib_promise_t *prom;
    struct _sync_list_node_t *next;
} sync_list_node_t;

// Sync variables
//
typedef struct {
    volatile chpl_bool  is_full;
    pthread_mutex_t lock;
    sync_list_node_t *wait_list;
    int active_critical_section;
} chpl_sync_aux_t;
#endif

#ifdef CHPL_TASK_SUPPORTS_REMOTE_CACHE_IMPL_DECL
#error "CHPL_TASK_SUPPORTS_REMOTE_CACHE_IMPL_DECL is already defined!"
#else
#define CHPL_TASK_SUPPORTS_REMOTE_CACHE_IMPL_DECL 1
#endif
static inline
int chpl_task_supportsRemoteCache(void) {
  return 0;
}
    
#ifdef __cplusplus
} // end extern "C"
#endif

#endif
