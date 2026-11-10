/* -*- C -*-
  Copyright (C) 2004, 2006, 2008, 2010, 2011, 2012, 2025
  Rocky Bernstein <rocky@gnu.org>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
   Regression test for BIN/CUE device driver: lib/driver/image/bincue.c.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h> /* chdir */
#endif

#include <cdio/cdio.h>
#include <cdio/logging.h>
#include "helper.h"

#ifndef DATA_DIR
#define DATA_DIR "../data"
#endif

#define NUM_GOOD_CUES 3
#define NUM_BAD_CUES 8

static int
check_too_many_tracks(void)
{
  const char *cue_name = "bincue-too-many-tracks.cue";
  FILE *cue;
  CdIo_t *p_cdio;
  unsigned int i;

  cue = fopen(cue_name, "w");
  if (!cue) {
    printf("Can't create %s\n", cue_name);
    return 1;
  }

  fprintf(cue, "FILE \"%s/cdda.bin\" BINARY\n", DATA_DIR);
  for (i = 1; i <= CDIO_CD_MAX_TRACKS; i++)
    fprintf(cue, "TRACK %02u AUDIO\n", i);
  fprintf(cue, "TRACK %02u AUDIO\n", CDIO_CD_MAX_TRACKS);
  fclose(cue);

  p_cdio = cdio_open_bincue(cue_name);
  remove(cue_name);
  if (p_cdio) {
    printf("Incorrect: %s with too many tracks opened.\n", cue_name);
    cdio_destroy(p_cdio);
    return 1;
  }
  return 0;
}

static int
check_file_before_first_track(void)
{
  CdIo_t *p_cdio = cdio_open_bincue(DATA_DIR "/cdda.cue");

  if (!p_cdio) {
    printf("Can't open cdda.cue with FILE before TRACK\n");
    return 1;
  }

  cdio_destroy(p_cdio);
  return 0;
}

/* BIN/CUE images have no sub-channel data and the driver can't run MMC
   commands, so cdio_read_audio_sectors_sub() only reads without it */
static int
check_read_audio_sectors_sub(void)
{
  static uint8_t audio[2 * CDIO_CD_FRAMESIZE_RAW];
  static uint8_t buf[2 * (CDIO_CD_FRAMESIZE_RAW + CDIO_CD_FRAMESIZE_SUB)];
  CdIo_t *p_cdio = cdio_open_bincue(DATA_DIR "/cdda.cue");
  int ret = 0;

  if (!p_cdio) {
    printf("Can't open cdda.cue\n");
    return 1;
  }

  if (DRIVER_OP_SUCCESS != cdio_read_audio_sectors(p_cdio, audio, 0, 2) ||
      DRIVER_OP_SUCCESS !=
      cdio_read_audio_sectors_sub(p_cdio, buf, 0, CDIO_READ_SUB_NONE, 2) ||
      memcmp(buf, audio, sizeof(audio))) {
    printf("cdda.cue read without sub-channel doesn't match the audio\n");
    ret = 1;
  }
  if (DRIVER_OP_UNSUPPORTED !=
      cdio_read_audio_sectors_sub(p_cdio, buf, 0, CDIO_READ_SUB_PW_RAW, 2) ||
      DRIVER_OP_UNSUPPORTED !=
      cdio_read_audio_sectors_sub(p_cdio, buf, 0, CDIO_READ_SUB_Q, 2)) {
    printf("cdda.cue read with sub-channel isn't unsupported\n");
    ret = 1;
  }
  if (DRIVER_OP_BAD_PARAMETER !=
      cdio_read_audio_sectors_sub(p_cdio, buf, 0, (cdio_read_sub_t) 3, 1)) {
    printf("cdda.cue read with an invalid sub-channel isn't rejected\n");
    ret = 1;
  }

  cdio_destroy(p_cdio);
  return ret;
}

static int
check_no_tracks(void)
{
  const char *cue_name = "bincue-no-tracks.cue";
  FILE *cue = fopen(cue_name, "w");
  CdIo_t *p_cdio;

  if (!cue) {
    printf("Can't create %s\n", cue_name);
    return 1;
  }
  fprintf(cue, "FILE \"%s/cdda.bin\" BINARY\n", DATA_DIR);
  fclose(cue);

  p_cdio = cdio_open_bincue(cue_name);
  remove(cue_name);
  if (p_cdio) {
    printf("Incorrect: %s with no tracks opened.\n", cue_name);
    cdio_destroy(p_cdio);
    return 1;
  }
  return 0;
}

