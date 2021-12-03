/****************************************************************************
 * apps/examples/tinyplay/tinyplay_http.c
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <arpa/inet.h>  /* htons() */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <assert.h>
#include <time.h>
#include <netdb.h>
#include <netutils/netlib.h>

#include <nuttx/audio/audio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HOSTNAME_SIZE 32
#define URL_SIZE 32
#define RESP_HEADER_SIZE 256

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char *g_ctype = "content-type:";
static const char *g_audio_mpeg = "audio/mpeg";

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: _check_resp_header
 ****************************************************************************/

static void _check_resp_header(int s, uint8_t *fmt)
{
  int  resp_chk = 0;
  int  resp_pos = 0;
  char resp_msg[] = "\r\n\r\n";
  char *resphdr;
  char c;
  int  n;

  resphdr = (char *)malloc(RESP_HEADER_SIZE);
  ASSERT(resphdr);

  while (1)
    {
      n = read(s, &c, 1);

      if (1 == n)
        {
          *(resphdr + resp_pos) = c;
          resp_pos++;
          ASSERT(resp_pos < RESP_HEADER_SIZE);

          if (resp_msg[resp_chk] == c)
            {
              resp_chk++;
            }
          else
            {
              resp_chk = 0;
            }
        }

      if (resp_chk == 2)
        {
          /* \r\n found */
          *(resphdr + resp_pos) = '\0';
          resp_pos = 0;

          printf("resphdr: %s ", resphdr);

          if (strcasestr(resphdr, g_ctype) &&
              strcasestr(resphdr + sizeof(g_ctype), g_audio_mpeg))
            {
              *fmt = AUDIO_FMT_MP3;
              printf("AUDIO_FMT_MP3 \n");
            }
        }

      if (resp_chk == 4)
        {
          /* \r\n\r\n found */
          break;
        }
    }

  free(resphdr);
}

/****************************************************************************
 * _set_socket_timeout
 ****************************************************************************/

static void _set_socket_timeout(int s, int t)
{
  struct timeval tv;
  tv.tv_sec  = t;
  tv.tv_usec = 0;

  (void)setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (FAR const void *)&tv,
                   sizeof(struct timeval));
  (void)setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (FAR const void *)&tv,
                   sizeof(struct timeval));
}

/****************************************************************************
 * Name: open_with_http
 *
 *   open_with_http() opens specified fullurl which is http:// or local file
 *   path and returns a file descriptor.
 *
 ****************************************************************************/

int open_with_http(const char *fullurl, uint8_t *fmt)
{
  char relurl[URL_SIZE];
  char hostname[HOSTNAME_SIZE];
  uint16_t  port;
  char buf[64];
  int  s;
  int  n;

  if (NULL == strstr(fullurl, "http://"))
    {
      /* assumes local file specified */

      s = open(fullurl, O_RDONLY);
      return s;
    }

  memset(relurl, 0, sizeof(relurl));
  port = 80;

  n = netlib_parsehttpurl(fullurl, &port,
                          hostname, sizeof(hostname) - 1,
                          relurl, sizeof(relurl) - 1);

  if (OK != n)
    {
      printf("netlib_parsehttpurl() returned %d \n", n);
      return n;
    }

  s = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT(s != -1);

  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_port   = htons(port);

  FAR struct hostent *he;
  he = gethostbyname(hostname);

  memcpy(&server.sin_addr.s_addr,
         he->h_addr, sizeof(in_addr_t));

  struct timespec ts0, ts1;
#ifdef CONFIG_CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &ts0);
#else
  clock_gettime(CLOCK_REALTIME, &ts0);
#endif
  printf("**** start to connect \n");

  /* timeout = 10 sec */

  _set_socket_timeout(s, 10);

  n = connect(s,
              (struct sockaddr *)&server,
              sizeof(struct sockaddr_in));

  if (-1 == n)
    {
      close(s);
      printf("Failed to connect \n");
      return -1;
    }

#ifdef CONFIG_CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &ts1);
#else
  clock_gettime(CLOCK_REALTIME, &ts1);
#endif

  uint64_t elapsed;
  elapsed  = (((uint64_t)ts1.tv_sec * NSEC_PER_SEC) + ts1.tv_nsec);
  elapsed -= (((uint64_t)ts0.tv_sec * NSEC_PER_SEC) + ts0.tv_nsec);
  elapsed /= NSEC_PER_MSEC; /* msec */

  printf("**** connection established d=%d (ms) \n", (int)elapsed);


  /* timeout = 0 sec */

  _set_socket_timeout(s, 0);

  /* Send GET request */

  snprintf(buf, sizeof(buf), "GET %s HTTP/1.0\r\n\r\n", relurl);
  n = write(s, buf, strlen(buf));

  usleep(100 * 1000); /* TODO */

  /* Check status line : e.g. "HTTP/1.x XXX" */

  memset(buf, 0, sizeof(buf));
  read(s, buf, 12);
  n = atoi(buf + 9);

  if (200 != n)
    {
      close(s);
      printf("Invalid Response %d \n", n);
      return -1;
    }

  _check_resp_header(s, fmt);

  return s;
}

