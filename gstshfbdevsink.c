/**
 * gst-sh-mobile-fbsink
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 *
 * Takashi Namiki <takashi.namiki@renesas.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <shveu/shveu.h>

#include "gstshfbdevsink.h"

/* elementfactory information */
static const GstElementDetails gst_shfbdevsink_details =
GST_ELEMENT_DETAILS("fbdev video sink for SH-Mobile",
			"Sink/Video",
			"A linux framebuffer videosink for SH-Mobile",
			"Takashi Namiki <takashi.namiki@renesas.com>");

static GstStaticPadTemplate gst_shfbdevsink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS (
				 "video/x-raw-yuv, "
				 "format = (fourcc) NV12,"
				 "framerate = (fraction) [0, 30],"
				 "width = (int) [48, 1280],"
				 "height = (int) [48, 720]" 
				 ));

GST_DEBUG_CATEGORY_STATIC (gst_sh_video_sink_debug);
#define GST_CAT_DEFAULT gst_sh_video_sink_debug


static void gst_shfbdevsink_base_init(gpointer g_class);
static void gst_shfbdevsink_class_init(GstSHFBDEVSinkClass * klass);
static void gst_shfbdevsink_get_times(GstBaseSink * basesink,
					  GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_shfbdevsink_setcaps(GstBaseSink * bsink, GstCaps * caps);
static GstFlowReturn gst_shfbdevsink_render(GstBaseSink * bsink, GstBuffer * buff);
static gboolean gst_shfbdevsink_start(GstBaseSink * bsink);
static gboolean gst_shfbdevsink_stop(GstBaseSink * bsink);
static void gst_shfbdevsink_finalize(GObject * object);
static GstCaps *gst_shfbdevsink_getcaps(GstBaseSink * bsink);
static GstVideoSinkClass *parent_class = NULL;

static void gst_shfbdevsink_base_init(gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details(element_class, &gst_shfbdevsink_details);
	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get(&gst_shfbdevsink_template_factory));
}

static void
gst_shfbdevsink_get_times(GstBaseSink * basesink, GstBuffer * buffer,
			  GstClockTime * start, GstClockTime * end)
{
	GstSHFBDEVSink *fbdevsink;

	fbdevsink = GST_SHFBDEVSINK(basesink);

	if (GST_BUFFER_TIMESTAMP_IS_VALID(buffer)) {
		*start = GST_BUFFER_TIMESTAMP(buffer);
		if (GST_BUFFER_DURATION_IS_VALID(buffer)) {
			*end = *start + GST_BUFFER_DURATION(buffer);
		} else {
			if (fbdevsink->fps_n > 0) {
				*end = *start +
					gst_util_uint64_scale_int(GST_SECOND, fbdevsink->fps_d,
								  fbdevsink->fps_n);
			}
		}
	}
}

static GstCaps *gst_shfbdevsink_getcaps(GstBaseSink * bsink)
{
	return gst_caps_copy (gst_pad_get_pad_template_caps (bsink->sinkpad));
}

static gboolean gst_shfbdevsink_setcaps(GstBaseSink * bsink, GstCaps * vscapslist)
{
	GstSHFBDEVSink *fbdevsink;
	GstStructure *structure;
	const GValue *fps;

	fbdevsink = GST_SHFBDEVSINK(bsink);

	structure = gst_caps_get_structure(vscapslist, 0);

	fps = gst_structure_get_value(structure, "framerate");
	fbdevsink->fps_n = gst_value_get_fraction_numerator(fps);
	fbdevsink->fps_d = gst_value_get_fraction_denominator(fps);

	gst_structure_get_int(structure, "width", &fbdevsink->width);
	gst_structure_get_int(structure, "height", &fbdevsink->height);

	/* calculate centering and scanlengths for the video */
	fbdevsink->bytespp = fbdevsink->fixinfo.line_length / fbdevsink->varinfo.xres;

	fbdevsink->cx = ((int) fbdevsink->varinfo.xres - fbdevsink->width) / 2;
	if (fbdevsink->cx < 0)
		fbdevsink->cx = 0;

	fbdevsink->cy = ((int) fbdevsink->varinfo.yres - fbdevsink->height) / 2;
	if (fbdevsink->cy < 0)
		fbdevsink->cy = 0;

	fbdevsink->linelen = fbdevsink->width * fbdevsink->bytespp;
	if (fbdevsink->linelen > fbdevsink->fixinfo.line_length)
		fbdevsink->linelen = fbdevsink->fixinfo.line_length;

	fbdevsink->lines = fbdevsink->height;
	if (fbdevsink->lines > fbdevsink->varinfo.yres)
		fbdevsink->lines = fbdevsink->varinfo.yres;

	return TRUE;
}

