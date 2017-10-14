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

// #define CHAPEL_TRACE
// #define CHAPEL_PROFILE
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

#ifdef USE_TICKET_LOCK
int sleep_task_with_ticket(chpl_sync_aux_t *s, ticket_lock_type type, uint_least32_t ticket) {
  int err = 0;
  hclib_promise_t *promise = hclib_promise_create();
  sync_list_node_t *node = (sync_list_node_t *)malloc(sizeof(*node));
  INTERNAL_ASSERT(node && promise);
  node->prom = promise;
  node->next = NULL;
  node->ticket = ticket;

  // list should be increasing order of ticket
  switch (type) {
  case TL_GENERAL:
    if (s->wait_list == NULL) {
      s->wait_list = node;
    } else if (ticket < s->wait_list->ticket) {
      node->next = s->wait_list;
      s->wait_list = node;
    } else {
      sync_list_node_t *curr = s->wait_list;
      while (curr->next && curr->next->ticket < ticket) {
	curr = curr->next;
      }
      node->next = curr->next;
      curr->next = node;
    }
    err = pthread_mutex_unlock(&s->lock);
    break;
#ifdef USE_SV_TICKET_LOCK
  case TL_READ:
    if (s->wait_listR == NULL) {
      s->wait_listR = node;
    } else if (ticket < s->wait_listR->ticket) {
      node->next = s->wait_listR;
      s->wait_listR = node;
    } else {
      sync_list_node_t *curr = s->wait_listR;
      while (curr->next && curr->next->ticket < ticket) {
	curr = curr->next;
      }
      node->next = curr->next;
      curr->next = node;
    }
    err = pthread_mutex_unlock(&s->lockR);
    break;

  case TL_WRITE:
    if (s->wait_listW == NULL) {
      s->wait_listW = node;
    } else if (ticket < s->wait_listW->ticket) {
      node->next = s->wait_listW;
      s->wait_listW = node;
    } else {
      sync_list_node_t *curr = s->wait_listW;
      while (curr->next && curr->next->ticket < ticket) {
	curr = curr->next;
      }
      node->next = curr->next;
      curr->next = node;
    }
    err = pthread_mutex_unlock(&s->lockW);
    break;
#endif
  default:
    INTERNAL_ASSERT(0);
  }

  hclib_future_wait(hclib_get_future_for_promise(promise));
  return err;
}

int awake_task_with_ticket(chpl_sync_aux_t *s, ticket_lock_type type, uint_least32_t ticket) {
  int err = 0;
  sync_list_node_t *to_signal = NULL;

  switch (type) {
  case TL_GENERAL:
    if (s->wait_list) {
      if (s->wait_list->ticket == ticket) {
	to_signal = s->wait_list;
	s->wait_list = to_signal->next;
	atomic_store_uint_least32_t(&(s->minSleep),
				    (s->wait_list) ? s->wait_list->ticket : UINT_LEAST32_MAX);
      } else {
	sync_list_node_t *curr = s->wait_list;
	while (curr->next && curr->next->ticket != ticket) {
	  curr = curr->next;
	}
	if (curr->next) {
	  to_signal = curr->next;
	  curr->next = to_signal->next;
	  atomic_store_uint_least32_t(&(s->minSleep),
				      (s->wait_list) ? s->wait_list->ticket : UINT_LEAST32_MAX);
	}
      }
    }
    err = pthread_mutex_unlock(&s->lock);
    break;
#ifdef USE_SV_TICKET_LOCK
  case TL_READ:
    if (s->wait_listR) {
      if (s->wait_listR->ticket == ticket) {
	to_signal = s->wait_listR;
	s->wait_listR = to_signal->next;
	atomic_store_uint_least32_t(&(s->minSleepR),
				    (s->wait_listR) ? s->wait_listR->ticket : UINT_LEAST32_MAX);
      } else {
	sync_list_node_t *curr = s->wait_listR;
	while (curr->next && curr->next->ticket != ticket) {
	  curr = curr->next;
	}
	if (curr->next) {
	  to_signal = curr->next;
	  curr->next = to_signal->next;
	  atomic_store_uint_least32_t(&(s->minSleepR),
				      (s->wait_listR) ? s->wait_listR->ticket : UINT_LEAST32_MAX);
	}
      }
    }
    err = pthread_mutex_unlock(&s->lockR);
    break;

  case TL_WRITE:
    if (s->wait_listW) {
      if (s->wait_listW->ticket == ticket) {
	to_signal = s->wait_listW;
	s->wait_listW = to_signal->next;
	atomic_store_uint_least32_t(&(s->minSleepW),
				    (s->wait_listW) ? s->wait_listW->ticket : UINT_LEAST32_MAX);
      } else {
	sync_list_node_t *curr = s->wait_listW;
	while (curr->next && curr->next->ticket != ticket) {
	  curr = curr->next;
	}
	if (curr->next) {
	  to_signal = curr->next;
	  curr->next = to_signal->next;
	  atomic_store_uint_least32_t(&(s->minSleepW),
				      (s->wait_listW) ? s->wait_listW->ticket : UINT_LEAST32_MAX);
	}
      }
    }
    err = pthread_mutex_unlock(&s->lockW);
    break;
#endif
  default:
    INTERNAL_ASSERT(0);
  }

  if (to_signal) {
    hclib_promise_put(to_signal->prom, NULL);
    // Todo: how to free to_signal->prom?
    // free(to_signal);
  }
  return err;
}
#endif

