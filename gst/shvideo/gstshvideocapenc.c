/**
 * gst-sh-mobile-camera-enc
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <linux/videodev2.h>	/* For pixel formats */
#include <shveu/shveu.h>
#include <shcodecs/shcodecs_encoder.h>

#include "gstshvideocapenc.h"
#include "ControlFileUtil.h"
#include "capture.h"
#include "display.h"
#include "thrqueue.h"

typedef enum {
	PREVIEW_OFF,
	PREVIEW_ON
} GstCameraPreview;


/**
 * Define Gstreamer SH Video Encoder structure
 */
struct _GstSHVideoCapEnc {
	GstElement element;
	GstPad *srcpad;

	gint offset;
	SHCodecs_Format format;
	SHCodecs_Encoder *encoder;
	gint width;
	gint height;
	gint fps_numerator;
	gint fps_denominator;

	APPLI_INFO ainfo;

	GstCaps *out_caps;
	gboolean caps_set;
	glong frame_number;

	GstClock *clock;
	gboolean start_time_set;
	GstClockTime start_time;
	GstBuffer *output_buf;

	pthread_t enc_thread;
	pthread_t capture_thread;
	pthread_t blit_thread;

	struct Queue * captured_queue;

	void *display;
	pthread_mutex_t launch_mutex;
	pthread_mutex_t encode_start_mutex;

	int veu;

	int cap_w;
	int cap_h;
	GstCameraPreview preview;

	gboolean output_lock;
	gboolean libshcodecs_stop;
};


/**
 * Define Gstreamer SH Video Encoder Class structure
 */
struct _GstSHVideoCapEncClass {
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
			  ("video/mpeg,"
			   "width = (int) [48, 1280],"
			   "height = (int) [48, 720],"
			   "framerate = (fraction) [0, 30],"
			   "mpegversion = (int) 4"
			   "; "
			   "video/x-h264,"
			   "width = (int) [48, 1280],"
			   "height = (int) [48, 720],"
			   "framerate = (fraction) [0, 30]"));

GST_DEBUG_CATEGORY_STATIC(gst_sh_mobile_debug);
#define GST_CAT_DEFAULT gst_sh_mobile_debug

static GstElementClass *parent_class = NULL;

/* Forward declarations */
static void gst_shvideo_enc_init_class(gpointer g_class, gpointer data);
static void gst_shvideo_enc_base_init(gpointer klass);
static void gst_shvideo_enc_dispose(GObject * object);
static void gst_shvideo_enc_class_init(GstSHVideoCapEncClass * klass);
static void gst_shvideo_enc_init(GstSHVideoCapEnc * shvideoenc, GstSHVideoCapEncClass * gklass);
static void gst_shvideo_enc_set_property(GObject * object, guint prop_id,
					 const GValue * value, GParamSpec * pspec);
static void gst_shvideo_enc_get_property(GObject * object, guint prop_id,
					 GValue * value, GParamSpec * pspec);
static gboolean gst_shvideo_enc_src_query(GstPad * pad, GstQuery * query);
static int gst_shvideo_enc_get_input(SHCodecs_Encoder * encoder, void *user_data);
static int gst_shvideo_enc_write_output(SHCodecs_Encoder * encoder,
					unsigned char *data, int length, void *user_data);
