if ENABLE_BLEND
BLEND_PLUGINS = libgstshvideomixer.la
endif

plugin_LTLIBRARIES = libgstshvideo.la $(BLEND_PLUGINS)

AM_CFLAGS = -I $(srcdir)

libgstshvideo_la_SOURCES = gstshvideoplugin.c gstshvideodec.c gstshvideoenc.c gstshvideosink.c gstshvideocapenc.c \
	gstshvideoresize.c ControlFileUtil.c gstshvideobuffer.c display.c capture.c thrqueue.c

libgstshvideo_la_CFLAGS = $(GST_CFLAGS) \
	$(SHCODECS_CFLAGS) $(SHVEU_CFLAGS) $(UIOMUX_CFLAGS)
libgstshvideo_la_LIBADD = $(GST_LIBS) \
	$(SHCODECS_LIBS) $(SHVEU_LIBS) $(UIOMUX_LIBS)
libgstshvideo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lrt -lgstvideo-0.10 \
	$(SHCODECS_LDFLAGS) $(SHVEU_LDFLAGS) $(UIOMUX_LDFLAGS)
libgstshvideo_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideomixer_la_SOURCES = shvideomixer.c gstshvideobuffer.c

libgstshvideomixer_la_CFLAGS = $(GST_CFLAGS) \
	$(SHBEU_CFLAGS) $(UIOMUX_CFLAGS)
libgstshvideomixer_la_LIBADD = $(GST_LIBS) \
	$(SHBEU_LIBS) $(UIOMUX_LIBS)
libgstshvideomixer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lgstvideo-0.10 \
	$(SHBSEU_LDFLAGS) $(UIOMUX_LDFLAGS)
libgstshvideomixer_la_LIBTOOLFLAGS = --tag=disable-static

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
