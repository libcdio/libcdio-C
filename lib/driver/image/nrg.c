/*
  Copyright (C) 2003-2006, 2008-2009, 2011-2012, 2014, 2017, 2025
  Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2001, 2003 Herbert Valerio Riedel <hvr@gnu.org>

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
/** This code implements low-level access functions for the Nero native
   CD-image format residing inside a disk file (*.nrg).
*/

#include "image.h"

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_GLOB_H
#include <glob.h>
#endif
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif
#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>

#include <cdio/bytesex.h>
#include <cdio/ds.h>
#include <cdio/logging.h>
#include <cdio/util.h>
#include <cdio/version.h>
#include "cdio_assert.h"
#include "_cdio_stdio.h"
#include "nrg.h"
#include "cdtext_private.h"

nero_id_t    nero_id;
nero_dtype_t nero_dtype;

/* reader */
#define DEFAULT_CDIO_DEVICE "image.nrg"

/*
   Link element of track structure as a linked list.
   Possibly redundant with above track_info_t */
typedef struct {
  uint32_t start_lsn;
  uint32_t sec_count;     /* Number of sectors in track. Does not
			     include pregap before next entry. */
  uint64_t img_offset;    /* Bytes offset from beginning of disk image file.*/
  uint32_t blocksize;     /* Number of bytes in a block */
} _mapping_t;


#define NEED_NERO_STRUCT
#include "image_common.h"

static bool  parse_nrg (_img_private_t *env, const char *psz_cue_name,
			const cdio_log_level_t log_level);
static lsn_t get_disc_last_lsn_nrg (void *p_user_data);

/* Updates internal track TOC, so we can later
   simulate ioctl(CDROMREADTOCENTRY).
 */
static bool
_register_mapping (_img_private_t *env, lsn_t start_lsn, uint32_t sec_count,
		   uint64_t img_offset, uint32_t blocksize,
		   track_format_t track_format, bool track_green)
{
  const int track_num=env->gen.i_tracks;
  track_info_t  *this_track;
  _mapping_t *_map;

  if (env->gen.i_tracks >= CDIO_CD_MAX_TRACKS) {
    cdio_warn ("NRG image has more than %d tracks", CDIO_CD_MAX_TRACKS);
    return false;
  }

  this_track = &(env->tocent[env->gen.i_tracks]);
  _map = calloc(1, sizeof (_mapping_t));

  if (_map == NULL)
    return false;
  _map->start_lsn  = start_lsn;
  _map->sec_count  = sec_count;
  _map->img_offset = img_offset;
  _map->blocksize  = blocksize;

  if (!env->mapping) env->mapping = _cdio_list_new ();
  _cdio_list_append (env->mapping, _map);

  env->size = MAX (env->size, (start_lsn + sec_count));

  /* Update *this_track and track_num. These structures are
     in a sense redundant with the obj->mapping list. Perhaps one
     or the other can be eliminated.
   */

  cdio_lba_to_msf (cdio_lsn_to_lba(start_lsn), &(this_track->start_msf));
  this_track->start_lba = cdio_msf_to_lba(&this_track->start_msf);
  this_track->track_num = track_num+1;
  this_track->blocksize = blocksize;
  if (env->is_cues)
    this_track->datastart = img_offset;
  else
    this_track->datastart = 0;

  if (track_green)
    this_track->datastart += CDIO_CD_SUBHEADER_SIZE;

  this_track->sec_count = sec_count;

  this_track->track_format= track_format;
  this_track->track_green = track_green;

  switch (this_track->track_format) {
  case TRACK_FORMAT_AUDIO:
    this_track->blocksize   = CDIO_CD_FRAMESIZE_RAW;
    this_track->datasize    = CDIO_CD_FRAMESIZE_RAW;
    /*this_track->datastart   = 0;*/
    this_track->endsize     = 0;
    break;
  case TRACK_FORMAT_CDI:
    this_track->datasize=CDIO_CD_FRAMESIZE;
    break;
  case TRACK_FORMAT_XA:
    if (track_green) {
      this_track->blocksize = CDIO_CD_FRAMESIZE;
      /*this_track->datastart = CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE;*/
      this_track->datasize  = M2RAW_SECTOR_SIZE;
      this_track->endsize   = 0;
    } else {
      /*this_track->datastart = CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE +
	CDIO_CD_SUBHEADER_SIZE;*/
      this_track->datasize  = CDIO_CD_FRAMESIZE;
      this_track->endsize   = CDIO_CD_SYNC_SIZE + CDIO_CD_ECC_SIZE;
    }
    break;
  case TRACK_FORMAT_DATA:
    if (track_green) {
      /*this_track->datastart = CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE;*/
      this_track->datasize  = CDIO_CD_FRAMESIZE;
      this_track->endsize   = CDIO_CD_EDC_SIZE + CDIO_CD_M1F1_ZERO_SIZE
	  + CDIO_CD_ECC_SIZE;
    } else {
      /* Is the below correct? */
      /*this_track->datastart = 0;*/
      this_track->datasize  = CDIO_CD_FRAMESIZE;
      this_track->endsize   = 0;
    }
    break;
  default:
    /*this_track->datasize=CDIO_CD_FRAMESIZE_RAW;*/
    cdio_warn ("track %d has unknown format %d",
	       env->gen.i_tracks, this_track->track_format);
  }

  env->gen.i_tracks++;

  cdio_debug ("start lsn: %lu sector count: %0lu -> %8ld (%08lx)",
	      (long unsigned int) start_lsn,
	      (long unsigned int) sec_count,
	      (long unsigned int) img_offset,
	      (long unsigned int) img_offset);
  return true;
}


/*
   Disk and track information for a Nero file are located at the end
   of the file. This routine extracts that information.

   FIXME: right now psz_nrg_name is not used. It will be in the future.
 */
