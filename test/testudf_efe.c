/* Tests reading a UDF image whose files are described by Extended File
   Entries (ECMA 167 4/14.17), as UDF 2.00 and later record, instead of
   File Entries (4/14.9). The image is a copy of udf102.iso with every
   File Entry rewritten as an Extended File Entry, and everything read
   from it must match what is read from udf102.iso. */

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

#include <cdio/cdio.h>
#include <cdio/udf.h>

#define EXPECTED_NAME    "FéжΘvrier"

/* Offsets in a File Entry (ECMA 167 4/14.9) and an Extended File Entry
   (4/14.17). Both start with the same fields up to the information
   length, which ends at offset 64. */
#define FE_SIZE          176  /* fixed part of a File Entry */
#define EFE_SIZE         216  /* fixed part of an Extended File Entry */
#define FE_INFO_LEN       56
#define FE_LOGBLKS        64
#define FE_ACCESS_TIME    72
#define FE_ATTR_TIME      96
#define FE_CHECKPOINT    108
#define FE_LEN_EA        168
#define FE_LEN_AD        172
#define EFE_OBJECT_SIZE   64
#define EFE_LOGBLKS       72
#define EFE_ACCESS_TIME   80
#define EFE_CREATE_TIME  104
#define EFE_ATTR_TIME    116
#define EFE_CHECKPOINT   128
#define EFE_EXT_ATTR_ICB 136  /* after 4 reserved bytes */
#define EFE_STREAM_ICB   152
#define EFE_IMP_ID       168
#define EFE_LEN_EA       208
#define EFE_LEN_AD       212

/* What is read about the expected file. */
typedef struct {
  uint64_t length;
  uint16_t links;
  mode_t   mode;
  time_t   access_time, modification_time, attribute_time;
  ssize_t  data_size;
  uint8_t  data[UDF_BLOCKSIZE];
} file_info_t;

static uint32_t
get32(const uint8_t *p)
{
  return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24;
}

static void
put32(uint8_t *p, uint32_t v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}

static void
set_tag_checksum(uint8_t *tag)
{
  uint8_t cksum = 0;
  unsigned int i;

  for (i = 0; i < 16; i++)
    if (i != 4)
      cksum = (uint8_t) (cksum + tag[i]);
  tag[4] = cksum;
}

/* Return true if block is a File Entry with a valid tag. */
static int
is_file_entry(const uint8_t *block)
{
  uint8_t copy[16];

  if (block[0] != (TAGID_FILE_ENTRY & 0xff)
      || block[1] != TAGID_FILE_ENTRY >> 8)
    return 0;
  memcpy(copy, block, sizeof(copy));
  set_tag_checksum(copy);
  return copy[4] == block[4];
}

/* Rewrite the File Entry in block as an Extended File Entry with the
   same contents. The creation time is set to 1 January 1999, which is
   unlike any time in the image, so that it's noticed if it's read in
   place of another one. If ea_len isn't 0, it's recorded as the length
   of the extended attributes instead of the real one. Return 0, or 1
   if the File Entry's extended attributes and allocation descriptors
   don't fit in an Extended File Entry. */
static int
fe_to_efe(uint8_t *block, uint32_t ea_len)
{
  uint8_t fe[UDF_BLOCKSIZE];
  const uint32_t i_ea = get32(block + FE_LEN_EA);
  const uint32_t i_ad = get32(block + FE_LEN_AD);

  if (i_ea > UDF_BLOCKSIZE - EFE_SIZE
      || i_ad > UDF_BLOCKSIZE - EFE_SIZE - i_ea)
    return 1;

  memcpy(fe, block, sizeof(fe));
  memset(block + FE_INFO_LEN + 8, 0, UDF_BLOCKSIZE - FE_INFO_LEN - 8);

  /* Object size, which is the information length with no streams. */
  memcpy(block + EFE_OBJECT_SIZE, fe + FE_INFO_LEN, 8);
  memcpy(block + EFE_LOGBLKS, fe + FE_LOGBLKS, 8);
  /* Access and modification times. */
  memcpy(block + EFE_ACCESS_TIME, fe + FE_ACCESS_TIME, 24);
  block[EFE_CREATE_TIME + 1] = 0x10;  /* type 1, offset 0 */
  block[EFE_CREATE_TIME + 2] = 1999 & 0xff;
  block[EFE_CREATE_TIME + 3] = 1999 >> 8;
  block[EFE_CREATE_TIME + 4] = 1;
  block[EFE_CREATE_TIME + 5] = 1;
  memcpy(block + EFE_ATTR_TIME, fe + FE_ATTR_TIME, 12);
  memcpy(block + EFE_CHECKPOINT, fe + FE_CHECKPOINT, 4);
  /* Extended attribute ICB, implementation identifier and unique ID,
     with no stream directory ICB in between. */
  memcpy(block + EFE_EXT_ATTR_ICB, fe + FE_CHECKPOINT + 4, 16);
  memcpy(block + EFE_IMP_ID, fe + FE_CHECKPOINT + 20, 40);
  put32(block + EFE_LEN_EA, ea_len ? ea_len : i_ea);
  put32(block + EFE_LEN_AD, i_ad);
  memcpy(block + EFE_SIZE, fe + FE_SIZE, i_ea + i_ad);

  block[0] = TAGID_EFE & 0xff;
  block[1] = TAGID_EFE >> 8;
  set_tag_checksum(block);
  return 0;
}

