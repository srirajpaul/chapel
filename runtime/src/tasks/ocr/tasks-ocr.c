/*
 * Copyright 2004-2017 Cray Inc.
 * Other additional copyright holders may be indicated within.
 * Copyright 2017 Rice University
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
//#include "chpl-mem.h"
#include "chpl-tasks.h"
//#include "chpl-tasks-callbacks-internal.h"
//#include "chplsys.h"
#include "chpl-linefile-support.h"
//#include "error.h"
//#include <stdio.h>
//#include <string.h>
//#include <signal.h>
#include <assert.h>
#include <inttypes.h>
//#include <errno.h>
//#include <sys/time.h>
//#include <sys/mman.h>
//#include <unistd.h>
//#include <math.h>

#include "extensions/ocr-legacy.h"
#include "extensions/ocr-runtime-itf.h"


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

//Structure of EDT local storage
typedef struct {
  chpl_bool serial_state;
} chpl_ocr_els_t;

static chpl_ocr_els_t* chpl_ocr_get_edtlocal(void) {
  chpl_ocr_els_t * ptr;
  u64 size;
  ocrEdtLocalStorageGet((void **) &ptr, &size);
  return ptr;
}

void chpl_sync_lock(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_lock, 1);
  chpl_thread_mutexLock(&s->lock);
}

void chpl_sync_unlock(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_unlock, 1);
  chpl_thread_mutexUnlock(&s->lock);
}

void chpl_sync_waitFullAndLock(chpl_sync_aux_t *s,
                                  int32_t lineno, int32_t filename) {
  PROFILE_INCR(profile_sync_waitFullAndLock, 1);
  chpl_sync_lock(s);
  while (s->is_full == 0) {
    pthread_cond_wait(&(s->signal_full), &(s->lock));
  }
}

void chpl_sync_waitEmptyAndLock(chpl_sync_aux_t *s,
                                   int32_t lineno, int32_t filename) {
  PROFILE_INCR(profile_sync_waitEmptyAndLock, 1);
  chpl_sync_lock(s);
  while (s->is_full != 0) {
    pthread_cond_wait(&(s->signal_empty), &(s->lock));
  }
}

void chpl_sync_markAndSignalFull(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_markAndSignalFull, 1);
  s->is_full = 1;
  pthread_cond_signal(&(s->signal_full));
  chpl_sync_unlock(s);
}

void chpl_sync_markAndSignalEmpty(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_markAndSignalEmpty, 1);
  s->is_full = 0;
  pthread_cond_signal(&(s->signal_empty));
  chpl_sync_unlock(s);
}

chpl_bool chpl_sync_isFull(void *val_ptr,
                            chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_isFull, 1);
  return s->is_full;
}

static void chpl_thread_condvar_init(chpl_thread_condvar_t* cv) {
  if (pthread_cond_init((pthread_cond_t*) cv, NULL))
    chpl_internal_error("pthread_cond_init() failed");
}

void chpl_sync_initAux(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_initAux, 1);
  s->is_full = false;
  chpl_thread_mutexInit(&s->lock);
  chpl_thread_condvar_init(&s->signal_full);
  chpl_thread_condvar_init(&s->signal_empty);
}

static void chpl_thread_condvar_destroy(chpl_thread_condvar_t* cv) {
  if (pthread_cond_destroy((pthread_cond_t*) cv))
    chpl_internal_error("pthread_cond_destroy() failed");
}

void chpl_sync_destroyAux(chpl_sync_aux_t *s) {
  PROFILE_INCR(profile_sync_destroyAux, 1);
  chpl_thread_condvar_destroy(&s->signal_full);
  chpl_thread_condvar_destroy(&s->signal_empty);
  chpl_thread_mutexDestroy(&s->lock);
}


// Tasks
typedef void (*main_ptr_t)(void);
typedef struct {
  chpl_task_bundle_t arg;
  main_ptr_t chpl_main;
} main_func_bundle_t;

static ocrGuid_t ocr_chapel_main_func(u32 argc, u64 *argv, u32 depc, ocrEdtDep_t depv[])
{
  main_func_bundle_t *m_bundle = (main_func_bundle_t*) argv;
  (m_bundle->chpl_main)();
  return NULL_GUID;
}

static ocrGuid_t ocr_chapel_conversion_func(u32 argc, u64 *argv, u32 depc, ocrEdtDep_t depv[])
{
  chpl_task_bundle_t *bundle = (chpl_task_bundle_t*) argv;
  chpl_task_setSerial(bundle->serial_state);
  (bundle->requested_fn)(argv);
  return NULL_GUID;
}

void chpl_task_init(void) {
  ocrEdtTemplateCreate(&ocr_chapel_conversion_edt_t, ocr_chapel_conversion_func, EDT_PARAM_UNK, EDT_PARAM_UNK);
}

void chpl_task_exit(void) {
#ifdef CHAPEL_PROFILE
    profile_print();
#endif /* CHAPEL_PROFILE */

  ocrEdtTemplateDestroy(ocr_chapel_conversion_edt_t);
  ocrShutdown();
}

