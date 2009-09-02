plugin_LTLIBRARIES = libgstshvideodec.la libgstshvideodec2.la libgstshvideoenc.la libgstshvideocapenc.la

EXTRA_DIST = \
	depcomp autogen.sh 

ACLOCAL_AMFLAGS = -I common/m4

libgstshvideodec_la_SOURCES = gstshvideodec.c
libgstshvideodec2_la_SOURCES = gstshvideodec2.c
libgstshvideoenc_la_SOURCES = gstshvideoenc.c cntlfile/ControlFileUtil.c
libgstshvideocapenc_la_SOURCES = gstshvideocapenc.c cntlfile/ControlFileUtil.c cntlfile/capture.c

libgstshvideodec_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(LIBSHCODECS_CFLAGS)
libgstshvideodec_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(LIBSHCODECS_LIBS)
libgstshvideodec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 -lshcodecs
libgstshvideodec_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideodec2_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(LIBSHCODECS_CFLAGS)
libgstshvideodec2_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(LIBSHCODECS_LIBS)
libgstshvideodec2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 -lshcodecs -lshveu
libgstshvideodec2_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideoenc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(LIBSHCODECS_CFLAGS)
libgstshvideoenc_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(LIBSHCODECS_LIBS)
libgstshvideoenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 -lshcodecs
libgstshvideoenc_la_LIBTOOLFLAGS = --tag=disable-static


libgstshvideocapenc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(LIBSHCODECS_CFLAGS)
libgstshvideocapenc_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(LIBSHCODECS_LIBS)
libgstshvideocapenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 -lshcodecs -lshveu
libgstshvideocapenc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = 

check-valgrind:
	@true

check-torture:
	@true
