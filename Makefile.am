#   Copyright (C) 2003-2004, 2006, 2008, 2011-2013, 2016, 2019, 2023-2025
#   Rocky Bernstein <rocky@gnu.org>
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
## Process this file with automake to produce Makefile.in
##   which configure then turns into a Makefile  ...
##     which make can then use to produce stuff. Isn't configuration simple?

.PHONY: ChangeLog-without-corrections check-leaks check-short rmChangeLog test

AUTOMAKE_OPTIONS = dist-bzip2

EXTRA_DIST = \
	.vs/cd-info.vcxproj \
	.vs/cd-info.vcxproj.filters \
	.vs/config.h \
	.vs/libcdio.vcxproj \
	.vs/libcdio.vcxproj.filters \
	.vs/README.txt \
	.vs/set_version.ps1 \
	NEWS.md \
	README.md \
	README-libcdio.md \
	THANKS \
	example/README \
	libcdio++.pc.in \
	libcdio.pc.in \
	libcdio.sln \
	libiso9660++.pc.in \
	libiso9660.pc.in \
	libudf.pc.in \
	make-check-filter.pl \
	make-check-filter.rb \
	package/libcdio.spec.in \
	test/README.multiextent \
	$(wildcard test/data/*.bin) \
	$(wildcard test/data/*.cue) \
	$(wildcard test/data/*.iso) \
	$(wildcard test/*.right)

SUBDIRS = doc include lib src test example

# pkg-config(1) related rules
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libcdio.pc  \
	         libiso9660.pc \
	         libudf.pc

if ENABLE_CXX_BINDINGS
pkgconfig_DATA += \
	libcdio++.pc \
	libiso9660++.pc
endif


$(pkgconfig_DATA): config.status

# List of additional files for expanded regression tests
DISTFILES_REGRESSION = tests/monvoisin.nrg  tests/monvoisin.right \
		       tests/svcdgs.nrg  tests/svcdgs.nrg \
	               tests/svcd_ogt_test_ntsc.bin \
	               tests/svcd_ogt_test_ntsc.cue \
	               tests/svcd_ogt_test_ntsc.right \
		       tests/vcd_demo.bin tests/vcd_demo.cue \
		       tests/vcd_demo.right

REGRESSION_VERSION = 1.1

distdir_regression = ../$(PACKAGE)-$(REGRESSION_VERSION)-tests

#: run regression tests. "test" is the same thing as "check"
test: check

#: Run all tests without bloated output
check-short:
	$(MAKE) check 2>&1  | ruby @abs_top_srcdir@/make-check-filter.rb

#: run valgrind on C programs
check-leaks:
	$(MAKE) -C test/driver check-leaks && \
	$(MAKE) -C example check-leaks

#: Make documentation via Doxygen http://www.stack.nl/~dimitri/doxygen/
doxygen:
	-( cd ${top_srcdir}/doc/doxygen && /bin/sh ${srcdir}/run_doxygen )

dist-regression: distdir-regression
	cd $(distdir) && $(AMTAR) chof - tests | GZIP=$(GZIP_ENV) gzip -c >$(distdir_regression).tar.gz
	$(am__remove_distdir)

distdir-regression: $(DISTFILES_REGRESSION)
	$(am__remove_distdir)
	mkdir $(distdir)
	@list='$(DISTFILES_REGRESSION)'; for file in $$list; do \
	  if test -f $$file || test -d $$file; then d=.; else d=$(srcdir); fi; \
	  dir=`echo "$$file" | sed -e 's,/[^/]*$$,,'`; \
	  if test "$$dir" != "$$file" && test "$$dir" != "."; then \
	    dir="/$$dir"; \
	    $(mkinstalldirs) "$(distdir)$$dir"; \
	  else \
	    dir=''; \
	  fi; \
	  if test -d $$d/$$file; then \
	    if test -d $(srcdir)/$$file && test $$d != $(srcdir); then \
	      cp -pR $(srcdir)/$$file $(distdir)$$dir || exit 1; \
	    fi; \
	    cp -pR $$d/$$file $(distdir)$$dir || exit 1; \
	  else \
	    test -f $(distdir_regression)/$$file \
	    || cp -p $$d/$$file $(distdir)/$$file \
	    || exit 1; \
	  fi; \
	done
	-find $(distdir) -type d ! -perm -755 -exec chmod a+rwx {} \; -o \
	  ! -type d ! -perm -444 -links 1 -exec chmod a+r {} \; -o \
	  ! -type d ! -perm -400 -exec chmod a+r {} \; -o \
	  ! -type d ! -perm -444 -exec $(SHELL) $(install_sh) -c -m a+r {} {} \; \
	|| chmod -R a+r $(distdir)

check_nrg.sh: $(top_builddir)/config.status check_nrg.sh.in
	cd $(top_builddir) && $(SHELL) ./config.status $(subdir)/$@
	chmod +x config_nrg.sh
check_cue.sh: $(top_builddir)/config.status check_cue.sh.in
	cd $(top_builddir) && $(SHELL) ./config.status $(subdir)/$@
	chmod +x config_cue.sh

check_iso.sh: $(top_builddir)/config.status check_iso.sh.in
	cd $(top_builddir) && $(SHELL) ./config.status $(subdir)/$@
	chmod +x config_iso.sh

MAINTAINERCLEANFILES = ChangeLog *.rej *.orig

if MAINTAINER_MODE

GIT2CL ?= git2cl

#: Remove ChangeLog
rmChangeLog:
	rm ChangeLog || true

#: Create ChangeLog from version control without corrections
ChangeLog-without-corrections:
	git log --pretty --numstat --summary | $(GIT2CL) >ChangeLog

#: Create ChangeLog from version control with additonal corrections
ChangeLog: ChangeLog-without-corrections
	patch ChangeLog < ChangeLog-spell-corrected.diff

ACLOCAL_AMFLAGS=-I m4

endif
