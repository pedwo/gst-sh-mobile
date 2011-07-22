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
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <linux/videodev2.h>	/* For pixel formats */
#include <uiomux/uiomux.h>
#include <shveu/shveu.h>
#include <shcodecs/shcodecs_encoder.h>

#include "gstshvideocapenc.h"
#include "ControlFileUtil.h"
#include "capture.h"
#include "display.h"
#include "thrqueue.h"

#define CHROMA_ALIGNMENT 16

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
	capture *ceu;

	GstCaps *out_caps;
	gboolean caps_set;
	glong frame_number;

	GstClock *clock;
	gboolean start_time_set;
	GstClockTime start_time;
	GstBuffer *buffered_output;

	pthread_t enc_thread;
	pthread_t capture_thread;

	struct Queue * enc_input_q;
	struct Queue * enc_input_empty_q;

	UIOMux *uiomux;
	SHVEU *veu;
	DISPLAY *display;

	int cap_w;
	int cap_h;
	GstCameraPreview preview;

	/* This is used to stop the plugin sending data downstream when PAUSED */
	gboolean hold_output;

	/* These flags stop the threads in turn */
	gboolean stop_capture_thr;
	gboolean stop_encode_thr;
};


/**
 * Define Gstreamer SH Video Encoder Class structure
 */
struct _GstSHVideoCapEncClass {
	GstElementClass parent;
};


/**
 * \var enc_src_factory
 * Name: src \n
 * Direction: src \n
 * Available: always \n
 * Caps:
 * - video/mpeg, width=(int)[48, 1280], height=(int)[48, 720],
 *   framerate=(fraction)[1, 30], mpegversion=(int)4
 * - video/x-h264, width=(int)[48, 1280], height=(int)[48, 720],
 *   framerate=(fraction)[1, 30], h264version=(int)h264
 */
static GstStaticPadTemplate enc_src_factory =
	GST_STATIC_PAD_TEMPLATE("src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS(
			"video/mpeg,"
			"width  = (int) [48, 1280],"
			"height = (int) [48, 720],"
			"framerate = (fraction) [0, 30],"
			"mpegversion = (int) 4"
			"; "
			"video/x-h264,"
			"width  = (int) [48, 1280],"
			"height = (int) [48, 720],"
			"framerate = (fraction) [0, 30],"
			"variant = (string) itu,"
			"h264version = (string) h264"
			)
		);

GST_DEBUG_CATEGORY_STATIC(gst_sh_video_capenc_debug);
#define GST_CAT_DEFAULT gst_sh_video_capenc_debug

static GstElementClass *parent_class = NULL;

/**
 * Define encoder properties
 */
enum {
	PROP_0,
	PROP_CNTL_FILE,
	PROP_PREVIEW,
	PROP_LAST
};

static void gst_sh_video_enc_init_class(gpointer g_class, gpointer data);
static void gst_sh_video_enc_base_init(gpointer klass);
static void gst_sh_video_enc_dispose(GObject * object);
static void gst_sh_video_enc_class_init(GstSHVideoCapEncClass * klass);
static void gst_sh_video_enc_init(GstSHVideoCapEnc * enc, GstSHVideoCapEncClass * gklass);
static void gst_sh_video_enc_set_property(GObject *object, guint prop_id,
					const GValue *value, GParamSpec * pspec);
static void gst_sh_video_enc_get_property(GObject * object, guint prop_id,
					GValue * value, GParamSpec * pspec);
static gboolean gst_sh_video_enc_src_query(GstPad * pad, GstQuery * query);
static int gst_sh_video_enc_get_input(SHCodecs_Encoder * encoder, void *user_data);
static int gst_sh_video_enc_write_output(SHCodecs_Encoder * encoder,
					unsigned char *data, int length, void *user_data);
