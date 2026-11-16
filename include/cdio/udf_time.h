/*
    Copyright (C) 2005, 2008, 2012 Rocky Bernstein <rocky@gnu.org>

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
 * \file udf_time.h
 *
 * \brief UDF time conversion and access files.
 *
*/

#ifndef UDF_TIME_H
#define UDF_TIME_H

#include <time.h>

#if defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR) && !defined(__struct_timespec_defined)
struct timespec {
  time_t  tv_sec;   /* Seconds */
  long    tv_nsec;  /* Nanoseconds */
};
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /**
    Return the access time of the file, or -1 if the timestamp isn't
    valid or doesn't fit in a time_t.
  */
  time_t udf_get_access_time(const udf_dirent_t *p_udf_dirent);

  /**
    Return the attribute (most recent create or access) time of the
    file, or -1 if the timestamp isn't valid or doesn't fit in a
    time_t.
  */
  time_t udf_get_attribute_time(const udf_dirent_t *p_udf_dirent);

  /**
    Return the modification time of the file, or -1 if the timestamp
    isn't valid or doesn't fit in a time_t.
  */
  time_t udf_get_modification_time(const udf_dirent_t *p_udf_dirent);

  /**
    Convert a UDF timestamp to a time_t. If microseconds are desired,
    use dest_usec. The return value is the same as dest, or NULL if the
    timestamp isn't valid or the time doesn't fit in a time_t, in which
    case *dest and *dest_usec are set to -1. A timestamp isn't valid if
    its type is reserved (3 to 15) or a field is out of the range ECMA
    167 1/7.3 gives it: a year outside 1-9999, a month outside 1-12, a
    day outside 1-31, an hour above 23, a minute above 59, a second
    above 59 (60 with type 2), or a fraction of a second above 99. A
    time zone offset outside -1440 to 1440 minutes is taken as not
    specified, meaning UTC. */
  time_t *udf_stamp_to_time(time_t *dest, long int *dest_usec,
			  const udf_timestamp_t src);

  /**
    Convert a time to a UDF timestamp in local time, recording the
    offset of the local time zone from UTC, or -2047 and UTC if the
    offset isn't known. The return value is the same as dest, or NULL
    if dest is NULL, ts.tv_nsec is out of range or the date is outside
    the years 1 to 9999. */
  udf_timestamp_t *udf_timespec_to_stamp(const struct timespec ts,
					 udf_timestamp_t *dest);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*UDF_TIME_H*/
