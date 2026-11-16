/*
  Copyright (C) 2013 Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2013-2014 Pete Batard <pete@akeo.ie>

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

/* Tests reading UDF info from an UDF image.  */

#ifndef DATA_DIR
#define DATA_DIR "./data"
#endif
#define UDF_IMAGE DATA_DIR "/udf102.iso"

#ifdef HAVE_CONFIG_H
#include "config.h"
#define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <time.h>

#include <cdio/cdio.h>
#include <cdio/udf.h>

#define EXPECTED_NAME    "FéжΘvrier"
#define EXPECTED_LENGTH  10

#define EXPECTED_MTIME   1392859512 /* 2014-02-20 01:25:12 UTC */

/* Read all of UDF_IMAGE. Return NULL on error. */
static uint8_t *
read_image(long *p_size)
{
  FILE *fp;
  uint8_t *image;
  long size;

  fp = fopen(UDF_IMAGE, "rb");
  if (!fp)
    return NULL;
  if (fseek(fp, 0, SEEK_END) || (size = ftell(fp)) <= 0
      || fseek(fp, 0, SEEK_SET)) {
    fclose(fp);
    return NULL;
  }
  image = malloc(size);
  if (!image || fread(image, 1, size, fp) != (size_t) size) {
    free(image);
    fclose(fp);
    return NULL;
  }
  fclose(fp);
  *p_size = size;
  return image;
}

/* Write image to a temporary file and free it. Return the file's name,
   which stays valid until the next call, or NULL on error. */
static const char *
write_temp_image(uint8_t *image, long size)
{
#ifdef HAVE_MKSTEMP
  static char psz_tmp[sizeof("libcdio-udf-XXXXXX")];
  int fd;
#else
  const char *psz_tmp;
#endif
  FILE *fp;

#ifdef HAVE_MKSTEMP
  strcpy(psz_tmp, "libcdio-udf-XXXXXX");
  fd = mkstemp(psz_tmp);
  fp = fd < 0 ? NULL : fdopen(fd, "wb");
  if (!fp && fd >= 0) {
    close(fd);
    remove(psz_tmp);
  }
#else
  psz_tmp = tmpnam(NULL);
  fp = psz_tmp ? fopen(psz_tmp, "wb") : NULL;
#endif
  if (!fp) {
    free(image);
    return NULL;
  }
  if (fwrite(image, 1, size, fp) != (size_t) size) {
    fclose(fp);
    remove(psz_tmp);
    free(image);
    return NULL;
  }
  fclose(fp);
  free(image);
  return psz_tmp;
}

/* Copy UDF_IMAGE to a temporary file, with the length byte of every
   Logical Volume Descriptor's identifier set to 0xff and its compression
   ID set to comp_id, and get the logical volume ID from it. The length
   must be kept within the field. If psz_expected isn't NULL, the ID must
   match it. */
static int
check_logvol_id_length(uint8_t comp_id, const char *psz_expected)
{
  char volume_id[192];
  const char *psz_tmp;
  uint8_t *image;
  long size, lba;
  unsigned int patched = 0;
  udf_t *p_udf;
  udf_dirent_t *p_udf_root;
  int len, rc = 1;

  image = read_image(&size);
  if (!image)
    return 1;

  for (lba = 0; (lba + 1) * UDF_BLOCKSIZE <= size; lba++) {
    logical_vol_desc_t *p_lvd = (logical_vol_desc_t *)
      (image + lba * UDF_BLOCKSIZE);
    const uint8_t *tag = (const uint8_t *) &p_lvd->tag;

    if (tag[0] == TAGID_LOGVOL && tag[1] == 0
        && (tag[12] | tag[13] << 8 | tag[14] << 16
            | (uint32_t) tag[15] << 24) == (uint32_t) lba) {
      p_lvd->logvol_id[0] = (char) comp_id;
      p_lvd->logvol_id[sizeof(p_lvd->logvol_id)-1] = (char) 0xff;
      patched++;
    }
  }
  if (!patched) {
    fprintf(stderr, "No Logical Volume Descriptor in %s\n", UDF_IMAGE);
    free(image);
    return 1;
  }

  psz_tmp = write_temp_image(image, size);
  if (!psz_tmp)
    return 1;

  p_udf = udf_open(psz_tmp);
  if (p_udf) {
    p_udf_root = udf_get_root(p_udf, true, 0);
    if (p_udf_root) {
      len = udf_get_logical_volume_id(p_udf, volume_id, sizeof(volume_id));
      if (len <= 0 || len > 3 * 128)
        fprintf(stderr, "Unexpected logical volume ID length %d\n", len);
      else if (psz_expected && strcmp(psz_expected, volume_id) != 0)
        fprintf(stderr, "Unexpected logical volume ID '%s'\n", volume_id);
      else
        rc = 0;
      udf_dirent_free(p_udf_root);
    }
    udf_close(p_udf);
  }
  remove(psz_tmp);
  return rc;
}

