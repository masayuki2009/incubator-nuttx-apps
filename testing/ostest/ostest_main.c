/****************************************************************************
 * apps/testing/ostest/ostest_main.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/wait.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sched.h>
#include <errno.h>

#ifdef CONFIG_TESTING_OSTEST_POWEROFF
#include <sys/boardctl.h>
#endif

#include <nuttx/init.h>

#include "ostest.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PRIORITY         100
#define NARGS              4
#define HALF_SECOND_USEC 500000L

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char arg1[] = "Arg1";
static const char arg2[] = "Arg2";
static const char arg3[] = "Arg3";
static const char arg4[] = "Arg4";

static const char write_data1[] = "stdio_test: write fd=1\n";
static const char write_data2[] = "stdio_test: write fd=2\n";

#ifdef SDCC
/* I am not yet certain why SDCC does not like the following
 * initializer.  It involves some issues with 2- vs 3-byte
 * pointer types.
 */

static const char *g_argv[NARGS + 1];
#else
static const char *g_argv[NARGS + 1] =
{
  arg1, arg2, arg3, arg4, NULL
};
#endif

static struct mallinfo g_mmbefore;
static struct mallinfo g_mmprevious;
static struct mallinfo g_mmafter;

#ifndef CONFIG_DISABLE_ENVIRON
static const char g_var1_name[]    = "Variable1";
static const char g_var1_value[]   = "GoodValue1";
static const char g_var2_name[]    = "Variable2";
static const char g_var2_value[]   = "GoodValue2";
static const char g_var3_name[]    = "Variable3";
static const char g_var3_value[]   = "GoodValue3";

static const char g_bad_value1[]   = "BadValue1";
static const char g_bad_value2[]   = "BadValue2";

static const char g_putenv_value[] = "Variable1=BadValue3";
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: show_memory_usage
 ****************************************************************************/

static void show_memory_usage(struct mallinfo *mmbefore,
                              struct mallinfo *mmafter)
{
  printf("VARIABLE  BEFORE   AFTER\n");
  printf("======== ======== ========\n");
  printf("arena    %8x %8x\n", mmbefore->arena,    mmafter->arena);
  printf("ordblks  %8d %8d\n", mmbefore->ordblks,  mmafter->ordblks);
  printf("mxordblk %8x %8x\n", mmbefore->mxordblk, mmafter->mxordblk);
  printf("uordblks %8x %8x\n", mmbefore->uordblks, mmafter->uordblks);
  printf("fordblks %8x %8x\n", mmbefore->fordblks, mmafter->fordblks);
}

/****************************************************************************
 * Name: check_test_memory_usage
 ****************************************************************************/

static void check_test_memory_usage(void)
{
  /* Wait a little bit to let any threads terminate */

  usleep(HALF_SECOND_USEC);

  /* Get the current memory usage */

  g_mmafter = mallinfo();

  /* Show the change from the previous time */

  printf("\nEnd of test memory usage:\n");
  show_memory_usage(&g_mmprevious, &g_mmafter);

  /* Set up for the next test */

  g_mmprevious = g_mmafter;

  /* If so enabled, show the use of priority inheritance resources */

  dump_nfreeholders("user_main:");
}

/****************************************************************************
 * Name: show_variable
 ****************************************************************************/

#ifndef CONFIG_DISABLE_ENVIRON
static void show_variable(const char *var_name, const char *exptd_value,
                          bool var_valid)
{
  char *actual_value = getenv(var_name);
  if (actual_value)
    {
      if (var_valid)
        {
          if (strcmp(actual_value, exptd_value) == 0)
            {
              printf("show_variable: Variable=%s has value=%s\n",
                     var_name, exptd_value);
            }
          else
            {
              printf("show_variable: ERROR Variable=%s has the wrong "
                     "value\n",
                     var_name);
              printf("show_variable:       found=%s expected=%s\n",
                     actual_value, exptd_value);
            }
        }
      else
        {
          printf("show_variable: ERROR Variable=%s has a value when it "
                 "should not\n",
                 var_name);
          printf("show_variable:       value=%s\n",
                 actual_value);
        }
    }
  else if (var_valid)
    {
      printf("show_variable: ERROR Variable=%s has no value\n",
             var_name);
      printf("show_variable:       Should have had value=%s\n",
             exptd_value);
    }
  else
    {
      printf("show_variable: Variable=%s has no value\n", var_name);
    }
}

