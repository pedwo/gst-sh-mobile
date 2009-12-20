/**
 * gst-sh-mobile-dec-sink
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
 * Pablo Virolainen <pablo.virolainen@nomovok.com>
 * Johannes Lahti <johannes.lahti@nomovok.com>
 * Aki Honkasuo <aki.honkasuo@nomovok.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/videodev2.h>	/* For pixel formats */
#include <shveu/shveu.h>
#include "gstshvideodec.h"
#include "display.h"

/**
 * Define capatibilities for the sink factory
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
								   GST_PAD_SINK,
								   GST_PAD_ALWAYS,
								   GST_STATIC_CAPS
								   ("video/mpeg,"
									"width	= (int) [48, 1280],"
									"height = (int) [48, 720],"
									"framerate = (fraction) [0, 30],"
									"mpegversion = (int) 4"
									"; "
									"video/x-h264,"
									"width	= (int) [48, 1280],"
									"height = (int) [48, 720],"
									"framerate = (fraction) [0, 30],"
									"variant = (string) itu,"
									"h264version = (string) h264"
									"; "
									"video/x-divx,"
									"width	= (int) [48, 1280],"
									"height = (int) [48, 720],"
									"framerate = (fraction) [0, 30],"
									"divxversion =	{4, 5, 6}"
									"; "
									"video/x-xvid,"
									"width	= (int) [48, 1280],"
									"height = (int) [48, 720],"
									"framerate = (fraction) [0, 30]")
	);

static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC(gst_sh_mobile_debug);
#define GST_CAT_DEFAULT gst_sh_mobile_debug

/**
 * Define decoder properties
 */
enum {
	PROP_0,
	PROP_LAST
};


static void gst_shvideodec_init_class(gpointer g_class, gpointer data)
{
	parent_class = g_type_class_peek_parent(g_class);
	gst_shvideodec_class_init((GstshvideodecClass *) g_class);
}

GType gst_shvideodec_get_type(void)
{
	static GType object_type = 0;

	if (object_type == 0) {
		static const GTypeInfo object_info = {
			sizeof(GstshvideodecClass),
			gst_shvideodec_base_init,
			NULL,
			gst_shvideodec_init_class,
			NULL,
			NULL,
			sizeof(Gstshvideodec),
			0,
			(GInstanceInitFunc) gst_shvideodec_init
		};

		object_type =
			g_type_register_static(GST_TYPE_ELEMENT,
					   "gst-sh-mobile-dec-sink", &object_info, (GTypeFlags) 0);
	}
	return object_type;
}

static void gst_shvideodec_base_init(gpointer klass)
{
	static const GstElementDetails plugin_details =
		GST_ELEMENT_DETAILS("SH hardware video decoder & sink",
				"Codec/Decoder/Video/Sink",
				"Decode video (H264 && Mpeg4)",
				"Pablo Virolainen <pablo.virolainen@nomovok.com>");

	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get(&sink_factory));
	gst_element_class_set_details(element_class, &plugin_details);
}

static void gst_shvideodec_dispose(GObject * object)
{
	Gstshvideodec *dec = GST_SHVIDEODEC(object);

	GST_LOG_OBJECT(dec, "%s called\n", __func__);

	if (dec->decoder != NULL) {
		GST_DEBUG_OBJECT(dec, "close decoder object %p", dec->decoder);
		shcodecs_decoder_close(dec->decoder);
	}
	if (dec->p_display) {
		display_close(dec->p_display);
	}
	shveu_close();
	G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void gst_shvideodec_class_init(GstshvideodecClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	GST_DEBUG_CATEGORY_INIT(gst_sh_mobile_debug, "gst-sh-mobile-dec-sink",
				0, "Decoder and player for H264/MPEG4 streams");

	gobject_class->dispose = gst_shvideodec_dispose;
	gstelement_class->set_clock = gst_shvideodec_set_clock;
}

static void gst_shvideodec_init(Gstshvideodec * dec, GstshvideodecClass * gklass)
{
	GstElementClass *kclass = GST_ELEMENT_GET_CLASS(dec);

	GST_LOG_OBJECT(dec, "%s called", __func__);

	dec->sinkpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(kclass, "sink"), "sink");

	gst_pad_set_setcaps_function(dec->sinkpad, gst_shvideodec_setcaps);
	gst_pad_set_chain_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_shvideodec_chain));

	gst_pad_set_event_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_shvideodec_sink_event));

	gst_element_add_pad(GST_ELEMENT(dec), dec->sinkpad);

	dec->caps_set = FALSE;
	dec->decoder = NULL;
	dec->first_frame = TRUE;
	dec->pcache = NULL;
}


