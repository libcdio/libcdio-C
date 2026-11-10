/* -*- C -*-
  Copyright (C) 2008, 2010, 2011, 2012, 2025 Rocky Bernstein <rocky@gnu.org>

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
   Regression test for Nero image driver: lib/driver/image/nrg.c.
*/
#if defined(HAVE_CONFIG_H)
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
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <cdio/cdio.h>
#include <cdio/logging.h>
#include <cdio/cdtext.h>
#include "helper.h"

#ifndef DATA_DIR
#define DATA_DIR "../data"
#endif

#define NUM_FIELDS 2

static void
put_be32(uint8_t *p, uint32_t value)
{
  p[0] = value >> 24;
  p[1] = value >> 16;
  p[2] = value >> 8;
  p[3] = value;
}

static void
put_be64(uint8_t *p, uint64_t value)
{
  put_be32(p, value >> 32);
  put_be32(p + 4, value);
}

/* Write len bytes of buf to a new temporary file whose name is stored
   in psz_tmp, which must hold at least NRG_TMP_SIZE bytes.
   Returns 0 on success. The caller removes the file. */
#define NRG_TMP_SIZE 64
static int
write_tmp_nrg(const uint8_t *buf, size_t len, char *psz_tmp)
{
  FILE *fp;
  int ret = 1;
#ifdef HAVE_MKSTEMP
  int fd;

  snprintf(psz_tmp, NRG_TMP_SIZE, "libcdio-nrg-XXXXXX");
  fd = mkstemp(psz_tmp);
  if (fd < 0)
    return ret;
  fp = fdopen(fd, "wb");
  if (!fp) {
    close(fd);
    remove(psz_tmp);
    return ret;
  }
#else
  const char *psz_name = tmpnam(NULL);

  if (!psz_name)
    return ret;
  snprintf(psz_tmp, NRG_TMP_SIZE, "%s", psz_name);
  fp = fopen(psz_tmp, "wb");
  if (!fp)
    return ret;
#endif

  if (fwrite(buf, 1, len, fp) == len)
    ret = 0;
  fclose(fp);
  if (ret)
    remove(psz_tmp);
  return ret;
}

static int
check_rejected_chunk(uint32_t id, uint32_t length)
{
  uint8_t nrg[20] = {0};
  char psz_tmp[NRG_TMP_SIZE];
  int ret;

  put_be32(&nrg[0], id);
  put_be32(&nrg[4], length);
  put_be32(&nrg[8], 0x4e455235); /* NER5 footer */
  put_be64(&nrg[12], 0);         /* footer starts at file offset 0 */
  if (write_tmp_nrg(nrg, sizeof(nrg), psz_tmp))
    return 1;
  ret = cdio_is_nrg(psz_tmp) ? 1 : 0;
  remove(psz_tmp);
  return ret;
}

/* A CUEX chunk uses a pair of entries per track. An unpaired last entry
   must be ignored, not paired with the bytes that follow the chunk. */
static int
check_odd_cuex(void)
{
  uint8_t nrg[5 * 8 + 8 + 8 + 12] = {0};
  uint8_t *p = nrg;
  char psz_tmp[NRG_TMP_SIZE];
  CdIo_t *p_cdio;
  int ret = 1;

  put_be32(p, 0x43554558);        /* CUEX */
  put_be32(p + 4, 5 * 8);         /* 5 entries */
  p += 8;
  put_be32(p + 4, (uint32_t) -150); /* entry 0: LSN -150 */
  p += 8;
  p += 8;                         /* entry 1: unused */
  p[0] = 0x41; p[1] = 1;          /* entry 2: track 1 starts at LSN 0 */
  put_be32(p + 4, 0);
  p += 8;
  p[0] = 0x41; p[1] = 0xaa;       /* entry 3: track 1 ends at LSN 100 */
  put_be32(p + 4, 100);
  p += 8;
  p[0] = 0x41; p[1] = 2;          /* entry 4: unpaired */
  put_be32(p + 4, 100);
  p += 8;
  put_be32(p, 0x454e4421);        /* END! */
  p += 8;
  put_be32(p, 0x4e455235);        /* NER5 footer */
  put_be64(p + 4, 0);             /* footer starts at file offset 0 */

  if (write_tmp_nrg(nrg, sizeof(nrg), psz_tmp))
    return 1;
  p_cdio = cdio_open_nrg(psz_tmp);
  remove(psz_tmp);
  if (!p_cdio)
    return 1;
  if (cdio_get_num_tracks(p_cdio) == 1)
    ret = 0;
  else
    printf("NRG image with an odd CUEX entry count has %d tracks, not 1\n",
           cdio_get_num_tracks(p_cdio));
  cdio_destroy(p_cdio);
  return ret;
}

