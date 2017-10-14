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

//
// OCR implementation of Chapel tasking interface
//

//#include "chplrt.h"
//#include "chpl_rt_utils_static.h"
//#include "chplcgfns.h"
//#include "chpl-comm.h"
//#include "chplexit.h"
//#include "chpl-locale-model.h"
// #include "chpl-mem.h"
#include "chpl-tasks.h"
//#include "chpl-tasks-callbacks-internal.h"
//#include "chplsys.h"
//#include "chpl-linefile-support.h"
//#include "error.h"
//#include <stdio.h>
//#include <string.h>
//#include <signal.h>
#include <inttypes.h>
//#include <errno.h>
//#include <sys/time.h>
//#include <sys/mman.h>
//#include <unistd.h>
//#include <math.h>

// #define CHAPEL_TRACE
//#define CHAPEL_PROFILE
#ifdef CHAPEL_PROFILE
# define PROFILE_INCR(counter,count)  {__sync_fetch_and_add(&counter,count); }

/* Tasks */
static long int profile_task_yield = 0;
static long int profile_task_addToTaskList = 0;
static long int profile_task_executeTasksInList = 0;
static long int profile_task_taskCallFTable = 0;
static long int profile_task_startMovedTask = 0;
static long int profile_task_getId = 0;
static long int profile_task_sleep = 0;
static long int profile_task_getSerial = 0;
static long int profile_task_setSerial = 0;
static long int profile_task_getCallStackSize = 0;
static long int profile_task_getMaxPar = 0;
/* Sync */
static long int profile_sync_lock= 0;
static long int profile_sync_unlock= 0;
static long int profile_sync_waitFullAndLock= 0;
static long int profile_sync_waitEmptyAndLock= 0;
static long int profile_sync_markAndSignalFull= 0;
static long int profile_sync_markAndSignalEmpty= 0;
static long int profile_sync_isFull= 0;
static long int profile_sync_initAux= 0;
static long int profile_sync_destroyAux= 0;

static void profile_print(void)
{
    /* Tasks */
    fprintf(stderr, "task yield: %lu\n", (unsigned long)profile_task_yield);
    fprintf(stderr, "task addToTaskList: %lu\n", (unsigned long)profile_task_addToTaskList);
    fprintf(stderr, "task executeTasksInList: %lu\n", (unsigned long)profile_task_executeTasksInList);
    fprintf(stderr, "task taskCallFTable: %lu\n", (unsigned long)profile_task_taskCallFTable);
    fprintf(stderr, "task startMovedTask: %lu\n", (unsigned long)profile_task_startMovedTask);
    fprintf(stderr, "task getId: %lu\n", (unsigned long)profile_task_getId);
    fprintf(stderr, "task sleep: %lu\n", (unsigned long)profile_task_sleep);
    fprintf(stderr, "task getSerial: %lu\n", (unsigned long)profile_task_getSerial);
    fprintf(stderr, "task setSerial: %lu\n", (unsigned long)profile_task_setSerial);
    fprintf(stderr, "task getCallStackSize: %lu\n", (unsigned long)profile_task_getCallStackSize);
    fprintf(stderr, "task getMaxPar: %lu\n", (unsigned long)profile_task_getMaxPar);
    /* Sync */
    fprintf(stderr, "sync lock: %lu\n", (unsigned long)profile_sync_lock);
    fprintf(stderr, "sync unlock: %lu\n", (unsigned long)profile_sync_unlock);
    fprintf(stderr, "sync waitFullAndLock: %lu\n", (unsigned long)profile_sync_waitFullAndLock);
    fprintf(stderr, "sync waitEmptyAndLock: %lu\n", (unsigned long)profile_sync_waitEmptyAndLock);
    fprintf(stderr, "sync markAndSignalFull: %lu\n", (unsigned long)profile_sync_markAndSignalFull);
    fprintf(stderr, "sync markAndSignalEmpty: %lu\n", (unsigned long)profile_sync_markAndSignalEmpty);
    fprintf(stderr, "sync isFull: %lu\n", (unsigned long)profile_sync_isFull);
    fprintf(stderr, "sync initAux: %lu\n", (unsigned long)profile_sync_initAux);
    fprintf(stderr, "sync destroyAux: %lu\n", (unsigned long)profile_sync_destroyAux);
}
#else
# define PROFILE_INCR(counter,count)
#endif /* CHAPEL_PROFILE */


#define INTERNAL_ASSERT(my_cond) { \
    if (!(my_cond)) { \
        fprintf(stderr, "Failed assertion at %s:%d\n", __FILE__, __LINE__); \
        abort(); \
    } \
}

static chpl_taskID_t task_id_counter = 1;

void chpl_sync_lock(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_lock, 1);

  // No need to lock if everything runs sequentially
}

void chpl_sync_unlock(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_unlock, 1);

  // No need to unlock sequentially
}

void chpl_sync_waitFullAndLock(chpl_sync_aux_t *s,
                                  int32_t lineno, int32_t filename) {
  PROFILE_INCR(profile_sync_waitFullAndLock, 1);

  INTERNAL_ASSERT(false);
}

