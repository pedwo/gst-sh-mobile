/**
 * gst-sh-mobile-video-dec
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA	02110-1301 USA
 *
 * Phil Edworthy <phil.edworthy@renesas.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/fb.h>

#include <shcodecs/shcodecs.h>
#include <shveu/shveu.h>

#include "gstshvideodec2.h"

/**
 * Define Gstreamer SH Video Decoder structure
 */
struct _Gstshvideodec {
	GstElement element;

	GstPad *sinkpad;
	GstPad *srcpad;

	/* Input stream */
	SHCodecs_Format format;
	gint width;
	gint height;
	gint fps_numerator;
	gint fps_denominator;
	SHCodecs_Decoder *decoder;

	gboolean caps_set;

	/* Buffer for input data that hasn't been consumed yet */
	GstBuffer *pcache;

	/* VEU handle */
	int veu;
	gint out_width;
	gint out_height;

	/* Timestamp info to pass from the input buffer to the output buffer */
	GstClockTime timestamp;
	GstClockTime duration;
	gboolean timestamp_valid;

	gboolean codec_data_present;
	gboolean codec_data_present_first;
	guint num_sps;
	guint sps_size;
	GstBuffer *codec_data_sps_buf;
	guint num_pps;
	guint pps_size;
	GstBuffer *codec_data_pps_buf;
	gint buffer;
	struct fb_fix_screeninfo fb_fix;
	struct fb_var_screeninfo fb_var;
};


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

/* Define source (output) pad capabilities. */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src",
								  GST_PAD_SRC,
								  GST_PAD_ALWAYS,
								  GST_STATIC_CAPS
								  ("video/x-raw-rgb, "
								   "bpp = (int) 16, "
								   "depth = (int) 16, "
								   "width = (int) [48, MAX], "
								   "height = (int) [48, MAX]")
	);


static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC(gst_sh_mobile_debug);
#define GST_CAT_DEFAULT gst_sh_mobile_debug


/* Forward declarations */
static void gst_shvideodec_init_class(gpointer g_class, gpointer data);
static void gst_shvideodec_class_init(GstshvideodecClass * klass);
static void gst_shvideodec_base_init(gpointer klass);
static void gst_shvideodec_dispose(GObject * object);
static void gst_shvideodec_init(Gstshvideodec * dec, GstshvideodecClass * g_class);
static gboolean gst_shvideodec_sink_event(GstPad * pad, GstEvent * event);
static gboolean gst_shvideodec_set_sink_caps(GstPad * pad, GstCaps * caps);
static gboolean gst_shvideodec_set_src_caps(GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_shvideodec_chain(GstPad * pad, GstBuffer * inbuffer);

static int gst_shcodecs_decoded_callback(SHCodecs_Decoder * decoder,
					 unsigned char *y_buf, int y_size,
					 unsigned char *c_buf, int c_size, void *user_data);



/** Initialize shvideodec class plugin, see GClassInitFunc.
 */
static void gst_shvideodec_init_class(gpointer g_class, gpointer class_data)
{
	parent_class = g_type_class_peek_parent(g_class);
	gst_shvideodec_class_init((GstshvideodecClass *) g_class);
}

/* Private Function: Initialize the class for decoder. */
static void gst_shvideodec_class_init(GstshvideodecClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	GST_DEBUG_CATEGORY_INIT(gst_sh_mobile_debug, "gst-sh-mobile-video-dec",
				0, "Decoder and player for H264/MPEG4 streams");

	gobject_class->dispose = gst_shvideodec_dispose;
}


/** Initialize SH video decoder, see GBaseInitFunc.
 */
static void gst_shvideodec_base_init(gpointer g_class)
{
	static const GstElementDetails plugin_details =
		GST_ELEMENT_DETAILS("SH hardware video decoder",
				"Codec/Decoder/Video",
				"Decode video (H264 & Mpeg4)",
				"Phil Edworthy <phil.edworthy@renesas.com>");

	GstElementClass *element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get(&sink_factory));
	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get(&src_factory));
	gst_element_class_set_details(element_class, &plugin_details);
}