static void gst_shvideo_enc_init_camera_encoder(GstSHVideoCapEnc * shvideoenc);
static void *launch_camera_encoder_thread(void *data);
static GType gst_camera_preview_get_type(void);
static gboolean gst_shvideoenc_src_event(GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_shvideo_enc_change_state(GstElement *
							 element, GstStateChange transition);
static gboolean gst_shvideoenc_set_clock(GstElement * element, GstClock * clock);
static gboolean gst_shvideocameraenc_set_src_caps(GstPad * pad, GstCaps * caps);
static void gst_shvideocameraenc_read_src_caps(GstSHVideoCapEnc * shvideoenc);

/**
 * Define encoder properties
 */
enum {
	PROP_0,
	PROP_CNTL_FILE,
	PROP_PREVIEW,
	PROP_LAST
};

#define GST_TYPE_CAMERA_PREVIEW (gst_camera_preview_get_type())
static GType gst_camera_preview_get_type(void)
{
	static GType camera_preview_type = 0;
	static const GEnumValue preview_method[] = {
		{PREVIEW_OFF, "No camera preview", "off"},
		{PREVIEW_ON, "Camera preview", "on"},
		{0, NULL, NULL},
	};

	if (!camera_preview_type) {
		camera_preview_type = g_enum_register_static("GstCameraPreview", preview_method);
	}
	return camera_preview_type;
}

/** ceu callback function
	@param capture 
	@param frame_data output buffer pointer
	@param length buffer size
	@param user_data user pointer
*/
static void capture_image_cb(capture * ceu, const unsigned char *frame_data, size_t length, void *user_data)
{
	GstSHVideoCapEnc *pvt = (GstSHVideoCapEnc *) user_data;

	GST_DEBUG_OBJECT(pvt, "Captured a frame");
	queue_enq (pvt->captured_queue, (void*)frame_data);
}

static void *capture_thread(void *data)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) data;
	guint64 time_diff, stamp_diff, sleep_time;
	GstClockTime time_now;

	while (1) {
		/* Camera sensors cannot always be set to the required frame rate. The v4l
		   camera driver attempts to set to the requested frame rate, but if not
		   possible it attempts to set a higher frame rate, therefore we wait... */
		time_now = gst_clock_get_time(enc->clock);
		if (enc->start_time_set == FALSE) {
			enc->start_time = time_now;
			enc->start_time_set = TRUE;
		}
		time_diff = GST_TIME_AS_MSECONDS(GST_CLOCK_DIFF(enc->start_time, time_now));
		stamp_diff = 1000 * enc->fps_denominator / enc->fps_numerator;
		enc->start_time = time_now;

		if (stamp_diff > time_diff) {
			sleep_time = stamp_diff - time_diff;
			GST_DEBUG_OBJECT(enc, "Waiting %lldms", sleep_time);
// TODO Waiting causes problems. It might be because we queue capture buffers in v4l2
//			usleep(sleep_time * 1000);
		} else {
			GST_DEBUG_OBJECT(enc, "Late by %lldms", time_diff-stamp_diff);
		}

		capture_get_frame(enc->ainfo.ceu, capture_image_cb, enc);
	}

	return NULL;
}

static void *blit_thread(void *data)
{
	GstSHVideoCapEnc *pvt = (GstSHVideoCapEnc *) data;
	unsigned long enc_y, enc_c;
	unsigned long cap_y, cap_c;

	GST_LOG_OBJECT(pvt, "Preview=%d", pvt->preview);

	while (1) {
		GST_LOG_OBJECT(pvt, "Waiting for capture");

		cap_y = (unsigned long) queue_deq(pvt->captured_queue);
		cap_c = cap_y + (pvt->cap_w * pvt->cap_h);

		GST_LOG_OBJECT(pvt, "Starting scale");

		shcodecs_encoder_get_input_physical_addr (pvt->encoder, (unsigned int *)&enc_y, (unsigned int *)&enc_c);

		shveu_operation(pvt->veu,
				cap_y,
				cap_c,
				pvt->cap_w,
				pvt->cap_h,
				pvt->cap_w,
				SHVEU_YCbCr420,
				enc_y,
				enc_c,
				(long) pvt->width,
				(long) pvt->height,
				(long) pvt->width,
				SHVEU_YCbCr420, SHVEU_NO_ROT);

		/* Let the encoder get_input function return */
		pthread_mutex_unlock(&pvt->encode_start_mutex);

		if (pvt->preview == PREVIEW_ON) {
			display_update(pvt->display,
					enc_y,
					enc_c,
					pvt->width,
					pvt->height,
					pvt->width,
					V4L2_PIX_FMT_NV12);
		}

		capture_queue_buffer (pvt->ainfo.ceu, (void *)cap_y);
	}

	return NULL;
}