static int
check_truncated_dao(void)
{
  return check_rejected_chunk(0x44414f49, 0); /* DAOI */
}

static int
check_short_sinf(void)
{
  return check_rejected_chunk(0x53494e46, 0); /* SINF */
}

static int
check_empty_cues(void)
{
  return check_rejected_chunk(0x43554553, 0); /* CUES */
}

int
main(int argc, const char *argv[])
{
  char psz_nrgfile[500];
  CdIo_t *p_cdio;
  const char *cdtext_check[NUM_FIELDS] = {
    "Richard Stallman",
    "Join us now we have the software"
  };
  const int cdtext_fields[NUM_FIELDS] = {CDTEXT_FIELD_PERFORMER, CDTEXT_FIELD_TITLE};

  cdio_loglevel_default = (argc > 1) ? CDIO_LOG_DEBUG : CDIO_LOG_INFO;
  /* snprintf(psz_nrgfile, sizeof(psz_nrgfile)-1,
	     "%s/%s", DATA_DIR, cue_file[i]);
  */
  if (!cdio_have_driver(DRIVER_NRG)) return(77);

  snprintf(psz_nrgfile, sizeof(psz_nrgfile)-1, "%s/%s",
	   DATA_DIR, "p1.nrg");

  p_cdio = cdio_open_nrg(psz_nrgfile);
  if (!p_cdio) {
    printf("Can't open Nero image file: %s.\n", psz_nrgfile);
    return(1);
  }
  {
    unsigned int i;
    cdtext_t *p_cdtext = cdio_get_cdtext(p_cdio);
    if (!p_cdtext) return(1);
    for (i=0; i<NUM_FIELDS; i++) {
      const char *psz_field = cdtext_get_const(p_cdtext, cdtext_fields[i], 0);
      if (!psz_field)
	return(2);
      if (0 != strncmp(psz_field, cdtext_check[i], strlen(cdtext_check[i]))) {
	printf("CD-Text compare mismatch.\n");
	printf("expected:\n\t'%s'\ngot:\n\t'%s'\n",
	       cdtext_check[i], psz_field);
	return(3);
      }
    }
  }

  check_mmc_supported(p_cdio, 1);
  check_access_mode(p_cdio, "image");
  check_get_arg_source(p_cdio, psz_nrgfile);

  cdio_destroy(p_cdio);

  snprintf(psz_nrgfile, sizeof(psz_nrgfile)-1, "%s/%s",
	   DATA_DIR, "too-many-tracks.nrg");
  p_cdio = cdio_open_nrg(psz_nrgfile);
  if (p_cdio) {
    printf("Accepted NRG image with too many tracks: %s.\n", psz_nrgfile);
    cdio_destroy(p_cdio);
    return(4);
  }

  if (check_truncated_dao() != 0) {
    printf("Accepted or failed to reject truncated DAO metadata.\n");
    return(5);
  }

  if (check_short_sinf() != 0) {
    printf("Accepted or failed to reject short SINF metadata.\n");
    return(6);
  }

  if (check_empty_cues() != 0) {
    printf("Accepted or failed to reject empty CUES metadata.\n");
    return(7);
  }

  if (check_odd_cuex() != 0) {
    printf("Mishandled CUEX metadata with an unpaired entry.\n");
    return(8);
  }

  return 0;
}