/** Dispose decoder
    @param object Gstreamer element class
*/
static void gst_shvideodec_dispose(GObject * object)
{
	Gstshvideodec *dec = GST_SHVIDEODEC(object);

	GST_LOG_OBJECT(dec, "%s called\n", __func__);

	if (dec->decoder != NULL) {
		GST_DEBUG_OBJECT(dec, "close decoder object %p", dec->decoder);
		shcodecs_decoder_close(dec->decoder);
	}
	shveu_close();
	G_OBJECT_CLASS(parent_class)->dispose(object);
}

/** Initialize the decoder, see GInstanceInitFunc.
 *	Initializes a new element instance, instantiates pads and sets the pad
 *	callback functions.
 */
static void gst_shvideodec_init(Gstshvideodec * dec, GstshvideodecClass * g_class)
{
	int fb;
	GstElementClass *kclass = GST_ELEMENT_GET_CLASS(dec);

	GST_LOG_OBJECT(dec, "%s called", __func__);

	/* Encoded video sink pad */
	dec->sinkpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(kclass, "sink"), "sink");
	gst_pad_set_setcaps_function(dec->sinkpad, gst_shvideodec_set_sink_caps);
	gst_pad_set_chain_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_shvideodec_chain));
	gst_pad_set_event_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_shvideodec_sink_event));

	/* Decoded video source pad */
	dec->srcpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(kclass, "src"), "src");
	gst_pad_set_setcaps_function(dec->srcpad, gst_shvideodec_set_src_caps);

	gst_element_add_pad(GST_ELEMENT(dec), dec->sinkpad);
	gst_element_add_pad(GST_ELEMENT(dec), dec->srcpad);

	dec->buffer = 0;	//for the FB flip
	/* get current settings */
	if (-1 == (fb = open("/dev/fb0", O_RDWR))) {
		fprintf(stderr, "Open %s: %s.\n", "/dev/fb0", strerror(errno));
	}
	if (-1 == ioctl(fb, FBIOGET_FSCREENINFO, &dec->fb_fix)) {
		fprintf(stderr, "Ioctl FBIOGET_FSCREENINFO error.\n");
	}
	if (-1 == ioctl(fb, FBIOGET_VSCREENINFO, &dec->fb_var)) {
		fprintf(stderr, "Ioctl FBIOGET_VSCREENINFO error.\n");
	}
	if (dec->fb_fix.type != FB_TYPE_PACKED_PIXELS) {
		fprintf(stderr, "This test can handle only packed pixel frame buffers.\n");

	}
	dec->fb_var.xoffset = 0;
	dec->fb_var.yoffset = 0;


	/* make sure the frame buffers are set up properly */
	if (-1 == ioctl(fb, FBIOPAN_DISPLAY, &dec->fb_var)) {
	}

	close(fb);
	/* end of the stuff for the framebuffer flip */

	dec->timestamp = 0;

	dec->veu = shveu_open();
	dec->out_width = -1;
	dec->out_height = -1;

	dec->caps_set = FALSE;
	dec->decoder = NULL;
	dec->pcache = NULL;
	dec->timestamp_valid = FALSE;

	dec->codec_data_present = FALSE;
	dec->codec_data_present_first = TRUE;
}


/** Event handler for decoder sink events, see GstPadEventFunction.
 */
static gboolean gst_shvideodec_sink_event(GstPad * pad, GstEvent * event)
{
	Gstshvideodec *dec = (Gstshvideodec *) (GST_OBJECT_PARENT(pad));
	gboolean ret = TRUE;

	GST_DEBUG_OBJECT(dec, "%s called event %i", __func__, GST_EVENT_TYPE(event));

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_EOS:
	case GST_EVENT_FLUSH_STOP:
		{
			/* Propagate event to downstream elements */
			ret = gst_pad_push_event(dec->srcpad, event);
			break;
		}
	default:
		{
			ret = gst_pad_event_default(pad, event);
			break;
		}
	}
	return ret;
}

/** Negotiate our sink pad capabilities, see GstPadSetCapsFunction.
 */
