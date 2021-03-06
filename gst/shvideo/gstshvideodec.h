/**
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
 * \author Pablo Virolainen <pablo.virolainen@nomovok.com>
 * \author Johannes Lahti <johannes.lahti@nomovok.com>
 * \author Aki Honkasuo <aki.honkasuo@nomovok.com>
 *
 */


#ifndef  GSTSHVIDEODEC_H
#define  GSTSHVIDEODEC_H

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/gstelement.h>
#include <semaphore.h>

G_BEGIN_DECLS
#define GST_TYPE_SH_VIDEO_DEC \
	(gst_sh_video_dec_get_type())
#define GST_SH_VIDEO_DEC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SH_VIDEO_DEC,GstSHVideoDec))
#define GST_SH_VIDEO_DEC_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SH_VIDEO_DEC,GstSHVideoDecClass))
#define GST_IS_SH_VIDEO_DEC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SH_VIDEO_DEC))
#define GST_IS_SH_VIDEO_DEC_CLASS(obj) \
	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SH_VIDEO_DEC))
typedef struct _GstSHVideoDec GstSHVideoDec;
typedef struct _GstSHVideoDecClass GstSHVideoDecClass;

#include <shcodecs/shcodecs_decoder.h>

/**
 * \struct _GstSHVideoDec gstshvideodec.h
 * \var element GstElement object
 * \var sinkpad Pointer to our sink pad
 * \var srcpad Pointer to our src pad
 * \var format Stream type. Possible values: 0(none), 1(MPEG4) and 2 (H264)
 * \var width Width of the video
 * \var height Height of the video
 * \var fps_numerator Numerator of the framerate fraction
 * \var fps_denominator Denominator of the framerate fraction
 * \var decoder pointer to the SHCodecs decoder object
 * \var caps_set A flag indicating whether the caps has been set for the pads
 * \var running A flag indicating that the decoding thread should be running
 * \var buffer Pointer to the cache buffer
 * \var push_buf Pointer to src buffer
 * \var push_thread Src thread for push buffer
 * \var dec_sem for the Src function
 * \var push_sem for the Src function
 */
struct _GstSHVideoDec
{
	GstElement element;

	GstPad *sinkpad;
	GstPad *srcpad;

	SHCodecs_Format format;
	gint width;
	gint height;
	gint fps_numerator;
	gint fps_denominator;
	SHCodecs_Decoder * decoder;

	gboolean caps_set;
	gboolean end;

	GstBuffer* buffer;
	GstBuffer* push_buf;

	pthread_t push_thread;

	sem_t dec_sem;
	sem_t push_sem;

	gboolean codec_data_present;
	gboolean codec_data_present_first;
	guint num_sps;
	guint sps_size;
	GstBuffer *codec_data_sps_buf;
	guint num_pps;
	guint pps_size;
	GstBuffer *codec_data_pps_buf;
};

/**
 * GstSHVideoDecClass
 * \struct _GstSHVideoDecClass
 * \var parent Parent class
 */
struct _GstSHVideoDecClass
{
	GstElementClass parent;
};

/** Get gst-sh-mobile-dec object type
* \var return object type
*/
GType gst_sh_video_dec_get_type (void);

G_END_DECLS
#endif
