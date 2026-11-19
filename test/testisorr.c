/*
  Copyright (C) 2013 Rocky Bernstein
  <rocky@gnu.org>

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

/* Tests reading ISO 9660 info from an ISO 9660 image.  */

#ifndef DATA_DIR
#define DATA_DIR "./data"
#endif
/* Set up a CD-DA image to test on which is in the libcdio distribution. */
#define ISO9660_IMAGE_PATH DATA_DIR "/"
#define ISO9660_IMAGE    ISO9660_IMAGE_PATH "copying.iso"
#define ISO9660_IMAGE_RR ISO9660_IMAGE_PATH "copying-rr.iso"

#define SKIP_TEST_RC 77

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
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <cdio/cdio.h>
#include <cdio/iso9660.h>

static void
put_733(uint8_t *p, uint32_t value)
{
  p[0] = value;
  p[1] = value >> 8;
  p[2] = value >> 16;
  p[3] = value >> 24;
  p[4] = value >> 24;
  p[5] = value >> 16;
  p[6] = value >> 8;
  p[7] = value;
}

static unsigned
add_cycle_record(uint8_t *dir, unsigned offset, char name, uint32_t extent,
                 int child_link)
{
  const unsigned length = child_link ? 46 : 34;
  uint8_t *record = dir + offset;

  memset(record, 0, length);
  record[0] = length;
  put_733(record + 2, extent);
  put_733(record + 10, ISO_BLOCKSIZE);
  record[25] = child_link ? 0 : ISO_DIRECTORY;
  record[32] = 1;
  record[33] = name;
  if (child_link) {
    record[34] = 'C';
    record[35] = 'L';
    record[36] = 12;
    record[37] = 1;
    put_733(record + 38, 888);
  }
  return offset + length;
}

static int
check_rock_ridge_cycle(void)
{
  uint8_t pvd[ISO_BLOCKSIZE] = {0};
  uint8_t dir[ISO_BLOCKSIZE] = {0};
  unsigned offset = 0;
  iso9660_t *p_iso = NULL;
  CdioISO9660FileList_t *entries = NULL;
  FILE *fp = NULL;
  int ret = 1;
#ifdef HAVE_MKSTEMP
  char psz_tmp[] = "libcdio-rock-cycle-XXXXXX";
  int fd = mkstemp(psz_tmp);
  if (fd < 0)
    return 1;
  fp = fdopen(fd, "wb");
#else
  char *psz_tmp = tmpnam(NULL);
  if (psz_tmp)
    fp = fopen(psz_tmp, "wb");
#endif

  if (!fp)
    goto out;
  if (fseek(fp, 21 * ISO_BLOCKSIZE - 1, SEEK_SET) != 0
      || fputc(0, fp) == EOF)
    goto out;

  pvd[0] = 1;
  memcpy(&pvd[1], "CD001", 5);
  pvd[6] = 1;
  put_733(&pvd[80], 21);
  pvd[128] = 0;
  pvd[129] = 8;
  pvd[156] = 34;
  put_733(&pvd[158], 20);
  put_733(&pvd[166], ISO_BLOCKSIZE);
  pvd[181] = 2;
  pvd[188] = 1;

  offset = add_cycle_record(dir, offset, '\0', 20, 0);
  offset = add_cycle_record(dir, offset, '\1', 20, 0);
  offset = add_cycle_record(dir, offset, 'A', 20, 0);
  add_cycle_record(dir, offset, 'X', 999, 1);

  if (fseek(fp, 16 * ISO_BLOCKSIZE, SEEK_SET) != 0
      || fwrite(pvd, 1, sizeof(pvd), fp) != sizeof(pvd))
    goto out;
  memset(pvd, 0, sizeof(pvd));
  pvd[0] = 255;
  memcpy(&pvd[1], "CD001", 5);
  pvd[6] = 1;
  if (fwrite(pvd, 1, sizeof(pvd), fp) != sizeof(pvd)
      || fseek(fp, 20 * ISO_BLOCKSIZE, SEEK_SET) != 0
      || fwrite(dir, 1, sizeof(dir), fp) != sizeof(dir)
      || fclose(fp) != 0)
    goto out;
  fp = NULL;

  p_iso = iso9660_open_ext(psz_tmp, ISO_EXTENSION_ALL);
  if (!p_iso)
    goto out;
  entries = iso9660_ifs_readdir(p_iso, "/");
  if (!entries)
    goto out;
  iso9660_filelist_free(entries);
  entries = NULL;
  ret = 0;

out:
  if (entries)
    iso9660_filelist_free(entries);
  if (p_iso)
    iso9660_close(p_iso);
  if (fp)
    fclose(fp);
  remove(psz_tmp);
  return ret;
}