static void gst_sh_video_enc_init_camera_encoder(GstSHVideoCapEnc * enc);
static void *launch_camera_encoder_thread(void *data);
static GType gst_camera_preview_get_type(void);
static gboolean gst_sh_video_enc_src_event(GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_sh_video_enc_change_state(GstElement *
							 element, GstStateChange transition);
static gboolean gst_sh_video_enc_set_clock(GstElement * element, GstClock * clock);
static gboolean gst_sh_video_enc_set_src_caps(GstPad * pad, GstCaps * caps);
static void gst_sh_video_enc_read_src_caps(GstSHVideoCapEnc * enc);

#define GST_TYPE_CAMERA_PREVIEW (gst_camera_preview_get_type())
static GType gst_camera_preview_get_type(void)
{
	static GType object_type = 0;
	static const GEnumValue preview_method[] = {
		{PREVIEW_OFF, "No camera preview", "off"},
		{PREVIEW_ON, "Camera preview", "on"},
		{0, NULL, NULL},
	};

	if (object_type == 0) {
		object_type = g_enum_register_static("GstCameraPreview", preview_method);
	}
	return object_type;
}

/******************
 * CAPTURE THREAD *
 ******************/

/**
 * CEU callback function
 * received a full frame from the camera
 * @param capture
 * @param frame_data output buffer pointer
 * @param length buffer size
 * @param user_data user pointer
 */
static void
capture_image_cb(capture * ceu, const unsigned char *frame_data, size_t length, void *user_data)
{
	GstSHVideoCapEnc *pvt = (GstSHVideoCapEnc *) user_data;
	struct ren_vid_surface cap_surface;
	struct ren_vid_surface enc_surface;

	GST_DEBUG_OBJECT(pvt, "Captured a frame");

	cap_surface.format = REN_NV12;
	cap_surface.w = pvt->cap_w;
	cap_surface.h = pvt->cap_h;
	cap_surface.pitch = cap_surface.w;
	cap_surface.py = (void*)frame_data;
	cap_surface.pc = cap_surface.py + (cap_surface.pitch * cap_surface.h);
	cap_surface.pa = NULL;

	enc_surface.format = REN_NV12;
	enc_surface.w = pvt->width;
	enc_surface.h = pvt->height;
	enc_surface.pitch = enc_surface.w;
	enc_surface.pa = NULL;

	/* Get an empty encoder input frame */
	enc_surface.py = queue_deq(pvt->enc_input_empty_q);
	enc_surface.pc = enc_surface.py + (enc_surface.pitch * enc_surface.h);

	GST_DEBUG_OBJECT(pvt, "Starting blit to encoder input buffer...");

	/* Hardware resize */
	shveu_resize(pvt->veu, &cap_surface, &enc_surface);

	GST_DEBUG_OBJECT(pvt, "Blit to encoder input buffer complete");

	pvt->stop_encode_thr = pvt->stop_capture_thr;
	queue_enq(pvt->enc_input_q, enc_surface.py);

	if (pvt->preview == PREVIEW_ON) {
		display_update(pvt->display, &cap_surface);
		GST_DEBUG_OBJECT(pvt, "Display update complete");
	}

	capture_queue_buffer (pvt->ceu, cap_surface.py);
}

static void *capture_thread(void *data)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) data;
	guint64 time_diff, stamp_diff, sleep_time;
	GstClockTime time_now;

	while (!enc->stop_encode_thr) {
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
			usleep(sleep_time * 1000);
		} else {
			GST_DEBUG_OBJECT(enc, "Late by %lldms", time_diff-stamp_diff);
		}

		capture_get_frame(enc->ceu, capture_image_cb, enc);
	}

	return NULL;
}

/**
 * Initialize shvideoenc class
 * @param g_class Gclass
 * @param data user data pointer, unused in the function
 */
static void
gst_sh_video_enc_init_class(gpointer g_class, gpointer data)
{
	parent_class = g_type_class_peek_parent(g_class);
	gst_sh_video_enc_class_init((GstSHVideoCapEncClass *) g_class);
}

/**
 * Initialize SH hardware video encoder
 * @param klass Gstreamer element class
 */