static void *launch_render_thread(void *data)
{
	GstSHFBDEVSink *fbdevsink = (GstSHFBDEVSink *)data;
	unsigned char *fb_screenMem = NULL;

	while(1)
	{
		pthread_mutex_lock(&fbdevsink->rendar_mutex);
		if (fbdevsink->pan_ioctl_present == TRUE)
		{
			if (fbdevsink->varinfo.yoffset == fbdevsink->varinfo.yres) {
				fb_screenMem = (unsigned char *)fbdevsink->fixinfo.smem_start;
				fbdevsink->varinfo.xoffset = 0;
				fbdevsink->varinfo.yoffset = 0;
			} else {
				fbdevsink->varinfo.xoffset = 0;
				fbdevsink->varinfo.yoffset = fbdevsink->varinfo.yres;
				fb_screenMem = (unsigned char *)fbdevsink->fixinfo.smem_start + 
		            (fbdevsink->varinfo.yres * fbdevsink->varinfo.xres * (fbdevsink->varinfo.bits_per_pixel / 8));
				fbdevsink->varinfo.yoffset = fbdevsink->varinfo.yres;
			}
		} else {
			fb_screenMem = (unsigned char *)fbdevsink->fixinfo.smem_start;
		}

		shveu_operation(  fbdevsink->shveu,
			  (unsigned long)fbdevsink->buf->data, (unsigned long)fbdevsink->buf->offset, fbdevsink->width, 
	          fbdevsink->height, fbdevsink->width, SHVEU_YCbCr420,
			  (unsigned long)fb_screenMem, 0, fbdevsink->varinfo.xres, fbdevsink->varinfo.yres, 
	          fbdevsink->varinfo.xres, SHVEU_RGB565, SHVEU_NO_ROT);

		if (fbdevsink->pan_ioctl_present == TRUE)
		{
			if (-1 == ioctl(fbdevsink->fd, FBIOPAN_DISPLAY, &fbdevsink->varinfo))
			{
				printf("FBDEV: ioctl failed\n");
		        fbdevsink->count--;
				return NULL;
			}
		}
		fbdevsink->count--;
	}
	return NULL;
}

static GstFlowReturn gst_shfbdevsink_render(GstBaseSink * bsink, GstBuffer * buf)
{
	GstSHFBDEVSink *fbdevsink;

	fbdevsink = GST_SHFBDEVSINK (bsink);
	fbdevsink->buf = buf; 

	while(fbdevsink->count > 0)
	{
		usleep(1000);
	}
	pthread_mutex_unlock(&fbdevsink->rendar_mutex);

	fbdevsink->count++;

	return GST_FLOW_OK;
}

