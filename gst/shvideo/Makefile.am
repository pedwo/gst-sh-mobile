plugin_LTLIBRARIES = libgstshvideodec.la libgstshvideodec2.la libgstshvideoenc.la libgstshvideocapenc.la libgstshfbdevsink.la

AM_CFLAGS = -I $(srcdir)

libgstshvideodec_la_SOURCES = gstshvideodec.c display.c
libgstshvideodec2_la_SOURCES = gstshvideodec2.c
libgstshvideoenc_la_SOURCES = gstshvideoenc.c ControlFileUtil.c
libgstshvideocapenc_la_SOURCES = gstshvideocapenc.c ControlFileUtil.c capture.c display.c
libgstshfbdevsink_la_SOURCES = gstshfbdevsink.c display.c

libgstshvideodec_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS) $(SHVEU_CFLAGS)
libgstshvideodec_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	$(SHCODECS_LIBS) $(SHVEU_LIBS)
libgstshvideodec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
	$(SHCODECS_LDFLAGS) $(SHVEU_LDFLAGS)
libgstshvideodec_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideodec2_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS)
libgstshvideodec2_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	$(SHCODECS_LIBS)
libgstshvideodec2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
	$(SHCODECS_LDFLAGS)
libgstshvideodec2_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideoenc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS)
libgstshvideoenc_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	$(SHCODECS_LIBS)
libgstshvideoenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lrt \
	$(SHCODECS_LDFLAGS)
libgstshvideoenc_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideocapenc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(SHCODECS_CFLAGS) $(SHVEU_CFLAGS) $(UIOMUX_CFLAGS)
libgstshvideocapenc_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	$(SHCODECS_LIBS) $(SHVEU_LIBS) $(UIOMUX_LIBS)
libgstshvideocapenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lrt \
	$(SHCODECS_LDFLAGS) $(SHVEU_LDFLAGS) $(UIOMUX_LDFLAGS)
libgstshvideocapenc_la_LIBTOOLFLAGS = --tag=disable-static

libgstshfbdevsink_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	 $(SHVEU_CFLAGS)
libgstshfbdevsink_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	$(SHVEU_LIBS)
libgstshfbdevsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lrt \
	$(SHVEU_LDFLAGS)
libgstshfbdevsink_la_LIBTOOLFLAGS = --tag=disable-static


noinst_HEADERS = \
	avcbencsmp.h \
	capture.h \
	ControlFileUtil.h \
	gstshfbdevsink.h \
	gstshvideocapenc.h \
	gstshvideodec2.h \
	gstshvideodec.h \
	gstshvideoenc.h \
	display.h