static gboolean gst_shvideodec_set_sink_caps(GstPad * pad, GstCaps * caps)
{
	GstStructure *structure = NULL;
	const GValue *value;
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
			|| !strcmp(gst_structure_get_name(structure), "video/mpeg")) {
			GST_DEBUG_OBJECT(dec, "codec format is video/mpeg");
			dec->format = SHCodecs_Format_MPEG4;
		} else {
			GST_DEBUG_OBJECT(dec, "%s failed (not supported: %s)",
					 __func__, gst_structure_get_name(structure));
			return FALSE;
		}
	}

	if ((value = gst_structure_get_value(structure, "codec_data"))) {
		guint size;
		guint8 *data;
		GstBuffer *buf;
		guint8 *buffer_data;
		GST_DEBUG_OBJECT(dec, "%s codec_data found\n", __func__);
		dec->codec_data_present = TRUE;
		if (dec->format == SHCodecs_Format_H264) {
			buf = GST_BUFFER_CAST(gst_value_get_mini_object(value));
			size = GST_BUFFER_SIZE(buf);
			data = GST_BUFFER_DATA(buf);
			GST_DEBUG_OBJECT(dec,
					 "%s AVC Decoder Configuration Record version = 0x%x\n",
					 __func__, (unsigned int) *data);
			data++;
			GST_DEBUG_OBJECT(dec, "%s Profile ICD = 0x%x\n", __func__,
					 (unsigned int) *data);
			data++;
			GST_DEBUG_OBJECT(dec, "%s Profile compatability = 0x%x\n",
					 __func__, (unsigned int) *data);
			data++;
			GST_DEBUG_OBJECT(dec, "%s Level IDC = 0x%x\n", (unsigned int) *data);
			data++;
			GST_DEBUG_OBJECT(dec, "%s NAL Length minus one = 0x%x\n",
					 __func__, (unsigned int) *data);
			data++;
			dec->num_sps = (((unsigned int) *data++) & ~0xe0);
			GST_DEBUG_OBJECT(dec, "%s Number of SPS's = 0x%x\n", __func__,
					 dec->num_sps);
			dec->sps_size = ((unsigned short) *data++) << 8;
			dec->sps_size += ((unsigned short) *data++);
			GST_DEBUG_OBJECT(dec, "%s Size of SPS = 0x%x\n", __func__,
					 dec->sps_size);
			//copy the sps data to the sps_buf
			dec->codec_data_sps_buf = gst_buffer_try_new_and_alloc(dec->sps_size + 4);
			if (dec->codec_data_sps_buf == NULL) {
				GST_DEBUG_OBJECT(dec, "%s codec_data_sps_buf allocation failed\n",
						 __func__);
			}

			buffer_data = GST_BUFFER_DATA(dec->codec_data_sps_buf);
			*buffer_data = 0x00;
			*(buffer_data + 1) = 0x00;
			*(buffer_data + 2) = 0x00;
			*(buffer_data + 3) = 0x01;
			memcpy(GST_BUFFER_DATA(dec->codec_data_sps_buf) + 4, data, dec->sps_size);
			data += dec->sps_size;
			dec->num_pps = (unsigned int) *data++;
			GST_DEBUG_OBJECT(dec, "%s Number of PPS's = 0x%x\n", __func__,
					 dec->num_pps);
			if (dec->num_pps > 0) {
				dec->pps_size = ((unsigned short) *data++) << 8;
				dec->pps_size += ((unsigned short) *data++);
				GST_DEBUG_OBJECT(dec, "%s Size of PPS = 0x%x\n", __func__,
						 dec->pps_size);
				dec->codec_data_pps_buf =
					gst_buffer_try_new_and_alloc(dec->pps_size + 4);
				if (dec->codec_data_pps_buf == NULL) {
					GST_DEBUG_OBJECT(dec,
							 "%s codec_data_sps_buf allocation failed\n",
							 __func__);
				}

				buffer_data = GST_BUFFER_DATA(dec->codec_data_pps_buf);
				*buffer_data = 0x00;
				*(buffer_data + 1) = 0x00;
				*(buffer_data + 2) = 0x00;
				*(buffer_data + 3) = 0x01;
				//copy the sps data to the sps_buf
				memcpy(GST_BUFFER_DATA(dec->codec_data_pps_buf) + 4, data,
					   dec->pps_size);
			}
		}
	} else {
		GST_DEBUG_OBJECT(dec, "%s codec_data not found\n", __func__);
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

	shcodecs_decoder_set_decoded_callback(dec->decoder,
						  gst_shcodecs_decoded_callback, (void *) dec);

	/* If nothing has been set, use the natural size */
	if (dec->out_width == -1) {
		dec->out_width = dec->width;
		dec->out_height = dec->height;
	}

	dec->caps_set = TRUE;

	GST_LOG_OBJECT(dec, "%s ok", __func__);
	return TRUE;
}


