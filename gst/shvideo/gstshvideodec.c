/**
 * \page dec gst-sh-mobile-dec
 * gst-sh-mobile-dec - Decodes MPEG4/H264 video stream to raw YUV image data
 * on SuperH environment using libshcodecs HW codec.
 *
 * \section dec-description Description
 * This element is designed to use the HW video processing modules of the
 * Renesas SuperH chipset to decode MPEG4/H264 video streams. This element
 * is not usable in any other environments and it requires libshcodes HW codec
 * to be installed.
 *
 * \section dec-examples Example launch lines
 *
 * \subsection dec-example-1 Decoding from a file to a file
 *
 * \code
 * gst-launch \
 *  filesrc location=test.m4v \
 *  ! "video/mpeg, width=320, height=240, framerate=30/1, mpegversion=4" \
 *  ! gst-sh-mobile-dec \
 *  ! filesink location=test.raw
 * \endcode
 * In this pipeline we use filesrc element to read the source file,
 * which is a MPEG4 video elementary stream. After filesrc we add static caps
 * as the filesrc does not do caps negotiation and the decoder requires them.
 * The last element in the pipeline is filesink, which writes the output YUV-data
 * into a file.
 *
 * \subsection dec-example-2 Decoding an AVI file with audio & video playback
 *
 * \code
 * gst-launch \
 * filesrc location=test.avi ! avidemux name=demux \
 *  demux.audio_00 ! queue ! decodebin ! audioconvert ! audioresample ! autoaudiosink \
 *  demux.video_00 ! queue ! gst-sh-mobile-dec ! gst-sh-mobile-sink
 * \endcode
 *
 * Filesrc element is used to read the file again, which this time is an AVI
 * wrapped video containing both audio and video stream. avidemux element is
 * used to strip the avi container. avidemux has two src-pads, which are
 * named “demux” using a property. Both of the avidemux src pads are first
 * connected to queue elements, which take care of the buffering of the data in
 * the pipeline.
 *
 * The audio stream is then connected to the decodebin element, which detects
 * the stream format and does the decoding. audioconvert and audioresample
 * elements are used to transform the data into a suitable format for
 * playback. The last element in the audio pipeline is autoaudiosink, which
 * automatically detects and connects the correct audio sink for playback. This
 * audio pipeline composition is very common in the gstreamer programming.
 *
 * The video pipeline is constructed from SuperH specific elements;
 * gst-sh-mobile-dec and gst-sh-mobile-sink. The gst-sh-mobile-sink is a
 * videosink element for SuperH.
 *
 * \subsection dec-example-3 Decoding a video stream from net
 *
 * \code
 * gst-launch \
 *  udpsrc port=5000 caps="application/x-rtp,clock-rate=90000" \
 *  ! gstrtpjitterbuffer latency=0 ! rtpmp4vdepay \
 *  ! "video/mpeg, width=320, height=240, framerate=15/1" \
 *  ! gst-sh-mobile-dec \
 *  ! gst-sh-mobile-sink
 * \endcode
 * Here the video stream is received from udpsrc element. gstrtpjitterbuffer
 * element is used to take care of ordering and storing the received RTP
 * packages. Next rtpmp4vdepay element is used to remove the RTP frames from
 * the buffers. Again, the static caps are needed to pass information to the
 * decoder.
 *
 * \section dec-properties Properties
 * \copydoc gstshvideodecproperties
 *
 * \section dec-pads Pads
 * \copydoc dec_sink_factory
 * \copydoc dec_src_factory
 *
 * \section dec-license License
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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <pthread.h>

#include "gstshvideodec.h"
#include "gstshvideobuffer.h"

/**
 * \var dec_sink_factory
 * Name: sink \n
 * Direction: sink \n
 * Available: always \n
 * Caps:
 * - video/mpeg, width=(int)[48,1280], height=(int)[48,720],
 *   framerate=(fraction)[1,30], mpegversion=(int)4
 * - video/x-h264, width=(int)[48,1280], height=(int)[48,720],
 *   framerate=(fraction)[1,30], h264version=(int)h264
 * - video/x-divx, width=(int)[48,1280], height=(int)[48,720],
 *   framerate=(fraction)[1,30], divxversion=(int){4,5,6}
 * - video/x-xvid, width=(int)[48,1280], height=(int)[48,720],
 *   framerate=(fraction)[1,30]
 *
 */
