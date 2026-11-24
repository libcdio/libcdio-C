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

/* A CUE sheet may start at a track number other than 1. The sector
   count of its last track must still be set, so seeking into it works */
static int
check_first_track_not_one(void)
{
  const char *cue_name = "bincue-first-track-50.cue";
  const char *bin_name = "bincue-first-track-50.bin";
  FILE *cue, *bin;
  CdIo_t *p_cdio;
  long i;
  int ret = 0;

  /* The BIN file is named as cue sheets name it, next to the cue sheet */
  bin = fopen(bin_name, "wb");
  if (!bin) {
    printf("Can't create %s\n", bin_name);
    return 1;
  }
  for (i = 0; i < 20 * CDIO_CD_FRAMESIZE_RAW; i++)
    fputc(0, bin);
  fclose(bin);
  cue = fopen(cue_name, "w");
  if (!cue) {
    printf("Can't create %s\n", cue_name);
    remove(bin_name);
    return 1;
  }
  fprintf(cue, "FILE \"%s\" BINARY\n", bin_name);
  fprintf(cue, "  TRACK 50 AUDIO\n");
  fprintf(cue, "    INDEX 01 00:00:00\n");
  fclose(cue);

  p_cdio = cdio_open_bincue(cue_name);
  remove(cue_name);
  if (!p_cdio) {
    printf("Can't open %s\n", cue_name);
    remove(bin_name);
    return 1;
  }

  if (cdio_get_first_track_num(p_cdio) != 50) {
    printf("%s first track is %d, not 50\n", cue_name,
           cdio_get_first_track_num(p_cdio));
    ret = 1;
  }
  if (cdio_lseek(p_cdio, 10 * CDIO_CD_FRAMESIZE_RAW, SEEK_SET) < 0) {
    printf("Can't seek into the last track of %s\n", cue_name);
    ret = 1;
  }

  cdio_destroy(p_cdio);
  remove(bin_name);
  return ret;
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

/* An INDEX or PREGAP given an MSF string out of range or that isn't
   one must be rejected, and the same cue sheet with a valid one
   accepted. Frames that weren't digits used to be read as -1 */
static int
check_msf_cue(const char *psz_line, const char *psz_msf, bool b_valid)
{
  const char *cue_name = "bincue-bad-msf.cue";
  const char *bin_name = "bincue-bad-msf.bin";
  FILE *cue, *bin;
  CdIo_t *p_cdio;
  long i;
  int ret = 0;

  /* The BIN file is named as cue sheets name it, next to the cue
     sheet: an absolute DATA_DIR may start with a drive letter */
  bin = fopen(bin_name, "wb");
  if (!bin) {
    printf("Can't create %s\n", bin_name);
    return 1;
  }
  for (i = 0; i < 4 * CDIO_CD_FRAMESIZE_RAW; i++)
    fputc(0, bin);
  fclose(bin);
  cue = fopen(cue_name, "w");
  if (!cue) {
    printf("Can't create %s\n", cue_name);
    remove(bin_name);
    return 1;
  }
  fprintf(cue, "FILE \"%s\" BINARY\n  TRACK 01 AUDIO\n", bin_name);
  if (0 == strcmp(psz_line, "INDEX"))
    fprintf(cue, "    INDEX 01 %s\n", psz_msf);
  else
    fprintf(cue, "    PREGAP %s\n    INDEX 01 00:00:00\n", psz_msf);
  fclose(cue);

  p_cdio = cdio_open_bincue(cue_name);
  remove(cue_name);
  if (b_valid != (NULL != p_cdio)) {
    printf("Incorrect: %s %s %s.\n", psz_line, psz_msf,
           b_valid ? "doesn't open" : "opened");
    ret = 1;
  }
  if (p_cdio)
    cdio_destroy(p_cdio);
  remove(bin_name);
  return ret;
}

static int
check_bad_msf(const char *psz_line)
{
  static const char *const bad[] = {
    "00:00:x", "477218:00:00", "954437:10:43", "00:60:00", "00:00:75",
  };
  unsigned int i;
  int ret = check_msf_cue(psz_line, "00:00:00", true);

  for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    if (check_msf_cue(psz_line, bad[i], false))
      ret = 1;
  return ret;
}

/* cdio_read() past the end of the image stops in the last track: it
   used to go on into the lead-out entry, which has no data, and from
   there past the end of tocent[]. Only a memory checker sees that */
static int
check_read_past_end(void)
{
  const size_t i_size = 710304; /* cdda.bin */
  char *buf = malloc(2 * i_size);
  CdIo_t *p_cdio = cdio_open_bincue(DATA_DIR "/cdda.cue");
  ssize_t i_read;
  int ret = 0;

  if (!buf || !p_cdio) {
    printf("Can't open cdda.cue\n");
    free(buf);
    if (p_cdio)
      cdio_destroy(p_cdio);
    return 1;
  }
  if (cdio_lseek(p_cdio, 0, SEEK_SET) < 0) {
    printf("Can't seek in cdda.cue\n");
    ret = 1;
  } else {
    i_read = cdio_read(p_cdio, buf, 2 * i_size);
    if (i_read < 0 || (size_t) i_read > i_size) {
      printf("Incorrect: read %ld bytes past the end of cdda.cue\n",
             (long) i_read);
      ret = 1;
    }
  }
  cdio_destroy(p_cdio);
  free(buf);
  return ret;
}

/* Write a cue sheet whose FILE is a 4-sector BIN file of zeros, followed
   by psz_body, and open it. Both files are in the current directory and
   are removed again; the BIN file only once p_cdio is destroyed, which
   remove_bin_cue() does. */
static const char *bin_cue_bin = "bincue-tracks.bin";

static CdIo_t *
open_bin_cue(const char *psz_body)
{
  const char *cue_name = "bincue-tracks.cue";
  FILE *cue, *bin;
  CdIo_t *p_cdio;
  long i;

  bin = fopen(bin_cue_bin, "wb");
  if (!bin) {
    printf("Can't create %s\n", bin_cue_bin);
    return NULL;
  }
  for (i = 0; i < 4 * CDIO_CD_FRAMESIZE_RAW; i++)
    fputc(0, bin);
  fclose(bin);
  cue = fopen(cue_name, "w");
  if (!cue) {
    printf("Can't create %s\n", cue_name);
    return NULL;
  }
  fprintf(cue, "FILE \"%s\" BINARY\n%s", bin_cue_bin, psz_body);
  fclose(cue);

  p_cdio = cdio_open_bincue(cue_name);
  remove(cue_name);
  return p_cdio;
}

static void
remove_bin_cue(CdIo_t *p_cdio)
{
  if (p_cdio)
    cdio_destroy(p_cdio);
  remove(bin_cue_bin);
}

/* The track getters must reject a track number the image doesn't have,
   also below the first track: they used to index the table of tracks
   with it */
static int
check_track_range(void)
{
  static const track_t bad[] = { 1, 98, 100 };
  CdIo_t *p_cdio = open_bin_cue("  TRACK 99 AUDIO\n    INDEX 01 00:00:00\n");
  unsigned int i;
  int ret = 0;

  if (!p_cdio) {
    printf("Can't open a cue sheet whose only track is 99\n");
    remove_bin_cue(NULL);
    return 1;
  }
  if (CDIO_TRACK_FLAG_ERROR == cdio_get_track_copy_permit(p_cdio, 99)
      || -1 == cdio_get_track_channels(p_cdio, 99)) {
    printf("Incorrect: track 99 is rejected.\n");
    ret = 1;
  }
  for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    char *isrc = cdio_get_track_isrc(p_cdio, bad[i]);

    if (isrc
        || -1 != cdio_get_track_channels(p_cdio, bad[i])
        || CDIO_TRACK_FLAG_ERROR != cdio_get_track_copy_permit(p_cdio, bad[i])
        || CDIO_TRACK_FLAG_ERROR != cdio_get_track_preemphasis(p_cdio, bad[i])
        || CDIO_INVALID_LBA != cdio_get_track_pregap_lba(p_cdio, bad[i])) {
      printf("Incorrect: track %u of a disc with only track 99 accepted.\n",
             (unsigned int) bad[i]);
      ret = 1;
    }
    free(isrc);
  }
  remove_bin_cue(p_cdio);
  return ret;
}

