/**
 * gst-sh-mobile-v4l2src
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
 * @author George A. Dorobantu <gdalex@gmail.com>
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <linux/videodev2.h> /* For pixel formats */
#include <uiomux/uiomux.h>

#include "gstshv4l2src.h"
#include "ControlFileUtil.h"
#include "capture.h"
#include "display.h"

#define CHROMA_ALIGNMENT 16

typedef enum {
	PREVIEW_OFF,
	PREVIEW_ON
} GstSHV4L2SrcPreview;


/**
 * Define Gstreamer SH V4L2Src structure
 */
struct _GstSHV4L2Src {
	GstElement element;
	GstPad *srcpad;

	guint64 offset;
	GstClockTime duration;       /* duration of one frame */
	gint width;
	gint height;
	gint fps_numerator;
	gint fps_denominator;

	capture *ceu;

	GstCaps *out_caps;
	gboolean caps_set;
	glong frame_number;

	GstClock *clock;
	gboolean start_time_set;
	GstClockTime start_time;

	pthread_t thread;

	DISPLAY *display;

	int cap_w;
	int cap_h;
	GstSHV4L2SrcPreview preview;

	/* This is used to stop the plugin sending data downstream when PAUSED */
	gboolean hold_output;

	/* These flags stop the threads in turn */
	gboolean stop_thread;
};


/**
 * Define Gstreamer SH v4l2src Class structure
 */
struct _GstSHV4L2SrcClass {
	GstElementClass parent;
};


/**
 * Define capatibilities for the source factory
 */

static GstStaticPadTemplate src_factory =
	GST_STATIC_PAD_TEMPLATE("src",
			  GST_PAD_SRC,
			  GST_PAD_ALWAYS,
			  GST_STATIC_CAPS
			  (
				"video/x-raw-yuv, "
				"format = (fourcc) NV12,"
				"width = (int) [48, 1280],"
				"height = (int) [48, 720],"
				"framerate = (fraction) [0, 30]"
			  ));

GST_DEBUG_CATEGORY_STATIC(gst_shv4l2src_debug);
#define GST_CAT_DEFAULT gst_shv4l2src_debug

static GstElementClass *parent_class = NULL;

/* Forward declarations */
static void gst_shv4l2src_init_class(gpointer g_class, gpointer data);
static void gst_shv4l2src_base_init(gpointer klass);
static void gst_shv4l2src_dispose(GObject * object);
static void gst_shv4l2src_class_init(GstSHV4L2SrcClass * klass);
static void gst_shv4l2src_init(GstSHV4L2Src * shv4l2src, GstSHV4L2SrcClass * gklass);
static void gst_shv4l2src_set_property(GObject * object, guint prop_id,
					 const GValue * value, GParamSpec * pspec);
static void gst_shv4l2src_get_property(GObject * object, guint prop_id,
					 GValue * value, GParamSpec * pspec);