static GstStaticPadTemplate dec_sink_factory =
	GST_STATIC_PAD_TEMPLATE ("sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS (
			  "video/mpeg,"
			  "width  = (int) [48, 1280],"
			  "height = (int) [48, 720],"
			  "framerate = (fraction) [0, 30],"
			  "mpegversion = (int) 4"
			  ";"
			  "video/x-h264,"
			  "width  = (int) [48, 1280],"
			  "height = (int) [48, 720],"
			  "framerate = (fraction) [0, 30],"
			  "variant = (string) itu,"
			  "h264version = (string) h264"
			  ";"
			  "video/x-divx,"
			  "width  = (int) [48, 1280],"
			  "height = (int) [48, 720],"
			  "framerate = (fraction) [0, 30],"
			  "divxversion =  {4, 5, 6}"
			  ";"
			  "video/x-xvid,"
			  "width  = (int) [48, 1280],"
			  "height = (int) [48, 720],"
			  "framerate = (fraction) [0, 30]"
			  )
		);

/**
 * \var dec_src_factory
 * Name: src \n
 * Direction: src \n
 * Available: always \n
 * Caps:
 * - video/x-raw-yuv, format=(fourcc)NV12, width=(int)[48,1280],
 *   height=(int)[48,720], framerate=(fraction)[1,30]
 */
static GstStaticPadTemplate dec_src_factory =
	GST_STATIC_PAD_TEMPLATE ("src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS (
			  "video/x-raw-yuv, "
			  "format = (fourcc) NV12,"
			  "width = (int) [48, 1280],"
			  "height = (int) [48, 720],"
			  "framerate = (fraction) [0, 30]"
			  )
		);


static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC (gst_sh_video_dec_debug);
#define GST_CAT_DEFAULT gst_sh_video_dec_debug

// STATIC DECLARATIONS

/**
 * Initialize shvideodec class plugin event handler
 * @param g_class Gclass
 * @param data user data pointer, unused in the function
 */
static void gst_sh_video_dec_init_class (gpointer g_class, gpointer data);

/**
 * Initialize SH hardware video decoder & sink
 * @param klass Gstreamer element class
 */
static void gst_sh_video_dec_base_init (gpointer klass);

/**
 * Dispose decoder
 * @param object Gstreamer element class
 */
static void gst_sh_video_dec_dispose (GObject * object);

/**
 * Initialize the class for decoder and player
 * @param klass Gstreamer SH video decodes class
 */
static void gst_sh_video_dec_class_init (GstSHVideoDecClass * klass);

/**
 * Initialize the decoder
 * @param dec Gstreamer SH video element
 * @param gklass Gstreamer SH video decode class
 */
static void gst_sh_video_dec_init (GstSHVideoDec * dec, GstSHVideoDecClass * gklass);

/**
 * Event handler for decoder sink events
 * @param pad Gstreamer sink pad
 * @param event The Gstreamer event
 * @return returns true if the event can be handled, else false
 */
static gboolean gst_sh_video_dec_sink_event (GstPad * pad, GstEvent * event);

/**
 * Initialize the decoder sink pad
 * @param pad Gstreamer sink pad
 * @param caps The capabilities of the video to decode
 * @return returns true if the video capabilities are supported and the video can be decoded, else false
 */
static gboolean gst_sh_video_dec_setcaps (GstPad * pad, GstCaps * caps);

/**
 * GStreamer buffer handling function
 * @param pad Gstreamer sink pad
 * @param inbuffer The input buffer
 * @return returns GST_FLOW_OK if buffer handling was successful. Otherwise GST_FLOW_UNEXPECTED
 */
static GstFlowReturn gst_sh_video_dec_chain (GstPad * pad, GstBuffer * inbuffer);

/**
 * Event handler for the video frame is decoded and can be shown on screen
 * @param decoder SHCodecs Decoder, unused in the function
 * @param y_buf Userland address to the Y buffer
 * @param y_size Size of the Y buffer
 * @param c_buf Userland address to the C buffer
 * @param c_size Size of the C buffer
 * @param user_data Contains GstSHVideoDec
 * @return The result of passing data to a pad
 */
static gint gst_shcodecs_decoded_callback (SHCodecs_Decoder * decoder,
					  guchar * y_buf, gint y_size,
					  guchar * c_buf, gint c_size,
					  void * user_data);

/** Push a decoded buffer function
* \var param data decoder object
*/
static void *gst_sh_video_dec_pad_push (void *data);

// DEFINITIONS