static bool
parse_nrg (_img_private_t *p_env, const char *psz_nrg_name,
	   const cdio_log_level_t log_level)
{
  off_t footer_start;
  off_t size;
  size_t footer_len;
  char *footer_buf = NULL;
  if (!p_env) return false;
  size = cdio_stream_stat (p_env->gen.data_source);
  if (size < (off_t) sizeof (_footer_t)) return false;

  {
    _footer_t buf;
    cdio_assert (sizeof (buf) == 12);

    if (cdio_stream_seek (p_env->gen.data_source, size - sizeof (buf), SEEK_SET)
        || cdio_stream_read (p_env->gen.data_source, (void *) &buf,
                             sizeof (buf), 1) != sizeof (buf))
      return false;

    if (buf.v50.ID == UINT32_TO_BE (NERO_ID)) {
      cdio_debug ("detected Nero version 5.0 (32-bit offsets) NRG magic");
      footer_start = uint32_to_be (buf.v50.footer_ofs);
    } else if (buf.v55.ID == UINT32_TO_BE (NER5_ID)) {
      cdio_debug ("detected Nero version 5.5.x (64-bit offsets) NRG magic");
      footer_start = uint64_from_be (buf.v55.footer_ofs);
    } else {
      cdio_log (log_level, "Image not recognized as either version 5.0 or "
		"version 5.5.x-6.x type NRG");
      return false;
    }

    if (footer_start < 0 || footer_start > size) {
      cdio_log (log_level, "invalid NRG footer offset");
      return false;
    }

    footer_len = (size_t) (size - footer_start);
    cdio_debug (".NRG footer start = %ld, length = %lu",
               (long) footer_start, (unsigned long) footer_len);

    if (footer_len > 4096) {
      cdio_log (log_level, "NRG footer is too large");
      return false;
    }

    footer_buf = calloc(1, footer_len);
    if (!footer_buf)
      return false;

    if (cdio_stream_seek (p_env->gen.data_source, footer_start, SEEK_SET)
        || cdio_stream_read (p_env->gen.data_source, footer_buf,
                             footer_len, 1) != footer_len) {
      free(footer_buf);
      return false;
    }
  }
  {
    size_t pos = 0;

    while (pos < footer_len) {
      uint32_t chunk_len;
      _chunk_t *chunk = (void *) (footer_buf + pos);
      uint32_t opcode;

      bool break_out = false;

      if (footer_len - pos < sizeof (*chunk)) {
        cdio_log (log_level, "truncated NRG footer chunk header");
        free(footer_buf);
        return false;
      }

      chunk_len = UINT32_FROM_BE (chunk->len);
      if (chunk_len > footer_len - pos - sizeof (*chunk)) {
        cdio_log (log_level, "truncated NRG footer chunk");
        free(footer_buf);
        return false;
      }
      opcode = UINT32_FROM_BE (chunk->id);

      switch (opcode) {

      case CUES_ID: /* "CUES" Seems to have sector size 2336 and 150 sector
		       pregap seems to be included at beginning of image.
		       */
      case CUEX_ID: /* "CUEX" */
	{
	  const uint32_t cue_chunk_len = UINT32_FROM_BE (chunk->len);
	  unsigned entries;
	  _cuex_array_t *_entries = (void *) chunk->data;

	  if (p_env->mapping != NULL
	      || cue_chunk_len % sizeof (_cuex_array_t) != 0) {
	    cdio_log (log_level, "invalid NRG CUE chunk length");
	    free(footer_buf);
	    return false;
	  }

	  cdio_assert ( sizeof (_cuex_array_t) == 8 );

	  entries = cue_chunk_len / sizeof (_cuex_array_t);

	  if (CUES_ID == opcode) {
	    unsigned int idx;
	    unsigned int i = 0;
	    lsn_t lsn; /* = UINT32_FROM_BE (_entries[0].lsn); */
	    /* CUES LSN has 150 pregap include at beginning? -/
	       cdio_assert (lsn == 0?);
	    */

	    cdio_debug ("CUES type image detected" );
	    if (entries < 2) {
	      cdio_log (log_level, "NRG CUES chunk has too few entries");
	      free(footer_buf);
	      return false;
	    }

	    p_env->is_cues           = true; /* HACK alert. */
	    p_env->gen.i_tracks      = 0;
	    p_env->gen.i_first_track = 1;
	    for (idx = 1; idx < entries-1; idx += 2, i++) {
	      lsn_t sec_count;
	      int cdte_format = _entries[idx].addr_ctrl / 16;
	      int cdte_ctrl   = _entries[idx].type >> 4;

	      if (i >= CDIO_CD_MAX_TRACKS) {
		cdio_warn ("NRG image has more than %d tracks", CDIO_CD_MAX_TRACKS);
		free(footer_buf);
		return false;
	      }

	      if ( COPY_PERMITTED & cdte_ctrl ) {
		if (p_env) p_env->tocent[i].flags |= COPY_PERMITTED;
	      } else {
		if (p_env) p_env->tocent[i].flags &= ~COPY_PERMITTED;
	      }

	      if ( PRE_EMPHASIS & cdte_ctrl ) {
		if (p_env) p_env->tocent[i].flags |= PRE_EMPHASIS;
	      } else {
		if (p_env) p_env->tocent[i].flags &= ~PRE_EMPHASIS;
	      }

	      if ( FOUR_CHANNEL_AUDIO & cdte_ctrl ) {
		if (p_env) p_env->tocent[i].flags |= FOUR_CHANNEL_AUDIO;
	      } else {
		if (p_env) p_env->tocent[i].flags &= ~FOUR_CHANNEL_AUDIO;
	      }

	      if (_entries[idx].track != _entries[idx + 1].track) {
		cdio_log (log_level, "invalid NRG CUES track pair");
		free(footer_buf);
		return false;
	      }

	      /* lsn and sec_count*2 aren't correct, but it comes closer on the
		 single example I have: svcdgs.nrg
		 We are picking up the wrong fields and/or not interpreting
		 them correctly.
	      */

	      switch (cdte_format) {
	      case 0:
		lsn = UINT32_FROM_BE (_entries[idx].lsn);
		break;
	      case 1:
		{
#if 0
		  msf_t msf = (msf_t) _entries[idx].lsn;
		  lsn = cdio_msf_to_lsn(&msf);
#else
		  lsn = CDIO_INVALID_LSN;
#endif
		  cdio_log (log_level,
			    "untested (i.e. probably wrong) CUE MSF code");
		  break;
		}
	      default:
		lsn = CDIO_INVALID_LSN;
		cdio_log(log_level,
			 "unknown cdte_format %d", cdte_format);
	      }

	      sec_count = UINT32_FROM_BE (_entries[idx + 1].lsn);

	      if (!_register_mapping (p_env, lsn, sec_count*2,
				      (lsn+CDIO_PREGAP_SECTORS) * M2RAW_SECTOR_SIZE,
				      M2RAW_SECTOR_SIZE, TRACK_FORMAT_XA, true)) {
		free(footer_buf);
		return false;
	      }
	    }
	  } else {
	    lsn_t lsn;
	    unsigned int idx;
	    unsigned int i = 0;

	    if (entries == 0) {
	      cdio_log (log_level, "NRG CUEX chunk has no entries");
	      free(footer_buf);
	      return false;
	    }
	    lsn = UINT32_FROM_BE (_entries[0].lsn);

	    cdio_debug ("CUEX type image detected");

	    /* LSN must start at -150 (LBA 0)? */
	    if (lsn != -150) {
	      cdio_log (log_level, "invalid NRG CUEX starting LSN");
	      free(footer_buf);
	      return false;
	    }

	    /* Each track uses a pair of entries; an unpaired last entry
	       is ignored, as in the CUES loop above. */
	    for (idx = 2; idx + 1 < entries; idx += 2, i++) {
	      lsn_t sec_count;
	      int cdte_format = _entries[idx].addr_ctrl >> 4;
	      int cdte_ctrl   = _entries[idx].type >> 4;

	      if (i >= CDIO_CD_MAX_TRACKS) {
		cdio_warn ("NRG image has more than %d tracks", CDIO_CD_MAX_TRACKS);
		free(footer_buf);
		return false;
	      }

	      if ( COPY_PERMITTED & cdte_ctrl ) {
		if (p_env) p_env->tocent[i].flags |= COPY_PERMITTED;
	      } else {
		if (p_env) p_env->tocent[i].flags &= ~COPY_PERMITTED;
	      }

	      if ( PRE_EMPHASIS & cdte_ctrl ) {
		if (p_env) p_env->tocent[i].flags |= PRE_EMPHASIS;
	      } else {
		if (p_env) p_env->tocent[i].flags &= ~PRE_EMPHASIS;
	      }

	      if ( FOUR_CHANNEL_AUDIO & cdte_ctrl ) {
		if (p_env) p_env->tocent[i].flags |= FOUR_CHANNEL_AUDIO;
	      } else {
		if (p_env) p_env->tocent[i].flags &= ~FOUR_CHANNEL_AUDIO;
	      }

	      /* extractnrg.pl has cdte_format for LBA's 0, and
		 for MSF 1. ???

		 FIXME: Should decode as appropriate for cdte_format.
	       */
	      if (cdte_format != 0 && cdte_format != 1) {
		cdio_log (log_level, "invalid NRG CUEX address format");
		free(footer_buf);
		return false;
	      }

	      if (_entries[idx].track == _entries[idx + 1].track) {
		cdio_log (log_level, "invalid NRG CUEX track pair");
		free(footer_buf);
		return false;
	      }

	      lsn       = UINT32_FROM_BE (_entries[idx].lsn);
	      sec_count = UINT32_FROM_BE (_entries[idx + 1].lsn);

	      if (!_register_mapping (p_env, lsn, sec_count - lsn,
				      (lsn + CDIO_PREGAP_SECTORS)*M2RAW_SECTOR_SIZE,
				      M2RAW_SECTOR_SIZE, TRACK_FORMAT_XA, true)) {
		free(footer_buf);
		return false;
	      }
	    }
	  }
	  break;
	}

      case DAOX_ID: /* "DAOX" */
      case DAOI_ID: /* "DAOI" */
	{
	  _daox_t *_xentries = NULL;
	  _daoi_t *_ientries = NULL;
	  _dao_array_common_t *_dao_array_common = NULL;
	  size_t dao_entry_size;
	  size_t dao_track_count = p_env->gen.i_tracks;
	  size_t dao_required;
	  _dao_common_t *_dao_common = (void *) chunk->data;
	  int disc_mode;
	  track_format_t track_format;
	  int i;

	  if (dao_track_count == 0)
	    dao_track_count = 1;
	  if (DAOX_ID == opcode) {
	    dao_entry_size = sizeof(_daox_array_t);
	    dao_required = offsetof(_daox_t, track_info);
	  } else {
	    dao_entry_size = sizeof(_daoi_array_t);
	    dao_required = offsetof(_daoi_t, track_info);
	  }
	  if (dao_track_count > (SIZE_MAX - dao_required) / dao_entry_size)
	    dao_required = SIZE_MAX;
	  else
	    dao_required += dao_track_count * dao_entry_size;
	  if (UINT32_FROM_BE(chunk->len) < dao_required) {
	    cdio_log(log_level, "truncated DAO%c chunk: need %lu bytes, got %u",
		      DAOX_ID == opcode ? 'X' : 'I',
		      (unsigned long) dao_required,
		      (unsigned int) UINT32_FROM_BE(chunk->len));
	    free(footer_buf);
	    return false;
	  }

	  disc_mode = _dao_common->unknown[1];

	  /* We include an extra 0 byte so these can be used as C strings.*/
	  free(p_env->psz_mcn);
	  p_env->psz_mcn = calloc(1, CDIO_MCN_SIZE+1);
	  if (p_env->psz_mcn == NULL) {
	    free(footer_buf);
	    return false;
	  }
	  memcpy(p_env->psz_mcn, &(_dao_common->psz_mcn), CDIO_MCN_SIZE);
	  p_env->psz_mcn[CDIO_MCN_SIZE] = '\0';

	  if (DAOX_ID == opcode) {
	    _xentries = (void *) chunk->data;
	    p_env->dtyp = _xentries->track_info[0].common.unknown[2];
	  } else {
	    _ientries = (void *) chunk->data;
	    p_env->dtyp = _ientries->track_info[0].common.unknown[2];
	  }

	  p_env->is_dao = true;
	  cdio_debug ("DAO%c tag detected, track format %d, mode %x\n",
		      opcode==DAOX_ID ? 'X': 'I', p_env->dtyp, disc_mode);
	  switch (p_env->dtyp) {
	  case 0:
	    /* Mode 1 */
	    track_format     = TRACK_FORMAT_DATA;
	    p_env->disc_mode = CDIO_DISC_MODE_CD_DATA;
	    break;
	  case 2:
	    /* Mode 2 form 1 */
	    disc_mode             = 0;
	    track_format     = TRACK_FORMAT_XA;
	    p_env->disc_mode = CDIO_DISC_MODE_CD_XA;
	    break;
	  case 3:
	    /* Mode 2 */
	    track_format     = TRACK_FORMAT_XA;
	    p_env->disc_mode = CDIO_DISC_MODE_CD_XA; /* ?? */
	    break;
	  case 0x6:
	    /* Mode2 form mix */
	    track_format     = TRACK_FORMAT_XA;
	    p_env->disc_mode = CDIO_DISC_MODE_CD_MIXED;
	    break;
	  case 0x20: /* ??? Mode2 form 2, Mode2 raw?? */
	    track_format     = TRACK_FORMAT_XA;
	    p_env->disc_mode = CDIO_DISC_MODE_CD_XA; /* ??. */
	    break;
	  case 0x7:
	    track_format     = TRACK_FORMAT_AUDIO;
	    p_env->disc_mode = CDIO_DISC_MODE_CD_DA;
	    break;
	  default:
	    cdio_log (log_level, "Unknown track format %x\n",
		      p_env->dtyp);
	    track_format = TRACK_FORMAT_AUDIO;
	  }
	  if (0 == disc_mode) {
	    for (i=0; i<p_env->gen.i_tracks; i++) {

	      if (i > CDIO_CD_MAX_TRACKS) {
		cdio_warn ("Track number %d is too large; maximum track number is %d.",
			   i, CDIO_CD_MAX_TRACKS);
		return false;
	      }

	      p_env->tocent[i].track_format= track_format;
	      p_env->tocent[i].datastart   = 0;
	      p_env->tocent[i].track_green = false;
	      if (TRACK_FORMAT_AUDIO == track_format) {
		p_env->tocent[i].blocksize   = CDIO_CD_FRAMESIZE_RAW;
		p_env->tocent[i].datasize    = CDIO_CD_FRAMESIZE_RAW;
		p_env->tocent[i].endsize     = 0;
	      } else {
		p_env->tocent[i].datasize    = CDIO_CD_FRAMESIZE;
		p_env->tocent[i].datastart  =  0;
	      }
	    }
	  } else if (2 == disc_mode) {
	    for (i=0; i<p_env->gen.i_tracks; i++) {
	      if (i > CDIO_CD_MAX_TRACKS) {
		cdio_warn ("Track number %d is too large; maximum track number is %d.",
			   i, CDIO_CD_MAX_TRACKS);
                return false;
	      }
	      p_env->tocent[i].track_green = true;
	      p_env->tocent[i].track_format= track_format;
	      p_env->tocent[i].datasize    = CDIO_CD_FRAMESIZE;
	      if (TRACK_FORMAT_XA == track_format) {
		p_env->tocent[i].datastart   = CDIO_CD_SYNC_SIZE
		  + CDIO_CD_HEADER_SIZE + CDIO_CD_SUBHEADER_SIZE;
		p_env->tocent[i].endsize     = CDIO_CD_SYNC_SIZE
		  + CDIO_CD_ECC_SIZE;
	      } else {
		p_env->tocent[i].datastart   = CDIO_CD_SYNC_SIZE
		  + CDIO_CD_HEADER_SIZE;
		p_env->tocent[i].endsize     = CDIO_CD_EDC_SIZE
		  + CDIO_CD_M1F1_ZERO_SIZE + CDIO_CD_ECC_SIZE;

	      }
	    }
	  } else if (0x20 == disc_mode) {
	    cdio_debug ("Mixed mode CD?\n");
	  } else {
	    /* Mixed mode CD */
	    cdio_log (log_level,
		      "Don't know if mode 1, mode 2 or mixed: %x\n",
		      disc_mode);
	  }

	  for (i=0; i<p_env->gen.i_tracks; i++) {

	    if (DAOX_ID == opcode) {
	      _dao_array_common = &_xentries->track_info[i].common;
	    } else {
	      _dao_array_common = &_ientries->track_info[i].common;
	    }
	    free(p_env->tocent[i].isrc);
	    p_env->tocent[i].isrc = calloc(1, CDIO_ISRC_SIZE+1);
	    if (p_env->tocent[i].isrc == NULL) {
	      free(footer_buf);
	      return false;
	    }
	    memcpy(p_env->tocent[i].isrc, _dao_array_common->psz_isrc, CDIO_ISRC_SIZE);
	    p_env->tocent[i].isrc[CDIO_ISRC_SIZE] = '\0';
	    if (p_env->tocent[i].isrc[0]) {
	       cdio_info("nrg isrc has value \"%s\"", p_env->tocent[i].isrc);
	    }

	    if (!p_env->tocent[i].datasize) {
	      continue;
	    }
	    if (DAOX_ID == opcode) {
	       p_env->tocent[i].pregap = (uint64_from_be
		(_xentries->track_info[i].index0)) / (p_env->tocent[i].datasize);
	    } else {
	       p_env->tocent[i].pregap = (uint32_from_be
		(_ientries->track_info[i].index0)) / (p_env->tocent[i].datasize);
	    }
	  }

	  break;
	}
      case NERO_ID:
      case NER5_ID:
	cdio_error ("unexpected nrg magic ID NER%c detected",
		    opcode==NERO_ID ? 'O': '5');
	free(footer_buf);
	return false;

      case END1_ID: /* "END!" */
	cdio_debug ("nrg end tag detected");
	break_out = true;
	break;

      case ETNF_ID: /* "ETNF" */ {
	unsigned entries = UINT32_FROM_BE (chunk->len);
	_etnf_array_t *_entries = (void *) chunk->data;

	if (p_env->mapping != NULL
	    || entries % sizeof (_etnf_array_t) != 0) {
	  cdio_log (log_level, "invalid NRG ETNF chunk length");
	  free(footer_buf);
	  return false;
	}

	cdio_assert ( sizeof (_etnf_array_t) == 20 );

	entries /= sizeof (_etnf_array_t);

	cdio_debug ("SAO type image (ETNF) detected");

	{
	  int idx;
	  for (idx = 0; idx < entries; idx++) {
	    uint32_t _len = UINT32_FROM_BE (_entries[idx].length);
	    uint32_t _start = UINT32_FROM_BE (_entries[idx].start_lsn);
	    uint32_t _start2 = UINT32_FROM_BE (_entries[idx].start);
	    uint32_t track_mode= uint32_from_be (_entries[idx].type);
	    bool     track_green = true;
	    track_format_t track_format = TRACK_FORMAT_XA;
	    uint16_t  blocksize;

	    switch (track_mode) {
	    case 0:
	      /* Mode 1 */
	      track_format   = TRACK_FORMAT_DATA;
	      track_green    = false; /* ?? */
	      blocksize      = CDIO_CD_FRAMESIZE;
	      p_env->disc_mode = CDIO_DISC_MODE_CD_DATA;
	      cdio_debug ("Format DATA, blocksize %u", CDIO_CD_FRAMESIZE);
	      break;
	    case 2:
	      /* Mode 2 form 1 */
	      track_format   = TRACK_FORMAT_XA;
	      track_green    = false; /* ?? */
	      blocksize      = CDIO_CD_FRAMESIZE;
	      p_env->disc_mode = CDIO_DISC_MODE_CD_XA;
	      cdio_debug ("Format XA, blocksize %u", CDIO_CD_FRAMESIZE);
	      break;
	    case 3:
	      /* Mode 2 */
	      track_format   = TRACK_FORMAT_XA;
	      track_green    = true;
	      blocksize      = M2RAW_SECTOR_SIZE;
	      p_env->disc_mode = CDIO_DISC_MODE_CD_XA; /* ?? */
	      cdio_debug ("Format XA, blocksize %u", M2RAW_SECTOR_SIZE);
	      break;
	    case 06:
	      /* Mode2 form mix */
	      track_format   = TRACK_FORMAT_XA;
	      track_green    = true;
	      blocksize      = M2RAW_SECTOR_SIZE;
	      p_env->disc_mode = CDIO_DISC_MODE_CD_MIXED;
	      cdio_debug ("Format MIXED CD, blocksize %u", M2RAW_SECTOR_SIZE);
	      break;
	    case 0x20: /* ??? Mode2 form 2, Mode2 raw?? */
	      track_format   = TRACK_FORMAT_XA;
	      track_green    = true;
	      blocksize      = M2RAW_SECTOR_SIZE;
	      p_env->disc_mode = CDIO_DISC_MODE_CD_XA; /* ??. */
	      cdio_debug ("Format MIXED CD, blocksize %u", M2RAW_SECTOR_SIZE);
	      break;
	    case 7:
	      track_format   = TRACK_FORMAT_AUDIO;
	      track_green    = false;
	      blocksize      = CDIO_CD_FRAMESIZE_RAW;
	      p_env->disc_mode = CDIO_DISC_MODE_CD_DA;
	      cdio_debug ("Format CD_DA, blocksize %u", CDIO_CD_FRAMESIZE_RAW);
	      break;
	    default:
	      cdio_log (log_level,
			"Don't know how to handle track mode (%lu)?",
			(long unsigned int) track_mode);
	      free(footer_buf);
	      return false;
	    }

	    if (_len % blocksize != 0) {
	      cdio_log (log_level, "invalid NRG ETNF block length");
	      free(footer_buf);
	      return false;
	    }

	    _len /= blocksize;

	    if ((uint64_t)_start * blocksize != _start2) {
	      cdio_log (log_level, "invalid NRG ETNF start offset");
	      free(footer_buf);
	      return false;
	    }

	    _start += idx * CDIO_PREGAP_SECTORS;
	    if (!_register_mapping (p_env, _start, _len, _start2, blocksize,
				   track_format, track_green)) {
	      free(footer_buf);
	      return false;
	    }

	  }
	}
	break;
      }

      case ETN2_ID: { /* "ETN2", same as above, but with 64bit stuff instead */
	unsigned entries = uint32_from_be (chunk->len);
	_etn2_array_t *_entries = (void *) chunk->data;

	if (p_env->mapping != NULL
	    || entries % sizeof (_etn2_array_t) != 0) {
	  cdio_log (log_level, "invalid NRG ETN2 chunk length");
	  free(footer_buf);
	  return false;
	}

	cdio_assert (sizeof (_etn2_array_t) == 32);

	entries /= sizeof (_etn2_array_t);

	cdio_debug ("SAO type image (ETN2) detected");

	{
	  int idx;
	  for (idx = 0; idx < entries; idx++) {
	    uint32_t _len = uint64_from_be (_entries[idx].length);
	    uint32_t _start = uint32_from_be (_entries[idx].start_lsn);
	    uint32_t _start2 = uint64_from_be (_entries[idx].start);
	    uint32_t track_mode= uint32_from_be (_entries[idx].type);
	    bool     track_green = true;
	    track_format_t track_format = TRACK_FORMAT_XA;
	    uint16_t  blocksize;


	    switch (track_mode) {
	    case 0:
	      track_format = TRACK_FORMAT_DATA;
	      track_green  = false; /* ?? */
	      blocksize    = CDIO_CD_FRAMESIZE;
	      break;
	    case 2:
	      track_format = TRACK_FORMAT_XA;
	      track_green  = false; /* ?? */
	      blocksize    = CDIO_CD_FRAMESIZE;
	      break;
	    case 3:
	      track_format = TRACK_FORMAT_XA;
	      track_green  = true;
	      blocksize    = M2RAW_SECTOR_SIZE;
	      break;
	    case 7:
	      track_format = TRACK_FORMAT_AUDIO;
	      track_green  = false;
	      blocksize    = CDIO_CD_FRAMESIZE_RAW;
	      break;
	    default:
	      cdio_log (log_level,
			"Don't know how to handle track mode (%lu)?",
			(long unsigned int) track_mode);
	      free(footer_buf);
	      return false;
	    }

	    if (_len % blocksize != 0) {
	      cdio_log (log_level,
			"length is not a multiple of blocksize "
			 "len %lu, size %d, rem %lu",
			 (long unsigned int) _len, blocksize,
			 (long unsigned int) _len % blocksize);
	      if (0 == _len % CDIO_CD_FRAMESIZE) {
		cdio_log(log_level, "Adjusting blocksize to %d",
			 CDIO_CD_FRAMESIZE);
		blocksize = CDIO_CD_FRAMESIZE;
	      } else if (0 == _len % M2RAW_SECTOR_SIZE) {
		cdio_log(log_level,
			 "Adjusting blocksize to %d", M2RAW_SECTOR_SIZE);
		blocksize = M2RAW_SECTOR_SIZE;
	      } else if (0 == _len % CDIO_CD_FRAMESIZE_RAW) {
		cdio_log(log_level,
			 "Adjusting blocksize to %d", CDIO_CD_FRAMESIZE_RAW);
		blocksize = CDIO_CD_FRAMESIZE_RAW;
	      }
	    }

	    _len /= blocksize;

	    if (_start * blocksize != _start2) {
	      cdio_log (log_level,
			"%lu * %d != %lu",
			 (long unsigned int) _start, blocksize,
			 (long unsigned int) _start2);
	      if (_start * CDIO_CD_FRAMESIZE == _start2) {
		cdio_log(log_level,
			 "Adjusting blocksize to %d", CDIO_CD_FRAMESIZE);
		blocksize = CDIO_CD_FRAMESIZE;
	      } else if (_start * M2RAW_SECTOR_SIZE == _start2) {
		cdio_log(log_level,
			 "Adjusting blocksize to %d", M2RAW_SECTOR_SIZE);
		blocksize = M2RAW_SECTOR_SIZE;
	      } else if (_start * CDIO_CD_FRAMESIZE_RAW == _start2) {
		cdio_log(log_level,
			 "Adjusting blocksize to %d", CDIO_CD_FRAMESIZE_RAW);
		blocksize = CDIO_CD_FRAMESIZE_RAW;
	      }
	    }

	    _start += idx * CDIO_PREGAP_SECTORS;
	    if (!_register_mapping (p_env, _start, _len, _start2, blocksize,
				   track_format, track_green)) {
	      free(footer_buf);
	      return false;
	    }
	  }
	}
	break;
      }

      case SINF_ID: { /* "SINF" */

	uint32_t _sessions;

	if (UINT32_FROM_BE (chunk->len) != sizeof (_sessions)) {
	  cdio_log (log_level, "invalid NRG SINF chunk length");
	  free(footer_buf);
	  return false;
	}

	memcpy(&_sessions, chunk->data, 4);

	cdio_debug ("SINF: %lu sessions",
		    (long unsigned int) UINT32_FROM_BE (_sessions));
      }
	break;

      case MTYP_ID: { /* "MTYP" */
	uint32_t mtyp_be;
	uint32_t mtyp;

	if (UINT32_FROM_BE (chunk->len) != sizeof (mtyp_be)) {
	  cdio_log (log_level, "invalid NRG MTYP chunk length");
	  free(footer_buf);
	  return false;
	}

	memcpy(&mtyp_be, chunk->data, 4);
	mtyp = UINT32_FROM_BE (mtyp_be);

	cdio_debug ("MTYP: %lu",
		    (long unsigned int) mtyp);

	if (mtyp != MTYP_AUDIO_CD) {
	  cdio_log (log_level,
		    "Unknown MTYP value: %u", (unsigned int) mtyp);
	}
	p_env->mtyp = mtyp;
      }
	break;

      case CDTX_ID: { /* "CD TEXT" */
        uint8_t *wdata = (uint8_t *) chunk->data;
        uint32_t len = UINT32_FROM_BE (chunk->len);
        if (len % CDTEXT_LEN_PACK != 0) {
          cdio_log (log_level, "invalid NRG CD TEXT chunk length");
          free(footer_buf);
          return false;
        }
        /* A later CD TEXT chunk replaces an earlier one */
        cdtext_destroy(p_env->gen.cdtext);
        p_env->gen.cdtext = cdtext_init ();
        if(0 !=cdtext_data_init (p_env->gen.cdtext, wdata, len))
        {
          /* cdtext_destroy() also frees the cdtext_t itself */
          cdtext_destroy(p_env->gen.cdtext);
          p_env->gen.cdtext = NULL;
        }

        break;
      }

      default:
	cdio_log (log_level,
		  "unknown tag %8.8x seen",
		  (unsigned int) UINT32_FROM_BE (chunk->id));
	break;
      }

      if (break_out)
	break;

      pos += sizeof (*chunk) + chunk_len;
    }
  }

  if (p_env->gen.i_tracks == 0) {
    cdio_log (log_level, "NRG footer contains no tracks");
    free(footer_buf);
    return false;
  }

  /* Fake out leadout track. */
  /* Don't use get_disc_last_lsn_nrg since that will lead to recursion since
     we haven't fully initialized things yet.
  */
  cdio_lsn_to_msf (p_env->size, &p_env->tocent[p_env->gen.i_tracks].start_msf);
  p_env->tocent[p_env->gen.i_tracks].start_lba = cdio_lsn_to_lba(p_env->size);
  p_env->tocent[p_env->gen.i_tracks-1].sec_count =
    cdio_lsn_to_lba(p_env->size - p_env->tocent[p_env->gen.i_tracks-1].start_lba);

  p_env->gen.b_cdtext_error = false;
  p_env->gen.toc_init       = true;
  free(footer_buf);
  return true;
}