static gboolean gst_shfbdevsink_start(GstBaseSink * bsink)
{
	GstSHFBDEVSink *fbdevsink;

	fbdevsink = GST_SHFBDEVSINK(bsink);

	if (!fbdevsink->device) {
		fbdevsink->device = g_strdup("/dev/fb0");
	}

	fbdevsink->fd = open(fbdevsink->device, O_RDWR);

	if (fbdevsink->fd == -1)
		return FALSE;

	/* get the fixed screen info */
	if (ioctl(fbdevsink->fd, FBIOGET_FSCREENINFO, &fbdevsink->fixinfo))
		return FALSE;

	/* get the variable screen info */
	if (ioctl(fbdevsink->fd, FBIOGET_VSCREENINFO, &fbdevsink->varinfo))
		return FALSE;

	fbdevsink->count = 0;

	fbdevsink->shveu = shveu_open();

/* Check to see if the LCDC driver has the PAN IOCTL */
	fbdevsink->pan_ioctl_present = TRUE;
	if (-1 == ioctl(fbdevsink->fd, FBIOPAN_DISPLAY, &fbdevsink->varinfo)) {
		printf("fbdev PAN ioctl not present\n");
		fbdevsink->pan_ioctl_present = FALSE;
	}

	pthread_mutex_init(&fbdevsink->rendar_mutex, NULL);
	pthread_mutex_lock(&fbdevsink->rendar_mutex);
	pthread_create(&fbdevsink->rendar_thread, NULL, launch_render_thread, fbdevsink);

	return TRUE;
}

static gboolean gst_shfbdevsink_stop(GstBaseSink * bsink)
{
	GstSHFBDEVSink *fbdevsink;

	fbdevsink = GST_SHFBDEVSINK(bsink);

	fbdevsink->varinfo.xoffset = 0;
	fbdevsink->varinfo.yoffset = 0;

	/* Restore the framebuffer to the front buffer */
	if (-1 == ioctl(fbdevsink->fd, FBIOPAN_DISPLAY, &fbdevsink->varinfo)) {

	}

	if (close(fbdevsink->fd))
		return FALSE;

	shveu_close();
	pthread_cancel(fbdevsink->rendar_thread);
	pthread_mutex_destroy(&fbdevsink->rendar_mutex);

	return TRUE;
}


static gboolean plugin_init(GstPlugin * plugin)
{
	if (!gst_element_register(plugin, "shfbdevsink", GST_RANK_NONE, GST_TYPE_SHFBDEVSINK))
		return FALSE;

	return TRUE;
}

static void gst_shfbdevsink_class_init(GstSHFBDEVSinkClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
	GstBaseSinkClass *gstvs_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	gstvs_class = (GstBaseSinkClass *) klass;

	GST_DEBUG_CATEGORY_INIT (gst_sh_video_sink_debug, 
				 "shfbdevsink",
				 0, "SH framebuffer sink");

	parent_class = g_type_class_peek_parent(klass);

	gobject_class->finalize = gst_shfbdevsink_finalize;

	gstvs_class->set_caps = GST_DEBUG_FUNCPTR(gst_shfbdevsink_setcaps);
	gstvs_class->get_caps = GST_DEBUG_FUNCPTR(gst_shfbdevsink_getcaps);
	gstvs_class->get_times = GST_DEBUG_FUNCPTR(gst_shfbdevsink_get_times);
	gstvs_class->preroll = GST_DEBUG_FUNCPTR(gst_shfbdevsink_render);
	gstvs_class->render = GST_DEBUG_FUNCPTR(gst_shfbdevsink_render);
	gstvs_class->start = GST_DEBUG_FUNCPTR(gst_shfbdevsink_start);
	gstvs_class->stop = GST_DEBUG_FUNCPTR(gst_shfbdevsink_stop);

}

static void gst_shfbdevsink_finalize(GObject * object)
{
	GstSHFBDEVSink *fbdevsink = GST_SHFBDEVSINK(object);

	g_free(fbdevsink->device);

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

GType gst_shfbdevsink_get_type(void)
{
	static GType fbdevsink_type = 0;

	if (!fbdevsink_type) {
		static const GTypeInfo fbdevsink_info = {
			sizeof(GstSHFBDEVSinkClass),
			gst_shfbdevsink_base_init,
			NULL,
			(GClassInitFunc) gst_shfbdevsink_class_init,
			NULL,
			NULL,
			sizeof(GstSHFBDEVSink),
			0,
			NULL
		};

		fbdevsink_type =
			g_type_register_static(GST_TYPE_BASE_SINK, "GstSHFBDEVSink", &fbdevsink_info,0);
	}
	return fbdevsink_type;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
		  GST_VERSION_MINOR,
		  "shfbdevsink",
		  "linux framebuffer video sink for SH-Mobile",
		  plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