static void
gst_sh_video_dec_init_class (gpointer g_class, gpointer data)
{
	parent_class = g_type_class_peek_parent (g_class);
	gst_sh_video_dec_class_init ((GstSHVideoDecClass *) g_class);
}

GType
gst_sh_video_dec_get_type (void)
{
	static GType object_type = 0;

	if (object_type == 0)
	{
		static const GTypeInfo object_info =
		{
			sizeof (GstSHVideoDecClass),
			gst_sh_video_dec_base_init,
			NULL,
			gst_sh_video_dec_init_class,
			NULL,
			NULL,
			sizeof (GstSHVideoDec),
			0,
			(GInstanceInitFunc) gst_sh_video_dec_init
		};

		object_type = g_type_register_static (GST_TYPE_ELEMENT,
						      "gst-sh-mobile-dec",
						      &object_info,
						      (GTypeFlags) 0);
	}
	return object_type;
}

static void
gst_sh_video_dec_base_init (gpointer klass)
{
	static const GstElementDetails plugin_details =
		GST_ELEMENT_DETAILS ("SH hardware video decoder",
				     "Codec/Decoder/Video",
				     "Decode video (H264 && Mpeg4)",
				     "Johannes Lahti <johannes.lahti@nomovok.com>"
				     );

	GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

	gst_element_class_add_pad_template (element_class,
			gst_static_pad_template_get (&dec_sink_factory));
	gst_element_class_add_pad_template (element_class,
			gst_static_pad_template_get (&dec_src_factory));
	gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_sh_video_dec_dispose (GObject * object)
{
	GstSHVideoDec *dec = GST_SH_VIDEO_DEC (object);

	if (dec->decoder != NULL) {
		shcodecs_decoder_close (dec->decoder);
	}
	dec->end = TRUE;

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_sh_video_dec_class_init (GstSHVideoDecClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	GST_DEBUG_CATEGORY_INIT (gst_sh_video_dec_debug, "gst-sh-mobile-dec",
				 0, "Decoder for H264/MPEG4 streams");

	gobject_class->dispose = gst_sh_video_dec_dispose;
}

static void
gst_sh_video_dec_init (GstSHVideoDec * dec, GstSHVideoDecClass * gklass)
{
	GstElementClass *kclass = GST_ELEMENT_GET_CLASS (dec);

	dec->sinkpad = gst_pad_new_from_template(gst_element_class_get_pad_template(kclass,"sink"),"sink");
	gst_pad_set_setcaps_function(dec->sinkpad, gst_sh_video_dec_setcaps);
	gst_pad_set_chain_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_sh_video_dec_chain));
	gst_pad_set_event_function (dec->sinkpad, GST_DEBUG_FUNCPTR(gst_sh_video_dec_sink_event));
	gst_element_add_pad(GST_ELEMENT(dec),dec->sinkpad);

	dec->srcpad = gst_pad_new_from_template(gst_element_class_get_pad_template(kclass,"src"),"src");
	gst_element_add_pad(GST_ELEMENT(dec),dec->srcpad);
	gst_pad_use_fixed_caps (dec->srcpad);

	dec->caps_set = FALSE;
	dec->decoder = NULL;
	dec->buffer = NULL;
	dec->codec_data_present = FALSE;
	dec->codec_data_present_first = TRUE;

	sem_init(&dec->dec_sem, 0, 1);
	sem_init(&dec->push_sem, 0, 0);

	dec->end = FALSE;
}

static gboolean
gst_sh_video_dec_sink_event (GstPad * pad, GstEvent * event)
{
	GstSHVideoDec *dec = (GstSHVideoDec *) (GST_OBJECT_PARENT (pad));

	GST_DEBUG_OBJECT(dec,"event %i", GST_EVENT_TYPE(event));

	if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
	{
		GST_DEBUG_OBJECT (dec, "EOS gst event");

		if (dec->decoder) {
			GST_DEBUG_OBJECT(dec,"We are done, calling finalize.");
			shcodecs_decoder_finalize(dec->decoder);
			GST_DEBUG_OBJECT(dec,
					 "Stream finalized. Total decoded %d frames.",
					 shcodecs_decoder_get_frame_count(dec->decoder));
		}
	}
	return gst_pad_push_event(dec->srcpad,event);
}