void chpl_sync_lock(chpl_sync_aux_t *s) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_lock, 1);

#ifdef USE_TICKET_LOCK
  uint_least32_t myTicket = atomic_fetch_add_uint_least32_t(&(s->ticket), 1);
  int idx = (myTicket % TL_PAR) * TL_LINE_SIZE;

#ifdef TL_FINITE_SPIN
  chpl_bool is_satisfied = false;
  int i;
  for (i = 0; i < TL_SPIN_COUNT; i++) {
    if (myTicket == s->arrCurrent[idx]) {
      is_satisfied = true;
      break;
    }
  }

  if (!is_satisfied) {
    int err = pthread_mutex_lock(&s->lock);
    INTERNAL_ASSERT(err == 0);
    int prev_min = atomic_load_uint_least32_t(&(s->minSleep));
    if (myTicket < prev_min)
      atomic_store_uint_least32_t(&(s->minSleep), myTicket);

    // atomic_thread_fence(memory_order_seq_cst); // use atomic minSleep instead memory fence.
    if (myTicket != s->arrCurrent[idx]) {
      err = sleep_task_with_ticket(s, TL_GENERAL, myTicket);
      INTERNAL_ASSERT(err == 0);
      INTERNAL_ASSERT(myTicket == s->arrCurrent[idx]);

    } else {
      atomic_store_uint_least32_t(&(s->minSleep), prev_min);
      err = pthread_mutex_unlock(&s->lock);
      INTERNAL_ASSERT(err == 0);
    }
  }
#else
  while (myTicket != s->arrCurrent[idx]);
#endif

#else // USE_TICKET_LOCK
  int do_wait = 0;
  hclib_promise_t *promise = hclib_promise_create();
  sync_list_node_t *node = (sync_list_node_t *)malloc(sizeof(*node));
  INTERNAL_ASSERT(node && promise);
  node->prom = promise;
  node->next = NULL;

  int err = pthread_mutex_lock(&s->lock);
  INTERNAL_ASSERT(err == 0);

  if (s->active_critical_section) {
      if (s->wait_list == NULL) {
          s->wait_list = node;
      } else {
          sync_list_node_t *curr = s->wait_list;
          while (curr->next) {
              curr = curr->next;
          }
          curr->next = node;
      }

      do_wait = 1;
  }
  s->active_critical_section = 1;

  err = pthread_mutex_unlock(&s->lock);
  INTERNAL_ASSERT(err == 0);

  if (do_wait) {
      hclib_future_wait(hclib_get_future_for_promise(promise));
  }
#endif