/** Initialize shvideoenc class plugin event handler
	@param g_class Gclass
	@param data user data pointer, unused in the function
*/
static void gst_shvideo_enc_init_class(gpointer g_class, gpointer data)
{
	GST_LOG("%s called", __func__);
	parent_class = g_type_class_peek_parent(g_class);
	gst_shvideo_enc_class_init((GstSHVideoCapEncClass *) g_class);
}

/** Initialize SH hardware video encoder
	@param klass Gstreamer element class
*/
static void gst_shvideo_enc_base_init(gpointer klass)
{
	static const GstElementDetails plugin_details =
		GST_ELEMENT_DETAILS("SH hardware camera capture & video encoder",
				"Codec/Encoder/Video/Src",
				"Encode mpeg-based video stream (mpeg4, h264)",
				"Takashi Namiki <takashi.namiki@renesas.com>");
	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	GST_LOG("%s called", __func__);
	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get(&src_factory));
	gst_element_class_set_details(element_class, &plugin_details);
}

/** Dispose encoder
	@param object Gstreamer element class
*/
static void gst_shvideo_enc_dispose(GObject * object)
{
	GstSHVideoCapEnc *shvideoenc = GST_SH_VIDEO_CAPENC(object);
	GST_LOG("%s called", __func__);

	pthread_mutex_lock(&shvideoenc->launch_mutex);

	capture_stop_capturing(shvideoenc->ainfo.ceu);

	if (shvideoenc->encoder != NULL) {
		shcodecs_encoder_close(shvideoenc->encoder);
		shvideoenc->encoder = NULL;
	}

	if (shvideoenc->preview == PREVIEW_ON) {
		display_close(shvideoenc->display);
	}

	shveu_close();
	capture_close(shvideoenc->ainfo.ceu);

	pthread_cancel(shvideoenc->enc_thread);
	pthread_cancel(shvideoenc->capture_thread);
	pthread_cancel(shvideoenc->blit_thread);

	pthread_mutex_destroy(&shvideoenc->launch_mutex);

	G_OBJECT_CLASS(parent_class)->dispose(object);
}


static gboolean gst_shvideoenc_set_clock(GstElement * element, GstClock * clock)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) element;

	GST_DEBUG_OBJECT(enc, "%s called", __func__);

	if (!clock) {
		GST_DEBUG_OBJECT(enc, "Using system clock");
		enc->clock = gst_system_clock_obtain();
		return FALSE;
	} else {
		GST_DEBUG_OBJECT(enc, "Clock accepted");
		enc->clock = clock;
		return TRUE;
	}
}