/**
  Initialize image structures.
 */
static bool
_init_nrg (_img_private_t *p_env)
{
  if (p_env->gen.init) {
    cdio_error ("init called more than once");
    return false;
  }

  if (!(p_env->gen.data_source = cdio_stdio_new (p_env->gen.source_name))) {
    cdio_warn ("can't open nrg image file %s for reading",
	       p_env->gen.source_name);
    return false;
  }

  p_env->psz_mcn       = NULL;
  p_env->disc_mode     = CDIO_DISC_MODE_NO_INFO;

  if ( !parse_nrg (p_env, p_env->gen.source_name, CDIO_LOG_WARN) ) {
    cdio_warn ("image file %s is not a Nero image",
	       p_env->gen.source_name);
    return false;
  }

  p_env->gen.init = true;
  return true;

}

/**
  Reads into buf the next size bytes.
  Returns -1 on error.
  Would be libc's seek() but we have to adjust for the extra track header
  information in each sector.
*/
static off_t
_lseek_nrg (void *p_user_data, off_t offset, int whence)
{
  _img_private_t *p_env = p_user_data;

  /* real_offset is the real byte offset inside the disk image
     The number below was determined empirically.
  */
  off_t real_offset= p_env->is_dao ? 0x4b000 : 0;

  unsigned int i;

  p_env->pos.lba = 0;
  for (i=0; i<p_env->gen.i_tracks; i++) {
    track_info_t  *this_track=&(p_env->tocent[i]);
    p_env->pos.index = i;
    if ( (this_track->sec_count*this_track->datasize) >= offset) {
      int blocks            = (int) (offset / this_track->datasize);
      int rem               = (int) (offset % this_track->datasize);
      off_t block_offset    = blocks * this_track->blocksize;
      real_offset          += block_offset + rem;
      p_env->pos.buff_offset = rem;
      p_env->pos.lba        += blocks;
      break;
    }
    real_offset   += this_track->sec_count*this_track->blocksize;
    offset        -= this_track->sec_count*this_track->datasize;
    p_env->pos.lba += this_track->sec_count;
  }

  if (i==p_env->gen.i_tracks) {
    cdio_warn ("seeking outside range of disk image");
    return -1;
  } else
    real_offset += p_env->tocent[i].datastart;
  return cdio_stream_seek(p_env->gen.data_source, real_offset, whence);
}

