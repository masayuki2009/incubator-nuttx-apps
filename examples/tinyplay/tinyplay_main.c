/****************************************************************************
 * apps/examples/tinyplay/tinyplay_main.c
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
#include <sys/ioctl.h>
#include <arpa/inet.h> /* ntoh */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>

#include <nuttx/audio/audio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MP3FH_SYNC(_h)       ((((_h)>>21) & 0x7FF) == 0x7FF)
#define MP3FH_VERSION(_h)    (((_h)>>19) & 0x3)
#define MP3FH_LAYER(_h)      (((_h)>>17) & 0x3)
#define MP3FH_BITRATE(_h)    (((_h)>>12) & 0xF)
#define MP3FH_SAMPLERATE(_h) (((_h)>>10) & 0x3)
#define MP3FH_PADDING(_h)    (((_h)>> 9) & 0x1)
#define MP3FH_CHANNEL(_h)    (((_h)>> 6) & 0x3)

#define BUF_SIZE 1024

/* #define DUMP_TO_FILE */

/****************************************************************************
 * Private Data
 ****************************************************************************/

struct file_info_s
{
  uint8_t  fmt;
  uint8_t  ch;
  uint32_t freq;
};

static bool g_tinyplay_running;

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern int open_with_http(const char *fullurl, uint8_t *fmt);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: _tinyplay_signal_handler
 ****************************************************************************/

static void _tinyplay_signal_handler(int number)
{
  g_tinyplay_running = false;
}

/****************************************************************************
 * Name: _mp3_sample_rate
 ****************************************************************************/

static uint32_t _mp3_sample_rate(int version, int info)
{
  static const uint32_t sample_rate[4][4] =
    {
      {11025,    0, 22050, 44100},
      {12000,    0, 24000, 48000},
      { 8000,    0, 16000, 32000},
      {    0,    0,     0,     0},
    };

  return sample_rate[info][version];
}

/****************************************************************************
 * Name: _mp3_channel_count
 ****************************************************************************/

static int _mp3_channel_count(int info)
{
  if (3 == info)
    {
      return 1;
    }
  else
    {
      return 2;
    }
}

/****************************************************************************
 * Name: _parse_file
 ****************************************************************************/

static int _parse_file(const char *file, struct file_info_s *info)
{
  int fd;
  uint8_t fmt;
  uint8_t tag[10];
  uint8_t wavh[40];

  fmt = AUDIO_FMT_UNDEF;

#ifdef CONFIG_EXAMPLES_TINYPLAY_HTTP_SUPPORT
  fd = open_with_http(file, &fmt);
#else
  fd = open(file, O_RDONLY);
#endif

  if (0 >= fd)
    {
      return fd;
    }

  info->ch   = 2;
  info->freq = 44100;

  if (strstr(file, ".wav"))
    {
      info->fmt = AUDIO_FMT_PCM;

      read(fd, wavh, sizeof(wavh));

      uint16_t *tmp1 = (uint16_t *)&wavh[22];
      info->ch = *tmp1;

      uint32_t *tmp2 = (uint32_t *)&wavh[24];
      info->freq = *tmp2;
    }

  else if (strstr(file, ".mp3") || fmt == AUDIO_FMT_MP3)
    {
      info->fmt = AUDIO_FMT_MP3;

      /* Check if ID3v2 tag exists */
      /* Firstly, read only 4 byts (may include MP3 haeder) */

      read(fd, tag, 4);

      if (0 == strncmp((FAR const char *)tag, "ID3", 3))
        {
          read(fd, &tag[4], sizeof(tag) - 4);
          uint32_t *tmp = (uint32_t *)&tag[6];
          uint32_t len = ntohl(*tmp);

          /* Skip len bytes */

          while (len--)
            {
              uint8_t dum_uint8;
              read(fd, &dum_uint8, sizeof(uint8_t));
            }

          /* Read MP3 header into tag */

          read(fd, tag, 4);
        }

      /* Get MP3 header */

      uint32_t data;
      memcpy(&data, tag, 4);
      uint32_t h = ntohl(data);

      if (MP3FH_SYNC(h))
        {
          info->freq = _mp3_sample_rate(
                                        MP3FH_VERSION(h),
                                        MP3FH_SAMPLERATE(h)
                                        );

          info->ch = _mp3_channel_count(MP3FH_CHANNEL(h));

        }

      /* TODO: MP3 header should be sent to audio device */

    }

  switch (info->fmt)
    {
      case AUDIO_FMT_MP3:
        printf("fmt=mp3 ");
        break;

      case AUDIO_FMT_PCM:
        printf("fmt=pcm ");
        break;

      default:
        ASSERT(false);
    }

  printf("ch=%d freq=%ld \n", info->ch, info->freq);
  return fd;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tinyplay_main
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int tinyplay_main(int argc, char *argv[])
#endif
{
  FAR struct ap_buffer_s *apb;
  struct audio_buf_desc_s desc;
  ssize_t bufsize;
  int ret;
  int fd[2];
  struct file_info_s info;

  memset(&info, 0, sizeof(info));
  fd[0] = _parse_file(argv[1], &info);

  if (0 >= fd[0])
    {
      return -1;
    }

  /* Open the I2S character device */

  fd[1] = open("/dev/i2schar0", O_RDWR);
  ASSERT(0 < fd[1]);

  /* Setup fmt/ch/freq for i2s output */

  uint32_t tx_th = 98; /* tx threshold = 98% */
  struct audio_caps_desc_s  cap_desc;
  cap_desc.caps.ac_len        = sizeof(struct audio_caps_s);
  cap_desc.caps.ac_type       = AUDIO_TYPE_OUTPUT;
  cap_desc.caps.ac_format.hw  = info.fmt;
  cap_desc.caps.ac_channels   = info.ch;
  cap_desc.caps.ac_controls.w = info.freq | (tx_th << 24);

  ioctl(fd[1], AUDIOIOC_CONFIGURE, (unsigned long)&cap_desc);

  /* Allocate an audio buffer */

  desc.numbytes   = BUF_SIZE;
  desc.u.pbuffer = &apb;

  ret = apb_alloc(&desc);
  ASSERT(ret == sizeof(desc));

  signal(1, _tinyplay_signal_handler);
  g_tinyplay_running = true;

#ifdef DUMP_TO_FILE
  int dump_fd = open("/mnt/sd0/tinyplay_dump.raw", O_WRONLY | O_CREAT | O_TRUNC);
  ASSERT(0 < dump_fd);
#endif

  while (g_tinyplay_running)
    {
      int total = 0;
      int rsize = BUF_SIZE;

      while (total < BUF_SIZE)
        {
          ret = read(fd[0], apb->samp + total, rsize);

          if (0 >= ret)
            {
              printf("read() returned %d errno=%d \n", ret, errno);
              g_tinyplay_running = 0;
              break;
            }

          total += ret;
          rsize -= ret;
        }

#ifdef DUMP_TO_FILE
      write(dump_fd, apb->samp, total);
#endif

      apb->nbytes = total;
      apb->nmaxbytes = total;

      bufsize = sizeof(struct ap_buffer_s) + apb->nbytes;
      ret = write(fd[1], apb, bufsize);

    }

#ifdef DUMP_TO_FILE
  close(dump_fd);
#endif

  apb_free(apb);
  close(fd[1]);
  close(fd[0]);

  return EXIT_SUCCESS;
}