static void
gst_sh_video_enc_base_init(gpointer klass)
{
	static const GstElementDetails plugin_details =
		GST_ELEMENT_DETAILS("SH hardware camera capture & video encoder",
				"Codec/Encoder/Video/Src",
				"Encode mpeg-based video stream (mpeg4, h264)",
				"Takashi Namiki <takashi.namiki@renesas.com>");
	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	GST_LOG("%s called", __func__);
	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get(&enc_src_factory));
	gst_element_class_set_details(element_class, &plugin_details);
}

/**
 * Dispose encoder
 * @param object Gstreamer element class
 */
static void
gst_sh_video_enc_dispose(GObject * object)
{
	GstSHVideoCapEnc *enc = GST_SH_VIDEO_CAPENC(object);
	void *thread_ret;
	GST_LOG("%s called", __func__);

	enc->stop_capture_thr = TRUE;
	pthread_join(enc->enc_thread, &thread_ret);

	capture_stop_capturing(enc->ceu);

	if (enc->encoder != NULL) {
		shcodecs_encoder_close(enc->encoder);
		enc->encoder = NULL;
	}

	if (enc->preview == PREVIEW_ON) {
		display_close(enc->display);
	}

	shveu_close(enc->veu);
	capture_close(enc->ceu);
	uiomux_close(enc->uiomux);

	G_OBJECT_CLASS(parent_class)->dispose(object);
}


static gboolean
gst_sh_video_enc_set_clock(GstElement * element, GstClock * clock)
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


/**
 * Initialize the class for encoder
 * @param klass Gstreamer SH video encoder class
 */
static void
gst_sh_video_enc_class_init(GstSHVideoCapEncClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	gobject_class->dispose = gst_sh_video_enc_dispose;
	gobject_class->set_property = gst_sh_video_enc_set_property;
	gobject_class->get_property = gst_sh_video_enc_get_property;
	gstelement_class->set_clock = gst_sh_video_enc_set_clock;
	gstelement_class->change_state = gst_sh_video_enc_change_state;

	GST_DEBUG_CATEGORY_INIT(gst_sh_video_capenc_debug,
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

/**
 * Initialize the encoder
 * @param enc Gstreamer SH video element
 * @param gklass Gstreamer SH video encode class
 */
static void
gst_sh_video_enc_init(GstSHVideoCapEnc * enc, GstSHVideoCapEncClass * gklass)
{
	GstElementClass *klass = GST_ELEMENT_GET_CLASS(enc);

	GST_LOG_OBJECT(enc, "%s called", __func__);

	enc->srcpad = gst_pad_new_from_template(
		gst_element_class_get_pad_template(klass, "src"), "src");
	gst_pad_set_setcaps_function(enc->srcpad, gst_sh_video_enc_set_src_caps);
	gst_pad_set_query_function(enc->srcpad, gst_sh_video_enc_src_query);
	gst_pad_set_event_function(enc->srcpad, gst_sh_video_enc_src_event);
	gst_element_add_pad(GST_ELEMENT(enc), enc->srcpad);

	enc->encoder = NULL;
	enc->caps_set = FALSE;
	enc->stop_capture_thr = FALSE;
	enc->stop_encode_thr = FALSE;
	enc->enc_thread = 0;

	/* Initialize the queues */
	enc->enc_input_q = queue_init();
	enc->enc_input_empty_q = queue_init();

	enc->format = SHCodecs_Format_NONE;
	enc->out_caps = NULL;
	enc->width = 0;
	enc->height = 0;
	enc->fps_numerator = 25;
	enc->fps_denominator = 1;
	enc->frame_number = 0;
	enc->preview = PREVIEW_OFF;
	enc->hold_output = TRUE;
	enc->start_time_set = FALSE;
	enc->buffered_output = NULL;
}


static GstStateChangeReturn
gst_sh_video_enc_change_state(GstElement * element, GstStateChange transition)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) element;
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	GST_DEBUG_OBJECT(enc, "%s called", __func__);

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		GST_DEBUG_OBJECT(enc, "GST_STATE_CHANGE_NULL_TO_READY");
		enc->hold_output = TRUE;
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		GST_DEBUG_OBJECT(enc, "GST_STATE_CHANGE_READY_TO_PAUSED");
		enc->hold_output = FALSE;
		gst_sh_video_enc_init_camera_encoder(enc);
		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		GST_DEBUG_OBJECT(enc, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
		enc->hold_output = FALSE;
		break;
	default:
		break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		GST_DEBUG_OBJECT(enc, "GST_STATE_CHANGE_PLAYING_TO_PAUSED");
		enc->hold_output = TRUE;
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		GST_DEBUG_OBJECT(enc, "GST_STATE_CHANGE_PAUSED_TO_READY");
		enc->hold_output = TRUE;
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_DEBUG_OBJECT(enc, "GST_STATE_CHANGE_READY_TO_NULL");
		enc->hold_output = TRUE;
		break;
	default:
		break;
	}
	return ret;
}



