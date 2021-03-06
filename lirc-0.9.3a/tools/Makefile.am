## Process this file with automake to produce Makefile.in
ACLOCAL_AMFLAGS         = -I m4

PYLINT                  = python3-pylint

AM_CPPFLAGS             = -I$(top_srcdir)/lib -I$(top_srcdir) @X_CFLAGS@
AM_CPPFLAGS             += -g -Wall -Wp,-D_FORTIFY_SOURCE=2
AM_LDFLAGS              =  -L../lib -llirc -llirc_client -ldl

if NEED_PYTHON3
BUILT_SOURCES           = python3
endif

LIRC_LIBS               = ../lib/liblirc.la ../lib/liblirc_client.la

EXTRA_PROGRAMS          =

if BUILD_XTOOLS
EXTRA_PROGRAMS          += xmode2 irxevent
endif

sbin_PROGRAMS           = lirc-lsplugins
bin_PROGRAMS            = ircat \
                          irexec \
                          irpipe \
                          irpty \
                          irrecord \
                          irsend \
                          irsimreceive \
                          irsimsend \
                          irtestcase \
                          irw \
                          lirc-lsremotes \
                          mode2 \
                          @x_progs@

## Simple programs
irw_SOURCES             = irw.cpp
irpty_SOURCES           = irpty.cpp
irpty_LDADD             = @forkpty@ -lpthread $(LIRC_LIBS)
irexec_SOURCES          = irexec.cpp
irexec_LDADD            = @daemon@ $(LIRC_LIBS)
ircat_SOURCES           = ircat.cpp
ircat_LDADD             = $(LIRC_LIBS)
irpipe_SOURCES		= irpipe.cpp
irpipe_CPPFLAGS         = -I$(top_srcdir)/include \
                          -I$(top_srcdir)/drivers/irpipe $(AM_CPPFLAGS)
irrecord_SOURCES        = irrecord-ui.cpp
irrecord_LDADD          = $(LIRC_LIBS)  ../lib/libirrecord.la
irsimsend_SOURCES       = irsimsend.cpp
irsimsend_LDADD         = $(LIRC_LIBS)
irsimreceive_SOURCES    = irsimreceive.cpp
irsimreceive_LDADD      = $(LIRC_LIBS)
mode2_SOURCES           = mode2.cpp
mode2_LDADD             = $(LIRC_LIBS)
irtestcase_SOURCES      = irtestcase.cpp
irtestcase_LDADD        = $(LIRC_LIBS)
irsend_SOURCES          = irsend.cpp
irsend_LDADD            = $(LIRC_LIBS)
lirc_lsplugins_SOURCES  = lirc-lsplugins.cpp
lirc_lsplugins_LDADD    = $(LIRC_LIBS)
lirc_lsremotes_SOURCES  = lirc-lsremotes.cpp
lirc_lsremotes_LDADD    = $(LIRC_LIBS)


## X programs
irxevent_SOURCES        = irxevent.cpp
irxevent_LDADD          = @X_LIBS@ @X_PRE_LIBS@ -lX11 @X_EXTRA_LIBS@ \
                          $(LIRC_LIBS)
xmode2_SOURCES          = xmode2.cpp
xmode2_LDADD            = @X_LIBS@ @X_PRE_LIBS@ -lX11 @X_EXTRA_LIBS@ \
                          $(LIRC_LIBS)

dist_bin_SCRIPTS        = lirc-config-tool


if NEED_PYTHON3
dist_bin_SCRIPTS        += python3
endif

if HAVE_PYTHON
dist_bin_SCRIPTS        += pronto2lirc irdb-get
dist_noinst_SCRIPTS     = make_rel_symlink.py check_configs.py
pkgpython_PYTHON        = lirc-setup/mvc_control.py \
                          lirc-setup/mvc_view.py \
                          lirc-setup/mvc_model.py \
                          lirc-setup/choosers.py \
                          lirc-setup/baseview.py
dist_pkgpython_SCRIPTS  = lirc-setup/lirc-setup
dist_pkgpython_DATA     = lirc-setup/lirc-setup.ui
endif

pylint: irdb-get $(pkgpython_PYTHON)
	PYTHONPATH=./lirc-setup $(PYLINT) --rcfile=pylint.conf \
	--msg-template='{path}:{line}: [{msg_id}({symbol}), {obj}] {msg}' \
	$?

pep8:   irdb-get $(pkgpython_PYTHON)
	python3-pep8 --config=pep8.conf $?

python3:
	echo "#!/bin/sh" > $@
	echo "exec $(PYTHON) \$$@" >> $@
	chmod 755 $@

install-exec-hook:
	$(SED) -i 's/@version@/$(VERSION)/' $(DESTDIR)$(bindir)/lirc-config-tool
	rm -f $(DESTDIR)/$(bindir)/lirc-setup
	$(srcdir)/make_rel_symlink.py \
	    $(DESTDIR)/$(pkgpythondir)/lirc-setup \
	    $(DESTDIR)/$(bindir)/lirc-setup

install-data-hook:
	rm -f $(DESTDIR)/$(pkgpythondir)/configs
	$(srcdir)/make_rel_symlink.py \
	    $(DESTDIR)$(pkgdatadir)/configs \
	    $(DESTDIR)$(pkgpythondir)/configs

uninstall-hook:
	rm -f $(DESTDIR)/$(bindir)/lirc-setup \
	      $(DESTDIR)/$(pkgpythondir)/configs

checkfiles:
	../git-tools/checkfiles $(SOURCES)