static gboolean gst_shvideodec_set_clock(GstElement * element, GstClock * clock)
{
	Gstshvideodec *dec = (Gstshvideodec *) element;

	GST_DEBUG_OBJECT(dec, "%s called", __func__);

	if (!clock) {
		GST_DEBUG_OBJECT(dec, "Using system clock");
		dec->clock = gst_system_clock_obtain();
		return FALSE;
	} else {
		GST_DEBUG_OBJECT(dec, "Clock accepted");
		dec->clock = clock;
		return TRUE;
	}
}

static gboolean gst_shvideodec_sink_event(GstPad * pad, GstEvent * event)
{
	Gstshvideodec *dec = (Gstshvideodec *) (GST_OBJECT_PARENT(pad));
	gboolean ret = TRUE;

	GST_DEBUG_OBJECT(dec, "%s called event %i", __func__, GST_EVENT_TYPE(event));

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_EOS:
		{
			GST_DEBUG_OBJECT(dec, "EOS gst event");

			GST_DEBUG_OBJECT(dec, "We are done, calling finalize.");
			shcodecs_decoder_finalize(dec->decoder);
			GST_DEBUG_OBJECT(dec,
					 "Stream finalized. Total decoded %d frames.",
					 shcodecs_decoder_get_frame_count(dec->decoder));

			gst_element_post_message((GstElement *) dec,
						 gst_message_new_eos((GstObject *) dec));
			break;
		}
	default:
		{
			ret = gst_pad_event_default(pad, event);
			break;
		}
	};

	return ret;
}

static gboolean gst_shvideodec_setcaps(GstPad * pad, GstCaps * caps)
{
	GstStructure *structure = NULL;
	Gstshvideodec *dec = (Gstshvideodec *) (GST_OBJECT_PARENT(pad));

	GST_LOG_OBJECT(dec, "%s called", __func__);

	if (dec->decoder != NULL) {
		GST_DEBUG_OBJECT(dec, "%s: Decoder already opened", __func__);
		return FALSE;
	}

	structure = gst_caps_get_structure(caps, 0);

	if (!strcmp(gst_structure_get_name(structure), "video/x-h264")) {
		GST_DEBUG_OBJECT(dec, "codec format is video/x-h264");
		dec->format = SHCodecs_Format_H264;
	} else {
		if (!strcmp(gst_structure_get_name(structure), "video/x-divx") ||
			!strcmp(gst_structure_get_name(structure), "video/x-xvid") ||
			!strcmp(gst_structure_get_name(structure), "video/x-gst-fourcc-libx")
			|| !strcmp(gst_structure_get_name(structure), "video/mpeg")
			) {
			GST_DEBUG_OBJECT(dec, "codec format is video/mpeg");
			dec->format = SHCodecs_Format_MPEG4;
		} else {
			GST_DEBUG_OBJECT(dec, "%s failed (not supported: %s)",
					 __func__, gst_structure_get_name(structure));
			return FALSE;
		}
	}

	if (!gst_structure_get_fraction(structure, "framerate",
					&dec->fps_numerator, &dec->fps_denominator)) {
		GST_DEBUG_OBJECT(dec, "%s failed (no framerate)", __func__);
		return FALSE;
	}

	if (gst_structure_get_int(structure, "width", &dec->width)
		&& gst_structure_get_int(structure, "height", &dec->height)) {
		GST_DEBUG_OBJECT(dec, "%s initializing decoder %dx%d",
				 __func__, dec->width, dec->height);
		dec->decoder = shcodecs_decoder_init(dec->width, dec->height, dec->format);
	} else {
		GST_DEBUG_OBJECT(dec, "%s failed (no width/height)", __func__);
		return FALSE;
	}

	if (dec->decoder == NULL) {
		GST_ELEMENT_ERROR((GstElement *) dec, CORE, FAILED,
				  ("Error on shdecodecs_decoder_init."),
				  ("%s failed (Error on shdecodecs_decoder_init)", __func__));
		return FALSE;
	}

	/* Set frame by frame */
	shcodecs_decoder_set_frame_by_frame(dec->decoder, 1);

	/* Use physical addresses for playback */
	shcodecs_decoder_set_use_physical(dec->decoder, 1);

	/* Display output */
	dec->veu = shveu_open();
	dec->p_display = display_open(dec->veu);
	if (!dec->p_display) {
		GST_ELEMENT_ERROR((GstElement *) dec, CORE, FAILED,
				  ("Error opening fb device"), (NULL));
	}

	shcodecs_decoder_set_decoded_callback(dec->decoder,
						  gst_shcodecs_decoded_callback, (void *) dec);

	dec->caps_set = TRUE;

	GST_LOG_OBJECT(dec, "%s ok", __func__);
	return TRUE;
}

