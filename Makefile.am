plugin_LTLIBRARIES = libgstshvideodec.la libgstshvideodec2.la libgstshvideoenc.la libgstshvideocapenc.la libgstshfbdevsink.la

EXTRA_DIST = \
	depcomp autogen.sh \
	cntlfile/avcbencsmp.h \
	cntlfile/capture.c \
	cntlfile/capture.h \
	cntlfile/ControlFileUtil.c \
	cntlfile/ControlFileUtil.h \
	vidix/dha.h \
	vidix/fourcc.h \
	vidix/README \
	vidix/README.dha \
	vidix/sh_veu_vid.c \
	vidix/vidix.h

ACLOCAL_AMFLAGS = -I common/m4

AM_CFLAGS = -I $(srcdir)

libgstshvideodec_la_SOURCES = gstshvideodec.c
libgstshvideodec2_la_SOURCES = gstshvideodec2.c
libgstshvideoenc_la_SOURCES = gstshvideoenc.c cntlfile/ControlFileUtil.c
libgstshvideocapenc_la_SOURCES = gstshvideocapenc.c cntlfile/ControlFileUtil.c cntlfile/capture.c display.c
libgstshfbdevsink_la_SOURCES = gstshfbdevsink.c

libgstshvideodec_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS)
libgstshvideodec_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(SHCODECS_LIBS)
libgstshvideodec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 $(SHCODECS_LDFLAGS)
libgstshvideodec_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideodec2_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS) $(SHVEU_CFLAGS) $(UIOMUX_CFLAGS)
libgstshvideodec2_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(SHCODECS_LIBS) $(SHVEU_LIBS) $(UIOMUX_LDFLAGS)
libgstshvideodec2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 $(SHCODECS_LDFLAGS) $(SHVEU_LDFLAGS) $(UIOMUX_LDFLAGS)
libgstshvideodec2_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideoenc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS)
libgstshvideoenc_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(SHCODECS_LIBS)
libgstshvideoenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 $(SHCODECS_LDFLAGS)
libgstshvideoenc_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideocapenc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS) $(SHVEU_CFLAGS) $(UIOMUX_CFLAGS)
libgstshvideocapenc_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(SHCODECS_LIBS) $(SHVEU_LIBS) $(UIOMUX_LIBS)
libgstshvideocapenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 $(SHCODECS_LDFLAGS) $(SHVEU_LDFLAGS) $(UIOMUX_LDFLAGS)
libgstshvideocapenc_la_LIBTOOLFLAGS = --tag=disable-static

libgstshfbdevsink_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS) $(SHVEU_CFLAGS) $(UIOMUX_CFLAGS)
libgstshfbdevsink_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(SHCODECS_LIBS) $(SHVEU_LIBS) $(UIOMUX_LIBS)
libgstshfbdevsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10
libgstshfbdevsink_la_LIBTOOLFLAGS = --tag=disable-static


noinst_HEADERS = \
	gstshfbdevsink.h \
	gstshvideocapenc.h \
	gstshvideodec2.h \
	gstshvideodec.h \
	gstshvideoenc.h \
	display.h

check-valgrind:
	@true

check-torture:
	@true