#ifdef CHAPEL_TRACE
    fprintf(stderr, "EXIT TRACE> %s\n", __func__);
#endif
}

void chpl_sync_unlock(chpl_sync_aux_t *s) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_unlock, 1);

#ifdef USE_TICKET_LOCK
  uint_least32_t nextTicket = s->current + 1;
  int idx = (nextTicket % TL_PAR) * TL_LINE_SIZE;
  s->current = nextTicket;
  s->arrCurrent[idx] = nextTicket;

#ifdef TL_FINITE_SPIN
  // atomic_thread_fence(memory_order_seq_cst); // use atomic minSleep instead memory fence.
  if (atomic_load_uint_least32_t(&(s->minSleep)) == nextTicket) {
    int err = pthread_mutex_lock(&s->lock);
    INTERNAL_ASSERT(err == 0);
    if (atomic_load_uint_least32_t(&(s->minSleep)) == nextTicket) {
      err = awake_task_with_ticket(s, TL_GENERAL, nextTicket);
      INTERNAL_ASSERT(err == 0);

    } else {
      err = pthread_mutex_unlock(&s->lock);
      INTERNAL_ASSERT(err == 0);
    }
  }
#endif

#else // USE_TICKET_LOCK
  int err = pthread_mutex_lock(&s->lock);
  INTERNAL_ASSERT(err == 0);

  sync_list_node_t *to_signal = NULL;
  if (s->wait_list) {
      to_signal = s->wait_list;
      s->wait_list = to_signal->next;
  } else {
      s->active_critical_section = 0;
  }

  err = pthread_mutex_unlock(&s->lock);
  INTERNAL_ASSERT(err == 0);

  if (to_signal) {
      hclib_promise_put(to_signal->prom, NULL);
  }
#endif

#ifdef CHAPEL_TRACE
    fprintf(stderr, "EXIT TRACE> %s\n", __func__);
#endif
}

void chpl_sync_waitFullAndLock(chpl_sync_aux_t *s,
                                  int32_t lineno, int32_t filename) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_waitFullAndLock, 1);

#ifdef USE_SV_TICKET_LOCK
  uint_least32_t myTicketR = atomic_fetch_add_uint_least32_t(&(s->ticketR), 1);
  int idx = (myTicketR % TL_PAR) * TL_LINE_SIZE;

#ifdef TL_FINITE_SPIN
  chpl_bool is_satisfied = false;
  int i;
  for (i = 0; i < TL_SPIN_COUNT; i++) {
    if (myTicketR == s->arrCurrentR[idx]) {
      is_satisfied = true;
      break;
    }
  }
  if (is_satisfied) {
    is_satisfied = false;
    for (i = 0; i < TL_SPIN_COUNT; i++) {
      if (s->state == SV_FULL) {
	is_satisfied = true;
	break;
      }
    }
  }

  if (!is_satisfied) {
    int err = pthread_mutex_lock(&s->lockR);
    INTERNAL_ASSERT(err == 0);
    int prev_min = atomic_load_uint_least32_t(&(s->minSleepR));
    if (myTicketR < prev_min)
      atomic_store_uint_least32_t(&(s->minSleepR), myTicketR);

    // atomic_thread_fence(memory_order_seq_cst); // use atomic minSleepR instead memory fence.
    chpl_bool cond1 = (myTicketR != s->arrCurrentR[idx]);
    chpl_bool cond2 = (s->state != SV_FULL);
    if (cond1 || cond2) {
      err = sleep_task_with_ticket(s, TL_READ, myTicketR);
      INTERNAL_ASSERT(err == 0);
      INTERNAL_ASSERT(myTicketR == s->arrCurrentR[idx]);
      INTERNAL_ASSERT(s->state == SV_FULL);

    } else {
      atomic_store_uint_least32_t(&(s->minSleepR), prev_min);
      err = pthread_mutex_unlock(&s->lockR);
      INTERNAL_ASSERT(err == 0);
    }
  }
#else
  while (myTicketR != s->arrCurrentR[idx]);
  while (s->state != SV_FULL);
#endif