/* Copy UDF_IMAGE to a temporary file, with the month of the
   modification time of every File Entry set to 13.
   udf_get_modification_time() must return -1 for the expected file, and
   the access time, which isn't changed, must still be read. */
static int
check_bad_fe_timestamp(void)
{
  const char *psz_tmp;
  uint8_t *image;
  long size, lba;
  unsigned int patched = 0;
  udf_t *p_udf;
  udf_dirent_t *p_udf_root, *p_udf_file;
  time_t t;
  int rc = 1;

  image = read_image(&size);
  if (!image)
    return 1;

  for (lba = 0; (lba + 1) * UDF_BLOCKSIZE <= size; lba++) {
    udf_file_entry_t *p_fe = (udf_file_entry_t *)
      (image + lba * UDF_BLOCKSIZE);
    const uint8_t *tag = (const uint8_t *) &p_fe->tag;

    if (tag[0] == (TAGID_FILE_ENTRY & 0xff)
        && tag[1] == TAGID_FILE_ENTRY >> 8) {
      p_fe->modification_time.month = 13;
      patched++;
    }
  }
  if (!patched) {
    fprintf(stderr, "No File Entry in %s\n", UDF_IMAGE);
    free(image);
    return 1;
  }

  psz_tmp = write_temp_image(image, size);
  if (!psz_tmp)
    return 1;

  p_udf = udf_open(psz_tmp);
  if (p_udf) {
    p_udf_root = udf_get_root(p_udf, true, 0);
    if (p_udf_root) {
      p_udf_file = udf_fopen(p_udf_root, EXPECTED_NAME);
      if (!p_udf_file)
        fprintf(stderr, "Could not locate expected file in UDF image\n");
      else {
        if ((t = udf_get_modification_time(p_udf_file)) != (time_t) -1)
          fprintf(stderr, "Modification time in month 13 read as %ld\n",
                  (long) t);
        else if (udf_get_access_time(p_udf_file) == (time_t) -1)
          fprintf(stderr, "Valid access time not read\n");
        else
          rc = 0;
        udf_dirent_free(p_udf_file);
      }
      udf_dirent_free(p_udf_root);
    }
    udf_close(p_udf);
  }
  remove(psz_tmp);
  return rc;
}

/* udf_stamp_to_time() must convert a timestamp for any year from 1 to
   9999 that fits in a time_t, apply a valid time zone offset, and reject
   a reserved type or a field out of range */