/**
 * Event handler for encoder src events, see GstPadEventFunction.
 */
static gboolean
gst_sh_video_enc_src_event(GstPad * pad, GstEvent * event)
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


/**
 * The function will set the user defined control file name value for decoder
 * @param object The object where to get Gstreamer SH video Encoder object
 * @param prop_id The property id
 * @param value In this case file name if prop_id is PROP_CNTL_FILE
 * @param value In this case file name if prop_id is PROP_PREVIEW
 * @param pspec not used in function
 */
static void
gst_sh_video_enc_set_property(GObject * object, guint prop_id,
				 const GValue * value, GParamSpec * pspec)
{
	GstSHVideoCapEnc *enc = GST_SH_VIDEO_CAPENC(object);

	GST_LOG("%s called", __func__);
	switch (prop_id) {
	case PROP_CNTL_FILE:
		strcpy(enc->ainfo.ctrl_file_name_buf, g_value_get_string(value));
		break;
	case PROP_PREVIEW:
		enc->preview = g_value_get_enum(value);
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
	@param pspec not used in function
*/
static void
gst_sh_video_enc_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	GstSHVideoCapEnc *enc = GST_SH_VIDEO_CAPENC(object);

	GST_LOG("%s called", __func__);
	switch (prop_id) {
	case PROP_CNTL_FILE:
		g_value_set_string(value, enc->ainfo.ctrl_file_name_buf);
		break;
	case PROP_PREVIEW:
		g_value_set_enum(value, enc->preview);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

/******************
 * ENCODER THREAD *
 ******************/

/* SHCodecs_Encoder_Input_Release callback */
static int
gst_sh_video_enc_release_input_buf(SHCodecs_Encoder * encoder,
                             unsigned char * y_input,
                             unsigned char * c_input,
                             void * user_data)
{
	GstSHVideoCapEnc *pvt = (GstSHVideoCapEnc *) user_data;

	GST_LOG_OBJECT(pvt, "Got an encoder input buffer");
	queue_enq (pvt->enc_input_empty_q, y_input);

	return 0;
}

/**
 * Callback function for the encoder input
 * @param encoder shcodecs encoder
 * @param user_data Gstreamer SH encoder object
 * @return 0 if encoder should continue. 1 if encoder should pause.
 */
static int
gst_sh_video_enc_get_input(SHCodecs_Encoder *encoder, void *user_data)
{
	GstSHVideoCapEnc *pvt = (GstSHVideoCapEnc *) user_data;
	void *py, *pc;

	GST_LOG_OBJECT(pvt, "Waiting for blit to complete");

	/* Get a scaled frame from the queue */
	py = queue_deq(pvt->enc_input_q);
	pc = py + (pvt->width * pvt->height);
	shcodecs_encoder_input_provide (encoder, py, pc);

	GST_LOG_OBJECT(pvt, "Got input buffer");

	if (pvt->stop_encode_thr == TRUE) {
		return -1;
	}
	return 0;
}

/**
 * Launches the encoder in an own thread
 * @param data encoder object
 */
static void *launch_camera_encoder_thread(void *data)
{
	gint ret;
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) data;
	void *thread_ret;
	int i;

	GST_LOG_OBJECT(enc, "%s called", __func__);

	/* wait for  READY status */
	while (enc->hold_output == TRUE) {
		usleep(10);
	}

	if (enc->stop_encode_thr == TRUE)
		return NULL;

	gst_sh_video_enc_read_src_caps(enc);
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

	/* UIOMux initialization */
	enc->uiomux = uiomux_open();
	if (enc->uiomux == NULL) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("Error opening uiomux"), (NULL));
	}

	/* VEU initialization */
	enc->veu = shveu_open_named("VEU");
	if (enc->veu == NULL) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("Error opening VEU"), (NULL));
	}

	/* Display output */
	if (enc->preview == PREVIEW_ON) {
		enc->display = display_open();
		if (!enc->display) {
			GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
					  ("Error opening fb device"), (NULL));
		}
	}

	/* ceu open */
	enc->ceu = capture_open_userio(enc->ainfo.input_file_name_buf,
					enc->ainfo.xpic, enc->ainfo.ypic);
	if (enc->ceu == NULL) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("Error opening CEU"), (NULL));
	}
	enc->cap_w = capture_get_width(enc->ceu);
	enc->cap_h = capture_get_height(enc->ceu);

	if (capture_get_pixel_format (enc->ceu) != V4L2_PIX_FMT_NV12) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("Camera capture pixel format is not supported"), (NULL));
	}

	/* Check for frame size that result in v4l2 capture buffers with the CbCr
	   plane located at an unsupported memory alignment. */
	if ((enc->width * enc->height) & (CHROMA_ALIGNMENT-1)) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
				  ("unsupported encode size due to Chroma plane alignment"), (NULL));
	}

	GST_DEBUG_OBJECT(enc, "Capturing at %dx%d", enc->cap_w, enc->cap_h);

	enc->encoder = shcodecs_encoder_init(enc->width, enc->height, enc->format);

	shcodecs_encoder_set_input_callback(enc->encoder, gst_sh_video_enc_get_input, enc);
	shcodecs_encoder_set_output_callback(enc->encoder, gst_sh_video_enc_write_output, enc);
	shcodecs_encoder_set_input_release_callback(enc->encoder, gst_sh_video_enc_release_input_buf, enc);

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

	/* Allocate & queue encoder input frames */
	for (i=0; i<2; i++) {
		int size = (enc->width * enc->height * 3) / 2;
		void *frame = uiomux_malloc(enc->uiomux, UIOMUX_SH_VEU, size, 32);
		if (frame == 0) {
			GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
					  ("Error allocating encoder input frames."), (NULL));
		}
		queue_enq(enc->enc_input_empty_q, frame);
	}

	GST_DEBUG_OBJECT(enc, "Encoder init: %ldx%ld %.2ffps format:%ld",
			 shcodecs_encoder_get_xpic_size(enc->encoder),
			 shcodecs_encoder_get_ypic_size(enc->encoder),
			 shcodecs_encoder_get_frame_rate(enc->encoder) / 10.0,
			 shcodecs_encoder_get_frame_rate(enc->encoder));

	capture_start_capturing(enc->ceu);

	/* Create the threads */
	if (!enc->capture_thread) {
		pthread_create(&enc->capture_thread, NULL, capture_thread, enc);
	}

	ret = shcodecs_encoder_run(enc->encoder);

	GST_DEBUG_OBJECT(enc, "shcodecs_encoder_run returned %d\n", ret);

	gst_pad_push_event(enc->srcpad, gst_event_new_eos());

	/* Wait for threads to finish */
	pthread_join(enc->capture_thread, &thread_ret);

	gst_pad_push_event(enc->srcpad, gst_event_new_eos());

	return NULL;
}


