/* Check that UDF extent walking rejects a descriptor beyond the allocation
   descriptor area. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#define __CDIO_CONFIG_H__ 1
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/udf/udf_private.h"

#ifndef DATA_DIR
#define DATA_DIR "./data"
#endif

static void
set_tag(udf_tag_t *tag, uint16_t id)
{
  uint8_t *bytes = (uint8_t *)tag;
  uint8_t checksum = 0;
  unsigned int i;

  memset(tag, 0, sizeof(*tag));
  tag->id = id;
  for (i = 0; i < 16; i++)
    if (i != 4)
      checksum = (uint8_t)(checksum + bytes[i]);
  tag->cksum = checksum;
}

static int
check_zero_length_file_id(udf_t *udf)
{
  udf_dirent_t *dirent = calloc(1, sizeof(*dirent));
  udf_fileid_desc_t *first;
  udf_fileid_desc_t *target;

  if (!dirent)
    return 1;
  dirent->p_udf = udf;
  dirent->i_loc = 0;
  dirent->i_loc_end = 0;
  dirent->dir_left = 80;
  dirent->sector = calloc(1, UDF_BLOCKSIZE);
  if (!dirent->sector) {
    free(dirent);
    return 1;
  }

  first = (udf_fileid_desc_t *)(dirent->sector + 1968);
  set_tag(&first->tag, TAGID_FID);
  first->i_file_id = 1;
  first->u.file_id.data[0] = 8;

  target = (udf_fileid_desc_t *)(dirent->sector + 2008);
  set_tag(&target->tag, TAGID_FID);
  target->i_file_id = 0;
  target->icb.loc.lba = 1;
  target->u.padding.data[0] = 8;
  dirent->fid = first;

  dirent = udf_readdir(dirent);
  if (!dirent || !dirent->psz_name || dirent->psz_name[0] != '\0') {
    if (dirent)
      udf_dirent_free(dirent);
    return 1;
  }
  udf_dirent_free(dirent);
  return 0;
}

int
main(void)
{
  struct {
    udf_dirent_t dirent;
    union {
      udf_short_ad_t short_ad;
      udf_long_ad_t long_ad;
    } next_ad;
  } *test = calloc(1, sizeof(*test));
  udf_t udf = {0};
  uint8_t buffer[UDF_BLOCKSIZE];
  udf_short_ad_t *ad;
  ssize_t nread;
  int ret = 1;

  if (!test)
    return ret;

  udf.b_stream = true;
  udf.stream = cdio_stdio_new(DATA_DIR "/cdda.bin");
  if (!udf.stream)
    goto out;

  if (check_zero_length_file_id(&udf) != 0)
    goto close;
  udf.i_position = 0;

  test->dirent.p_udf = &udf;
  test->dirent.fe.icb_tag.strat_type = ICBTAG_STRATEGY_TYPE_4;
  test->dirent.fe.icb_tag.flags = ICBTAG_FLAG_AD_SHORT;
  test->dirent.fe.info_len = 4096;
  test->dirent.fe.u_extended_attr = 1864;
  test->dirent.fe.u_alloc_descs = sizeof(udf_short_ad_t);

  ad = (udf_short_ad_t *)&test->dirent.fe.u.pad_to_one_block[1864];
  ad->len = UDF_BLOCKSIZE;
  ad->pos = 0;

  /* This descriptor is just beyond the file entry. The unfixed extent walk
     would read it as a second extent. */
  test->next_ad.short_ad.len = 2 * UDF_BLOCKSIZE;
  test->next_ad.short_ad.pos = 0;

  nread = udf_read_block(&test->dirent, buffer, 1);
  if (nread != UDF_BLOCKSIZE)
    goto close;

  nread = udf_read_block(&test->dirent, buffer, 1);
  if (nread != DRIVER_OP_ERROR)
    goto close;

  memset(&test->dirent.fe, 0, sizeof(test->dirent.fe));
  udf.i_position = 0;
  test->dirent.fe.icb_tag.strat_type = ICBTAG_STRATEGY_TYPE_4;
  test->dirent.fe.icb_tag.flags = ICBTAG_FLAG_AD_LONG;
  test->dirent.fe.info_len = 4096;
  test->dirent.fe.u_extended_attr = 1856;
  test->dirent.fe.u_alloc_descs = sizeof(udf_long_ad_t);

  {
    udf_long_ad_t *long_ad =
      (udf_long_ad_t *)&test->dirent.fe.u.pad_to_one_block[1856];
    long_ad->len = UDF_BLOCKSIZE;
    long_ad->loc.lba = 0;
  }
  test->next_ad.long_ad.len = 2 * UDF_BLOCKSIZE;
  test->next_ad.long_ad.loc.lba = 0;

  nread = udf_read_block(&test->dirent, buffer, 1);
  if (nread != UDF_BLOCKSIZE)
    goto close;

  nread = udf_read_block(&test->dirent, buffer, 1);
  if (nread != DRIVER_OP_ERROR)
    goto close;

  memset(&test->dirent.fe, 0, sizeof(test->dirent.fe));
  udf.i_position = 0;
  test->dirent.fe.icb_tag.strat_type = ICBTAG_STRATEGY_TYPE_4;
  test->dirent.fe.icb_tag.flags = ICBTAG_FLAG_AD_SHORT;
  test->dirent.fe.info_len = UDF_BLOCKSIZE;
  test->dirent.fe.u_extended_attr = UDF_BLOCKSIZE;
  test->dirent.fe.u_alloc_descs = sizeof(udf_short_ad_t);

  nread = udf_read_block(&test->dirent, buffer, 1);
  if (nread != DRIVER_OP_ERROR)
    goto close;

  ret = 0;
close:
  cdio_stdio_destroy(udf.stream);
out:
  free(test);
  return ret;
}
