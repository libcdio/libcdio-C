/*
  Copyright (C) 2016 Rocky Bernstein <rocky@gnu.org>

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
  Regression test for malformed UCS-2BE names in UDF File Identifier
  Descriptors.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#define __CDIO_CONFIG_H__ 1
#endif

#include <cdio/bytesex.h>
#include <cdio/ecma_167.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../lib/driver/_cdio_stdio.h"
#include "../lib/udf/udf_private.h"

static uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0;
  size_t i;
  unsigned int bit;

  for (i = 0; i < length; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
  }
  return crc;
}

static void set_tag_checksum(udf_tag_t *tag) {
  const uint8_t *bytes = (const uint8_t *)tag;
  uint8_t checksum = 0;
  unsigned int i;

  for (i = 0; i < sizeof(*tag); i++)
    if (i != 4)
      checksum = (uint8_t)(checksum + bytes[i]);
  tag->cksum = checksum;
}

int main(void) {
  uint8_t image[2 * UDF_BLOCKSIZE] = {0};
  udf_fileid_desc_t *fid = (udf_fileid_desc_t *)image;
  udf_t udf = {0};
  udf_dirent_t root = {0};
  udf_short_ad_t *ad;
  const char *path = "libcdio-udf-bad-fid-XXXXXX";
  char temp_path[sizeof("libcdio-udf-bad-fid-XXXXXX")];
  int fd, rc = 1;

  /* Compression ID 16 followed by three UCS-2BE bytes has an odd payload
     length, so conversion must fail. Keep the descriptor tag and CRC valid
     to ensure the malformed filename is what the reader rejects. */
  fid->tag.id = uint16_to_le(TAGID_FID);
  fid->tag.desc_version = uint16_to_le(2);
  fid->tag.desc_CRC_len = uint16_to_le(26);
  fid->i_file_id = 4;
  fid->icb.loc.lba = uint32_to_le(1);
  fid->u.file_id.data[0] = 16;
  fid->u.file_id.data[1] = 0xd8;
  fid->u.file_id.data[2] = 0x00;
  fid->u.file_id.data[3] = 0x00;
  fid->tag.desc_CRC =
      uint16_to_le(crc16((const uint8_t *)fid + sizeof(fid->tag),
                         uint16_from_le(fid->tag.desc_CRC_len)));
  set_tag_checksum(&fid->tag);

  strcpy(temp_path, path);
  fd = mkstemp(temp_path);
  if (fd < 0)
    return rc;
  if (write(fd, image, sizeof(image)) != (ssize_t)sizeof(image)) {
    close(fd);
    remove(temp_path);
    return rc;
  }
  close(fd);

  udf.b_stream = true;
  udf.stream = cdio_stdio_new(temp_path);
  if (!udf.stream)
    goto out;

  root.p_udf = &udf;
  root.psz_name = "/";
  root.b_dir = true;
  root.fe.info_len = uint64_to_le(UDF_BLOCKSIZE);
  root.fe.u_alloc_descs = uint32_to_le(sizeof(*ad));
  root.fe.icb_tag.flags = uint16_to_le(ICBTAG_FLAG_AD_SHORT);
  ad = (udf_short_ad_t *)root.fe.u.alloc_descs;
  ad->len = uint32_to_le(UDF_BLOCKSIZE);
  ad->pos = 0;

  /* Check that when fix udf_readdir() returns an entry with psz_name == NULL,
     udf_fopen() fails cleanly. */
  if (udf_fopen(&root, "not-present") == NULL)
    rc = 0;

  cdio_stdio_destroy(udf.stream);
out:
  remove(temp_path);
  return rc;
}