static gboolean gst_shv4l2src_src_query(GstPad * pad, GstQuery * query);
static void gst_shv4l2src_init_camera_encoder(GstSHV4L2Src * cam_cap);
static void *shv4l2src_thread(void *data);
static GType gst_shv4l2src_preview_get_type(void);
static gboolean gst_shv4l2src_src_event(GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_shv4l2src_change_state(GstElement *
							 element, GstStateChange transition);
static gboolean gst_shv4l2src_set_clock(GstElement * element, GstClock * clock);
static gboolean gst_shv4l2src_set_src_caps(GstPad * pad, GstCaps * caps);
static void gst_shv4l2src_read_src_caps(GstSHV4L2Src * cam_cap);

/**
 * Define camera capture properties
 */
enum {
	PROP_0,
	PROP_PREVIEW,
	PROP_LAST
};

#define GST_TYPE_SHV4L2SRC_PREVIEW (gst_shv4l2src_preview_get_type())
static GType gst_shv4l2src_preview_get_type(void)
{
	static GType shv4l2src_preview_type = 0;
	static const GEnumValue preview_method[] = {
		{PREVIEW_OFF, "No camera preview", "off"},
		{PREVIEW_ON, "Camera preview", "on"},
		{0, NULL, NULL},
	};

	if (!shv4l2src_preview_type) {
		shv4l2src_preview_type = g_enum_register_static("GstSHV4L2SrcPreview", preview_method);
	}
	return shv4l2src_preview_type;
}

/******************
 * CAPTURE THREAD *
 ******************/

/** ceu callback function
 * received a full frame from the camera
	@param capture
	@param frame_data output buffer pointer
	@param length buffer size
	@param user_data user pointer
*/
static void capture_image_cb(capture * ceu, const unsigned char *frame_data, size_t length, void *user_data)
{
	GstSHV4L2Src *shv4l2src = (GstSHV4L2Src *) user_data;

	GST_DEBUG_OBJECT(shv4l2src, "Captured a frame");

	GstBuffer *buf = gst_buffer_new();
	gst_buffer_set_data(buf, (unsigned char*)frame_data, length);

	GstClockTime timestamp;

	GST_BUFFER_OFFSET(buf) = shv4l2src->offset++;
	GST_BUFFER_OFFSET_END(buf) = shv4l2src->offset;

	if (G_LIKELY(shv4l2src->clock)) {
		/* the time now is the time of the clock minus the base time */
		timestamp = gst_clock_get_time(shv4l2src->clock) - GST_ELEMENT(shv4l2src)->base_time;
		/* if we have a framerate adjust timestamp for frame latency */
		if (GST_CLOCK_TIME_IS_VALID(shv4l2src->duration)) {
			if (timestamp > shv4l2src->duration) {
				timestamp -= shv4l2src->duration;
			} else {
				timestamp = 0;
			}
		}
	} else {
		GST_DEBUG_OBJECT(shv4l2src, "No have a valid clock to calculate the timestamp");
		timestamp = 0;
	}

	/* FIXME: use the timestamp from the buffer itself! */
	GST_BUFFER_TIMESTAMP(buf) = timestamp;
	GST_BUFFER_DURATION(buf) = shv4l2src->duration;

	int ret = gst_pad_push(shv4l2src->srcpad, buf);
	if (GST_FLOW_OK != ret) {
		GST_DEBUG_OBJECT(shv4l2src, "pad_push failed: %s.", gst_flow_get_name(ret));
	}

	if (shv4l2src->preview == PREVIEW_ON) {
		struct ren_vid_surface frame_surface;
		frame_surface.format = REN_NV12;
		frame_surface.w = shv4l2src->cap_w;
		frame_surface.h = shv4l2src->cap_h;
		frame_surface.pitch = frame_surface.w;
		frame_surface.py = (void*)frame_data;
		frame_surface.pc = frame_surface.py + (frame_surface.w * frame_surface.h);
		frame_surface.pa = NULL;

		display_update(shv4l2src->display, &frame_surface);
		GST_DEBUG_OBJECT(shv4l2src, "Display update complete");
	}

	capture_queue_buffer(shv4l2src->ceu, frame_data);
}

static void *capture_loop(void *data)
{
	GstSHV4L2Src *shv4l2src = (GstSHV4L2Src *) data;
	guint64 time_diff, stamp_diff, sleep_time;
	GstClockTime time_now;

	while (!shv4l2src->stop_thread) {
		/* Camera sensors cannot always be set to the required frame rate. The v4l
		   camera driver attempts to set to the requested frame rate, but if not
		   possible it attempts to set a higher frame rate, therefore we wait... */
		time_now = gst_clock_get_time(shv4l2src->clock);
		if (shv4l2src->start_time_set == FALSE) {
			shv4l2src->start_time = time_now;
			shv4l2src->start_time_set = TRUE;
		}
		time_diff = GST_TIME_AS_MSECONDS(GST_CLOCK_DIFF(shv4l2src->start_time, time_now));
		stamp_diff = 1000 * shv4l2src->fps_denominator / shv4l2src->fps_numerator;
		shv4l2src->start_time = time_now;

		if (stamp_diff > time_diff) {
			sleep_time = stamp_diff - time_diff;
			GST_DEBUG_OBJECT(shv4l2src, "Waiting %lldms", sleep_time);
			usleep(sleep_time * 1000);
		} else {
			GST_DEBUG_OBJECT(shv4l2src, "Late by %lldms", time_diff-stamp_diff);
		}

		capture_get_frame(shv4l2src->ceu, capture_image_cb, shv4l2src);
	}

	return NULL;
}

/** Initialize shvideocap class plugin event handler
	@param g_class Gclass
	@param data user data pointer, unused in the function
*/
static void gst_shv4l2src_init_class(gpointer g_class, gpointer data)
{
	GST_LOG("%s called", __func__);
	parent_class = g_type_class_peek_parent(g_class);
	gst_shv4l2src_class_init((GstSHV4L2SrcClass *) g_class);
}

/** Initialize SH shv4l2src
	@param klass Gstreamer element class
*/
static void gst_shv4l2src_base_init(gpointer klass)
{
	static const GstElementDetails plugin_details =
		GST_ELEMENT_DETAILS("SH v4l2src plugin",
				"Video/Src",
				"Camera capture video stream (NV12)",
				"George A. Dorobantu <gdalex@gmail.com>");
	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	GST_LOG("%s called", __func__);
	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get(&src_factory));
	gst_element_class_set_details(element_class, &plugin_details);
}