#else // USE_SV_TICKET_LOCK
  chpl_sync_lock(s);
  while (s->is_full == 0) {
      chpl_sync_unlock(s);
      chpl_sync_lock(s);
  }
#endif

#ifdef CHAPEL_TRACE
    fprintf(stderr, "EXIT TRACE> %s\n", __func__);
#endif
}

void chpl_sync_waitEmptyAndLock(chpl_sync_aux_t *s,
                                   int32_t lineno, int32_t filename) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_waitEmptyAndLock, 1);

#ifdef USE_SV_TICKET_LOCK
  uint_least32_t myTicketW = atomic_fetch_add_uint_least32_t(&(s->ticketW), 1);
  int idx = (myTicketW % TL_PAR) * TL_LINE_SIZE;

#ifdef TL_FINITE_SPIN
  chpl_bool is_satisfied = false;
  int i;
  for (i = 0; i < TL_SPIN_COUNT; i++) {
    if (myTicketW == s->arrCurrentW[idx]) {
      is_satisfied = true;
      break;
    }
  }
  if (is_satisfied) {
    is_satisfied = false;
    for (i = 0; i < TL_SPIN_COUNT; i++) {
      if (s->state == SV_EMPTY) {
	is_satisfied = true;
	break;
      }
    }
  }

  if (!is_satisfied) {
    int err = pthread_mutex_lock(&s->lockW);
    INTERNAL_ASSERT(err == 0);
    int prev_min = atomic_load_uint_least32_t(&(s->minSleepW));
    if (myTicketW < prev_min)
      atomic_store_uint_least32_t(&(s->minSleepW), myTicketW);

    // atomic_thread_fence(memory_order_seq_cst); // use atomic minSleepW instead memory fence.
    chpl_bool cond1 = (myTicketW != s->arrCurrentW[idx]);
    chpl_bool cond2 = (s->state != SV_EMPTY);
    if (cond1 || cond2) {
      err = sleep_task_with_ticket(s, TL_WRITE, myTicketW);
      INTERNAL_ASSERT(err == 0);
      INTERNAL_ASSERT(myTicketW == s->arrCurrentW[idx]);
      INTERNAL_ASSERT(s->state == SV_EMPTY);

    } else {
      atomic_store_uint_least32_t(&(s->minSleepW), prev_min);
      err = pthread_mutex_unlock(&s->lockW);
      INTERNAL_ASSERT(err == 0);
    }
  }
#else
  while (myTicketW != s->arrCurrentW[idx]);
  while (s->state != SV_EMPTY);
#endif

#else // USE_SV_TICKET_LOCK
  chpl_sync_lock(s);
  while (s->is_full != 0) {
      chpl_sync_unlock(s);
      chpl_sync_lock(s);
  }
#endif

#ifdef CHAPEL_TRACE
    fprintf(stderr, "EXIT TRACE> %s\n", __func__);
#endif
}

void chpl_sync_markAndSignalFull(chpl_sync_aux_t *s) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_markAndSignalFull, 1);

#ifdef USE_SV_TICKET_LOCK
  s->state = SV_TRANSIT;
  uint_least32_t myTicketW = s->currentW;
  uint_least32_t val = myTicketW + 1;
  int idx = (val % TL_PAR) * TL_LINE_SIZE;
  s->currentW = val;
  s->arrCurrentW[idx] = val;
  s->state = SV_FULL;

#ifdef TL_FINITE_SPIN
  // atomic_thread_fence(memory_order_seq_cst); // use atomic minSleepR instead memory fence.
  if (atomic_load_uint_least32_t(&(s->minSleepR)) == myTicketW) {
    int err = pthread_mutex_lock(&s->lockR);
    INTERNAL_ASSERT(err == 0);
    if (atomic_load_uint_least32_t(&(s->minSleepR)) == myTicketW) {
      err = awake_task_with_ticket(s, TL_READ, myTicketW);
      INTERNAL_ASSERT(err == 0);

    } else {
      err = pthread_mutex_unlock(&s->lockR);
      INTERNAL_ASSERT(err == 0);
    }
  }