GType gst_shvideo_capenc_get_type(void)
{
	static GType object_type = 0;

	if (object_type == 0)
	{
		static const GTypeInfo object_info =
		{
			sizeof(GstSHVideoCapEncClass),
			gst_sh_video_enc_base_init,
			NULL,
			gst_sh_video_enc_init_class,
			NULL,
			NULL,
			sizeof(GstSHVideoCapEnc),
			0,
			(GInstanceInitFunc)gst_sh_video_enc_init
		};

		object_type =
			g_type_register_static(GST_TYPE_ELEMENT,
						"gst-sh-mobile-camera-enc",
						&object_info, (GTypeFlags)0);
	}

	return object_type;
}

/**
 * Reads the capabilities of the peer element connected to the source pad
 *  @param shvideoenc encoder object
 */
static void
gst_sh_video_enc_read_src_caps(GstSHVideoCapEnc * enc)
{
	GstStructure *structure;

	GST_LOG_OBJECT(enc, "%s called", __func__);

	/* Get the caps of the next element in chain */
	enc->out_caps = gst_pad_peer_get_caps(enc->srcpad);

	/* Any format is ok too */
	if (!gst_caps_is_any(enc->out_caps)) {
		structure = gst_caps_get_structure(enc->out_caps, 0);
		if (!strcmp(gst_structure_get_name(structure), "video/mpeg")) {
			enc->format = SHCodecs_Format_MPEG4;
		} else if (!strcmp(gst_structure_get_name(structure), "video/x-h264")) {
			enc->format = SHCodecs_Format_H264;
		}

		gst_structure_get_int(structure, "width", &enc->width);
		gst_structure_get_int(structure, "height", &enc->height);
		gst_structure_get_fraction(structure, "framerate",
					   &enc->fps_numerator,
					   &enc->fps_denominator);
	}
}