void chpl_sync_waitEmptyAndLock(chpl_sync_aux_t *s,
                                   int32_t lineno, int32_t filename) {
  PROFILE_INCR(profile_sync_waitEmptyAndLock, 1);

  INTERNAL_ASSERT(false);
}

void chpl_sync_markAndSignalFull(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_markAndSignalFull, 1);

  INTERNAL_ASSERT(false);
}

void chpl_sync_markAndSignalEmpty(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_markAndSignalEmpty, 1);

  INTERNAL_ASSERT(false);
}

chpl_bool chpl_sync_isFull(void *val_ptr,
                            chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_isFull, 1);
  return s->is_full;
}

void chpl_sync_initAux(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_initAux, 1);
  s->is_full = false;
}

void chpl_sync_destroyAux(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_destroyAux, 1);
}

void chpl_task_init(void) {
}

void chpl_task_exit(void) {
#ifdef CHAPEL_PROFILE
    profile_print();
#endif /* CHAPEL_PROFILE */
}

void chpl_task_callMain(void (*chpl_main)(void)) {
    chpl_main();
}

int chpl_task_createCommTask(chpl_fn_p fn, void* arg) {
  INTERNAL_ASSERT(false);
  return 0;
}

static void taskCaller(void *data) {
    chpl_task_bundle_t *bundle = (chpl_task_bundle_t *)data;
    chpl_task_setSerial(bundle->serial_state);
    (bundle->requested_fn)(bundle);
}

void chpl_task_addToTaskList(chpl_fn_int_t fid,
                             chpl_task_bundle_t* arg, size_t arg_size,
                             c_sublocid_t subloc,
                             void** p_task_list_void,
                             int32_t task_list_locale,
                             chpl_bool is_begin_stmt,
                             int lineno,
                             int32_t filename) {
  PROFILE_INCR(profile_task_addToTaskList,1);

  (chpl_ftable[fid])(arg);
}

void chpl_task_executeTasksInList(void** p_task_list_void) {
  PROFILE_INCR(profile_task_executeTasksInList,1);
}


void chpl_task_taskCallFTable(chpl_fn_int_t fid,
                        chpl_task_bundle_t* arg, size_t arg_size,
                        c_sublocid_t subloc,
                        int lineno, int32_t filename) {
  PROFILE_INCR(profile_task_taskCallFTable,1);
  INTERNAL_ASSERT(false);
}

void chpl_task_startMovedTask(chpl_fn_int_t  fid, chpl_fn_p fp,
                              chpl_task_bundle_t* arg, size_t arg_size,
                              c_sublocid_t subloc,
                              chpl_taskID_t id,
                              chpl_bool serial_state) {
  PROFILE_INCR(profile_task_startMovedTask,1);
  INTERNAL_ASSERT(false);
}


//
// chpl_task_getSubloc() is in tasks-fifo.h.
//


//
// chpl_task_setSubloc() is in tasks-fifo.h.
//


//
// chpl_task_getRequestedSubloc() is in tasks-fifo.h.
//


chpl_taskID_t chpl_task_getId(void) {
  return 1;
}

chpl_bool chpl_task_idEquals(chpl_taskID_t id1, chpl_taskID_t id2) {
    return id1 == id2;
}

char* chpl_task_idToString(char* buff, size_t size, chpl_taskID_t id) {
  int ret = snprintf(buff, size, "%u", id);
  if(ret>0 && ret<size)
    return buff;
  else
    return NULL;
}

void chpl_task_yield(void) {
  PROFILE_INCR(profile_task_yield,1);
  INTERNAL_ASSERT(false);
}

void chpl_task_sleep(double secs) {
  PROFILE_INCR(profile_task_sleep,1);
  INTERNAL_ASSERT(false);
}

// TODO Support serial tasks
chpl_bool chpl_task_getSerial(void) {
  PROFILE_INCR(profile_task_getSerial,1);

  return false;
}

// TODO Allow tasks to be set to serial, not incurring launch overhead
void chpl_task_setSerial(chpl_bool state) {
  PROFILE_INCR(profile_task_setSerial,1);

  return;
}

uint32_t chpl_task_getMaxPar(void) {
  PROFILE_INCR(profile_task_getMaxPar,1);
  return 1;
}

c_sublocid_t chpl_task_getNumSublocales(void) {
  INTERNAL_ASSERT(false);
  return 0;
}

chpl_task_prvData_t* chpl_task_getPrvData(void) {
  INTERNAL_ASSERT(false);
  return NULL;
}

size_t chpl_task_getCallStackSize(void) {
  PROFILE_INCR(profile_task_getCallStackSize,1);
  return chpl_thread_getCallStackSize();
}

uint32_t chpl_task_getNumQueuedTasks(void) {
    INTERNAL_ASSERT(false);
    return 0;
}

uint32_t chpl_task_getNumRunningTasks(void) {
  INTERNAL_ASSERT(false);
  return 0;
}

int32_t  chpl_task_getNumBlockedTasks(void) {
  INTERNAL_ASSERT(false);
  return 0;
}

// Threads

uint32_t chpl_task_getNumThreads(void) {
  INTERNAL_ASSERT(false);
  return 0;
}

uint32_t chpl_task_getNumIdleThreads(void) {
  INTERNAL_ASSERT(false);
  return 0;
}