/** Initialize the class for encoder
	@param klass Gstreamer SH video encoder class
*/
static void gst_shvideo_enc_class_init(GstSHVideoCapEncClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	GST_LOG("%s called", __func__);
	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	gobject_class->dispose = gst_shvideo_enc_dispose;
	gobject_class->set_property = gst_shvideo_enc_set_property;
	gobject_class->get_property = gst_shvideo_enc_get_property;
	gstelement_class->set_clock = gst_shvideoenc_set_clock;
	gstelement_class->change_state = gst_shvideo_enc_change_state;

	GST_DEBUG_CATEGORY_INIT(gst_sh_mobile_debug,
				"gst-sh-mobile-camera-enc", 0, "Encoder for H264/MPEG4 streams");

	g_object_class_install_property(gobject_class, PROP_CNTL_FILE,
					g_param_spec_string("cntl-file",
								"Control file location",
								"Location of the file including encoding parameters",
								NULL,
								G_PARAM_READWRITE |
								G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, PROP_PREVIEW,
					g_param_spec_enum("preview",
							  "Camera preview",
							  "camera preview",
							  GST_TYPE_CAMERA_PREVIEW,
							  PREVIEW_OFF, G_PARAM_READWRITE));
}

/** Initialize the encoder
	@param shvideoenc Gstreamer SH video element
	@param gklass Gstreamer SH video encode class
*/
static void gst_shvideo_enc_init(GstSHVideoCapEnc * shvideoenc, GstSHVideoCapEncClass * gklass)
{
	GstElementClass *klass = GST_ELEMENT_GET_CLASS(shvideoenc);

	GST_LOG_OBJECT(shvideoenc, "%s called", __func__);

	shvideoenc->srcpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(klass, "src"), "src");
	gst_pad_set_setcaps_function(shvideoenc->srcpad, gst_shvideocameraenc_set_src_caps);

	gst_pad_set_query_function(shvideoenc->srcpad,
				   GST_DEBUG_FUNCPTR(gst_shvideo_enc_src_query));
	gst_pad_set_event_function(shvideoenc->srcpad, GST_DEBUG_FUNCPTR(gst_shvideoenc_src_event));

	gst_element_add_pad(GST_ELEMENT(shvideoenc), shvideoenc->srcpad);

	shvideoenc->encoder = NULL;
	shvideoenc->caps_set = FALSE;
	shvideoenc->libshcodecs_stop = FALSE;
	shvideoenc->enc_thread = 0;

	pthread_mutex_init(&shvideoenc->launch_mutex, NULL);
	pthread_mutex_init(&shvideoenc->encode_start_mutex, NULL);

	/* Initialize the queues */
	shvideoenc->captured_queue = queue_init();
	queue_limit (shvideoenc->captured_queue, 2);

	shvideoenc->format = SHCodecs_Format_NONE;
	shvideoenc->out_caps = NULL;
	shvideoenc->width = 0;
	shvideoenc->height = 0;
	shvideoenc->fps_numerator = 25;
	shvideoenc->fps_denominator = 1;
	shvideoenc->frame_number = 0;
	shvideoenc->preview = PREVIEW_OFF;
	shvideoenc->output_lock = TRUE;
	shvideoenc->start_time_set = FALSE;
	shvideoenc->output_buf = NULL;
}


static GstStateChangeReturn
gst_shvideo_enc_change_state(GstElement * element, GstStateChange transition)
{
	GstSHVideoCapEnc *shvideoenc = (GstSHVideoCapEnc *) element;
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	GST_DEBUG_OBJECT(shvideoenc, "%s called", __func__);

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		GST_DEBUG_OBJECT(shvideoenc, "GST_STATE_CHANGE_NULL_TO_READY");
		shvideoenc->output_lock = TRUE;
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		GST_DEBUG_OBJECT(shvideoenc, "GST_STATE_CHANGE_READY_TO_PAUSED");
		shvideoenc->output_lock = FALSE;
		gst_shvideo_enc_init_camera_encoder(shvideoenc);
		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		GST_DEBUG_OBJECT(shvideoenc, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
		shvideoenc->output_lock = FALSE;
		break;
	default:
		break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		GST_DEBUG_OBJECT(shvideoenc, "GST_STATE_CHANGE_PLAYING_TO_PAUSED");
		shvideoenc->output_lock = TRUE;
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		GST_DEBUG_OBJECT(shvideoenc, "GST_STATE_CHANGE_PAUSED_TO_READY");
		shvideoenc->output_lock = TRUE;
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_DEBUG_OBJECT(shvideoenc, "GST_STATE_CHANGE_READY_TO_NULL");
		shvideoenc->libshcodecs_stop = TRUE;
		shvideoenc->output_lock = TRUE;
		break;
	default:
		break;
	}
	return ret;
}



/** Event handler for encoder src events, see GstPadEventFunction.
 */