/**
  Reads into buf the next size bytes.
  Returns -1 on error.
  FIXME:
   At present we assume a read doesn't cross sector or track
   boundaries.
*/
static ssize_t
_read_nrg (void *p_user_data, void *buf, size_t size)
{
  _img_private_t *p_env = p_user_data;
  return cdio_stream_read(p_env->gen.data_source, buf, size, 1);
}

/**
  Get the size of the CD in logical block address (LBA) units.

  @param p_cdio the CD object queried
  @return the lsn. On error 0 or CDIO_INVALD_LSN.
*/
static lsn_t
get_disc_last_lsn_nrg (void *p_user_data)
{
  _img_private_t *p_env = p_user_data;

  return p_env->size;
}

/**
   Reads a single audio sector from CD device into data starting
   from LSN.
 */
static driver_return_code_t
_read_audio_sectors_nrg (void *p_user_data, void *data, lsn_t lsn,
			  unsigned int nblocks)
{
  _img_private_t *p_env = p_user_data;
  CdioListNode_t *node;

  if (lsn >= p_env->size)
    {
      cdio_warn ("trying to read beyond image size (%lu >= %lu)",
		 (long unsigned int) lsn, (long unsigned int) p_env->size);
      return -1;
    }

  if (p_env->is_dao) {
    int ret;

    ret = cdio_stream_seek (p_env->gen.data_source,
              (lsn + CDIO_PREGAP_SECTORS) * CDIO_CD_FRAMESIZE_RAW, SEEK_SET);
    if (ret!=0) return ret;

    ret = cdio_stream_read (p_env->gen.data_source, data,
              CDIO_CD_FRAMESIZE_RAW, nblocks);

    /* ret is number of bytes if okay, but we need to return 0 okay. */
    return ret == 0;
  }

  _CDIO_LIST_FOREACH (node, p_env->mapping) {
    _mapping_t *_map = _cdio_list_node_data (node);

    if (CDIO_IN (lsn, _map->start_lsn, (_map->start_lsn + _map->sec_count - 1))) {
      int ret;
      long int img_offset = _map->img_offset;

      img_offset += (lsn - _map->start_lsn) * CDIO_CD_FRAMESIZE_RAW;

      ret = cdio_stream_seek (p_env->gen.data_source, img_offset,
			      SEEK_SET);
      if (ret!=0) return ret;
      ret = cdio_stream_read (p_env->gen.data_source, data,
			      CDIO_CD_FRAMESIZE_RAW, nblocks);
      if (ret==0) return ret;
      break;
    }
  }

  if (!node) cdio_warn ("reading into pre gap (lsn %lu)",
			(long unsigned int) lsn);

  return 0;
}