/** Negotiate our src pad capabilities, see GstPadSetCapsFunction.
 */
static gboolean gst_shvideodec_set_src_caps(GstPad * pad, GstCaps * caps)
{
	GstStructure *structure = NULL;
	Gstshvideodec *dec = (Gstshvideodec *) (GST_OBJECT_PARENT(pad));

	GST_LOG_OBJECT(dec, "%s called", __func__);

	structure = gst_caps_get_structure(caps, 0);

	if (gst_structure_get_int(structure, "width", &dec->out_width)
		&& gst_structure_get_int(structure, "height", &dec->out_height)) {
		GST_DEBUG_OBJECT(dec, "%s output size = %dx%d", __func__,
				 dec->out_width, dec->out_height);
	} else {
		GST_DEBUG_OBJECT(dec, "%s failed (no width/height)", __func__);
		return FALSE;
	}

	GST_LOG_OBJECT(dec, "%s ok", __func__);
	return TRUE;
}


/** GStreamer buffer handling function, see GstPadChainFunction.
 */
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

	if (dec->codec_data_present == TRUE) {	//This is for mp4 file playback
		if (dec->format == SHCodecs_Format_H264) {
			gint orig_bsize;

			bdata = GST_BUFFER_DATA(inbuf);
			bsize = orig_bsize = GST_BUFFER_SIZE(inbuf);
			if (dec->codec_data_present_first == TRUE) {
				if (*(bdata + 4) == 0x09) {	//an AUD NAL at the beginning
					guint size = 0;
					size = (*bdata++) << 24;
					size += (*bdata++) << 16;
					size += (*bdata++) << 8;
					size += (*bdata++);
					bdata += size;
					bsize -= size;
					if (*(bdata + 4) == 0x06) {	//an SEI NAL
						size = 0;
						size = (*bdata++) << 24;
						size += (*bdata++) << 16;
						size += (*bdata++) << 8;
						size += (*bdata++);
						bdata += size;
						bsize -= size;
					}
					if (*(bdata + 4) == 0x67) {	//an SPS NAL  
						dec->codec_data_present_first = FALSE;	//SPS and PPS NAL already in data
						inbuf =
							gst_buffer_create_sub(inbuf, orig_bsize - bsize,
									  bsize);
					}
				}
			}
			*bdata = 0x00;
			*(bdata + 1) = 0x00;
			*(bdata + 2) = 0x00;
			*(bdata + 3) = 0x01;

			if (dec->codec_data_present_first == TRUE) {
				dec->codec_data_present_first = FALSE;
				dec->codec_data_sps_buf =
					gst_buffer_join(dec->codec_data_sps_buf,
							dec->codec_data_pps_buf);
				inbuf = gst_buffer_join(dec->codec_data_sps_buf, inbuf);
			}
		}
	}

	GST_LOG_OBJECT(dec,
			   "Received new data of size %d, time %" GST_TIME_FORMAT,
			   GST_BUFFER_SIZE(inbuf), GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(inbuf)));

	if (dec->timestamp_valid == FALSE) {
		dec->duration = GST_BUFFER_DURATION(inbuf);
	}

	if (dec->pcache) {
		inbuf = gst_buffer_join(dec->pcache, inbuf);
		dec->pcache = NULL;
	}

	bdata = GST_BUFFER_DATA(inbuf);
	bsize = GST_BUFFER_SIZE(inbuf);

	GST_LOG_OBJECT(dec, "Calling shcodecs_decode with %d bytes", bsize);
	bused = shcodecs_decode(dec->decoder, bdata, bsize);
	GST_LOG_OBJECT(dec, "shcodecs_decode returned %d", bused);

	if (bused < 0) {

		GST_ELEMENT_ERROR((GstElement *) dec, CORE, FAILED,
				  ("Decode error"), ("%s failed (Error on shcodecs_decode)",
							 __func__));

		return GST_FLOW_ERROR;
	}

	if (bused != bsize) {
		GST_LOG_OBJECT(dec, "Keeping %d bytes of data", bsize);
		dec->pcache = gst_buffer_create_sub(inbuf, bused, (bsize - bused));
	}

	gst_buffer_unref(inbuf);

	return ret;
}