static unsigned
add_directory_record(uint8_t *dir, unsigned offset, uint32_t extent,
                     uint8_t flags, uint8_t name)
{
  uint8_t *record = dir + offset;

  memset(record, 0, 34);
  record[0] = 34;
  put_733(record + 2, extent);
  put_733(record + 10, ISO_BLOCKSIZE);
  record[25] = flags;
  record[28] = 1;
  record[32] = 1;
  record[33] = name;
  return offset + 34;
}

static int
check_excessive_path_depth(void)
{
  const unsigned depth = 300;
  uint8_t pvd[ISO_BLOCKSIZE] = {0};
  uint8_t dir[ISO_BLOCKSIZE] = {0};
  char path[depth * 2];
  unsigned i;
  unsigned offset;
  iso9660_t *p_iso = NULL;
  iso9660_stat_t *p_stat = NULL;
  FILE *fp = NULL;
  int ret = 1;
#ifdef HAVE_MKSTEMP
  char psz_tmp[] = "libcdio-iso-depth-XXXXXX";
  int fd = mkstemp(psz_tmp);
  if (fd < 0)
    return 1;
  fp = fdopen(fd, "wb");
#else
  char *psz_tmp = tmpnam(NULL);
  if (psz_tmp)
    fp = fopen(psz_tmp, "wb");
#endif

  if (!fp)
    goto out;
  if (fseek(fp, (22 + depth) * ISO_BLOCKSIZE - 1, SEEK_SET) != 0
      || fputc(0, fp) == EOF)
    goto out;

  pvd[0] = 1;
  memcpy(&pvd[1], "CD001", 5);
  pvd[6] = 1;
  put_733(&pvd[80], 22 + depth);
  pvd[128] = 0;
  pvd[129] = 8;
  pvd[156] = 34;
  put_733(&pvd[158], 20);
  put_733(&pvd[166], ISO_BLOCKSIZE);
  pvd[181] = ISO_DIRECTORY;
  pvd[188] = 1;
  pvd[189] = 0;

  if (fseek(fp, 16 * ISO_BLOCKSIZE, SEEK_SET) != 0
      || fwrite(pvd, 1, sizeof(pvd), fp) != sizeof(pvd))
    goto out;
  memset(pvd, 0, sizeof(pvd));
  pvd[0] = 255;
  memcpy(&pvd[1], "CD001", 5);
  pvd[6] = 1;
  if (fwrite(pvd, 1, sizeof(pvd), fp) != sizeof(pvd))
    goto out;

  for (i = 0; i <= depth; i++) {
    offset = 0;
    memset(dir, 0, sizeof(dir));
    offset = add_directory_record(dir, offset, 20 + i, ISO_DIRECTORY, 0);
    offset = add_directory_record(dir, offset, i ? 19 + i : 20,
                                  ISO_DIRECTORY, 1);
    add_directory_record(dir, offset, 21 + i, ISO_DIRECTORY, 'D');
    if (fseek(fp, (20 + i) * ISO_BLOCKSIZE, SEEK_SET) != 0
        || fwrite(dir, 1, sizeof(dir), fp) != sizeof(dir))
      goto out;
  }
  if (fclose(fp) != 0)
    goto out;
  fp = NULL;

  for (i = 0; i < depth; i++) {
    path[2 * i] = 'D';
    path[2 * i + 1] = '/';
  }
  path[2 * depth - 1] = '\0';

  p_iso = iso9660_open_ext(psz_tmp, ISO_EXTENSION_NONE);
  if (!p_iso)
    goto out;
  p_stat = iso9660_ifs_stat(p_iso, path);
  if (p_stat)
    iso9660_stat_free(p_stat);
  else
    ret = 0;

out:
  if (p_iso)
    iso9660_close(p_iso);
  if (fp)
    fclose(fp);
  remove(psz_tmp);
  return ret;
}