static driver_return_code_t
_read_mode1_sector_nrg (void *p_user_data, void *data, lsn_t lsn,
			 bool b_form2)
{
  _img_private_t *p_env = p_user_data;
  char buf[CDIO_CD_FRAMESIZE_RAW] = { 0, };

  CdioListNode_t *node;

  if (lsn >= p_env->size)
    {
      cdio_warn ("trying to read beyond image size (%lu >= %lu)",
		 (long unsigned int) lsn, (long unsigned int) p_env->size);
      return -1;
    }

  _CDIO_LIST_FOREACH (node, p_env->mapping) {
    _mapping_t *_map = _cdio_list_node_data (node);

    if (CDIO_IN (lsn, _map->start_lsn, (_map->start_lsn + _map->sec_count - 1))) {
      int ret;
      long int img_offset = _map->img_offset;

      img_offset += (lsn - _map->start_lsn) * _map->blocksize;

      ret = cdio_stream_seek (p_env->gen.data_source, img_offset,
			      SEEK_SET);
      if (ret!=0) return ret;

      /* FIXME: Not completely sure the below is correct. */
      ret = cdio_stream_read (p_env->gen.data_source,
			      (M2RAW_SECTOR_SIZE == _map->blocksize)
			      ? (buf + CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE)
			      : buf,
			      _map->blocksize, 1);
      if (ret==0) return ret;
      break;
    }
  }

  if (!node)
    cdio_warn ("reading into pre gap (lsn %lu)", (long unsigned int) lsn);

  memcpy (data, buf + CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE,
	  b_form2 ? M2RAW_SECTOR_SIZE: CDIO_CD_FRAMESIZE);

  return 0;
}