#endif

#else // USE_SV_TICKET_LOCK
  // chpl_sync_lock(s);

  //INTERNAL_ASSERT(!(s->is_full));
  s->is_full = 1;

  chpl_sync_unlock(s);
#endif

#ifdef CHAPEL_TRACE
    fprintf(stderr, "EXIT TRACE> %s\n", __func__);
#endif
}

void chpl_sync_markAndSignalEmpty(chpl_sync_aux_t *s) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_markAndSignalEmpty, 1);

#ifdef USE_SV_TICKET_LOCK
  s->state = SV_TRANSIT;
  uint_least32_t myTicketR = s->currentR;
  uint_least32_t val = myTicketR + 1;
  int idx = (val % TL_PAR) * TL_LINE_SIZE;
  s->currentR = val;
  s->arrCurrentR[idx] = val;
  s->state = SV_EMPTY;

#ifdef TL_FINITE_SPIN
  // atomic_thread_fence(memory_order_seq_cst); // use atomic minSleepW instead memory fence.
  if (atomic_load_uint_least32_t(&(s->minSleepW)) == myTicketR+1) {
    int err = pthread_mutex_lock(&s->lockW);
    INTERNAL_ASSERT(err == 0);
    if (atomic_load_uint_least32_t(&(s->minSleepW)) == myTicketR+1) {
      err = awake_task_with_ticket(s, TL_WRITE, myTicketR+1);
      INTERNAL_ASSERT(err == 0);

    } else {
      err = pthread_mutex_unlock(&s->lockW);
      INTERNAL_ASSERT(err == 0);
    }
  }
#endif

#else // USE_SV_TICKET_LOCK
  // chpl_sync_lock(s);

  //INTERNAL_ASSERT(s->is_full);
  s->is_full = 0;

  chpl_sync_unlock(s);
#endif

#ifdef CHAPEL_TRACE
    fprintf(stderr, "EXIT TRACE> %s\n", __func__);
#endif
}

chpl_bool chpl_sync_isFull(void *val_ptr,
                            chpl_sync_aux_t *s) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_isFull, 1);
#ifdef USE_SV_TICKET_LOCK
  return s->state == SV_FULL;
#else
  return s->is_full;
#endif
}

void chpl_sync_initAux(chpl_sync_aux_t *s) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_initAux, 1);

#ifdef USE_TICKET_LOCK
  int i, err0;
  atomic_init_uint_least32_t(&(s->ticket), 0);
  atomic_init_uint_least32_t(&(s->minSleep), UINT_LEAST32_MAX);
  for (i = 0; i < TL_PAR; i++) {
    s->arrCurrent[i*TL_LINE_SIZE] = 0;
  }
  s->current = 0;
#ifdef USE_SV_TICKET_LOCK
  err0 = pthread_mutex_init(&s->lockR, NULL);
  INTERNAL_ASSERT(err0 == 0);
  err0 = pthread_mutex_init(&s->lockW, NULL);
  INTERNAL_ASSERT(err0 == 0);
  s->wait_listR = NULL;
  s->wait_listW = NULL;
  atomic_init_uint_least32_t(&(s->ticketR), 0);
  atomic_init_uint_least32_t(&(s->ticketW), 0);
  atomic_init_uint_least32_t(&(s->minSleepR), UINT_LEAST32_MAX);
  atomic_init_uint_least32_t(&(s->minSleepW), UINT_LEAST32_MAX);
  for (i = 0; i < TL_PAR; i++) {
    s->arrCurrentR[i*TL_LINE_SIZE] = 0;
    s->arrCurrentW[i*TL_LINE_SIZE] = 0;
  }
  s->currentR = 0;
  s->currentW = 0;
  s->state = SV_EMPTY;
#else
  s->is_full = false;
#endif

#else // USE_TICKET_LOCK
  s->is_full = false;
  s->active_critical_section = 0;
#endif
  const int err = pthread_mutex_init(&s->lock, NULL);
  INTERNAL_ASSERT(err == 0);

  s->wait_list = NULL;
}