static int
check_malformed_record(void)
{
  const char *source_name = DATA_DIR "/bad-dir.iso";
  char temp_name[] = "libcdio-malformed-rr-XXXXXX";
  FILE *source = NULL;
  FILE *temp = NULL;
  iso9660_t *p_iso = NULL;
  uint8_t buffer[4096];
  size_t nread;
  int fd;
  int result = 1;

  source = fopen(source_name, "rb");
  fd = mkstemp(temp_name);
  if (!source || fd < 0) {
    if (fd >= 0)
      close(fd);
    goto out;
  }
  temp = fdopen(fd, "wb+");
  if (!temp) {
    close(fd);
    goto out;
  }

  while ((nread = fread(buffer, 1, sizeof(buffer), source)) != 0) {
    if (fwrite(buffer, 1, nread, temp) != nread)
      goto out;
  }
  if (fseek(temp, 23 * ISO_BLOCKSIZE, SEEK_SET) != 0
      || fputc(1, temp) == EOF)
    goto out;
  if (fclose(temp) != 0) {
    temp = NULL;
    goto out;
  }
  temp = NULL;

  p_iso = iso9660_open_ext(temp_name, ISO_EXTENSION_ROCK_RIDGE);
  if (!p_iso || iso9660_have_rr(p_iso, 0) != dunno)
    goto out;
  result = 0;

out:
  if (p_iso)
    iso9660_close(p_iso);
  if (temp)
    fclose(temp);
  if (source)
    fclose(source);
  remove(temp_name);
  return result;
}

/* Opens a new temporary file for writing. psz_tmp receives its name. */
static FILE *
open_temp(char *psz_tmp, const char *psz_template)
{
#ifdef HAVE_MKSTEMP
  int fd;
  FILE *fp;

  strcpy(psz_tmp, psz_template);
  fd = mkstemp(psz_tmp);
  if (fd < 0)
    return NULL;
  fp = fdopen(fd, "wb");
  if (!fp)
    close(fd);
  return fp;
#else
  (void) psz_template;
  if (!tmpnam(psz_tmp))
    return NULL;
  return fopen(psz_tmp, "wb");
#endif
}

#define TEST_IMAGE_BLOCKS 40
#define TEST_ROOT_LSN     20

/* Writes a 40-block image whose root directory, at block 20, is dir. */
static int
write_test_image(char *psz_tmp, const char *psz_template,
                 const uint8_t dir[ISO_BLOCKSIZE])
{
  uint8_t pvd[ISO_BLOCKSIZE] = {0};
  FILE *fp = open_temp(psz_tmp, psz_template);

  if (!fp)
    return 1;

  pvd[0] = 1;
  memcpy(&pvd[1], "CD001", 5);
  pvd[6] = 1;
  put_733(&pvd[80], TEST_IMAGE_BLOCKS);
  pvd[128] = 0;
  pvd[129] = 8;
  pvd[156] = 34;
  put_733(&pvd[158], TEST_ROOT_LSN);
  put_733(&pvd[166], ISO_BLOCKSIZE);
  pvd[181] = ISO_DIRECTORY;
  pvd[188] = 1;

  if (fseek(fp, TEST_IMAGE_BLOCKS * ISO_BLOCKSIZE - 1, SEEK_SET) != 0
      || fputc(0, fp) == EOF
      || fseek(fp, 16 * ISO_BLOCKSIZE, SEEK_SET) != 0
      || fwrite(pvd, 1, sizeof(pvd), fp) != sizeof(pvd))
    goto fail;
  memset(pvd, 0, sizeof(pvd));
  pvd[0] = 255;
  memcpy(&pvd[1], "CD001", 5);
  pvd[6] = 1;
  if (fwrite(pvd, 1, sizeof(pvd), fp) != sizeof(pvd)
      || fseek(fp, TEST_ROOT_LSN * ISO_BLOCKSIZE, SEEK_SET) != 0
      || fwrite(dir, 1, ISO_BLOCKSIZE, fp) != ISO_BLOCKSIZE)
    goto fail;
  return fclose(fp) != 0;

fail:
  fclose(fp);
  return 1;
}