/* Copy UDF_IMAGE to a temporary file with every File Entry rewritten as
   an Extended File Entry. If bad_ea_len isn't 0, the Extended File
   Entry of the last file is given that length of extended attributes.
   If loc_high isn't 0, the top byte of its tag location is set to it,
   as for a descriptor beyond sector 16777215. Return the file's name,
   which stays valid until the next call, or NULL on error. */
static const char *
write_efe_image(uint32_t bad_ea_len, uint8_t loc_high)
{
#ifdef HAVE_MKSTEMP
  static char psz_tmp[sizeof("libcdio-udf-efe-XXXXXX")];
  int fd;
#else
  const char *psz_tmp;
#endif
  FILE *fp;
  uint8_t *image;
  long size, lba, last = -1;
  unsigned int converted = 0;

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

  for (lba = 0; (lba + 1) * UDF_BLOCKSIZE <= size; lba++) {
    uint8_t *block = image + lba * UDF_BLOCKSIZE;

    if (is_file_entry(block)) {
      if (fe_to_efe(block, 0)) {
        fprintf(stderr, "File Entry at %ld doesn't fit in an Extended "
                "File Entry\n", lba);
        free(image);
        return NULL;
      }
      last = lba;
      converted++;
    }
  }
  /* udf102.iso has a File Entry for the root directory and one for the
     file. */
  if (converted != 2) {
    fprintf(stderr, "%u File Entries in %s, expected 2\n", converted,
            UDF_IMAGE);
    free(image);
    return NULL;
  }
  if (bad_ea_len)
    put32(image + last * UDF_BLOCKSIZE + EFE_LEN_EA, bad_ea_len);
  if (loc_high)
    image[last * UDF_BLOCKSIZE + 15] = loc_high;
  set_tag_checksum(image + last * UDF_BLOCKSIZE);

#ifdef HAVE_MKSTEMP
  strcpy(psz_tmp, "libcdio-udf-efe-XXXXXX");
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

/* Open psz_image, list its root directory and read what is known about
   EXPECTED_NAME into p_info. Return 0 on success. */
static int
read_file_info(const char *psz_image, file_info_t *p_info)
{
  udf_t *p_udf;
  udf_dirent_t *p_udf_root, *p_udf_dirent, *p_udf_file;
  unsigned int i_found = 0;
  int rc = 1;

  memset(p_info, 0, sizeof(*p_info));
  p_udf = udf_open(psz_image);
  if (!p_udf) {
    fprintf(stderr, "Couldn't open %s as a UDF image\n", psz_image);
    return 1;
  }
  p_udf_root = udf_get_root(p_udf, true, 0);
  if (!p_udf_root) {
    fprintf(stderr, "Could not locate the UDF root directory\n");
    goto exit;
  }

  /* The root directory must list the file. udf_readdir() frees the
     copy of the root at the end. */
  p_udf_dirent = udf_fopen(p_udf_root, "/");
  while (p_udf_dirent && (p_udf_dirent = udf_readdir(p_udf_dirent)))
    if (strcmp(udf_get_filename(p_udf_dirent), EXPECTED_NAME) == 0)
      i_found++;
  if (i_found != 1) {
    fprintf(stderr, "File listed %u times in the root directory\n",
            i_found);
    goto exit;
  }

  p_udf_file = udf_fopen(p_udf_root, EXPECTED_NAME);
  if (!p_udf_file) {
    fprintf(stderr, "Could not locate the file in the UDF image\n");
    goto exit;
  }
  p_info->length = udf_get_file_length(p_udf_file);
  p_info->links = udf_get_link_count(p_udf_file);
  p_info->mode = udf_get_posix_filemode(p_udf_file);
  p_info->access_time = udf_get_access_time(p_udf_file);
  p_info->modification_time = udf_get_modification_time(p_udf_file);
  p_info->attribute_time = udf_get_attribute_time(p_udf_file);
  p_info->data_size = udf_read_block(p_udf_file, p_info->data, 1);
  udf_dirent_free(p_udf_file);
  rc = 0;

 exit:
  if (p_udf_root)
    udf_dirent_free(p_udf_root);
  udf_close(p_udf);
  return rc;
}

int
main(void)
{
  file_info_t fe_info, efe_info;
  const char *psz_tmp;
  udf_t *p_udf;
  int rc;

  if (read_file_info(UDF_IMAGE, &fe_info))
    return 1;
  if (fe_info.length == 0 || fe_info.data_size <= 0
      || fe_info.modification_time == (time_t) -1) {
    fprintf(stderr, "Unexpected file in %s\n", UDF_IMAGE);
    return 1;
  }

  psz_tmp = write_efe_image(0, 0);
  if (!psz_tmp)
    return 2;
  rc = read_file_info(psz_tmp, &efe_info);
  remove(psz_tmp);
  if (rc)
    return 3;

  if (efe_info.length != fe_info.length
      || efe_info.links != fe_info.links
      || efe_info.mode != fe_info.mode) {
    fprintf(stderr, "Extended File Entry: length %llu, %u links, mode "
            "%o; File Entry: length %llu, %u links, mode %o\n",
            (unsigned long long) efe_info.length, efe_info.links,
            (unsigned int) efe_info.mode,
            (unsigned long long) fe_info.length, fe_info.links,
            (unsigned int) fe_info.mode);
    return 4;
  }
  printf("-- Good! Length, link count and mode match\n");

  if (efe_info.access_time != fe_info.access_time
      || efe_info.modification_time != fe_info.modification_time
      || efe_info.attribute_time != fe_info.attribute_time) {
    fprintf(stderr, "Extended File Entry times %ld %ld %ld; File Entry "
            "times %ld %ld %ld\n", (long) efe_info.access_time,
            (long) efe_info.modification_time,
            (long) efe_info.attribute_time, (long) fe_info.access_time,
            (long) fe_info.modification_time,
            (long) fe_info.attribute_time);
    return 5;
  }
  printf("-- Good! Access, modification and attribute times match\n");

  if (efe_info.data_size != fe_info.data_size
      || memcmp(efe_info.data, fe_info.data, fe_info.data_size) != 0) {
    fprintf(stderr, "File data read through the Extended File Entry "
            "differs\n");
    return 6;
  }
  printf("-- Good! File data matches\n");

  /* The tag checksum covers all of the tag location (ECMA 167 3/7.2.3),
     so a descriptor beyond sector 16777215, whose location has a top
     byte that isn't 0, must still be accepted. */
  psz_tmp = write_efe_image(0, 0x01);
  if (!psz_tmp)
    return 11;
  rc = read_file_info(psz_tmp, &efe_info);
  remove(psz_tmp);
  if (rc)
    return 12;
  if (efe_info.modification_time != fe_info.modification_time
      || efe_info.data_size != fe_info.data_size
      || memcmp(efe_info.data, fe_info.data, fe_info.data_size) != 0) {
    fprintf(stderr, "Extended File Entry with a tag location beyond "
            "sector 16777215 misread\n");
    return 13;
  }
  printf("-- Good! Tag checksum covers the whole tag location\n");

  /* An Extended File Entry whose extended attributes don't fit in the
     block must be rejected, while the root directory is still read. */
  psz_tmp = write_efe_image(UDF_BLOCKSIZE, 0);
  if (!psz_tmp)
    return 7;
  rc = 0;
  p_udf = udf_open(psz_tmp);
  if (!p_udf) {
    rc = 8;
  } else {
    udf_dirent_t *p_udf_root = udf_get_root(p_udf, true, 0);

    if (!p_udf_root) {
      fprintf(stderr, "Root directory not read next to a bad Extended "
              "File Entry\n");
      rc = 9;
    } else {
      udf_dirent_t *p_udf_file = udf_fopen(p_udf_root, EXPECTED_NAME);

      if (p_udf_file) {
        fprintf(stderr, "Extended File Entry with too many extended "
                "attributes accepted\n");
        udf_dirent_free(p_udf_file);
        rc = 10;
      }
      udf_dirent_free(p_udf_root);
    }
    udf_close(p_udf);
  }
  remove(psz_tmp);
  if (rc)
    return rc;
  printf("-- Good! Oversized Extended File Entry is rejected\n");
  return 0;
}