void chpl_sync_destroyAux(chpl_sync_aux_t *s) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_sync_destroyAux, 1);

#ifdef USE_TICKET_LOCK
  int err0;
  atomic_destroy_uint_least32_t(&(s->ticket));
  atomic_destroy_uint_least32_t(&(s->minSleep));
#ifdef USE_SV_TICKET_LOCK
  err0 = pthread_mutex_destroy(&s->lockR);
  INTERNAL_ASSERT(err0 == 0);
  err0 = pthread_mutex_destroy(&s->lockW);
  INTERNAL_ASSERT(err0 == 0);
  atomic_destroy_uint_least32_t(&(s->ticketR));
  atomic_destroy_uint_least32_t(&(s->ticketW));
  atomic_destroy_uint_least32_t(&(s->minSleepR));
  atomic_destroy_uint_least32_t(&(s->minSleepW));
#endif
#endif
  const int err = pthread_mutex_destroy(&s->lock);
  INTERNAL_ASSERT(err == 0);
}

// Tasks
typedef void (*main_ptr_t)(void);
typedef struct {
  chpl_task_bundle_t arg; //keep this as first argumemt so as match with chpl_task_bundle_t*
  main_ptr_t chpl_main;
} main_func_bundle_t;

static void ocr_chapel_main_func(void *user_data) {
    //main_ptr_t fp = (main_ptr_t)user_data;
    //fp();
    main_func_bundle_t *m_bundle = (main_func_bundle_t*) user_data;
    (m_bundle->chpl_main)();
}

void chpl_task_init(void) {
}

void chpl_task_exit(void) {
#ifdef CHAPEL_PROFILE
    profile_print();
#endif /* CHAPEL_PROFILE */
    //printf("\n");
    hclib_print_runtime_stats(stdout);
}

void chpl_task_callMain(void (*chpl_main)(void)) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
    main_func_bundle_t *new_arg = NULL;
    hclib_start_finish();
    {
        new_arg = (main_func_bundle_t *)malloc(sizeof(main_func_bundle_t));
        new_arg->chpl_main  = chpl_main;
        new_arg->arg.id = __sync_fetch_and_add(&task_id_counter, 1);
        hclib_async(ocr_chapel_main_func, new_arg, NULL, 0, NULL);
    }
    hclib_end_finish();
    free(new_arg);
}

int chpl_task_createCommTask(chpl_fn_p fn, void* arg) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif

  // Create a task that calls polling(void *x) in runtime/src/comm/gasnet/comm-gasnet.c
  // *x is actually not used
  
  hclib_async(fn, arg, NULL, 0, NULL);
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
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s, subloc=%p (is any? %d), serial=%d\n", __func__,
            subloc, subloc == c_sublocid_any, chpl_task_getSerial());
#endif
  PROFILE_INCR(profile_task_addToTaskList,1);
  chpl_bool serial_state = chpl_task_getSerial();

  if(serial_state) {
    arg->serial_state = serial_state;
    (chpl_ftable[fid])(arg);
    return;
  }

  // chpl_task_bundle_t *arg = (chpl_task_bundle_t *)chpl_mem_alloc(arg_size,
  //         CHPL_RT_MD_TASK_ARG_AND_POOL_DESC, lineno, filename);
  chpl_task_bundle_t *new_arg = (chpl_task_bundle_t *)malloc(arg_size);
  INTERNAL_ASSERT(new_arg);
  memcpy(new_arg, arg, arg_size);

  new_arg->serial_state      = serial_state;
  new_arg->countRunning      = false;
  new_arg->is_executeOn      = false;
  new_arg->requestedSubloc   = subloc;
  new_arg->requested_fid     = fid;
  new_arg->requested_fn      = chpl_ftable[fid];
  new_arg->lineno            = lineno;
  new_arg->filename          = filename;
  new_arg->id                = __sync_fetch_and_add(&task_id_counter, 1);

  hclib_async(taskCaller, new_arg, NULL, 0, NULL);
}