static gboolean
gst_sh_video_dec_setcaps (GstPad * pad, GstCaps * sink_caps)
{
	GstStructure *structure = NULL;
	GstCaps* src_caps = NULL;
	GstSHVideoDec *dec = (GstSHVideoDec *) (GST_OBJECT_PARENT (pad));
	gboolean ret = TRUE;
	const GValue *value;

	if (dec->decoder != NULL) {
		GST_DEBUG_OBJECT(dec,"Decoder already opened");
		return FALSE;
	}

	structure = gst_caps_get_structure (sink_caps, 0);

	if (!strcmp (gst_structure_get_name (structure), "video/x-h264")) {
		GST_INFO_OBJECT(dec, "codec format is video/x-h264");
		dec->format = SHCodecs_Format_H264;
	} else {
		if (!strcmp (gst_structure_get_name (structure), "video/x-divx") ||
		    !strcmp (gst_structure_get_name (structure), "video/x-xvid") ||
		    !strcmp (gst_structure_get_name (structure), "video/mpeg")) {
			GST_INFO_OBJECT (dec, "codec format is video/mpeg");
			dec->format = SHCodecs_Format_MPEG4;
		} else {
			GST_INFO_OBJECT(dec,"Failed (not supported: %s)",
					gst_structure_get_name (structure));
			return FALSE;
		}
	}

	if ((value = gst_structure_get_value(structure, "codec_data"))) {
		guint size;
		guint8 *data;
		guint8 *sps_data = NULL;
		guint8 *pps_data = NULL;
		GstBuffer *buf;
		guint8 *buffer_data;
		GST_DEBUG_OBJECT(dec, "codec_data found");
		dec->codec_data_present = TRUE;
		if (dec->format == SHCodecs_Format_H264) {
			buf = GST_BUFFER_CAST(gst_value_get_mini_object(value));
			size = GST_BUFFER_SIZE(buf);
			data = GST_BUFFER_DATA(buf);
			/* Skip over avcC */
			GST_DEBUG_OBJECT(dec, "AVC Decoder Configuration Record version = 0x%x", *data);
			data++;
			GST_DEBUG_OBJECT(dec, "Profile ICD = 0x%x", *data);
			data++;
			GST_DEBUG_OBJECT(dec, "Profile compatibility = 0x%x",*data);
			data++;
			GST_DEBUG_OBJECT(dec, "Level IDC = 0x%x", *data);
			data++;
			GST_DEBUG_OBJECT(dec, "NAL Length minus one = 0x%x", *data);
			data++;

			/* Actually only ever get 1 SPS,PPS */
			dec->num_sps = (((unsigned int) *data++) & ~0xe0);
			GST_DEBUG_OBJECT(dec, "Number of SPS's = 0x%x", dec->num_sps);
			if (dec->num_sps > 0) {
				dec->sps_size = ((unsigned short) *data++) << 8;
				dec->sps_size += ((unsigned short) *data++);
				sps_data = data;
				data += dec->sps_size;
				GST_DEBUG_OBJECT(dec, "Size of SPS = 0x%x", dec->sps_size);
			}

			dec->num_pps = (unsigned int) *data++;
			GST_DEBUG_OBJECT(dec, "Number of PPS's = 0x%x", dec->num_pps);
			if (dec->num_pps > 0) {
				dec->pps_size = ((unsigned short) *data++) << 8;
				dec->pps_size += ((unsigned short) *data++);
				pps_data = data;
				GST_DEBUG_OBJECT(dec, "Size of PPS = 0x%x", dec->pps_size);
			}

			dec->buffer = gst_buffer_try_new_and_alloc(dec->sps_size + dec->pps_size + 8);
			if (dec->buffer == NULL) {
				GST_DEBUG_OBJECT(dec, "dec_buffer allocation failed");
			}
			buffer_data = GST_BUFFER_DATA(dec->buffer);
			GST_DEBUG_OBJECT(dec, "Saving SPS/PPS data into decode buffer");

			/* Copy the SPS/PPS data to the data buffer and put Start Codes in front */
			if (dec->num_sps > 0) {
				*buffer_data++ = 0x00;
				*buffer_data++ = 0x00;
				*buffer_data++ = 0x00;
				*buffer_data++ = 0x01;
				memcpy(buffer_data, sps_data, dec->sps_size);
				buffer_data += dec->sps_size;
			}

			if (dec->num_pps > 0) {
				*buffer_data++ = 0x00;
				*buffer_data++ = 0x00;
				*buffer_data++ = 0x00;
				*buffer_data++ = 0x01;
				memcpy(buffer_data, pps_data, dec->pps_size);
			}
		}
	} else {
		GST_DEBUG_OBJECT(dec, "codec_data not found");
	}

	if (gst_structure_get_fraction (structure, "framerate",
					&dec->fps_numerator, &dec->fps_denominator))
	{
		GST_INFO_OBJECT(dec,"Framerate: %d/%d",dec->fps_numerator,
				dec->fps_denominator);
	} else {
		GST_INFO_OBJECT(dec,"Failed (no framerate)");
		return FALSE;
	}

	if (gst_structure_get_int (structure, "width",  &dec->width)
	    && gst_structure_get_int (structure, "height", &dec->height))
	{
		GST_INFO_OBJECT(dec,"Initializing decoder %dx%d",
				dec->width,dec->height);
		dec->decoder=shcodecs_decoder_init(dec->width,dec->height,
						   dec->format);
	} else {
		GST_INFO_OBJECT(dec,"Failed (no width/height)");
		return FALSE;
	}

	if (dec->decoder == NULL) {
		GST_ELEMENT_ERROR((GstElement*)dec,CORE,FAILED,
				  ("Error on shcodecs_decoder_init."),
				  ("Failed (Error on shcodecs_decoder_init)"));
		return FALSE;
	}

	/* Set frame by frame as it is natural for GStreamer data flow */
	shcodecs_decoder_set_frame_by_frame(dec->decoder,1);

	shcodecs_decoder_set_decoded_callback(dec->decoder,
						gst_shcodecs_decoded_callback,
						dec);

	/* Set SRC caps */
	src_caps = gst_caps_new_simple (
		"video/x-raw-yuv",
		"format",    GST_TYPE_FOURCC, GST_MAKE_FOURCC('N','V','1','2'),
		"framerate", GST_TYPE_FRACTION, dec->fps_numerator, dec->fps_denominator,
		"width",     G_TYPE_INT, dec->width,
		"height",    G_TYPE_INT, dec->height,
		"framerate", GST_TYPE_FRACTION, dec->fps_numerator, dec->fps_denominator,
		NULL);

	if (!gst_pad_set_caps(dec->srcpad,src_caps)) {
		GST_ELEMENT_ERROR((GstElement*)dec,CORE,NEGOTIATION,
				  ("Source pad not linked."), (NULL));
		ret = FALSE;
	}

	gst_caps_unref(src_caps);

	dec->caps_set = TRUE;

	GST_LOG_OBJECT(dec,"Ok");
	return ret;
}