static int
check_mode2_seek(const char *cue_name, const char *mode)
{
  CdIo_t *p_cdio;
  char cue_path[500];
  int result = 1;

  snprintf(cue_path, sizeof(cue_path), "%s/%s", DATA_DIR, cue_name);
  p_cdio = cdio_open_bincue(cue_path);
  if (!p_cdio) {
    printf("Can't open %s track image\n", mode);
    return 1;
  }
  if (cdio_lseek(p_cdio, 0, SEEK_SET) < 0) {
    printf("Can't seek %s track image\n", mode);
  } else {
    result = 0;
  }
  cdio_destroy(p_cdio);
  return result;
}

int
main(int argc, const char *argv[])
{
  const char *cue_file[NUM_GOOD_CUES] = {
    "cdda.cue",
    "cdda2.cue",
    "isofs-m1.cue",
  };

  const char *badcue_file[NUM_BAD_CUES] = {
    "bad-cat1.cue",
    "bad-cat2.cue",
    "bad-cat3.cue",
    "bad-mode1.cue",
    "bad-msf-1.cue",
    "bad-msf-2.cue",
    "bad-msf-3.cue",
    "bad-tracknum-exceeded.cue",
  };
  int ret=0;
  unsigned int i;
  char psz_cuefile[500];
  unsigned int verbose = (argc > 1);

  psz_cuefile[sizeof(psz_cuefile)-1] = '\0';
  cdio_loglevel_default = (argc > 1) ? CDIO_LOG_DEBUG : CDIO_LOG_WARN;

  if (check_too_many_tracks())
    ret = 1;
  if (check_no_tracks())
    ret = 1;
  if (check_file_before_first_track())
    ret = 1;
  if (check_read_audio_sectors_sub())
    ret = 1;
  if (check_mode2_seek("mode2-2048.cue", "MODE2/2048")
      || check_mode2_seek("mode2-2324.cue", "MODE2/2324"))
    ret = 1;

  for (i=0; i<NUM_GOOD_CUES; i++) {
    char *psz_binfile;
    snprintf(psz_cuefile, sizeof(psz_cuefile)-1,
             "%s/%s", DATA_DIR, cue_file[i]);
    psz_binfile = cdio_is_cuefile(psz_cuefile);
    if (!psz_binfile) {
      printf("Incorrect: %s doesn't parse as a CDRWin CUE file.\n",
             cue_file[i]);
      ret=i+1;
    } else {
        if (verbose)
            printf("Correct: %s parses as a CDRWin CUE file.\n",
                   cue_file[i]);
      free(psz_binfile);
    }
  }

  for (i=0; i<NUM_BAD_CUES; i++) {
    char *psz_binfile;
    snprintf(psz_cuefile, sizeof(psz_cuefile)-1,
             "%s/%s", DATA_DIR, badcue_file[i]);
    psz_binfile=cdio_is_cuefile(psz_cuefile);
    if (!psz_binfile) {
        if (verbose)
            printf("Correct: %s doesn't parse as a CDRWin CUE file.\n",
                   badcue_file[i]);
    } else {
      printf("Incorrect: %s parses as a CDRWin CUE file.\n",
             badcue_file[i]);
      free(psz_binfile);
      ret+=50*i+1;
      break;
    }
  }

  {
    CdIo_t *p_cdio;
    snprintf(psz_cuefile, sizeof(psz_cuefile)-1,
             "%s/%s", DATA_DIR, "cdda.cue");
    p_cdio  = cdio_open (psz_cuefile, DRIVER_UNKNOWN);
    if (!p_cdio) {
      printf("Can't open cdda.cue\n");
    } else {
      char *psz_device;

      /* Just test performing some operations. */
      cdio_set_blocksize(p_cdio, 2048);

#ifdef HAVE_CHDIR
      if (0 == chdir(DATA_DIR))
#endif
      {
          psz_device = cdio_get_default_device(p_cdio);

          check_mmc_supported(p_cdio, 1);
          check_access_mode(p_cdio, "image");
          // check_get_arg_source(p_cdio, psz_device);

          /* Could chdir to srcdir to hedge the bet? */
          if (psz_device)
              free(psz_device);
          else {
              /* Unless we do the chdir, will fail. So don't set as an
               * error. */
              printf("Can't get default device\n");
          }
          cdio_set_speed(p_cdio, 5);
      }

      cdio_destroy(p_cdio);

    }

  }

  return ret;
}