static int
check_bad_timestamp(void)
{
  static const struct {
    uint16_t year;
    uint8_t month, day;
    int64_t t;
  } good[] = {
    { 1970,  1,  1,            0 },
    { 1969, 12, 31,       -86400 },
    { 1901, 12, 14,  -2147472000 },
    { 2000,  2, 29,    951782400 },
    { 2038,  1, 19,   2147472000 },
    /* These need a 64-bit time_t. */
    { 2039,  1,  1,   2177452800 },
    { 2100,  3,  1,   4107542400 },
    { 1600,  2, 29, -11670998400 },
    {    1,  1,  1, -62135596800 },
    { 9999, 12, 31, 253402214400 },
  };
  /* Each field and the type at 2000-01-01 00:00:00, which is
     946684800, with one of them changed. */
  static const struct {
    uint16_t type_tz;
    int16_t year;
    uint8_t month, day, hour, minute, second, cs, hus, us;
    int64_t t;    /* -1 if the timestamp must be rejected */
    long usec;
  } fields[] = {
    /* Time zone offsets, which are only used with type 1. -2047 and an
       offset beyond 1440 minutes are taken as UTC. */
    { 0x1000 |  120,        2000, 1, 1,  0,  0,  0,   0,   0,   0,
      946684800 - 7200, 0 },
    { 0x1000 | (-1440 & 0x0FFF), 2000, 1, 1, 0, 0, 0, 0,   0,   0,
      946684800 + 86400, 0 },
    { 0x1000 | (-2047 & 0x0FFF), 2000, 1, 1, 0, 0, 0, 0,   0,   0,
      946684800, 0 },
    { 0x1000 | 1441,        2000, 1, 1,  0,  0,  0,   0,   0,   0,
      946684800, 0 },
    { 0x0000 |  120,        2000, 1, 1,  0,  0,  0,   0,   0,   0,
      946684800, 0 },
    { 0x2000 |  120,        2000, 1, 1,  0,  0,  0,   0,   0,   0,
      946684800, 0 },
    /* The largest value of each field, and a leap second with type 2. */
    { 0x1000,               2000, 1, 1, 23, 59, 59,  99,  99,  99,
      946684800 + 86399, 999999 },
    { 0x2000,               2000, 1, 1,  0,  0, 60,   0,   0,   0,
      946684800 + 60, 0 },
    /* Out of range. */
    { 0x3000,               2000, 1, 1,  0,  0,  0,   0,   0,   0, -1, 0 },
    { 0xF000,               2000, 1, 1,  0,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,                  0, 1, 1,  0,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,                 -1, 1, 1,  0,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,              10000, 1, 1,  0,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 0, 1,  0,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 13, 1, 0,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 255, 1, 0, 0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 1, 0,  0,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 1, 32, 0,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 1, 255, 0, 0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 1, 1, 24,  0,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 1, 1,  0, 60,  0,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 1, 1,  0,  0, 60,   0,   0,   0, -1, 0 },
    { 0x0000,               2000, 1, 1,  0,  0, 60,   0,   0,   0, -1, 0 },
    { 0x2000,               2000, 1, 1,  0,  0, 61,   0,   0,   0, -1, 0 },
    { 0x1000,               2000, 1, 1,  0,  0,  0, 100,   0,   0, -1, 0 },
    { 0x1000,               2000, 1, 1,  0,  0,  0,   0, 100,   0, -1, 0 },
    { 0x1000,               2000, 1, 1,  0,  0,  0,   0,   0, 100, -1, 0 },
  };
  udf_timestamp_t stamp;
  time_t t;
  long int usec;
  unsigned int i;

  memset(&stamp, 0, sizeof(stamp));
  for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
    const time_t expected = (time_t) good[i].t;

    stamp.year = good[i].year;
    stamp.month = good[i].month;
    stamp.day = good[i].day;
    if ((int64_t) expected != good[i].t) {
      /* Doesn't fit in this time_t, so it must be rejected. */
      if (udf_stamp_to_time(&t, &usec, stamp) != NULL || t != -1) {
        fprintf(stderr, "UDF timestamp %u-%02u-%02u accepted with a "
                "%u-bit time_t\n", good[i].year, good[i].month,
                good[i].day, (unsigned int) (8 * sizeof(time_t)));
        return 1;
      }
    } else if (!udf_stamp_to_time(&t, &usec, stamp) || t != expected) {
      fprintf(stderr, "Unexpected time for UDF timestamp %u-%02u-%02u\n",
              good[i].year, good[i].month, good[i].day);
      return 1;
    }
  }

  for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
    time_t *p_t;

    stamp.type_tz = fields[i].type_tz;
    stamp.year = fields[i].year;
    stamp.month = fields[i].month;
    stamp.day = fields[i].day;
    stamp.hour = fields[i].hour;
    stamp.minute = fields[i].minute;
    stamp.second = fields[i].second;
    stamp.centiseconds = fields[i].cs;
    stamp.hundreds_of_microseconds = fields[i].hus;
    stamp.microseconds = fields[i].us;
    p_t = udf_stamp_to_time(&t, &usec, stamp);
    if (fields[i].t == -1 ? (p_t != NULL || t != -1 || usec != -1)
        : (!p_t || t != (time_t) fields[i].t || usec != fields[i].usec)) {
      fprintf(stderr, "UDF timestamp %04x %d-%02u-%02u "
              "%02u:%02u:%02u.%02u%02u%02u %s\n", fields[i].type_tz,
              fields[i].year, fields[i].month, fields[i].day,
              fields[i].hour, fields[i].minute, fields[i].second,
              fields[i].cs, fields[i].hus, fields[i].us,
              fields[i].t == -1 ? "accepted" : "converted wrongly");
      return 1;
    }
  }
  return 0;
}