static gboolean gst_shvideoenc_src_event(GstPad * pad, GstEvent * event)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) (GST_OBJECT_PARENT(pad));
	gboolean ret = TRUE;

	GST_DEBUG_OBJECT(enc, "%s called event %i", __func__, GST_EVENT_TYPE(event));

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_LATENCY:
		ret = TRUE;
		break;
	default:
		ret = FALSE;
		break;
	}
	return ret;
}


/** The function will set the user defined control file name value for decoder
	@param object The object where to get Gstreamer SH video Encoder object
	@param prop_id The property id
	@param value In this case file name if prop_id is PROP_CNTL_FILE
	@param value In this case file name if prop_id is PROP_PREVIEW
	@param pspec not used in fuction
*/
static void
gst_shvideo_enc_set_property(GObject * object, guint prop_id,
				 const GValue * value, GParamSpec * pspec)
{
	GstSHVideoCapEnc *shvideoenc = GST_SH_VIDEO_CAPENC(object);

	GST_LOG("%s called", __func__);
	switch (prop_id) {
	case PROP_CNTL_FILE:
		strcpy(shvideoenc->ainfo.ctrl_file_name_buf, g_value_get_string(value));
		break;
	case PROP_PREVIEW:
		shvideoenc->preview = g_value_get_enum(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

/** The function will return the control file name from decoder to value
	@param object The object where to get Gstreamer SH video Encoder object
	@param prop_id The property id
	@param value In this case file name if prop_id is PROP_CNTL_FILE
	@param pspec not used in fuction
*/
static void
gst_shvideo_enc_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	GstSHVideoCapEnc *shvideoenc = GST_SH_VIDEO_CAPENC(object);

	GST_LOG("%s called", __func__);
	switch (prop_id) {
	case PROP_CNTL_FILE:
		g_value_set_string(value, shvideoenc->ainfo.ctrl_file_name_buf);
		break;
	case PROP_PREVIEW:
		g_value_set_enum(value, shvideoenc->preview);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

/** Callback function for the encoder input
	@param encoder shcodecs encoder
	@param user_data Gstreamer SH encoder object
	@return 0 if encoder should continue. 1 if encoder should pause.
*/
static int gst_shvideo_enc_get_input(SHCodecs_Encoder *encoder, void *user_data)
{
	GstSHVideoCapEnc *pvt = (GstSHVideoCapEnc *) user_data;
	GST_LOG_OBJECT(pvt, "Waiting for blit to complete");

	/* This mutex is unlocked once the capture buffer has been copied to the
	   encoder input buffer */
	pthread_mutex_lock(&pvt->encode_start_mutex);
	GST_LOG_OBJECT(pvt, "Got input buffer");

	if (pvt->libshcodecs_stop == TRUE)
		return -1;
	return 0;
}

/** Callback function for the encoder output
	@param encoder shcodecs encoder
	@param data the encoded video frame
	@param length size the encoded video frame buffer
	@param user_data Gstreamer SH encoder object
	@return 0 if encoder should continue. 1 if encoder should pause.
*/
static int
gst_shvideo_enc_write_output(SHCodecs_Encoder * encoder,
				 unsigned char *data, int length, void *user_data)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) user_data;
	GstBuffer *buf = NULL;
	gint ret = 0;
	int frm_delta;

	GST_LOG_OBJECT(enc, "Got %d bytes data\n", length);

	if (length <= 0)
		return 0;

	buf = gst_buffer_new();
	gst_buffer_set_data(buf, data, length);

	if (enc->output_buf != NULL){		
		buf = gst_buffer_join(enc->output_buf, buf);
		enc->output_buf = NULL;
	}
	frm_delta = shcodecs_encoder_get_frame_num_delta(enc->encoder);

	if (frm_delta){
		GST_BUFFER_DURATION(buf) =
			frm_delta * enc->fps_denominator * 1000 * GST_MSECOND / enc->fps_numerator;
		GST_BUFFER_TIMESTAMP(buf) = enc->frame_number * GST_BUFFER_DURATION(buf);
		GST_BUFFER_OFFSET(buf) = enc->frame_number;
		enc->frame_number += frm_delta;
		ret = gst_pad_push(enc->srcpad, buf);
		if (ret != GST_FLOW_OK) {
			GST_DEBUG_OBJECT(enc, "pad_push failed: %s", gst_flow_get_name(ret));
			return -1;
		}
	} else {
		enc->output_buf = buf;
	}

	return 0;
}

/** Gstreamer source pad query 
	@param pad Gstreamer source pad
	@param query Gsteamer query
	@returns Returns the value of gst_pad_query_default
*/
static gboolean gst_shvideo_enc_src_query(GstPad * pad, GstQuery * query)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) (GST_OBJECT_PARENT(pad));
	GST_LOG_OBJECT(enc, "%s called", __func__);
	return gst_pad_query_default(pad, query);
}