/* Adds a directory record with an optional System Use area. */
static unsigned
add_su_record(uint8_t *dir, unsigned offset, uint32_t extent, uint32_t size,
              uint8_t flags, const char *name, unsigned name_len,
              const uint8_t *su, unsigned su_len)
{
  uint8_t *record = dir + offset;
  unsigned length = 33 + name_len;

  if (length % 2)
    length++;
  memset(record, 0, length + su_len);
  record[0] = length + su_len;
  put_733(record + 2, extent);
  put_733(record + 10, size);
  record[25] = flags;
  record[28] = 1;
  record[32] = name_len;
  memcpy(record + 33, name, name_len);
  if (su_len)
    memcpy(record + length, su, su_len);
  return offset + length + su_len;
}

/* Adds the "." and ".." records of the root directory. */
static unsigned
add_root_dots(uint8_t *dir)
{
  unsigned offset = add_su_record(dir, 0, TEST_ROOT_LSN, ISO_BLOCKSIZE,
                                  ISO_DIRECTORY, "\0", 1, NULL, 0);
  return add_su_record(dir, offset, TEST_ROOT_LSN, ISO_BLOCKSIZE,
                       ISO_DIRECTORY, "\1", 1, NULL, 0);
}

/* A Rock Ridge SL entry for the one-component link "x". */
static const uint8_t sl_x[] = { 'S', 'L', 8, 1, 0, 0, 1, 'x' };

/* The stat that iso9660_ifs_find_lsn() returns owns its symlink. */
static int
check_find_lsn_symlink(void)
{
  uint8_t dir[ISO_BLOCKSIZE] = {0};
  char psz_tmp[64 + L_tmpnam];
  iso9660_t *p_iso = NULL;
  iso9660_stat_t *p_stat = NULL;
  int ret = 1;

  add_su_record(dir, add_root_dots(dir), 31, 0, 0, "L;1", 3,
                sl_x, sizeof(sl_x));
  if (write_test_image(psz_tmp, "libcdio-rr-lsn-XXXXXX", dir) != 0)
    goto out;

  p_iso = iso9660_open_ext(psz_tmp, ISO_EXTENSION_ALL);
  if (!p_iso)
    goto out;
  p_stat = iso9660_ifs_find_lsn(p_iso, 31);
  if (p_stat && p_stat->rr.psz_symlink
      && 0 == strcmp(p_stat->rr.psz_symlink, "x"))
    ret = 0;

out:
  if (p_stat)
    iso9660_stat_free(p_stat);
  if (p_iso)
    iso9660_close(p_iso);
  remove(psz_tmp);
  return ret;
}

/* A Rock Ridge CL entry takes the symlink of the entry it points to. */
static int
check_child_link_symlink(void)
{
  uint8_t dir[ISO_BLOCKSIZE] = {0};
  uint8_t cl[12] = { 'C', 'L', 12, 1 };
  char psz_tmp[64 + L_tmpnam];
  iso9660_t *p_iso = NULL;
  CdioISO9660FileList_t *entries = NULL;
  CdioListNode_t *node;
  unsigned offset;
  int links = 0;
  int ret = 1;

  put_733(cl + 4, 31);
  offset = add_su_record(dir, add_root_dots(dir), 30, 0, 0, "C;1", 3,
                         cl, sizeof(cl));
  add_su_record(dir, offset, 31, 0, 0, "L;1", 3, sl_x, sizeof(sl_x));
  if (write_test_image(psz_tmp, "libcdio-rr-cl-XXXXXX", dir) != 0)
    goto out;

  p_iso = iso9660_open_ext(psz_tmp, ISO_EXTENSION_ALL);
  if (!p_iso)
    goto out;
  entries = iso9660_ifs_readdir(p_iso, "/");
  if (!entries)
    goto out;
  _CDIO_LIST_FOREACH (node, entries) {
    iso9660_stat_t *p_stat = _cdio_list_node_data(node);
    if (p_stat->rr.psz_symlink && 0 == strcmp(p_stat->rr.psz_symlink, "x"))
      links++;
  }
  /* Both the link and the entry that points to it. */
  if (links == 2)
    ret = 0;

out:
  if (entries)
    iso9660_filelist_free(entries);
  if (p_iso)
    iso9660_close(p_iso);
  remove(psz_tmp);
  return ret;
}