/** Dispose encoder
	@param object Gstreamer element class
*/
static void gst_shv4l2src_dispose(GObject * object)
{
	GstSHV4L2Src *shv4l2src = GST_SHV4L2SRC(object);
	void *thread_ret;
	GST_LOG("%s called", __func__);

	shv4l2src->stop_thread = TRUE;
	pthread_join(shv4l2src->thread, &thread_ret);

	capture_stop_capturing(shv4l2src->ceu);

	if (shv4l2src->preview == PREVIEW_ON) {
		display_close(shv4l2src->display);
	}

	capture_close(shv4l2src->ceu);

	G_OBJECT_CLASS(parent_class)->dispose(object);
}


static gboolean gst_shv4l2src_set_clock(GstElement * element, GstClock * clock)
{
	GstSHV4L2Src *shv4l2src = (GstSHV4L2Src *) element;

	GST_DEBUG_OBJECT(shv4l2src, "%s called", __func__);

	if (!clock) {
		GST_DEBUG_OBJECT(shv4l2src, "Using system clock");
		shv4l2src->clock = gst_system_clock_obtain();
		return TRUE;
	} else {
		GST_DEBUG_OBJECT(shv4l2src, "Clock accepted");
		shv4l2src->clock = clock;
		return TRUE;
	}
}


/** Initialize the class
	@param klass Gstreamer SH v4l2src class
*/
static void gst_shv4l2src_class_init(GstSHV4L2SrcClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	GST_LOG("%s called", __func__);
	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	gobject_class->dispose = gst_shv4l2src_dispose;
	gobject_class->set_property = gst_shv4l2src_set_property;
	gobject_class->get_property = gst_shv4l2src_get_property;
	gstelement_class->set_clock = gst_shv4l2src_set_clock;
	gstelement_class->change_state = gst_shv4l2src_change_state;

	GST_DEBUG_CATEGORY_INIT(gst_shv4l2src_debug,
				"gst-sh-mobile-v4l2src", 0, "Camera capturer for NV12 streams");

	g_object_class_install_property(gobject_class, PROP_PREVIEW,
					g_param_spec_enum("preview",
							  "preview",
							  "preview",
							  GST_TYPE_SHV4L2SRC_PREVIEW,
							  PREVIEW_OFF, G_PARAM_READWRITE));
}

/** Initialize the internal data
	@param enc Gstreamer SH shv4l2src element
	@param gklass Gstreamer SH shv4l2src encode class
*/
static void gst_shv4l2src_init(GstSHV4L2Src * shv4l2src, GstSHV4L2SrcClass * gklass)
{
	GstElementClass *klass = GST_ELEMENT_GET_CLASS(shv4l2src);

	GST_LOG_OBJECT(shv4l2src, "%s called", __func__);

	shv4l2src->srcpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(klass, "src"), "src");
	gst_pad_set_setcaps_function(shv4l2src->srcpad, gst_shv4l2src_set_src_caps);

	gst_pad_set_query_function(shv4l2src->srcpad,
				   GST_DEBUG_FUNCPTR(gst_shv4l2src_src_query));
	gst_pad_set_event_function(shv4l2src->srcpad, GST_DEBUG_FUNCPTR(gst_shv4l2src_src_event));

	gst_element_add_pad(GST_ELEMENT(shv4l2src), shv4l2src->srcpad);

	shv4l2src->caps_set = FALSE;
	shv4l2src->stop_thread = FALSE;
	shv4l2src->thread = 0;

	shv4l2src->out_caps = NULL;
	shv4l2src->width = 0;
	shv4l2src->height = 0;
	shv4l2src->fps_numerator = 10;
	shv4l2src->fps_denominator = 1;
	shv4l2src->frame_number = 0;
	shv4l2src->preview = PREVIEW_OFF;
	shv4l2src->hold_output = TRUE;
	shv4l2src->start_time_set = FALSE;
}