/** Initializes the SH Hardware encoder
	@param shvideoenc encoder object
*/
static void gst_shvideo_enc_init_camera_encoder(GstSHVideoCapEnc * shvideoenc)
{
	gint ret = 0;
	glong fmt = 0;

	GST_LOG_OBJECT(shvideoenc, "%s called", __func__);

	ret = GetFromCtrlFTop((const char *)
				  shvideoenc->ainfo.ctrl_file_name_buf, &shvideoenc->ainfo, &fmt);
	if (ret < 0) {
		GST_ELEMENT_ERROR((GstElement *) shvideoenc, CORE, FAILED,
				  ("Error reading control file."), (NULL));
	}

	/* Initalise the mutexes */
	pthread_mutex_lock(&shvideoenc->launch_mutex);

	if (!shvideoenc->enc_thread) {
		/* We'll have to launch the encoder in 
		   a separate thread to keep the pipeline running */
		pthread_create(&shvideoenc->enc_thread, NULL, launch_camera_encoder_thread,
				   shvideoenc);
	}
}

/** Launches the encoder in an own thread
	@param data encoder object
*/
static void *launch_camera_encoder_thread(void *data)
{
	gint ret;
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) data;

	GST_LOG_OBJECT(enc, "%s called", __func__);

	if (enc->libshcodecs_stop == TRUE)
		return NULL;
#if 0
	while (gst_pad_get_peer(enc->srcpad) == NULL) {
		usleep(10);

	}