/**
   Reads nblocks of mode2 sectors from cd device into data starting
   from lsn.
 */
static driver_return_code_t
_read_mode1_sectors_nrg (void *p_user_data, void *data, lsn_t lsn,
			 bool b_form2, unsigned nblocks)
{
  _img_private_t *p_env = p_user_data;
  int i;
  int retval;
  unsigned int blocksize = b_form2 ? M2RAW_SECTOR_SIZE : CDIO_CD_FRAMESIZE;

  for (i = 0; i < nblocks; i++) {
    if ( (retval = _read_mode1_sector_nrg (p_env,
					    ((char *)data) + (blocksize * i),
					    lsn + i, b_form2)) )
      return retval;
  }
  return 0;
}

static driver_return_code_t
_read_mode2_sector_nrg (void *p_user_data, void *data, lsn_t lsn,
			bool b_form2)
{
  _img_private_t *p_env = p_user_data;
  char buf[CDIO_CD_FRAMESIZE_RAW] = { 0, };

  CdioListNode_t *node;

  if (lsn >= p_env->size)
    {
      cdio_warn ("trying to read beyond image size (%lu >= %lu)",
		 (long unsigned int) lsn, (long unsigned int) p_env->size);
      return -1;
    }

  _CDIO_LIST_FOREACH (node, p_env->mapping) {
    _mapping_t *_map = _cdio_list_node_data (node);

    if (CDIO_IN (lsn, _map->start_lsn, (_map->start_lsn + _map->sec_count - 1))) {
      int ret;
      long int img_offset = _map->img_offset;

      img_offset += (lsn - _map->start_lsn) * _map->blocksize;

      ret = cdio_stream_seek (p_env->gen.data_source, img_offset,
			      SEEK_SET);
      if (ret!=0) return ret;
      ret = cdio_stream_read (p_env->gen.data_source,
			      (M2RAW_SECTOR_SIZE == _map->blocksize)
			      ? (buf + CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE)
			      : buf,
			      _map->blocksize, 1);
      if (ret==0) return ret;
      break;
    }
  }

  if (!node)
    cdio_warn ("reading into pre gap (lsn %lu)", (long unsigned int) lsn);

  if (b_form2)
    memcpy (data, buf + CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE,
	    M2RAW_SECTOR_SIZE);
  else
    memcpy (data, buf + CDIO_CD_XA_SYNC_HEADER, CDIO_CD_FRAMESIZE);

  return 0;
}