static GstFlowReturn gst_shvideodec_chain(GstPad * pad, GstBuffer * _data)
{
	Gstshvideodec *dec = (Gstshvideodec *) (GST_OBJECT_PARENT(pad));
	GstFlowReturn ret = GST_FLOW_OK;
	GstBuffer *inbuf;
	gint bsize, bused = 1;
	guint8 *bdata;

	if (!dec->caps_set) {
		GST_ELEMENT_ERROR((GstElement *) dec, CORE, NEGOTIATION, ("Caps not set."), (NULL));
		return GST_FLOW_UNEXPECTED;
	}

	inbuf = GST_BUFFER(_data);

	GST_DEBUG_OBJECT(dec,
			 "Received new data of size %d, time %"
			 GST_TIME_FORMAT, GST_BUFFER_SIZE(inbuf),
			 GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(inbuf)));

	if (dec->pcache) {
		inbuf = gst_buffer_join(dec->pcache, inbuf);
		dec->pcache = NULL;
	} else {
		dec->playback_timestamp = GST_BUFFER_TIMESTAMP(inbuf);
		dec->playback_played = 0;
	}

	bdata = GST_BUFFER_DATA(inbuf);
	bsize = GST_BUFFER_SIZE(inbuf);

	GST_DEBUG_OBJECT(dec, "Calling shcodecs_decode with %d bytes", bsize);
	bused = shcodecs_decode(dec->decoder, bdata, bsize);
	GST_DEBUG_OBJECT(dec, "shcodecs_decode returned %d", bused);

	if (bused < 0) {
		GST_ELEMENT_ERROR((GstElement *) dec, CORE, FAILED,
				  ("Decode error"), ("%s failed (Error on shcodecs_decode)", __func__));
		return GST_FLOW_ERROR;
	}

	if (bused != bsize) {
		GST_DEBUG_OBJECT(dec, "Keeping %d bytes of data", bsize);
		dec->pcache = gst_buffer_create_sub(inbuf, bused, (bsize - bused));
	}

	gst_buffer_unref(inbuf);

	return ret;
}

static int
gst_shcodecs_decoded_callback(SHCodecs_Decoder * decoder,
				  unsigned char *y_buf, int y_size,
				  unsigned char *c_buf, int c_size, void *user_data)
{
	GstClockTime time_now;
	long long unsigned int time_diff, stamp_diff, sleep_time;
	Gstshvideodec *dec = (Gstshvideodec *) user_data;

	GST_DEBUG_OBJECT(dec, "Frame decoded");

	time_now = gst_clock_get_time(dec->clock);

	if (dec->first_frame) {
		dec->start_time = time_now;
		dec->first_timestamp = dec->playback_timestamp;
		dec->first_frame = FALSE;
	}

	time_diff = GST_TIME_AS_MSECONDS(GST_CLOCK_DIFF(dec->start_time, time_now));
	stamp_diff = GST_TIME_AS_MSECONDS(dec->playback_timestamp)
		+ (dec->playback_played * (1000 * dec->fps_denominator / dec->fps_numerator))
		- GST_TIME_AS_MSECONDS(dec->first_timestamp);

	GST_DEBUG_OBJECT(dec,
			 "Frame number: %d time from start: %llu stamp diff: %llu",
			 dec->playback_played, time_diff, stamp_diff);

	if (stamp_diff > time_diff) {
		sleep_time = stamp_diff - time_diff;
		GST_DEBUG_OBJECT(dec, "sleeping for: %llums", sleep_time);
		usleep(sleep_time * 1000);
	}

	display_update(dec->p_display,
			(unsigned) y_buf,
			(unsigned) c_buf,
			dec->width,
			dec->height,
			dec->width,
			V4L2_PIX_FMT_NV12);

	dec->playback_played++;
	return 1;
}

gboolean gst_shvideo_dec_plugin_init(GstPlugin * plugin)
{
	GST_LOG_OBJECT("%s called\n", __func__);

	if (!gst_element_register
		(plugin, "gst-sh-mobile-dec-sink", GST_RANK_NONE, GST_TYPE_SHVIDEODEC))
		return FALSE;
	return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
		  GST_VERSION_MINOR,
		  "gst-sh-mobile-dec-sink",
		  "gst-sh-mobile",
		  gst_shvideo_dec_plugin_init,
		  VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
