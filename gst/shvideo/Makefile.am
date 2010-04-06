plugin_LTLIBRARIES = libgstshvideo.la

AM_CFLAGS = -I $(srcdir)

libgstshvideo_la_SOURCES = gstshvideoplugin.c gstshvideodec.c gstshvideoenc.c gstshvideosink.c gstshvideocapenc.c \
	ControlFileUtil.c gstshvideobuffer.c display.c capture.c thrqueue.c

libgstshvideo_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS) $(SHVEU_CFLAGS) $(UIOMUX_CFLAGS)
libgstshvideo_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	$(SHCODECS_LIBS) $(SHVEU_LIBS) $(UIOMUX_LIBS)
libgstshvideo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lrt -lgstvideo-0.10 \
	$(SHCODECS_LDFLAGS) $(SHVEU_LDFLAGS) $(UIOMUX_LDFLAGS)
libgstshvideo_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	avcbencsmp.h \
	thrqueue.h \
	capture.h \
	ControlFileUtil.h \
	gstshvideocapenc.h \
	gstshvideodec.h \
	gstshvideoenc.h \
	display.h