static GstFlowReturn
gst_sh_video_dec_chain (GstPad * pad, GstBuffer * inbuffer)
{
	GstSHVideoDec *dec = (GstSHVideoDec *) (GST_OBJECT_PARENT (pad));
	GstFlowReturn ret = GST_FLOW_OK;
	gint used_bytes;
	GstBuffer* buffer = GST_BUFFER(inbuffer);

	if (!dec->push_thread) {
		pthread_create( &dec->push_thread, NULL, gst_sh_video_dec_pad_push, dec);
	}

	if ((dec->codec_data_present == TRUE) &&
	    (dec->format == SHCodecs_Format_H264)) {
		/* This is for mp4 file playback */
		/* All NALs are preceded with a 4 byte size field, which we replace with start codes. */
		/* Note that we might get more than one packet... */
		gint bsize;
		guint8 *bdata = GST_BUFFER_DATA(buffer);

		GST_DEBUG_OBJECT(dec, "codec_data_present");

		/* Replace all 4 byte size fields with Start Codes */
		used_bytes = 0;
		while (used_bytes < GST_BUFFER_SIZE(buffer)) {
			/* Extract the 4 byte size field */
			bsize =  bdata[0] << 24;
			bsize += bdata[1] << 16;
			bsize += bdata[2] << 8;
			bsize += bdata[3];
			GST_DEBUG_OBJECT(dec, "NAL size = %d", bsize);
			bsize += 4;

			if ((GST_BUFFER_SIZE(buffer)-used_bytes) < bsize) {
				GST_ELEMENT_ERROR((GstElement *) dec, CORE, FAILED,
				  ("Malformed input"), ("Buffer contains partial NAL"));
				break;
			}

			/* Replace it with a Start Code */
			bdata[0] = 0x00;
			bdata[1] = 0x00;
			bdata[2] = 0x00;
			bdata[3] = 0x01;

			/* Move to next packet */
			bdata += bsize;
			used_bytes += bsize;
		}
		buffer = gst_buffer_create_sub(buffer, 0, used_bytes);
	}

	GST_DEBUG_OBJECT(
		dec, "Got buffer. Size %d timestamp: %llu duration: %llu",
		GST_BUFFER_SIZE(buffer),
		GST_TIME_AS_MSECONDS(GST_BUFFER_TIMESTAMP (buffer)),
		GST_TIME_AS_MSECONDS(GST_BUFFER_DURATION (buffer)));

	/* Buffering */
	if (dec->buffer) {
		buffer = gst_buffer_join(dec->buffer,buffer);
		GST_LOG_OBJECT(dec,"Buffer added. Now storing %d bytes", GST_BUFFER_SIZE(buffer));
	}

	dec->buffer = NULL;

	used_bytes = shcodecs_decode(dec->decoder,
			GST_BUFFER_DATA (buffer),
			GST_BUFFER_SIZE (buffer));

	GST_DEBUG_OBJECT(dec, "used_bytes. %d bytes", used_bytes);
	if (used_bytes < 0) {
		GST_ELEMENT_ERROR((GstElement *) dec, CORE, FAILED,
				  ("Decode error"), ("Failed (Error on shcodecs_decode)"));
		return GST_FLOW_ERROR;
	}

	// Preserve the data that was not used
	if (GST_BUFFER_SIZE(buffer) != used_bytes) {
		dec->buffer = gst_buffer_create_sub(buffer,
						    used_bytes,
						    GST_BUFFER_SIZE(buffer)-used_bytes);
	}

	gst_buffer_unref(buffer);
	return ret;

}

