/****************************************************************************
 * examples/cstest/cstest_main.c
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

#include <pthread.h>
#include <semaphore.h>
#include <nuttx/arch.h>
#include <sched.h>

static sem_t g_sem;

static void usleep_udelay(bool delay, const char *name)
{
  struct timespec ts0;
  struct timespec ts1;
  uint64_t elapsed;

  clock_gettime(CLOCK_MONOTONIC, &ts0);

  if (delay)
    {
      printf("%s: call up_udelay() with approx. 1sec \n", name);
      up_udelay(1 * 1000 * 1000);
      printf("%s: up_udelay() done \n", name);
    }
  else
    {
      printf("%s: call usleep() with 1sec \n", name);
      usleep(1 * 1000 * 1000);
      printf("%s: usleep() done \n", name);
    }

  clock_gettime(CLOCK_MONOTONIC, &ts1);

  elapsed  = (((uint64_t)ts1.tv_sec * NSEC_PER_SEC) + ts1.tv_nsec);
  elapsed -= (((uint64_t)ts0.tv_sec * NSEC_PER_SEC) + ts0.tv_nsec);
  elapsed /= NSEC_PER_MSEC; /* msec */
  printf("%s: took %" PRIu64 " msec \n", name, elapsed);
}

/****************************************************************************
 * Name: lpthread_func
 ****************************************************************************/

static void *lpthread_func(void *arg)
{
  printf("%s: started thread=%d with priority=%d \n",
         __func__, getpid(), getpriority(0, 0));

  if (NULL == arg)
    {
      while (1)
        {
        }
    }
  else
    {
      usleep_udelay(true, __func__);
    }

  printf("%s: exit \n", __func__);
  return NULL;
}

/****************************************************************************
 * Name: semthread_func
 ****************************************************************************/

static void *semthread_func(void *arg)
{
  printf("%s: started thread=%d with priority=%d \n",
         __func__, getpid(), getpriority(0, 0));

  printf("%s: call sem_wait() \n", __func__);
  sem_wait(&g_sem);
  printf("%s: sem_wait() done \n", __func__);

  printf("%s: exit \n", __func__);
  return NULL;
}

/****************************************************************************
 * Name: create_thread
 ****************************************************************************/

static pthread_t create_thread(int mode)
{
  struct sched_param param;
  pthread_attr_t attr;
  pthread_t thread = 0;
  int duration = 1;
  int ret = 0;

  pthread_attr_init(&attr);

  switch (mode)
    {
      case 0: /* with semaphore */
        param.sched_priority = getpriority(0, 0) + 10;
        pthread_attr_setschedparam(&attr, &param);
        ret = pthread_create(&thread, &attr, semthread_func, NULL);
        break;

      case 1:
        param.sched_priority = 50; /* start with low pirority(50) */
        pthread_attr_setschedparam(&attr, &param);
        ret = pthread_create(&thread, &attr, lpthread_func, NULL);
        break;

      case 2:
        param.sched_priority = 50; /* start with low pirority(50) */
        pthread_attr_setschedparam(&attr, &param);
        ret = pthread_create(&thread, &attr, lpthread_func, &duration);
        break;

      default:
        ASSERT(false);
    }

  ASSERT(0 == ret);

  printf("%s: created thread=%d with priority=%d  \n",
         __func__, thread, param.sched_priority);

  return thread;
}

/****************************************************************************
 * Name: cs_enter
 ****************************************************************************/

static irqstate_t cs_enter(bool cs, bool lock, const char *name)
{
  irqstate_t flags = 0;

  if (cs)
    {
      printf("%s: call enter_critical_section() \n", name);
      flags = enter_critical_section();
      printf("%s: enter_critical_section() done \n", name);
    }

  if (lock)
    {
      printf("%s: call sched_lock() \n", name);
      sched_lock();
      printf("%s: sched_lock() done \n", name);
    }

  return flags;
}

/****************************************************************************
 * Name: cs_exit
 ****************************************************************************/

static void cs_exit(irqstate_t flags, bool cs, bool lock, const char *name)
{
  if (lock)
    {
      printf("%s: call sched_unlock() \n", name);
      sched_unlock();
      printf("%s: sched_unlock() done \n", name);
    }

  if (cs)
    {
      printf("%s: call leave_critical_section() \n", name);
      leave_critical_section(flags);
      printf("%s: leave_critical_section() done \n", name);
    }
}

/****************************************************************************
 * Name: sleep_udelay_test
 ****************************************************************************/

static void sleep_udelay_test(bool cs, bool lock, bool delay)
{
  irqstate_t flags;
  pthread_t thread;
  void *val;

  printf("%s: caller's priority=%d \n", __func__, getpriority(0, 0));

  thread = create_thread(1);

  flags = cs_enter(cs, lock, __func__);

  usleep_udelay(delay, __func__);

  cs_exit(flags, cs, lock, __func__);

  /* Cancel the thread */

  pthread_cancel(thread);

  printf("%s: call pthread_join() \n", __func__);
  pthread_join(thread, &val);
  printf("%s: pthread_join() done \n", __func__);
}

/****************************************************************************
 * Name: sem_test
 ****************************************************************************/

static void sem_test(bool cs, bool lock)
{
  irqstate_t flags;
  pthread_t thread;
  void *val;

  printf("%s: caller's priority=%d \n", __func__, getpriority(0, 0));

  sem_init(&g_sem, 0, 0);
  thread = create_thread(0);

  flags = cs_enter(cs, lock, __func__);

  printf("%s: call sem_post() \n", __func__);
  sem_post(&g_sem);
  printf("%s: sem_post() done \n", __func__);

  cs_exit(flags, cs, lock, __func__);

  printf("%s: call pthread_join() \n", __func__);
  pthread_join(thread, &val);
  printf("%s: pthread_join() done \n", __func__);
}

/****************************************************************************
 * Name: priority_test
 ****************************************************************************/

static void priority_test(bool cs, bool lock, bool boost)
{
  irqstate_t flags;
  pthread_t thread;
  void *val;

  int prio = getpriority(0, 0);
  printf("%s: caller's priority=%d \n", __func__, prio);

  thread = create_thread(2);

  flags = cs_enter(cs, lock, __func__);

  if (boost)
    {
      /* boost the thread */

      printf("%s: setpriority 150 to thread=%d \n", __func__, thread);
      setpriority(0, thread, 150);
      printf("%s: setpriority() done \n", __func__);
    }
  else
    {
      /* set lower priority to the caller thread */

      printf("%s: setpriority 40 to the caller thread \n", __func__);
      setpriority(0, 0, 40);
      printf("%s: setpriority() done \n", __func__);
    }

  cs_exit(flags, cs, lock, __func__);

  printf("%s: call pthread_join() \n", __func__);
  pthread_join(thread, &val);
  printf("%s: pthread_join() done \n", __func__);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int option;
  bool boost = false;
  bool cs = false;
  bool delay = false;
  bool lock = false;
  int tcase = 0;

  while ((option = getopt(argc, argv, "bcdlt:")) != ERROR)
    {
      switch (option)
        {
          case 'b':
            boost = true;
            break;

         case 'c':
            cs = true;
            break;

         case 'd':
            delay = true;
            break;

         case 'l':
           lock = true;
           break;

         case 't':
           if (optarg)
             {
               tcase = (int)atoi(optarg);
             }
           break;
        }
    }

  switch (tcase)
    {
      case 0:
        sleep_udelay_test(cs, lock, delay);
        break;

      case 1:
        sem_test(cs, lock);
        break;

      case 2:
        priority_test(cs, lock, boost);
        break;
    }

  return 0;
}