#endif

	/* wait for  READY status */
	while (enc->output_lock == TRUE) {
		usleep(10);
	}

	gst_shvideocameraenc_read_src_caps(enc);
	GST_LOG_OBJECT(enc, "set caps fps numerator %x fps denominator %x \n",
			   enc->fps_numerator, enc->fps_denominator);

	if (enc->format == SHCodecs_Format_NONE) {
		enc->format = 0;
	}

	if (!enc->width) {
		enc->width = enc->ainfo.xpic;
	}

	if (!enc->height) {
		enc->height = enc->ainfo.ypic;
	}
	snprintf(enc->ainfo.input_file_name_buf, 256, "%s/%s",
		 enc->ainfo.buf_input_yuv_file_with_path, enc->ainfo.buf_input_yuv_file);

	/* VEU initialisation */
	enc->veu = shveu_open();
	if (enc->veu < 0) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("Error opening VEU"), (NULL));
	}

	/* Display output */
	if (enc->preview == PREVIEW_ON) {
		enc->display = display_open(enc->veu);
		if (!enc->display) {
			GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
					  ("Error opening fb device"), (NULL));
		}
	}

	/* ceu open */
	enc->ainfo.ceu = capture_open_userio(enc->ainfo.input_file_name_buf,
					enc->ainfo.xpic, enc->ainfo.ypic);
	if (enc->ainfo.ceu == NULL) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("Error opening CEU"), (NULL));
	}
	capture_set_use_physical(enc->ainfo.ceu, 1);
	enc->cap_w = capture_get_width(enc->ainfo.ceu);
	enc->cap_h = capture_get_height(enc->ainfo.ceu);

	if (capture_get_pixel_format (enc->ainfo.ceu) != V4L2_PIX_FMT_NV12) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("Camera capture pixel format is not supported"), (NULL));
	}

	GST_DEBUG_OBJECT(enc, "Capturing at %dx%d", enc->cap_w, enc->cap_h);

	enc->encoder = shcodecs_encoder_init(enc->width, enc->height, enc->format);

	shcodecs_encoder_set_input_callback(enc->encoder, gst_shvideo_enc_get_input, enc);
	shcodecs_encoder_set_output_callback(enc->encoder, gst_shvideo_enc_write_output, enc);

	ret = GetFromCtrlFtoEncParam(enc->encoder, &enc->ainfo);
	if (ret < 0) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("Error reading control file."), (NULL));
	}

	if (enc->fps_numerator && enc->fps_denominator) {
		shcodecs_encoder_set_frame_rate(enc->encoder,
						(enc->fps_numerator / enc->fps_denominator) * 10);

		if (enc->format == SHCodecs_Format_H264) {
			shcodecs_encoder_set_h264_sps_frame_rate_info(enc->encoder,
									  enc->fps_numerator,
									  enc->fps_denominator);
		}
	}
	shcodecs_encoder_set_xpic_size(enc->encoder, enc->width);
	shcodecs_encoder_set_ypic_size(enc->encoder, enc->height);

	shcodecs_encoder_set_frame_no_increment(enc->encoder,
						shcodecs_encoder_get_frame_num_resolution
						(enc->encoder) /
						(shcodecs_encoder_get_frame_rate(enc->encoder) /
						 10));

	GST_DEBUG_OBJECT(enc, "Encoder init: %ldx%ld %.2ffps format:%ld",
			 shcodecs_encoder_get_xpic_size(enc->encoder),
			 shcodecs_encoder_get_ypic_size(enc->encoder),
			 shcodecs_encoder_get_frame_rate(enc->encoder) / 10.0,
			 shcodecs_encoder_get_frame_rate(enc->encoder));

	capture_start_capturing(enc->ainfo.ceu);

	/* Create the threads */
	if (!enc->capture_thread) {
		pthread_create(&enc->capture_thread, NULL, capture_thread, enc);
	}
	if (!enc->blit_thread) {
		pthread_create(&enc->blit_thread, NULL, blit_thread, enc);
	}

	ret = shcodecs_encoder_run(enc->encoder);

	GST_DEBUG_OBJECT(enc, "shcodecs_encoder_run returned %d\n", ret);

	gst_pad_push_event(enc->srcpad, gst_event_new_eos());

	pthread_mutex_unlock(&enc->launch_mutex);

	return NULL;
}


GType gst_shvideo_capenc_get_type(void)
{
	static GType object_type = 0;

	GST_LOG("%s called", __func__);
	if (object_type == 0) {
		static const GTypeInfo object_info = {
			sizeof(GstSHVideoCapEncClass),
			gst_shvideo_enc_base_init,
			NULL,
			gst_shvideo_enc_init_class,
			NULL,
			NULL,
			sizeof(GstSHVideoCapEnc),
			0,
			(GInstanceInitFunc) gst_shvideo_enc_init
		};

		object_type =
			g_type_register_static(GST_TYPE_ELEMENT,
					   "gst-sh-mobile-camera-enc", &object_info,
					   (GTypeFlags) 0);
	}
	return object_type;
}