static void show_environment(bool var1_valid, bool var2_valid,
                             bool var3_valid)
{
  show_variable(g_var1_name, g_var1_value, var1_valid);
  show_variable(g_var2_name, g_var2_value, var2_valid);
  show_variable(g_var3_name, g_var3_value, var3_valid);
}
#else
# define show_environment()
#endif

/****************************************************************************
 * Name: user_main
 ****************************************************************************/

static int user_main(int argc, char *argv[])
{
  timedwait_test();
  return 0;
}

/****************************************************************************
 * Name: stdio_test
 ****************************************************************************/

static void stdio_test(void)
{
  /* Verify that we can communicate */

  write(1, write_data1, sizeof(write_data1)-1);
  printf("stdio_test: Standard I/O Check: printf\n");

  write(2, write_data2, sizeof(write_data2)-1);
#ifdef CONFIG_FILE_STREAM
  fprintf(stderr, "stdio_test: Standard I/O Check: fprintf to stderr\n");
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * ostest_main
 ****************************************************************************/

int main(int argc, FAR char **argv)
{
  int result;
#ifdef CONFIG_TESTING_OSTEST_WAITRESULT
  int ostest_result = ERROR;
#else
  int ostest_result = OK;
#endif

  /* Verify that stdio works first */

  stdio_test();

#ifdef SDCC
  /* I am not yet certain why SDCC does not like the following initilizers.
   * It involves some issues with 2- vs 3-byte pointer types.
   */

  g_argv[0] = arg1;
  g_argv[1] = arg2;
  g_argv[2] = arg3;
  g_argv[3] = arg4;
  g_argv[4] = NULL;
#endif

  /* Set up some environment variables */

#ifndef CONFIG_DISABLE_ENVIRON
  printf("ostest_main: putenv(%s)\n", g_putenv_value);
  putenv(g_putenv_value);                   /* Varaible1=BadValue3 */
  printf("ostest_main: setenv(%s, %s, TRUE)\n", g_var1_name, g_var1_value);
  setenv(g_var1_name, g_var1_value, TRUE);  /* Variable1=GoodValue1 */

  printf("ostest_main: setenv(%s, %s, FALSE)\n", g_var2_name, g_bad_value1);
  setenv(g_var2_name, g_bad_value1, FALSE); /* Variable2=BadValue1 */
  printf("ostest_main: setenv(%s, %s, TRUE)\n", g_var2_name, g_var2_value);
  setenv(g_var2_name, g_var2_value, TRUE);  /* Variable2=GoodValue2 */

  printf("ostest_main: setenv(%s, %s, FALSE)\n", g_var3_name, g_var3_name);
  setenv(g_var3_name, g_var3_value, FALSE); /* Variable3=GoodValue3 */
  printf("ostest_main: setenv(%s, %s, FALSE)\n", g_var3_name, g_var3_name);
  setenv(g_var3_name, g_bad_value2, FALSE); /* Variable3=GoodValue3 */
  show_environment(true, true, true);
#endif

  /* Verify that we can spawn a new task */

  result = task_create("ostest", PRIORITY, STACKSIZE, user_main,
                       (FAR char * const *)g_argv);
  if (result == ERROR)
    {
      printf("ostest_main: ERROR Failed to start user_main\n");
      ostest_result = ERROR;
    }
  else
    {
      printf("ostest_main: Started user_main at PID=%d\n", result);

#ifdef CONFIG_TESTING_OSTEST_WAITRESULT
      /* Wait for the test to complete to get the test result */

      if (waitpid(result, &ostest_result, 0) != result)
        {
          printf("ostest_main: ERROR Failed to wait for user_main to "
                 "terminate\n");
          ostest_result = ERROR;
        }
#endif
    }

  printf("ostest_main: Exiting with status %d\n", ostest_result);

#ifdef CONFIG_TESTING_OSTEST_POWEROFF
  /* Power down, providing the test result.  This is really only an
   * interesting case when used with the NuttX simulator.  In that case,
   * test management logic can received the result of the test.
   */

  boardctl(BOARDIOC_POWEROFF, ostest_result);
#endif

  return ostest_result;
}
