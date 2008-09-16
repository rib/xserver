bin_PROGRAMS = Xclutter
noinst_LIBRARIES = libfbcmap.a

AM_CFLAGS = -DHAVE_DIX_CONFIG_H \
            -DNO_HW_ONLY_EXTS \
            -DNO_MODULE_EXTS \
            $(XCLUTTERMODULES_CFLAGS) \
	    $(DIX_CFLAGS)

SRCS =	xclutter-input.c \
	xclutter-output.c \
	xclutter.c \
	xclutter.h \
	xclutter-xnest-client.c \
	xclutter-xnest-client.h \
	$(top_srcdir)/Xext/dpmsstubs.c \
	$(top_srcdir)/Xi/stubs.c \
	$(top_srcdir)/mi/miinitext.c

#	xclutter-xnest-server.c
#	xclutter-xnest-server.h

libfbcmap_a_CFLAGS = $(AM_CFLAGS)
libfbcmap_a_SOURCES = $(top_srcdir)/fb/fbcmap_mi.c

Xclutter_SOURCES = $(SRCS)
#Xclutter_CFLAGS = @XCLUTTER_CFLAGS@ $(AM_CFLAGS)
Xclutter_CFLAGS = $(AM_CFLAGS)

XCLUTTER_LIBS = \
        @XCLUTTER_LIBS@ \
	libfbcmap.a \
	$(XSERVER_LIBS)

Xclutter_LDADD = $(XCLUTTER_LIBS) $(XCLUTTER_SYS_LIBS) $(XSERVER_SYS_LIBS)

# Man page
include $(top_srcdir)/cpprules.in

appmandir = $(APP_MAN_DIR)

appman_PRE = Xclutter.man
appman_DATA = $(appman_PRE:man=@APP_MAN_SUFFIX@)

BUILT_SOURCES = $(appman_PRE)
CLEANFILES = $(appman_PRE) $(appman_DATA)

SUFFIXES += .$(APP_MAN_SUFFIX) .man

.man.$(APP_MAN_SUFFIX):
	-rm -f $@
	$(LN_S) $< $@

EXTRA_DIST = Xclutter.man.pre

relink:
	rm -f Xvfb && $(MAKE) Xvfb