static GstStateChangeReturn
gst_shv4l2src_change_state(GstElement * element, GstStateChange transition)
{
	GstSHV4L2Src *shv4l2src = (GstSHV4L2Src *) element;
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	GST_DEBUG_OBJECT(shv4l2src, "%s called", __func__);

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		GST_DEBUG_OBJECT(shv4l2src, "GST_STATE_CHANGE_NULL_TO_READY");
		shv4l2src->hold_output = TRUE;
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		GST_DEBUG_OBJECT(shv4l2src, "GST_STATE_CHANGE_READY_TO_PAUSED");
		shv4l2src->hold_output = FALSE;
		gst_shv4l2src_init_camera_encoder(shv4l2src);
		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		GST_DEBUG_OBJECT(shv4l2src, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
		shv4l2src->hold_output = FALSE;
		break;
	default:
		break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		GST_DEBUG_OBJECT(shv4l2src, "GST_STATE_CHANGE_PLAYING_TO_PAUSED");
		shv4l2src->hold_output = TRUE;
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		GST_DEBUG_OBJECT(shv4l2src, "GST_STATE_CHANGE_PAUSED_TO_READY");
		shv4l2src->hold_output = TRUE;
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_DEBUG_OBJECT(shv4l2src, "GST_STATE_CHANGE_READY_TO_NULL");
		shv4l2src->hold_output = TRUE;
		break;
	default:
		break;
	}
	return ret;
}


/** Event handler for encoder src events, see GstPadEventFunction.
 */
static gboolean gst_shv4l2src_src_event(GstPad * pad, GstEvent * event)
{
	GstSHV4L2Src *shv4l2src = (GstSHV4L2Src *) (GST_OBJECT_PARENT(pad));
	GST_DEBUG_OBJECT(shv4l2src, "%s called event %i", __func__, GST_EVENT_TYPE(event));
	return (GST_EVENT_LATENCY == GST_EVENT_TYPE(event)) ? TRUE : FALSE;
}