/**
 * Sets the capabilities of the source pad
 * @param shvideoenc encoder object
 * @return TRUE if the capabilities could be set, otherwise FALSE
 */
static gboolean
gst_sh_video_enc_set_src_caps(GstPad * pad, GstCaps * caps)
{
	GstStructure *structure = NULL;
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) (GST_OBJECT_PARENT(pad));
	gboolean ret = TRUE;

	GST_LOG_OBJECT(enc, "%s called", __func__);

	if (enc->encoder != NULL) {
		GST_DEBUG_OBJECT(enc, "%s: Encoder already opened", __func__);
		return FALSE;
	}

	structure = gst_caps_get_structure(caps, 0);

	if (!strcmp(gst_structure_get_name(structure), "video/x-h264")) {
		GST_DEBUG_OBJECT(enc, "codec format is video/x-h264");
		enc->format = SHCodecs_Format_H264;
	} else if (!strcmp(gst_structure_get_name(structure), "video/mpeg")) {
		GST_DEBUG_OBJECT(enc, "codec format is video/mpeg");
		enc->format = SHCodecs_Format_MPEG4;
	} else {
		GST_DEBUG_OBJECT(enc, "%s failed (not supported: %s)",
				 __func__, gst_structure_get_name(structure));
		return FALSE;
	}

	if (!gst_structure_get_fraction(structure, "framerate",
					&enc->fps_numerator, &enc->fps_denominator)) {
		GST_DEBUG_OBJECT(enc, "%s failed (no framerate)", __func__);
		return FALSE;
	}

	if (!gst_structure_get_int(structure, "width", &enc->width)) {
		GST_DEBUG_OBJECT(enc, "%s failed (no width)", __func__);
		return FALSE;
	}

	if (!gst_structure_get_int(structure, "height", &enc->height)) {
		GST_DEBUG_OBJECT(enc, "%s failed (no height)", __func__);
		return FALSE;
	}

	/* Check for frame size that result in v4l2 capture buffers with the CbCr
	   plane located at an unsupported memory alignment. */
	if ((enc->width * enc->height) & (CHROMA_ALIGNMENT-1)) {
		GST_DEBUG_OBJECT(enc, "%s failed "
				"(unsupported size due to Chroma plane alignment)", __func__);
		return FALSE;
	}

	if (!gst_pad_set_caps(enc->srcpad, caps)) {
		GST_ELEMENT_ERROR((GstElement *)enc, CORE, NEGOTIATION,
					("Source pad not linked."), (NULL));
		ret = FALSE;
	}
	if (!gst_pad_set_caps(gst_pad_get_peer(enc->srcpad), caps)) {
		GST_ELEMENT_ERROR((GstElement *)enc, CORE, NEGOTIATION,
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

/**
 * Initializes the SH Hardware encoder
 * @param shvideoenc encoder object
 */
static void
gst_sh_video_enc_init_camera_encoder(GstSHVideoCapEnc * enc)
{
	gint ret = 0;
	glong fmt = 0;

	GST_LOG_OBJECT(enc, "%s called", __func__);

	ret = GetFromCtrlFTop((const char *)
			enc->ainfo.ctrl_file_name_buf,
			&enc->ainfo,
			&fmt);
	if (ret < 0) {
		GST_ELEMENT_ERROR((GstElement *) enc, CORE, FAILED,
			("Error reading control file."), (NULL));
	}

	if (!enc->enc_thread) {
		/* We'll have to launch the encoder in
		   a separate thread to keep the pipeline running */
		pthread_create(&enc->enc_thread, NULL, launch_camera_encoder_thread, enc);
	}
}

/**
 * Callback function for the encoder output
 * @param encoder shcodecs encoder
 * @param data the encoded video frame
 * @param length size the encoded video frame buffer
 * @param user_data Gstreamer SH encoder object
 * @return 0 if encoder should continue. 1 if encoder should pause.
 */
static int
gst_sh_video_enc_write_output(SHCodecs_Encoder * encoder,
			unsigned char *data, int length, void *user_data)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) user_data;
	GstBuffer *buf = NULL;
	gint ret = 0;
	int frm_delta;

	GST_LOG_OBJECT(enc, "Got %d bytes data frame number: %ld\n",
				   length, enc->frame_number);

	if (length <= 0)
		return 0;

	buf = gst_buffer_new();
	gst_buffer_set_data(buf, data, length);

	if (enc->buffered_output != NULL) {
		buf = gst_buffer_join(enc->buffered_output, buf);
		enc->buffered_output = NULL;
	}
	frm_delta = shcodecs_encoder_get_frame_num_delta(enc->encoder);

	if (frm_delta > 0) {
		GST_BUFFER_DURATION(buf) =
			frm_delta * enc->fps_denominator * 1000 * GST_MSECOND / enc->fps_numerator;
		GST_BUFFER_TIMESTAMP(buf) = enc->frame_number * GST_BUFFER_DURATION(buf);
		GST_BUFFER_OFFSET(buf) = enc->frame_number;
		enc->frame_number += frm_delta;
		ret = gst_pad_push(enc->srcpad, buf);
		if (ret != GST_FLOW_OK) {
			GST_DEBUG_OBJECT(enc, "pad_push failed: %s.", gst_flow_get_name(ret));
			// Do not return -1. This would cause shcodecs_encoder_run to stop.
			// TODO should keep data in case PAUSED before PLAYING
		}
	} else {
		enc->buffered_output = buf;
	}

	return 0;
}

/**
 * Gstreamer source pad query
 * @param pad Gstreamer source pad
 * @param query Gstreamer query
 * @return Returns the value of gst_pad_query_default
 */
static gboolean
gst_sh_video_enc_src_query(GstPad * pad, GstQuery * query)
{
	GstSHVideoCapEnc *enc = (GstSHVideoCapEnc *) (GST_OBJECT_PARENT(pad));
	GST_LOG_OBJECT(enc, "%s called", __func__);
	return gst_pad_query_default(pad, query);
}