/**
   Reads nblocks of mode2 sectors from cd device into data starting
   from lsn.
   Returns 0 if no error.
 */
static driver_return_code_t
_read_mode2_sectors_nrg (void *p_user_data, void *data, lsn_t lsn,
			 bool b_form2, unsigned nblocks)
{
  _img_private_t *p_env = p_user_data;
  int i;
  int retval;
  unsigned int blocksize = b_form2 ? M2RAW_SECTOR_SIZE : CDIO_CD_FRAMESIZE;

  for (i = 0; i < nblocks; i++) {
    if ( (retval = _read_mode2_sector_nrg (p_env,
					    ((char *)data) + (blocksize * i),
					    lsn + i, b_form2)) )
      return retval;
  }
  return 0;
}

/*
  Free memory resources associated with NRG object.
*/
static void
_free_nrg (void *p_user_data)
{
  _img_private_t *p_env = p_user_data;

  if (NULL == p_env) return;
  if (NULL != p_env->mapping)
    _cdio_list_free (p_env->mapping, true, free);

  /* The remaining part of the image is like the other image drivers,
     so free that in the same way. */
  _free_image(p_user_data);
}

/**
  Eject media -- there's nothing to do here except free resources.
  We always return 2.
 */
static driver_return_code_t
_eject_media_nrg(void *obj)
{
  _free_nrg (obj);
  return DRIVER_OP_UNSUPPORTED;
}