void chpl_task_callMain(void (*chpl_main)(void)) {
  ocrGuid_t ocr_chapel_main_edt_t, ocr_chapel_main_edt, ocr_chapel_main_out;
  main_func_bundle_t m_bundle;
  m_bundle.chpl_main  = chpl_main;

  ocrEventCreate(&ocr_chapel_main_out, OCR_EVENT_STICKY_T, EVT_PROP_NONE);
  ocrEdtTemplateCreate(&ocr_chapel_main_edt_t, ocr_chapel_main_func, EDT_PARAM_UNK, EDT_PARAM_UNK);

#ifdef USE_OCR_VIENNA
  ocrGuid_t ocr_chapel_main_out_temp;
  ocrEdtCreate(&ocr_chapel_main_edt, ocr_chapel_main_edt_t, U64_COUNT(sizeof(m_bundle)), (u64*) &m_bundle,
    1, NULL, EDT_PROP_FINISH, NULL_GUID, &ocr_chapel_main_out_temp);
  ocrAddDependence(ocr_chapel_main_out_temp, ocr_chapel_main_out, 0, DB_MODE_RO);
  ocrAddDependence(NULL_GUID, ocr_chapel_main_edt, 0, DB_MODE_RO);
#else
  ocrEdtCreate(&ocr_chapel_main_edt, ocr_chapel_main_edt_t, U64_COUNT(sizeof(m_bundle)), (u64*) &m_bundle,
    0, NULL, EDT_PROP_FINISH | EDT_PROP_OEVT_VALID, NULL_HINT, &ocr_chapel_main_out);
#endif

  ocrLegacyBlockProgress(ocr_chapel_main_out, NULL, NULL, NULL, LEGACY_PROP_NONE);
  ocrEdtTemplateDestroy(ocr_chapel_main_edt_t);
  ocrEventDestroy(ocr_chapel_main_out);
}

int chpl_task_createCommTask(chpl_fn_p fn, void* arg) {
  ocrGuid_t ocr_chapel_conversion_edt;
  chpl_task_bundle_t edtArg;

  // Create a task that calls polling(void *x) in runtime/src/comm/gasnet/comm-gasnet.c
  // *x is actually not used
  edtArg.requested_fn = fn;
  edtArg.serial_state = false;
  ocrEdtCreate(&ocr_chapel_conversion_edt, ocr_chapel_conversion_edt_t, U64_COUNT(sizeof(edtArg)), (u64*) &edtArg,
    0, NULL, EDT_PROP_NONE, 
#ifdef USE_OCR_VIENNA
    NULL_GUID,
#else
    NULL_HINT, 
#endif
    NULL);
  return 0;
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
  chpl_bool serial_state = chpl_task_getSerial();

  if(serial_state)
  {
    arg->serial_state = serial_state;
    (chpl_ftable[fid])(arg);
    return;
  }

  ocrGuid_t ocr_chapel_conversion_edt;
  arg->serial_state      = serial_state;
  arg->countRunning      = false;
  arg->is_executeOn      = false;
  arg->requestedSubloc   = subloc;
  arg->requested_fid     = fid;
  arg->requested_fn      = chpl_ftable[fid];
  arg->lineno            = lineno;
  arg->filename          = filename;
  arg->id                = chpl_nullTaskID;
  ocrEdtCreate(&ocr_chapel_conversion_edt, ocr_chapel_conversion_edt_t, U64_COUNT(arg_size), (u64*) arg,
    0, NULL, EDT_PROP_NONE, 
#ifdef USE_OCR_VIENNA
    NULL_GUID,
#else
    NULL_HINT, 
#endif
    NULL);
}

void chpl_task_executeTasksInList(void** p_task_list_void) {
  PROFILE_INCR(profile_task_executeTasksInList,1);
  //assert(false);
}