#if defined(HAVE_STRUCT_TIMESPEC) && defined(HAVE_SETENV) \
  && defined(HAVE_TZSET)
/* Convert tv_sec with udf_timespec_to_stamp() in time zone tz, a POSIX
   TZ string, check the fields of the timestamp and convert it back with
   udf_stamp_to_time(). With year 0, the conversion must fail. */
static int
check_one_timespec(const char *tz, int64_t tv_sec, long tv_nsec,
                   uint16_t type_tz, uint16_t year, uint8_t month,
                   uint8_t day, uint8_t hour, uint8_t minute,
                   uint8_t second)
{
  struct timespec ts;
  udf_timestamp_t stamp;
  time_t t;
  long int usec;

  ts.tv_sec = (time_t) tv_sec;
  ts.tv_nsec = tv_nsec;
  if ((int64_t) ts.tv_sec != tv_sec)
    return 0; /* doesn't fit in this time_t */

  setenv("TZ", tz, 1);
  tzset();
  memset(&stamp, 0, sizeof(stamp));
  if (!udf_timespec_to_stamp(ts, &stamp)) {
    if (year == 0)
      return 0;
    fprintf(stderr, "udf_timespec_to_stamp(%lld) failed in %s\n",
            (long long) tv_sec, tz);
    return 1;
  }
  if (year == 0) {
    fprintf(stderr, "udf_timespec_to_stamp(%lld) accepted in %s\n",
            (long long) tv_sec, tz);
    return 1;
  }
  if (stamp.type_tz != type_tz || stamp.year != year
      || stamp.month != month || stamp.day != day || stamp.hour != hour
      || stamp.minute != minute || stamp.second != second
      || stamp.centiseconds != tv_nsec / 10000000
      || stamp.hundreds_of_microseconds != tv_nsec / 100000 % 100
      || stamp.microseconds != tv_nsec / 1000 % 100) {
    fprintf(stderr, "udf_timespec_to_stamp(%lld) in %s gave "
            "%04x %d-%02u-%02u %02u:%02u:%02u.%02u%02u%02u\n",
            (long long) tv_sec, tz, stamp.type_tz, stamp.year,
            stamp.month, stamp.day, stamp.hour, stamp.minute,
            stamp.second, stamp.centiseconds,
            stamp.hundreds_of_microseconds, stamp.microseconds);
    return 1;
  }
  if (!udf_stamp_to_time(&t, &usec, stamp) || t != ts.tv_sec
      || usec != tv_nsec / 1000) {
    fprintf(stderr, "udf_timespec_to_stamp(%lld) in %s doesn't convert "
            "back\n", (long long) tv_sec, tz);
    return 1;
  }
  return 0;
}

/* udf_timespec_to_stamp() must record the local time and its offset
   from UTC in minutes, for any time from year 1 to 9999 */
static int
check_timespec_to_stamp(void)
{
  const char *psz_tz = getenv("TZ");
  char *psz_saved = NULL;
  struct timespec ts;
  udf_timestamp_t stamp;
  int rc = 0;

  if (psz_tz) {
    psz_saved = malloc(strlen(psz_tz) + 1);
    if (!psz_saved)
      return 1;
    strcpy(psz_saved, psz_tz);
  }

  rc |= check_one_timespec("UTC0", 0, 0,
                           0x1000, 1970, 1, 1, 0, 0, 0);
  rc |= check_one_timespec("UTC0", 1392859512, 123456789,
                           0x1000, 2014, 2, 20, 1, 25, 12);
  rc |= check_one_timespec("UTC0", -1, 0,
                           0x1000, 1969, 12, 31, 23, 59, 59);
  /* 2 hours east of UTC is an offset of +120 minutes. */
  rc |= check_one_timespec("EET-2", 1392859512, 0,
                           0x1000 | 120, 2014, 2, 20, 3, 25, 12);
  /* 5 hours west of UTC is an offset of -300 minutes. */
  rc |= check_one_timespec("EST5", 1392859512, 0,
                           0x1000 | (-300 & 0x0FFF), 2014, 2, 19, 20, 25, 12);
  /* Beyond a 32-bit time_t, and the limits of the years 1 to 9999. */
  rc |= check_one_timespec("UTC0", INT64_C(4107542400), 0,
                           0x1000, 2100, 3, 1, 0, 0, 0);
  rc |= check_one_timespec("UTC0", INT64_C(-62135596800), 0,
                           0x1000, 1, 1, 1, 0, 0, 0);
  rc |= check_one_timespec("UTC0", INT64_C(253402300799), 999999999,
                           0x1000, 9999, 12, 31, 23, 59, 59);
  rc |= check_one_timespec("UTC0", INT64_C(-62135596801), 0,
                           0, 0, 0, 0, 0, 0, 0);
  rc |= check_one_timespec("UTC0", INT64_C(253402300800), 0,
                           0, 0, 0, 0, 0, 0, 0);
  rc |= check_one_timespec("EET-2", INT64_MAX, 0,
                           0, 0, 0, 0, 0, 0, 0);

  ts.tv_sec = 0;
  ts.tv_nsec = 1000000000L;
  if (udf_timespec_to_stamp(ts, &stamp)) {
    fprintf(stderr, "udf_timespec_to_stamp() accepted 10^9 nanoseconds\n");
    rc = 1;
  }

  if (psz_saved) {
    setenv("TZ", psz_saved, 1);
    free(psz_saved);
  } else {
#ifdef HAVE_UNSETENV
    unsetenv("TZ");
#endif
  }
  tzset();
  return rc;
}
#endif

