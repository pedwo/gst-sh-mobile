OUR_SOURCES=
OUR_CFLAGS=
OUR_LIBS=
OUR_LDFLAGS=

if ENABLE_SCALE
OUR_SOURCES += gstshvideoresize.c
OUR_CFLAGS += $(SHVEU_CFLAGS)
OUR_LIBS += $(SHVEU_LIBS)
OUR_LDFLAGS += $(SHVEU_LDFLAGS)
endif

if ENABLE_BLEND
OUR_SOURCES += shvideomixer.c
OUR_CFLAGS += $(SHBEU_CFLAGS)
OUR_LIBS += $(SHBEU_LIBS)
OUR_LDFLAGS += $(SHBEU_LDFLAGS)
endif

plugin_LTLIBRARIES = libgstshvideo.la

AM_CFLAGS = -I $(srcdir)

libgstshvideo_la_SOURCES = gstshvideoplugin.c gstshvideodec.c gstshvideoenc.c gstshvideosink.c gstshvideocapenc.c \
	ControlFileUtil.c gstshvideobuffer.c display.c capture.c thrqueue.c $(OUR_SOURCES)

libgstshvideo_la_CFLAGS = $(GST_CFLAGS) \
	$(SHCODECS_CFLAGS) $(SHVEU_CFLAGS) $(OUR_CFLAGS) $(UIOMUX_CFLAGS)
libgstshvideo_la_LIBADD = $(GST_LIBS) \
	$(SHCODECS_LIBS) $(SHVEU_LIBS) $(OUR_LIBS) $(UIOMUX_LIBS)
libgstshvideo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lrt -lgstvideo-0.10 \
	$(SHCODECS_LDFLAGS) $(SHVEU_LDFLAGS) $(OUR_LDFLAGS) $(UIOMUX_LDFLAGS)
libgstshvideo_la_LIBTOOLFLAGS = --tag=disable-static


noinst_HEADERS = \
	avcbencsmp.h \
	thrqueue.h \
	capture.h \
	ControlFileUtil.h \
	gstshvideobuffer.h \
	gstshvideocapenc.h \
	gstshvideodec.h \
	gstshvideoenc.h \
	gstshencdefaults.h \
	gstshvideoresize.h \
	gstshvideosink.h \
	shvideomixer.h \
	shvideomixerpad.h \
	display.h
