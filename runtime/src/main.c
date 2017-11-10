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

#include "chplrt.h"
#include "chpl_rt_utils_static.h"
#include "chpl-init.h"
#include "chplexit.h"
#include "config.h"

#ifdef USE_OCR_TASKS
ocrGuid_t mainEdt(u32 paramc, u64 * paramv, u32 depc, ocrEdtDep_t depv[]);

ocrGuid_t mainEdt(u32 paramc, u64 * paramv, u32 depc, ocrEdtDep_t depv[]) {
  int argc = getArgc(depv[0].ptr), i;
  char *argv[argc];
  for(i=0;i<argc;i++)
    argv[i] = getArgv(depv[0].ptr, i);

#elif defined(USE_HCLIB_TASKS)
void entrypoint(void *data);

typedef struct _main_captures {
    int argc;
    char **argv;
} main_captures;

int main(int argc, char **argv) {
    main_captures caps;
    caps.argc = argc;
    caps.argv = argv;

    char *deps[] = {"system"};
    hclib_launch(entrypoint, &caps, deps, 1);

    return 0;
}

void entrypoint(void *data) {
    int argc = ((main_captures *)data)->argc;
    char **argv = ((main_captures *)data)->argv;

#else
int main(int argc, char* argv[]) {
#endif

  // Initialize the runtime
  chpl_rt_init(argc, argv);                 

  // Run the main function for this node.
  chpl_task_callMain(chpl_executable_init); 

  // have everyone exit, returning the value returned by the user written main
  // or 0 if it didn't return anything
  chpl_rt_finalize(chpl_gen_main_arg.return_value);

#ifdef USE_OCR_TASKS
  return NULL_GUID;
#elif USE_HCLIB_TASKS
  ;
#else
  return 0; // should never get here
#endif
}