/* An SL entry whose only component has an unknown flag stores nothing. */
static int
check_unknown_symlink_component(void)
{
  static const uint8_t sl[] = { 'S', 'L', 7, 1, 1, 0x10, 0 };
  uint8_t dir[ISO_BLOCKSIZE] = {0};
  char psz_tmp[64 + L_tmpnam];
  iso9660_t *p_iso = NULL;
  CdioISO9660FileList_t *entries = NULL;
  int ret = 1;

  add_su_record(dir, add_root_dots(dir), 30, 0, 0, "A;1", 3, sl, sizeof(sl));
  if (write_test_image(psz_tmp, "libcdio-rr-sl-XXXXXX", dir) != 0)
    goto out;

  p_iso = iso9660_open_ext(psz_tmp, ISO_EXTENSION_ALL);
  if (!p_iso)
    goto out;
  entries = iso9660_ifs_readdir(p_iso, "/");
  if (entries)
    ret = 0;

out:
  if (entries)
    iso9660_filelist_free(entries);
  if (p_iso)
    iso9660_close(p_iso);
  remove(psz_tmp);
  return ret;
}

/* Looking for an LSN goes past a directory that can't be read. */
static int
check_find_lsn_unreadable_directory(void)
{
  uint8_t dir[ISO_BLOCKSIZE] = {0};
  char psz_tmp[64 + L_tmpnam];
  iso9660_t *p_iso = NULL;
  iso9660_stat_t *p_stat = NULL;
  unsigned offset;
  int ret = 1;

  /* A directory of length 0, then a file after it. */
  offset = add_su_record(dir, add_root_dots(dir), 19, 0, ISO_DIRECTORY,
                         "D", 1, NULL, 0);
  add_su_record(dir, offset, 32, 0, 0, "F;1", 3, NULL, 0);
  if (write_test_image(psz_tmp, "libcdio-rr-dir-XXXXXX", dir) != 0)
    goto out;

  p_iso = iso9660_open_ext(psz_tmp, ISO_EXTENSION_ALL);
  if (!p_iso)
    goto out;
  /* Not on the image: every directory is searched. */
  p_stat = iso9660_ifs_find_lsn(p_iso, 35);
  if (p_stat)
    goto out;
  p_stat = iso9660_ifs_find_lsn(p_iso, 32);
  if (p_stat)
    ret = 0;

out:
  if (p_stat)
    iso9660_stat_free(p_stat);
  if (p_iso)
    iso9660_close(p_iso);
  remove(psz_tmp);
  return ret;
}

/* Records shorter than their fixed part at the end of the last block of
   a directory aren't read past the block. Only a memory checker sees
   the difference. */
static int
check_short_record_at_block_end(void)
{
  uint8_t dir[ISO_BLOCKSIZE] = {0};
  char psz_tmp[64 + L_tmpnam];
  iso9660_t *p_iso = NULL;
  CdioISO9660FileList_t *entries = NULL;
  unsigned offset = add_root_dots(dir);
  int ret = 1;

  memset(dir + offset, 1, ISO_BLOCKSIZE - offset);
  if (write_test_image(psz_tmp, "libcdio-rr-short-XXXXXX", dir) != 0)
    goto out;

  p_iso = iso9660_open_ext(psz_tmp, ISO_EXTENSION_ALL);
  if (!p_iso)
    goto out;
  entries = iso9660_ifs_readdir(p_iso, "/");
  ret = 0;

out:
  if (entries)
    iso9660_filelist_free(entries);
  if (p_iso)
    iso9660_close(p_iso);
  remove(psz_tmp);
  return ret;
}