/** The function will set the user defined control file name value for decoder
	@param object The object where to get Gstreamer SH v4l2src object
	@param prop_id The property id
	@param value In this case file name if prop_id is PROP_PREVIEW
	@param pspec not used in fuction
*/
static void
gst_shv4l2src_set_property(GObject * object, guint prop_id,
				 const GValue * value, GParamSpec * pspec)
{
	GstSHV4L2Src *shv4l2src = GST_SHV4L2SRC(object);

	GST_LOG("%s called", __func__);
	switch (prop_id) {
	case PROP_PREVIEW:
		shv4l2src->preview = g_value_get_enum(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

/** The function will return the control file name from decoder to value
	@param object The object where to get Gstreamer SH v4l2src object
	@param prop_id The property id
	@param value In this case file name if prop_id is PROP_PREVIEW
	@param pspec not used in fuction
*/
static void
gst_shv4l2src_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	GstSHV4L2Src *shv4l2src = GST_SHV4L2SRC(object);

	GST_LOG("%s called", __func__);
	switch (prop_id) {
	case PROP_PREVIEW:
		g_value_set_enum(value, shv4l2src->preview);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

/******************
 * ENCODER THREAD *
 ******************/

/** Gstreamer source pad query
	@param pad Gstreamer source pad
	@param query Gsteamer query
	@returns Returns the value of gst_pad_query_default
*/
static gboolean gst_shv4l2src_src_query(GstPad * pad, GstQuery * query)
{
	GstSHV4L2Src *shv4l2src = (GstSHV4L2Src *) (GST_OBJECT_PARENT(pad));
	GST_LOG_OBJECT(shv4l2src, "%s called", __func__);
	return gst_pad_query_default(pad, query);
}

/** Initializes the SH Hardware encoder
	@param enc encoder object
*/
static void gst_shv4l2src_init_camera_encoder(GstSHV4L2Src * shv4l2src)
{
	GST_LOG_OBJECT(shv4l2src, "%s called", __func__);

	if (!shv4l2src->thread) {
		/* We'll have to launch the encoder in
		   a separate thread to keep the pipeline running */
		pthread_create(&shv4l2src->thread, NULL, shv4l2src_thread, shv4l2src);
	}
}

/** Launches the encoder in an own thread
	@param data encoder object
*/
static void *shv4l2src_thread(void *data)
{
	GstSHV4L2Src *shv4l2src = (GstSHV4L2Src *) data;
	GST_LOG_OBJECT(shv4l2src, "%s called", __func__);

	/* wait for  READY status */
	while (shv4l2src->hold_output == TRUE) {
		usleep(10);
	}

	if (shv4l2src->stop_thread == TRUE)
		return NULL;

	gst_shv4l2src_read_src_caps(shv4l2src);
	GST_LOG_OBJECT(shv4l2src, "set caps fps numerator %d fps denominator %d \n",
			   shv4l2src->fps_numerator, shv4l2src->fps_denominator);
	shv4l2src->duration = gst_util_uint64_scale_int(GST_SECOND,
			   shv4l2src->fps_denominator, shv4l2src->fps_numerator);
	GST_LOG_OBJECT(shv4l2src, "set duration to %llu\n", shv4l2src->duration);

	if (!shv4l2src->width) {
		shv4l2src->width = 1280;
	}

	if (!shv4l2src->height) {
		shv4l2src->height = 720;
	}

	shv4l2src->offset = 0;

	/* Display output */
	if (shv4l2src->preview == PREVIEW_ON) {
		shv4l2src->display = display_open();
		if (!shv4l2src->display) {
			GST_ELEMENT_ERROR((GstElement *) shv4l2src, CORE, FAILED,
					  ("Error opening fb device"), (NULL));
		}
	}

	/* ceu open */
	shv4l2src->ceu = capture_open_userio("/dev/video0",
					shv4l2src->width, shv4l2src->height);
	if (shv4l2src->ceu == NULL) {
		GST_ELEMENT_ERROR((GstElement *) shv4l2src, CORE, FAILED,
				  ("Error opening CEU"), (NULL));
	}
	shv4l2src->cap_w = capture_get_width(shv4l2src->ceu);
	shv4l2src->cap_h = capture_get_height(shv4l2src->ceu);

	if (capture_get_pixel_format (shv4l2src->ceu) != V4L2_PIX_FMT_NV12) {
		GST_ELEMENT_ERROR((GstElement *) shv4l2src, CORE, FAILED,
				  ("Camera capture pixel format is not supported"), (NULL));
	}

	/* Check for frame size that result in v4l2 capture buffers with the CbCr
	   plane located at an unsupported memory alignment. */
	if ((shv4l2src->width * shv4l2src->height) & (CHROMA_ALIGNMENT-1)) {
		GST_ELEMENT_ERROR((GstElement *) shv4l2src, CORE, FAILED,
				  ("unsupported encode size due to Chroma plane alignment"), (NULL));
	}

	GST_DEBUG_OBJECT(shv4l2src, "Capturing at %dx%d", shv4l2src->cap_w, shv4l2src->cap_h);

	capture_start_capturing(shv4l2src->ceu);

	/* capture loop */
	capture_loop(shv4l2src);

	gst_pad_push_event(shv4l2src->srcpad, gst_event_new_eos());

	return NULL;
}


GType gst_shv4l2src_get_type(void)
{
	static GType object_type = 0;

	GST_LOG("%s called", __func__);
	if (object_type == 0) {
		static const GTypeInfo object_info = {
			sizeof(GstSHV4L2SrcClass),
			gst_shv4l2src_base_init,
			NULL,
			gst_shv4l2src_init_class,
			NULL,
			NULL,
			sizeof(GstSHV4L2Src),
			0,
			(GInstanceInitFunc) gst_shv4l2src_init
		};

		object_type =
			g_type_register_static(GST_TYPE_ELEMENT,
					   "gst-sh-mobile-shv4l2src", &object_info,
					   (GTypeFlags) 0);
	}
	return object_type;
}

/** Reads the capabilities of the peer element behind source pad
	@param enc encoder object
*/
static void gst_shv4l2src_read_src_caps(GstSHV4L2Src * shv4l2src)
{
	GstStructure *structure;

	GST_LOG_OBJECT(shv4l2src, "%s called", __func__);

	/* Get the caps of the next element in chain */
	shv4l2src->out_caps = gst_pad_peer_get_caps(shv4l2src->srcpad);

	/* Any format is ok too */
	if (!gst_caps_is_any(shv4l2src->out_caps)) {
		structure = gst_caps_get_structure(shv4l2src->out_caps, 0);

		gst_structure_get_int(structure, "width", &shv4l2src->width);
		gst_structure_get_int(structure, "height", &shv4l2src->height);
		gst_structure_get_fraction(structure, "framerate",
					   &shv4l2src->fps_numerator,
					   &shv4l2src->fps_denominator);
	}
}


/** Sets the capabilities of the source pad
	@param enc encoder object
	@return TRUE if the capabilities could be set, otherwise FALSE
*/
static gboolean gst_shv4l2src_set_src_caps(GstPad * pad, GstCaps * caps)
{
	GstStructure *structure = NULL;
	GstSHV4L2Src *shv4l2src = (GstSHV4L2Src *) (GST_OBJECT_PARENT(pad));
	gboolean ret = TRUE;

	GST_LOG_OBJECT(shv4l2src, "%s called", __func__);

	structure = gst_caps_get_structure(caps, 0);

	if (!strcmp(gst_structure_get_name(structure), "video/x-raw-yuv")) {
		GST_DEBUG_OBJECT(shv4l2src, "codec format is video/x-raw-yuv");
	} else {
		GST_DEBUG_OBJECT(shv4l2src, "%s failed (not supported: %s)",
				 __func__, gst_structure_get_name(structure));
		return FALSE;
	}

	if (!gst_structure_get_fraction(structure, "framerate",
					&shv4l2src->fps_numerator, &shv4l2src->fps_denominator)) {
		GST_DEBUG_OBJECT(shv4l2src, "%s failed (no framerate)", __func__);
		return FALSE;
	}

	if (!gst_structure_get_int(structure, "width", &shv4l2src->width)) {
		GST_DEBUG_OBJECT(shv4l2src, "%s failed (no width)", __func__);
		return FALSE;
	}

	if (!gst_structure_get_int(structure, "height", &shv4l2src->height)) {
		GST_DEBUG_OBJECT(shv4l2src, "%s failed (no height)", __func__);
		return FALSE;
	}

	/* Check for frame size that result in v4l2 capture buffers with the CbCr
	   plane located at an unsupported memory alignment. */
	if ((shv4l2src->width * shv4l2src->height) & (CHROMA_ALIGNMENT-1)) {
		GST_DEBUG_OBJECT(shv4l2src, "%s failed "
				"(unsupported size due to Chroma plane alignment)", __func__);
		return FALSE;
	}

	if (!gst_pad_set_caps(shv4l2src->srcpad, caps)) {
		GST_ELEMENT_ERROR((GstElement *) shv4l2src, CORE, NEGOTIATION,
				  ("Source pad not linked."), (NULL));
		ret = FALSE;
	}
	if (!gst_pad_set_caps(gst_pad_get_peer(shv4l2src->srcpad), caps)) {
		GST_ELEMENT_ERROR((GstElement *) shv4l2src, CORE, NEGOTIATION,
				  ("Source pad not linked."), (NULL));
		ret = FALSE;
	}
	gst_caps_unref(caps);

	return ret;
}

gboolean gst_shv4l2src_plugin_init(GstPlugin * plugin)
{
	GST_LOG("%s called", __func__);
	if (!gst_element_register
		(plugin, "gst-sh-mobile-shv4l2src", GST_RANK_PRIMARY, GST_TYPE_SHV4L2SRC))
		return FALSE;

	return TRUE;
}

