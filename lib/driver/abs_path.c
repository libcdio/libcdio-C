/*
  To compile as a standalone program:
  gcc -Wall -g -I../.. -DHAVE_CONFIG_H -DSTANDALONE -o abs_path abs_path.c
*/
#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif
#include "cdio_private.h"

#ifdef HAVE_STRING_H
# include <string.h>
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_STDIO_H
# include <stdio.h>
#endif

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#ifndef NULL
# define NULL 0
#endif

#ifndef CDIO_FILE_SEPARATOR
# define CDIO_FILE_SEPARATOR '/'
#endif

/* Windows takes backslashes as well as slashes in paths, and a drive
   letter starts an absolute path there. */
#if defined(_WIN32) || defined(__CYGWIN__)
# define CDIO_FILE_ALT_SEPARATOR '\\'
# define has_drive_letter(p) \
    ((((p)[0] >= 'A' && (p)[0] <= 'Z') || ((p)[0] >= 'a' && (p)[0] <= 'z')) \
     && (p)[1] == ':')
#else
# define has_drive_letter(p) 0
#endif

#ifdef CDIO_FILE_ALT_SEPARATOR
# define isdirsep(x) ((x) == CDIO_FILE_SEPARATOR || (x) == CDIO_FILE_ALT_SEPARATOR)
#else
# define isdirsep(x) ((x) == CDIO_FILE_SEPARATOR)
#endif

#define skipprefix(path) (path)

#if !defined(CharNext) || defined(_MSC_VER) /* defined as CharNext[AW] on Windows. */
# undef CharNext
# define CharNext(p) ((p) + 1)
#endif

static char *
strrdirsep(const char *path)
{
    char *last = NULL;
    while (*path) {
	if (isdirsep(*path)) {
	    const char *tmp = path++;
	    while (isdirsep(*path)) path++;
	    if (!*path) break;
	    last = (char *)tmp;
	}
	else {
	    path = CharNext(path);
	}
    }
    return last;
}

char *
cdio_dirname(const char *fname)
{
    const char *p;
    p = strrdirsep(fname);
    if (!p) return strdup(".");
    return strndup(fname, p - fname);
}

/* If fname isn't absolute, add cwd to it. */
char *
cdio_abspath(const char *cwd, const char *fname)
{
    if (isdirsep(*fname) || has_drive_letter(fname))
      return strdup(fname);
    else {
      size_t len   = strlen(cwd) + strlen(fname) + 2;
      char* result = calloc(sizeof(char), len);
      snprintf(result, len, "%s%c%s",
	       cwd, CDIO_FILE_SEPARATOR, fname);
      return result;
    }
}

#ifdef STANDALONE
int main(int argc, char **argv)
{
  char *dest;
  char *dirname;
  if (argc != 3) {
    fprintf(stderr, "Usage: %s FILE REPLACE_BASENAME\n", argv[0]);
    fprintf(stderr,
     "       Make PATH absolute\n");
    exit(1);
  }

  dirname = cdio_dirname(argv[1]);
  dest = cdio_abspath(dirname, argv[2]);
  printf("%s -> %s\n", argv[1], dest);
  free(dirname);
  free(dest);
  exit(0);
}
#endif
