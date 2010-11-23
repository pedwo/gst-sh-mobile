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
#ifndef GSTSHVIDEOBUFFER_H
#define GSTSHVIDEOBUFFER_H

#include <linux/videodev2.h>
#include <uiomux/uiomux.h>
#include <gst/gst.h>
#include <gst/video/video.h>

// TODO video formats, structs & helper functions shoudl be moved out of libshveu
#include <shveu/shveu.h>

#define GST_TYPE_SH_VIDEO_BUFFER (gst_sh_video_buffer_get_type())
#define GST_IS_SH_VIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SH_VIDEO_BUFFER))
#define GST_SH_VIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SH_VIDEO_BUFFER, GstSHVideoBuffer))
#define GST_SH_VIDEO_BUFFER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_SH_VIDEO_BUFFER, GstSHVideoBufferclass))
#define GST_SH_VIDEO_BUFFER_CAST(obj)  ((GstSHVideoBuffer *)(obj))

typedef struct _GstSHVideoBuffer GstSHVideoBuffer;
typedef struct _GstSHVideoBufferclass GstSHVideoBufferclass;

/**
 * \struct _GstSHVideoBuffer
 * \brief SuperH HW buffer for YUV-data
 * \var buffer Parent buffer
 */
struct _GstSHVideoBuffer 
{
	GstBuffer buffer;

	UIOMux *uiomux;
	int format;
	int allocated;
	gint allocated_size;
};

/**
 * \struct _GstSHVideoBufferclass
 * \var parent Parent
 */
struct _GstSHVideoBufferclass
{
	GstBufferClass parent;
};

/** 
 * Get Gstshbuffer object type
 * @return object type
 */
GType gst_sh_video_buffer_get_type (void);

/**
 * Allocate a buffer that can be directly accessed by the SH hardware
 */
GstBuffer *gst_sh_video_buffer_new(UIOMux *uiomux, gint width, gint height, int fmt);


/***************************** Helper functions *****************************/

/* Create additional video formats - hopefully clear of any others */
#ifndef GST_VIDEO_FORMAT_NV12
#define GST_VIDEO_FORMAT_NV12 (1000)
#endif
#ifndef GST_VIDEO_FORMAT_NV16
#define GST_VIDEO_FORMAT_NV16 (1001)
#endif
#ifndef GST_VIDEO_FORMAT_RGB16
#define GST_VIDEO_FORMAT_RGB16 (1002)
#endif


/* Extend gst_video_format_get_size to support NV12, NV16 & RGB16 */
int gst_sh_video_format_get_size(GstVideoFormat format, int width, int height);

/* Extend gst_video_format_parse_caps to support NV12, NV16 & RGB16 */
gboolean gst_sh_video_format_parse_caps (
	GstCaps *caps,
	GstVideoFormat *format,
	int *width,
	int *height);

gboolean gst_caps_to_renesas_format (GstCaps *caps, ren_vid_format_t *ren_format);

int get_renesas_format (GstVideoFormat format);

void *get_c_addr (void *y, ren_vid_format_t ren_format, int width, int height);

#endif //GSTSHVIDEOBUFFER_H