/* Track numbers must follow each other. A first track of 99 followed by
   2 and 3 used to give a disc whose last track is 101 */
static int
check_track_sequence(void)
{
  static const char *const bad[] = {
    "  TRACK 99 AUDIO\n    INDEX 01 00:00:00\n"
    "  TRACK 02 AUDIO\n    INDEX 01 00:00:01\n"
    "  TRACK 03 AUDIO\n    INDEX 01 00:00:02\n",
    "  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
    "  TRACK 03 AUDIO\n    INDEX 01 00:00:01\n",
    "  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n"
    "  TRACK 01 AUDIO\n    INDEX 01 00:00:01\n",
  };
  unsigned int i;
  int ret = 0;
  CdIo_t *p_cdio = open_bin_cue("  TRACK 98 AUDIO\n    INDEX 01 00:00:00\n"
                                "  TRACK 99 AUDIO\n    INDEX 01 00:00:01\n");

  if (!p_cdio || 99 != cdio_get_last_track_num(p_cdio)) {
    printf("Incorrect: tracks 98 and 99 don't open.\n");
    ret = 1;
  }
  remove_bin_cue(p_cdio);
  for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    p_cdio = open_bin_cue(bad[i]);
    if (p_cdio) {
      printf("Incorrect: tracks out of sequence opened (case %u).\n", i);
      ret = 1;
    }
    remove_bin_cue(p_cdio);
  }
  return ret;
}

/* A keyword given twice replaces the first value. The first value
   used to be leaked, which only a memory checker sees */
static int
check_repeated_keywords(void)
{
  CdIo_t *p_cdio = open_bin_cue("CATALOG 0000000000001\n"
                                "CATALOG 0000000000002\n"
                                "FILE \"bincue-tracks.bin\" BINARY\n"
                                "  TRACK 01 AUDIO\n"
                                "    ISRC USAAA0000001\n"
                                "    ISRC USAAA0000002\n"
                                "    INDEX 01 00:00:00\n");
  char *psz;
  int ret = 0;

  if (!p_cdio) {
    printf("Incorrect: repeated CATALOG, FILE and ISRC don't open.\n");
    remove_bin_cue(NULL);
    return 1;
  }
  psz = cdio_get_track_isrc(p_cdio, 1);
  if (!psz || strcmp(psz, "USAAA0000002")) {
    printf("Incorrect: ISRC is %s, not the last one given.\n",
           psz ? psz : "(none)");
    ret = 1;
  }
  free(psz);
  psz = cdio_get_mcn(p_cdio);
  if (!psz || strcmp(psz, "0000000000002")) {
    printf("Incorrect: CATALOG is %s, not the last one given.\n",
           psz ? psz : "(none)");
    ret = 1;
  }
  free(psz);
  remove_bin_cue(p_cdio);
  return ret;
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
  if (check_first_track_not_one())
    ret = 1;
  if (check_read_audio_sectors_sub())
    ret = 1;
  if (check_bad_msf("INDEX") || check_bad_msf("PREGAP"))
    ret = 1;
  if (check_read_past_end())
    ret = 1;
  if (check_track_range() || check_track_sequence()
      || check_repeated_keywords())
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