/** Reads the capabilities of the peer element behind source pad
	@param shvideoenc encoder object
*/
static void gst_shvideocameraenc_read_src_caps(GstSHVideoCapEnc * shvideoenc)
{
	GstStructure *structure;

	GST_LOG_OBJECT(shvideoenc, "%s called", __func__);

	/* Get the caps of the next element in chain */
	shvideoenc->out_caps = gst_pad_peer_get_caps(shvideoenc->srcpad);

	/* Any format is ok too */
	if (!gst_caps_is_any(shvideoenc->out_caps)) {
		structure = gst_caps_get_structure(shvideoenc->out_caps, 0);
		if (!strcmp(gst_structure_get_name(structure), "video/mpeg")) {
			shvideoenc->format = SHCodecs_Format_MPEG4;
		} else if (!strcmp(gst_structure_get_name(structure), "video/x-h264")) {
			shvideoenc->format = SHCodecs_Format_H264;
		}

		gst_structure_get_int(structure, "width", &shvideoenc->width);
		gst_structure_get_int(structure, "height", &shvideoenc->height);
		gst_structure_get_fraction(structure, "framerate",
					   &shvideoenc->fps_numerator,
					   &shvideoenc->fps_denominator);
	}
}


/** Sets the capabilities of the source pad
	@param shvideoenc encoder object
	@return TRUE if the capabilities could be set, otherwise FALSE
*/
static gboolean gst_shvideocameraenc_set_src_caps(GstPad * pad, GstCaps * caps)
{
	GstStructure *structure = NULL;
	GstSHVideoCapEnc *shvideoenc = (GstSHVideoCapEnc *) (GST_OBJECT_PARENT(pad));
	gboolean ret = TRUE;

	GST_LOG_OBJECT(shvideoenc, "%s called", __func__);

	if (shvideoenc->encoder != NULL) {
		GST_DEBUG_OBJECT(shvideoenc, "%s: Encoder already opened", __func__);
		return FALSE;
	}

	structure = gst_caps_get_structure(caps, 0);

	if (!strcmp(gst_structure_get_name(structure), "video/x-h264")) {
		GST_DEBUG_OBJECT(shvideoenc, "codec format is video/x-h264");
		shvideoenc->format = SHCodecs_Format_H264;
	} else if (!strcmp(gst_structure_get_name(structure), "video/mpeg")) {
		GST_DEBUG_OBJECT(shvideoenc, "codec format is video/mpeg");
		shvideoenc->format = SHCodecs_Format_MPEG4;
	} else {
		GST_DEBUG_OBJECT(shvideoenc, "%s failed (not supported: %s)",
				 __func__, gst_structure_get_name(structure));
		return FALSE;
	}

	if (!gst_structure_get_fraction(structure, "framerate",
					&shvideoenc->fps_numerator, &shvideoenc->fps_denominator)) {
		GST_DEBUG_OBJECT(shvideoenc, "%s failed (no framerate)", __func__);
		return FALSE;
	}

	if (!gst_structure_get_int(structure, "width", &shvideoenc->width)) {
		GST_DEBUG_OBJECT(shvideoenc, "%s failed (no width)", __func__);
		return FALSE;
	}

	if (!gst_structure_get_int(structure, "height", &shvideoenc->height)) {
		GST_DEBUG_OBJECT(shvideoenc, "%s failed (no height)", __func__);
		return FALSE;
	}

	if (!gst_pad_set_caps(shvideoenc->srcpad, caps)) {
		GST_ELEMENT_ERROR((GstElement *) shvideoenc, CORE, NEGOTIATION,
				  ("Source pad not linked."), (NULL));
		ret = FALSE;
	}
	if (!gst_pad_set_caps(gst_pad_get_peer(shvideoenc->srcpad), caps)) {
		GST_ELEMENT_ERROR((GstElement *) shvideoenc, CORE, NEGOTIATION,
				  ("Source pad not linked."), (NULL));
		ret = FALSE;
	}
	gst_caps_unref(caps);

	return ret;
}

gboolean gst_shvideo_camera_enc_plugin_init(GstPlugin * plugin)
{
	GST_LOG("%s called", __func__);
	if (!gst_element_register
		(plugin, "gst-sh-mobile-camera-enc", GST_RANK_PRIMARY, GST_TYPE_SH_VIDEO_CAPENC))
		return FALSE;

	return TRUE;
}

