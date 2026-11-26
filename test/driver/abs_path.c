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

char *cdio_abspath(const char *cwd, const char *fname);
char *cdio_dirname(const char *fname);

static int
check(const char *cue_filename, const char *data_filename, const char *expect)
{
  char *dirname = cdio_dirname(cue_filename);
  char *dest = cdio_abspath(dirname, data_filename);
  int ok = 0 == strcmp(expect, dest);

  if (!ok)
    fprintf(stderr, "Incorrect: %s next to %s: expecting %s, got %s.\n",
            data_filename, cue_filename, expect, dest);
  free(dirname);
  free(dest);
  return ok;
}

int
main(int argc, const char *argv[])
{
  int rc=0;

  if (!check("/tmp/foo.bar", "baz", "/tmp/baz")) rc=1;
  if (!check("foo.bar", "baz", "./baz")) rc=2;
  if (!check("/tmp/foo.bar", "/data/baz", "/data/baz")) rc=3;
#if defined(_WIN32) || defined(__CYGWIN__)
  /* Windows paths may use backslashes and start with a drive letter. */
  if (!check("C:\\tmp\\foo.bar", "baz", "C:\\tmp/baz")) rc=4;
  if (!check("C:\\tmp/sub\\foo.bar", "baz", "C:\\tmp/sub/baz")) rc=5;
  if (!check("C:\\tmp\\foo.bar", "D:\\data\\baz", "D:\\data\\baz")) rc=6;
  if (!check("C:\\tmp\\foo.bar", "\\data\\baz", "\\data\\baz")) rc=7;
#endif
  exit(rc);
}