/* Private function: Event handler for the decoded video frame from libshcodecs
 * See SHCodecs_Decoded_.
 *   @param decoder SHCodecs Decoder, unused in the function
 *   @param y_buf Physical address to the Y buffer
 *   @param y_size Size of the Y buffer
 *   @param c_buf Physical address to the C buffer
 *   @param c_size Size of the C buffer
 *   @param user_data Contains Gstshvideodec
 *   @return The result of passing data to a pad
 */
static int
gst_shcodecs_decoded_callback(SHCodecs_Decoder * decoder,
				  unsigned char *y_buf, int y_size,
				  unsigned char *c_buf, int c_size, void *user_data)
{
	unsigned char *fb_screenMem = NULL;
	Gstshvideodec *dec = (Gstshvideodec *) user_data;
	GstCaps *caps = NULL;
	GstBuffer *outbuf;
	// Point to the back buffer
	if (dec->buffer == 0) {
		fb_screenMem = (unsigned char *) dec->fb_fix.smem_start;
	} else {
		fb_screenMem =
			(unsigned char *) dec->fb_fix.smem_start +
			(dec->fb_var.yres * dec->fb_var.xres * (dec->fb_var.bits_per_pixel / 8));
	}

	shveu_operation(dec->veu,
			(unsigned long) y_buf, (unsigned long) c_buf,
			dec->width, dec->height, dec->width, SHVEU_YCbCr420,
			(unsigned long) fb_screenMem, 0, dec->fb_var.xres,
			dec->fb_var.yres, dec->fb_var.xres, SHVEU_RGB565, SHVEU_NO_ROT);

	caps = gst_caps_new_simple("video/x-raw-rgb",
				   "bpp", G_TYPE_INT, 16,
				   "depth", G_TYPE_INT, 16,
				   "framerate", GST_TYPE_FRACTION,
				   dec->fps_numerator, dec->fps_denominator,
				   "width", G_TYPE_INT, dec->out_width,
				   "height", G_TYPE_INT, dec->out_height, NULL);
	gst_pad_set_caps(dec->srcpad, caps);

	outbuf = gst_buffer_new();	//allocate a new emtpy buffer
	gst_buffer_set_caps(outbuf, caps);
	outbuf->offset = dec->buffer;
	dec->buffer = (dec->buffer + 1) & 1;

	gst_caps_unref(caps);

	GST_BUFFER_TIMESTAMP(outbuf) = dec->timestamp;
	GST_BUFFER_DURATION(outbuf) = dec->duration;

	dec->timestamp += dec->duration;

	dec->timestamp_valid = TRUE;

	GST_DEBUG_OBJECT(dec, "pushing buffer to source pad with timestamp : %"
			 GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT,
			 GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(outbuf)),
			 GST_TIME_ARGS(GST_BUFFER_DURATION(outbuf)));

	/* Push the buffer to the source pad */
	if (gst_pad_push(dec->srcpad, outbuf) != GST_FLOW_OK) {
		GST_DEBUG("push to source pad failed\n");
		return 0;
	}
	return 1;
}


GType gst_shm_videodec_get_type(void)
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
					   "gst-sh-mobile-video-dec", &object_info, (GTypeFlags) 0);
	}
	return object_type;
}


gboolean gst_shm_videodec_plugin_init(GstPlugin * plugin)
{
	GST_LOG_OBJECT("%s called\n", __func__);

	if (!gst_element_register
		(plugin, "gst-sh-mobile-video-dec", GST_RANK_NONE, GST_TYPE_SHVIDEODEC))
		return FALSE;
	return TRUE;
}


GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
		  GST_VERSION_MINOR,
		  "gst-sh-mobile-video-dec",
		  "gst-sh-mobile",
		  gst_shm_videodec_plugin_init,
		  VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
