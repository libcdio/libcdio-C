/*
  Copyright (C) 2004, 2005, 2008, 2011, 2012
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

/**
  Header for image drivers. In contrast to image_common.h which contains
  routines, this header like most C headers does not depend on anything
  defined before it is included.
*/

#ifndef CDIO_DRIVER_IMAGE_H_
#define CDIO_DRIVER_IMAGE_H_

#if defined(HAVE_CONFIG_H) && !defined(__CDIO_CONFIG_H__)
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#endif

#include <cdio/types.h>
#include <cdio/cdtext.h>
#include "cdio_private.h"
#include <cdio/sector.h>

/**
  Sub-channel data stored after each sector in a disc image.
*/
typedef enum {
  IMAGE_SUBCHANNEL_NONE = 0,
  IMAGE_SUBCHANNEL_RW,          /**< 96 bytes of packed R-W (cdrdao RW) */
  IMAGE_SUBCHANNEL_RW_RAW       /**< 96 bytes of raw interleaved sub-channel
                                     data (cdrdao RW_RAW). cdrdao read-cd
                                     stores the drive's raw P-W data. */
} image_subchannel_t;

/**
  The universal format for information about a track for CD image readers
  It may be that some fields can be derived from other fields.
  Over time this structure may get cleaned up. Possibly this can be
  expanded/reused for real CD formats.
*/

typedef struct {
  track_t        track_num;     /**< Probably is index+1 */
  msf_t          start_msf;
  lba_t          start_lba;
  int            start_index;
  lba_t          pregap;        /**< pre-gap */
  lba_t          silence;       /**< pre-gap with zero audio data */
  int            sec_count;     /**< Number of sectors in this track. Does not
                                     include pregap */
  int            num_indices;
  flag_t         flags;         /**< "[NO] COPY", "4CH", "[NO] PREMPAHSIS" */
  char          *isrc;          /**< IRSC Code (5.22.4) exactly 12 bytes */
  char          *filename;
  CdioDataSource_t *data_source;
  off_t          offset;        /**< byte offset into data_start of track
                                     beginning. In cdrdao for example, one
                                     filename may cover many tracks and
                                     each track would then have a different
                                     offset.
                                */
  track_format_t track_format;
  bool           track_green;

  trackmode_t    mode;
  uint16_t       datasize;      /**< How much is in the portion we return
                                     back? */
  uint16_t       datastart;     /**<  Offset from beginning of frame
                                      that data starts */
  uint16_t       endsize;       /**< How much stuff at the end to skip over.
                                     This stuff may have error correction
                                     (EDC, or ECC).*/
  uint16_t       blocksize;     /**< total block size = start + size + end */
  image_subchannel_t subchannel; /**< sub-channel data in endsize */
  lba_t          data_lba;      /**< LBA of the sector stored at offset in
                                     data_source, 0 if unknown */
} track_info_t;


#endif /* CDIO_DRIVER_IMAGE_H_ */