static inline void taskCallBody(chpl_fn_int_t fid, chpl_fn_p fp,
                                void *arg, size_t arg_size,
                                c_sublocid_t subloc,  chpl_bool serial_state,
                                int lineno, int32_t filename)
{
    chpl_task_bundle_t *bundle = (chpl_task_bundle_t*) arg;
    ocrGuid_t ocr_chapel_conversion_edt;
    
    bundle->serial_state       = serial_state;
    bundle->countRunning       = false;
    bundle->is_executeOn       = true;
    bundle->requestedSubloc    = subloc;
    bundle->requested_fid      = fid;
    bundle->requested_fn       = fp;
    bundle->lineno             = lineno;
    bundle->filename           = filename;
    bundle->id                 = chpl_nullTaskID;

    ocrEdtCreate(&ocr_chapel_conversion_edt, ocr_chapel_conversion_edt_t, U64_COUNT(arg_size), (u64*) bundle,
    0, NULL, EDT_PROP_NONE, 
#ifdef USE_OCR_VIENNA
    NULL_GUID,
#else
    NULL_HINT, 
#endif
    NULL);
    
}


void chpl_task_taskCallFTable(chpl_fn_int_t fid,
                        chpl_task_bundle_t* arg, size_t arg_size,
                        c_sublocid_t subloc,
                        int lineno, int32_t filename) {
  PROFILE_INCR(profile_task_taskCallFTable,1);
//  assert(false);
  taskCallBody(fid, chpl_ftable[fid], arg, arg_size, subloc, false, lineno, filename);
}

void chpl_task_startMovedTask(chpl_fn_int_t  fid, chpl_fn_p fp,
                              chpl_task_bundle_t* arg, size_t arg_size,
                              c_sublocid_t subloc,
                              chpl_taskID_t id,
                              chpl_bool serial_state) {
  assert(chpl_task_idEquals(id,chpl_nullTaskID));
  PROFILE_INCR(profile_task_startMovedTask,1);
  taskCallBody(fid, fp, arg, arg_size, subloc, serial_state, 0, CHPL_FILE_IDX_UNKNOWN);  
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
  PROFILE_INCR(profile_task_getId,1);
  chpl_taskID_t id;
  ocrCurrentEdtGet(&id);
  return id;
}

chpl_bool chpl_task_idEquals(chpl_taskID_t id1, chpl_taskID_t id2) {
  return ocrGuidIsEq(id1, id2);
}

char* chpl_task_idToString(char* buff, size_t size, chpl_taskID_t id) {
  int ret = snprintf(buff, size, GUIDF, GUIDA(id));
  if(ret>0 && ret<size)
    return buff;
  else
    return NULL;
}

void chpl_task_yield(void) {
  PROFILE_INCR(profile_task_yield,1);
  //chpl_thread_yield();
}


void chpl_task_sleep(double secs) {
  PROFILE_INCR(profile_task_sleep,1);
  usleep(secs * 1000);
}

chpl_bool chpl_task_getSerial(void) {
  PROFILE_INCR(profile_task_getSerial,1);
#ifdef USE_OCR_VIENNA
  return false;
#endif
  chpl_ocr_els_t* els_p = chpl_ocr_get_edtlocal();
  return els_p->serial_state;
}

void chpl_task_setSerial(chpl_bool state) {
  PROFILE_INCR(profile_task_setSerial,1);
#ifdef USE_OCR_VIENNA
  return;
#endif
  chpl_ocr_els_t* els_p = chpl_ocr_get_edtlocal();
  els_p->serial_state = state;
}

uint32_t chpl_task_getMaxPar(void) {
  PROFILE_INCR(profile_task_getMaxPar,1);
  return ocrNbWorkers();
}

c_sublocid_t chpl_task_getNumSublocales(void) {
  assert(false);
  return 0;
}

chpl_task_prvData_t* chpl_task_getPrvData(void) {
  assert(false);
  return NULL;
}

size_t chpl_task_getCallStackSize(void) {
  PROFILE_INCR(profile_task_getCallStackSize,1);
  //assert(false);
  return chpl_thread_getCallStackSize();
}

uint32_t chpl_task_getNumQueuedTasks(void) { assert(false); return 0; }

uint32_t chpl_task_getNumRunningTasks(void) {
  assert(false);
  chpl_internal_error("chpl_task_getNumRunningTasks() called");
  return 0;
}

int32_t  chpl_task_getNumBlockedTasks(void) {
  assert(false);
  return 0;
}


// Threads

uint32_t chpl_task_getNumThreads(void) {
  assert(false);
  return 0;
}

uint32_t chpl_task_getNumIdleThreads(void) {
  assert(false);
  return 0;
}