/* The fuzzy search for a PVD doesn't read past a frame that holds no
   NUL. Only a memory checker sees the difference. */
static int
check_fuzzy_frame_without_nul(void)
{
  uint8_t frame[CDIO_CD_FRAMESIZE_RAW];
  char psz_tmp[64 + L_tmpnam];
  iso9660_t *p_iso = NULL;
  FILE *fp = open_temp(psz_tmp, "libcdio-rr-fuzzy-XXXXXX");
  int i;
  int ret = 1;

  if (!fp)
    return 1;
  memset(frame, 'C', sizeof(frame));
  for (i = 0; i < 24; i++)
    if (fwrite(frame, 1, sizeof(frame), fp) != sizeof(frame)) {
      fclose(fp);
      goto out;
    }
  if (fclose(fp) != 0)
    goto out;

  p_iso = iso9660_open_fuzzy_ext(psz_tmp, ISO_EXTENSION_ALL, 20);
  if (!p_iso)
    ret = 0;

out:
  if (p_iso)
    iso9660_close(p_iso);
  remove(psz_tmp);
  return ret;
}

int
main(int argc, const char *argv[])
{
  char const *psz_fname;
  iso9660_t *p_iso;

  psz_fname = ISO9660_IMAGE;
  p_iso = iso9660_open_ext(psz_fname, ISO_EXTENSION_ROCK_RIDGE);

  if (NULL == p_iso) {
    fprintf(stderr, "Sorry, couldn't open %s as an ISO-9660 image\n",
	    psz_fname);
    return 1;
  }

  if (iso9660_have_rr(p_iso, 0) != nope) {
    fprintf(stderr, "-- Should not find Rock Ridge for %s\n", psz_fname);
    return 2;
  } else {
    printf("-- Good! Did not find Rock Ridge in %s\n", psz_fname);
  }

  iso9660_close(p_iso);

  psz_fname = ISO9660_IMAGE_RR;
  p_iso = iso9660_open_ext(psz_fname, ISO_EXTENSION_ROCK_RIDGE);

  if (NULL == p_iso) {
    fprintf(stderr, "Sorry, couldn't open %s as an ISO-9660 image\n",
	    psz_fname);
    return 3;
  }

  if (iso9660_have_rr(p_iso, 0) == yep) {
      printf("-- Good! found Rock Ridge in %s\n", psz_fname);
  } else {
    fprintf(stderr, "-- Should have found Rock Ridge for %s\n", psz_fname);
    return 2;
  }

  if (iso9660_have_rr(p_iso, 3) == yep) {
      printf("-- Good! Found Rock Ridge again in %s\n", psz_fname);
  } else {
    fprintf(stderr, "-- Should have found Rock Ridge for %s\n", psz_fname);
    return 3;
  }

  iso9660_close(p_iso);

  if (check_rock_ridge_cycle() != 0) {
    fprintf(stderr, "Rock Ridge deep-directory cycle was not rejected\n");
    return 4;
  }

  if (check_excessive_path_depth() != 0) {
    fprintf(stderr, "Excessive ISO path depth was not rejected\n");
    return 5;
  }

  if (check_malformed_record() != 0) {
    fprintf(stderr, "Malformed directory record was not rejected\n");
    return 6;
  }

  if (check_find_lsn_symlink() != 0) {
    fprintf(stderr, "Symlink of a stat found by LSN is wrong\n");
    return 7;
  }

  if (check_child_link_symlink() != 0) {
    fprintf(stderr, "Symlink of a Rock Ridge child link is wrong\n");
    return 8;
  }

  if (check_unknown_symlink_component() != 0) {
    fprintf(stderr, "Unknown symlink component was not handled\n");
    return 9;
  }

  if (check_find_lsn_unreadable_directory() != 0) {
    fprintf(stderr, "Unreadable directory stopped the LSN search\n");
    return 10;
  }

  if (check_short_record_at_block_end() != 0) {
    fprintf(stderr, "Short records at the end of a block were not read\n");
    return 11;
  }

  if (check_fuzzy_frame_without_nul() != 0) {
    fprintf(stderr, "Fuzzy open found a PVD in a frame without one\n");
    return 12;
  }

  return 0;
}