int
main(int argc, const char *argv[])
{
  char const *psz_fname = UDF_IMAGE;
  char volume_id[192];         /* 3*64 to account for UTF-8 */
  udf_t* p_udf = NULL;
  udf_dirent_t *p_udf_root = NULL, *p_udf_file = NULL;
  int64_t file_length;
  int rc=0;

  p_udf = udf_open(psz_fname);

  if (NULL == p_udf) {
    fprintf(stderr, "Couldn't open %s as an UDF image\n",
      psz_fname);
    return 1;
  }

  p_udf_root = udf_get_root(p_udf, true, 0);
  if (NULL == p_udf_root) {
    fprintf(stderr, "Could not locate UDF root directory\n");
    rc=2;
    goto exit;
  }

  if ( (udf_get_logical_volume_id(p_udf, volume_id, sizeof(volume_id)) <= 0)
    || (strcmp(EXPECTED_NAME, volume_id) != 0) ) {
    fprintf(stderr, "Unexpected UDF logical volume ID\n");
    rc=3;
    goto exit;
  }
  printf("-- Good! Volume id matches expected UTF-8 data\n");

  p_udf_file = udf_fopen(p_udf_root, EXPECTED_NAME);
  if (!p_udf_file) {
    fprintf(stderr, "Could not locate expected file in UDF image\n");
    rc=4;
    goto exit;
  }
  printf("-- Good! File name matches expected UTF-8 data\n");

  file_length = udf_get_file_length(p_udf_file);
  if (file_length != EXPECTED_LENGTH) {
    fprintf(stderr, "Unexpected UDF file length\n");
    rc=5;
    goto exit;
  }
  printf("-- Good! File length matches expected length\n");

  if (udf_get_modification_time(p_udf_file) != EXPECTED_MTIME) {
    fprintf(stderr, "Unexpected UDF modification time\n");
    rc=8;
    goto exit;
  }
  printf("-- Good! Modification time matches expected time\n");

  if (check_logvol_id_length(16, EXPECTED_NAME)
      || check_logvol_id_length(8, NULL)) {
    fprintf(stderr, "Mishandled an oversized logical volume ID length\n");
    rc=6;
    goto exit;
  }
  printf("-- Good! Oversized logical volume ID length is bounded\n");

  if (check_bad_timestamp()) {
    rc=7;
    goto exit;
  }
  printf("-- Good! Invalid UDF timestamps are rejected\n");

  if (check_bad_fe_timestamp()) {
    rc=9;
    goto exit;
  }
  printf("-- Good! Invalid File Entry timestamp read as -1\n");

#if defined(HAVE_STRUCT_TIMESPEC) && defined(HAVE_SETENV) \
  && defined(HAVE_TZSET)
  if (check_timespec_to_stamp()) {
    rc=10;
    goto exit;
  }
  printf("-- Good! Times convert to UDF timestamps and back\n");
#endif

 exit:
  if (p_udf_root != NULL)
    udf_dirent_free(p_udf_root);
  if (p_udf_file != NULL)
    udf_dirent_free(p_udf_file);
  udf_close(p_udf);
  return rc;
}