#if !defined(HAVE_GLOB_H) && defined(_WIN32)
static void Win32Glob(const char* pattern, const char* szCurPath, char ***drives, unsigned int *num_files)
{
  char szPath[MAX_PATH];
  WIN32_FIND_DATAA ffd;
  HANDLE hFind;
  BOOL bFound;

  SetCurrentDirectoryA(szCurPath);

  hFind = FindFirstFileA(pattern, &ffd);
  bFound = (hFind != INVALID_HANDLE_VALUE);
  while (bFound) {
    cdio_add_device_list(drives, ffd.cFileName, num_files);
    bFound = FindNextFileA(hFind, &ffd);
  }
  if (hFind != INVALID_HANDLE_VALUE)
    FindClose(hFind);

  hFind = FindFirstFileA("*", &ffd);
  bFound = (hFind != INVALID_HANDLE_VALUE);
  while (bFound) {
    if ( (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
         (strcmp(ffd.cFileName, ".") != 0) && (strcmp(ffd.cFileName, "..") != 0) ) {
      GetFullPathNameA(ffd.cFileName, sizeof(szPath), szPath, NULL);
      Win32Glob(pattern, szPath, drives, num_files);
      SetCurrentDirectoryA(szCurPath);
    }
    bFound = FindNextFileA(hFind, &ffd);
  }
  if (hFind != INVALID_HANDLE_VALUE)
    FindClose(hFind);
}
#endif

/**
  Return an array of strings giving possible NRG disk images.
 */
char **
cdio_get_devices_nrg (void)
{
  char **drives = NULL;
  unsigned int num_files=0;
#ifdef HAVE_GLOB_H
  unsigned int i;
  glob_t globbuf;
  globbuf.gl_offs = 0;
  glob("*.nrg", GLOB_DOOFFS, NULL, &globbuf);
  for (i=0; i<globbuf.gl_pathc; i++) {
    cdio_add_device_list(&drives, globbuf.gl_pathv[i], &num_files);
  }
  globfree(&globbuf);
#elif defined(_WIN32)
  char szStartDir[MAX_PATH];
  GetCurrentDirectoryA(sizeof(szStartDir), szStartDir);
  Win32Glob("*.nrg", szStartDir, &drives, &num_files);
#else
  cdio_add_device_list(&drives, DEFAULT_CDIO_DEVICE, &num_files);
#endif /*HAVE_GLOB_H*/
  cdio_add_device_list(&drives, NULL, &num_files);
  return drives;
}

/**
  Return a string containing the default CD device.
 */
char *
cdio_get_default_device_nrg(void)
{
  char **drives = cdio_get_devices_nrg();
  char *drive = (drives[0] == NULL) ? NULL : strdup(drives[0]);
  cdio_free_device_list(drives);
  return drive;
}

static bool
get_hwinfo_nrg ( const CdIo *p_cdio, /*out*/ cdio_hwinfo_t *hw_info)
{
  strncpy(hw_info->psz_vendor, "libcdio",
	 sizeof(hw_info->psz_vendor)-1);
  hw_info->psz_vendor[sizeof(hw_info->psz_vendor)-1] = '\0';
  strncpy(hw_info->psz_model, "Nero",
	  sizeof(hw_info->psz_model)-1);
  hw_info->psz_model[sizeof(hw_info->psz_model)-1] = '\0';
  strncpy(hw_info->psz_revision, CDIO_VERSION,
	  sizeof(hw_info->psz_revision)-1);
  hw_info->psz_revision[sizeof(hw_info->psz_revision)-1] = '\0';
  return true;

}

/**
  Return the number of tracks in the current medium.
  CDIO_INVALID_TRACK is returned on error.
*/
static track_format_t
get_track_format_nrg(void *p_user_data, track_t track_num)
{
  _img_private_t *p_env = p_user_data;

  if (track_num > p_env->gen.i_tracks || track_num == 0)
    return TRACK_FORMAT_ERROR;

  if ( p_env->dtyp != DTYP_INVALID) {
    switch (p_env->dtyp) {
    case DTYP_MODE2_XA:
      return TRACK_FORMAT_XA;
    case DTYP_MODE1:
      return TRACK_FORMAT_DATA;
    default: ;
    }
  }

  /*if ( MTYP_AUDIO_CD == p_env->mtyp) return TRACK_FORMAT_AUDIO; */
  return p_env->tocent[track_num-1].track_format;
}

/**
  Return true if we have XA data (green, mode2 form1) or
  XA data (green, mode2 form2). That is track begins:
  sync - header - subheader
  12     4      -  8

  FIXME: there's gotta be a better design for this and get_track_format?
*/
static bool
_get_track_green_nrg(void *p_user_data, track_t track_num)
{
  _img_private_t *p_env = p_user_data;

  if (track_num > p_env->gen.i_tracks || track_num == 0)
    return false;

  if ( MTYP_AUDIO_CD == p_env->mtyp) return false;
  return p_env->tocent[track_num-1].track_green;
}

/**
  Check that a NRG file is valid.
*/
bool
cdio_is_nrg(const char *psz_nrg)
{
  _img_private_t *p_env  = calloc(1, sizeof (_img_private_t));
  bool is_nrg = false;

  if (p_env == NULL || psz_nrg == NULL) {
    is_nrg = false;
    goto exit;
  }

  if (!(p_env->gen.data_source = cdio_stdio_new (psz_nrg))) {
    cdio_warn ("can't open nrg image file %s for reading", psz_nrg);
    is_nrg = false;
    goto exit;
  }

  if (parse_nrg(p_env, psz_nrg, CDIO_LOG_INFO)) {
    is_nrg = true;
#ifdef ALSO_TEST_NAME
    size_t psz_len;
    psz_len = strlen(psz_nrg);
    /* At least 4 characters needed for .nrg extension */
    if ( psz_len < 4 ) {
      is_nrg = false;
      goto exit;
    }

    is_nrg = strncasecmp( psz_nrg+(psz_len-3), "nrg", 3 ) == 0;
#endif
  }
 exit:
  _free_nrg(p_env);
  return is_nrg;
}

/**
  Initialization routine. This is the only thing that doesn't
  get called via a function pointer. In fact *we* are the
  ones to set that up.
 */
CdIo *
cdio_open_am_nrg (const char *psz_source_name, const char *psz_access_mode)
{
  if (psz_access_mode != NULL && strcmp(psz_access_mode, "image"))
    cdio_warn ("there is only one access mode for nrg. Arg %s ignored",
	       psz_access_mode);
  return cdio_open_nrg(psz_source_name);
}


CdIo *
cdio_open_nrg (const char *psz_source)
{
  CdIo *ret;
  _img_private_t *_data;

  cdio_funcs_t _funcs;

  memset( &_funcs, 0, sizeof(_funcs) );

  _funcs.eject_media           = _eject_media_nrg;
  _funcs.free                  = _free_nrg;
  _funcs.get_arg               = _get_arg_image;
  _funcs.get_cdtext            = _get_cdtext_image;
  _funcs.get_cdtext_raw        = NULL;
  _funcs.get_default_device    = cdio_get_default_device_nrg;
  _funcs.get_device_fd         = NULL;
  _funcs.get_devices           = cdio_get_devices_nrg;
  _funcs.get_disc_last_lsn     = get_disc_last_lsn_nrg;
  _funcs.get_discmode          = _get_discmode_image;
  _funcs.get_drive_cap         = _get_drive_cap_image;
  _funcs.get_first_track_num   = _get_first_track_num_image;
  _funcs.get_hwinfo            = get_hwinfo_nrg;
  _funcs.get_media_changed     = get_media_changed_image;
  _funcs.get_mcn               = _get_mcn_image;
  _funcs.get_num_tracks        = _get_num_tracks_image;
  _funcs.get_track_channels    = get_track_channels_generic;
  _funcs.get_track_copy_permit = get_track_copy_permit_image;
  _funcs.get_track_format      = get_track_format_nrg;
  _funcs.get_track_green       = _get_track_green_nrg;
  _funcs.get_track_lba         = NULL; /* Will use generic routine via msf */
  _funcs.get_track_msf         = _get_track_msf_image;
  _funcs.get_track_preemphasis = get_track_preemphasis_generic;
  _funcs.get_track_pregap_lba  = get_track_pregap_lba_image;
  _funcs.get_track_isrc        = get_track_isrc_image;
  _funcs.lseek                 = _lseek_nrg;
  _funcs.read                  = _read_nrg;
  _funcs.read_audio_sectors    = _read_audio_sectors_nrg;
  _funcs.read_data_sectors     = read_data_sectors_image;
  _funcs.read_mode1_sector     = _read_mode1_sector_nrg;
  _funcs.read_mode1_sectors    = _read_mode1_sectors_nrg;
  _funcs.read_mode2_sector     = _read_mode2_sector_nrg;
  _funcs.read_mode2_sectors    = _read_mode2_sectors_nrg;
  _funcs.run_mmc_cmd           = NULL;
  _funcs.set_arg               = _set_arg_image;

  _data                   = calloc(1, sizeof (_img_private_t));
  if (_data == NULL) {
    return NULL;
  }
  _data->gen.init         = false;

  _data->gen.i_tracks     = 0;
  _data->mtyp             = 0;
  _data->dtyp             = DTYP_INVALID;
  _data->gen.i_first_track= 1;
  _data->is_dao           = false;
  _data->is_cues          = false; /* FIXME: remove is_cues. */

  ret = cdio_new ((void *)_data, &_funcs);

  if (ret == NULL) {
    free(_data);
    return NULL;
  }

  ret->driver_id = DRIVER_NRG;
  _set_arg_image(_data, "source", (NULL == psz_source)
	       ? DEFAULT_CDIO_DEVICE: psz_source);
  _set_arg_image (_data, "access-mode", "Nero");

  _data->psz_cue_name   = strdup(_get_arg_image(_data, "source"));

  if (!cdio_is_nrg(_data->psz_cue_name)) {
    cdio_debug ("source name %s is not recognized as a NRG image",
		_data->psz_cue_name);
    _free_nrg(_data);
    free(ret);
    return NULL;
  }

  if (_init_nrg(_data))
    return ret;
  else {
    _free_nrg(_data);
    free(ret);
    return NULL;
  }

}

bool
cdio_have_nrg (void)
{
  return true;
}