void chpl_task_executeTasksInList(void** p_task_list_void) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
  PROFILE_INCR(profile_task_executeTasksInList,1);
}

static inline void taskCallBody(chpl_fn_int_t fid, chpl_fn_p fp,
                                void *arg, size_t arg_size,
                                c_sublocid_t subloc,  chpl_bool serial_state,
                                int lineno, int32_t filename)
{
    chpl_task_bundle_t *new_arg = (chpl_task_bundle_t *)malloc(arg_size);
    INTERNAL_ASSERT(new_arg);
    memcpy(new_arg, arg, arg_size);

    new_arg->serial_state       = serial_state;
    new_arg->countRunning       = false;
    new_arg->is_executeOn       = true;
    new_arg->requestedSubloc    = subloc;
    new_arg->requested_fid      = fid;
    new_arg->requested_fn       = fp;
    new_arg->lineno             = lineno;
    new_arg->filename           = filename;
    new_arg->id                 = __sync_fetch_and_add(&task_id_counter, 1);

    hclib_async(taskCaller, new_arg, NULL, 0, NULL);
}

void chpl_task_taskCallFTable(chpl_fn_int_t fid,
                        chpl_task_bundle_t* arg, size_t arg_size,
                        c_sublocid_t subloc,
                        int lineno, int32_t filename) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif

  PROFILE_INCR(profile_task_taskCallFTable,1);
  taskCallBody(fid, chpl_ftable[fid], arg, arg_size, subloc, false, lineno, filename);
}

void chpl_task_startMovedTask(chpl_fn_int_t  fid, chpl_fn_p fp,
                              chpl_task_bundle_t* arg, size_t arg_size,
                              c_sublocid_t subloc,
                              chpl_taskID_t id,
                              chpl_bool serial_state) {
#ifdef CHAPEL_TRACE
    fprintf(stderr, "TRACE> %s\n", __func__);
#endif
    
  assert(id == chpl_nullTaskID);
      
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

chpl_task_bundle_t *  get_local_task_bundle_t() {
  void (*fp)(void *);
  void *args;

  hclib_get_curr_task_info(&fp, &args);
  INTERNAL_ASSERT(fp == taskCaller || fp == ocr_chapel_main_func);
  chpl_task_bundle_t *bundle = (chpl_task_bundle_t *)args;
  return bundle; 
}

chpl_taskID_t chpl_task_getId(void) {
  PROFILE_INCR(profile_task_getId,1);
  //void (*fp)(void *);
  //void *args;

  //hclib_get_curr_task_info(&fp, &args);
  //INTERNAL_ASSERT(fp == taskCaller || fp == ocr_chapel_main_func);
  //chpl_task_bundle_t *bundle = (chpl_task_bundle_t *)args;
  chpl_task_bundle_t *bundle = get_local_task_bundle_t();
  return bundle->id;
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

// TODO For HClib, we could use hclib_yield to implement this?
void chpl_task_yield(void) {
  PROFILE_INCR(profile_task_yield,1);
  // INTERNAL_ASSERT(false);
  hclib_yield(NULL);
  //chpl_thread_yield();
}

// TODO For HClib, we could use a while loop and hclib_yield to implement this?
void chpl_task_sleep(double secs) {
  PROFILE_INCR(profile_task_sleep,1);
  //INTERNAL_ASSERT(false);
  usleep(secs * 1000);
}

// TODO Support serial tasks
chpl_bool chpl_task_getSerial(void) {
  PROFILE_INCR(profile_task_getSerial,1);
  chpl_task_bundle_t *bundle = get_local_task_bundle_t();
  return bundle->serial_state;
}

// TODO Allow tasks to be set to serial, not incurring launch overhead
void chpl_task_setSerial(chpl_bool state) {
  PROFILE_INCR(profile_task_setSerial,1);
  chpl_task_bundle_t *bundle = get_local_task_bundle_t();
  bundle->serial_state = state;
  return;
}

uint32_t chpl_task_getMaxPar(void) {
  PROFILE_INCR(profile_task_getMaxPar,1);
  return hclib_get_num_workers();
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