static gint
gst_shcodecs_decoded_callback (SHCodecs_Decoder * decoder,
			       guchar * y_buf, gint y_size,
			       guchar * c_buf, gint c_size,
			       void * user_data)
{
	GstSHVideoDec *dec = (GstSHVideoDec *) user_data;
	gint offset = shcodecs_decoder_get_frame_count(dec->decoder);

	/* We require the chroma plane of the video decoder output frame to follow the luma
	   plane - without this, it is not possible for standard GStreamer elements
	   to use the buffers */
	if (c_buf != (y_buf + y_size)) {
		GST_ELEMENT_ERROR((GstElement *) dec, CORE, FAILED,
				  ("Decode error"), ("Decoded frame chroma plane does not follow luma plane!"));
		return -1;
	}

	sem_wait(&dec->dec_sem);
	GST_LOG_OBJECT(dec,"Frame decoded");

	/* Wrap the video decoder output buffer in a GST buffer */
	dec->push_buf = (GstBuffer *) gst_mini_object_new (GST_TYPE_SH_VIDEO_BUFFER);
	GST_BUFFER_MALLOCDATA(dec->push_buf) = NULL;
	GST_BUFFER_DATA(dec->push_buf) = y_buf;
	GST_BUFFER_SIZE(dec->push_buf) = y_size + c_size;

	GST_BUFFER_OFFSET(dec->push_buf) = offset;
	GST_BUFFER_CAPS(dec->push_buf) = gst_caps_copy(GST_PAD_CAPS(dec->srcpad));
	GST_BUFFER_DURATION(dec->push_buf) = GST_SECOND * dec->fps_denominator / dec->fps_numerator;
	GST_BUFFER_TIMESTAMP(dec->push_buf) = offset * GST_BUFFER_DURATION(dec->push_buf);
	GST_BUFFER_OFFSET_END(dec->push_buf) = offset;

	GST_LOG_OBJECT (dec, "Pushing frame number: %d time: %" GST_TIME_FORMAT,
			offset,
			GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (dec->push_buf)));

	sem_post(&dec->push_sem);

	return 0; /* continue decoding */
}

static void *
gst_sh_video_dec_pad_push (void *data)
{
	GstFlowReturn ret;

	GstSHVideoDec *dec = (GstSHVideoDec *)data;

	while(1)
	{
		sem_wait(&dec->push_sem);
		GST_LOG_OBJECT(dec,"%s called", __func__);

		ret = gst_pad_push (dec->srcpad, dec->push_buf);

		if (ret != GST_FLOW_OK) {
			GST_DEBUG_OBJECT (dec, "pad_push failed: %s", gst_flow_get_name (ret));
		}

		if (dec->end)
			break;
		sem_post(&dec->dec_sem);
	}

	sem_destroy(&dec->dec_sem);
	sem_destroy(&dec->push_sem);

	return NULL;
}
