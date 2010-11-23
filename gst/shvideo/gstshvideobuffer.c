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
#include <uiomux/uiomux.h>
#include <shveu/shveu.h>

#include "gstshvideobuffer.h"

static GstBufferClass *parent_class;

/** 
 * Initialize the buffer
 * \param shbuffer GstSHVideoBuffer object
 * \param g_class GClass pointer
 */
static void
gst_sh_video_buffer_init (GstSHVideoBuffer *shbuffer, gpointer g_class)
{
	/* Mark the buffer as not allocated by us */
	shbuffer->allocated = 0;
}

/** 
 * Create a new SH buffer
 * \param width Width of frame
 * \param height Height of frame
 * \param fmt Video format
 */
GstBuffer *gst_sh_video_buffer_new(UIOMux *uiomux, gint width, gint height, int fmt)
{
	GstSHVideoBuffer *buf;
	gint size;

	// TODO the size calc should really take into account that the chroma plane needs to
	// be 32-byte aligned. We should also cover min width/height requirements of all IP
	// so that the buffer can be used with all HW.
	// This also means that the buffer can't be used by non-SH elements.

	size = size_y(fmt, width * height) + size_c(fmt, width * height);

	buf = (GstSHVideoBuffer*)gst_mini_object_new(GST_TYPE_SH_VIDEO_BUFFER);
	g_return_val_if_fail(buf != NULL, NULL);

	/* User space address */
	GST_BUFFER_DATA(buf) = uiomux_malloc(uiomux, UIOMUX_SH_VEU, size, 32);
	GST_BUFFER_SIZE(buf) = size;

	if (GST_BUFFER_DATA(buf) == NULL) {
		gst_mini_object_unref(GST_MINI_OBJECT(buf));
		return NULL;
	}

	/* Mark the buffer as allocated by us, so it needs freeing */
	buf->allocated = 1;
	buf->allocated_size = size;
	buf->uiomux = uiomux;
	buf->format = fmt;

	return GST_BUFFER(buf);
}


/** 
 * Finalize the buffer
 * \param shbuffer GstSHVideoBuffer object
 */
static void
gst_sh_video_buffer_finalize (GstSHVideoBuffer *shbuffer)
{
	if (shbuffer->allocated && shbuffer->uiomux) {
		/* Free the buffer */
		uiomux_free (shbuffer->uiomux, UIOMUX_SH_VEU,
			GST_BUFFER_DATA(shbuffer), shbuffer->allocated_size);
	}

	/* Set malloc_data to NULL to prevent parent class finalize
	* from trying to free allocated data. This buffer is used only in HW
	* address space, where we don't allocate data but just map it. 
	*/
	GST_BUFFER_MALLOCDATA(shbuffer) = NULL;
	GST_BUFFER_DATA(shbuffer) = NULL;

	GST_MINI_OBJECT_CLASS (parent_class)->finalize (GST_MINI_OBJECT (shbuffer));
}

/** 
 * Initialize the buffer class
 * \param g_class GClass pointer
 * \param class_data Optional data pointer
 */
static void
gst_sh_video_buffer_class_init (gpointer g_class, gpointer class_data)
{
	GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

	parent_class = g_type_class_peek_parent (g_class);

	mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
			gst_sh_video_buffer_finalize;
}

GType
gst_sh_video_buffer_get_type (void)
{
	static GType gst_sh_video_buffer_type;

	if (G_UNLIKELY (gst_sh_video_buffer_type == 0)) {
		static const GTypeInfo gst_sh_video_buffer_info = {
			sizeof (GstBufferClass),
			NULL,
			NULL,
			gst_sh_video_buffer_class_init,
			NULL,
			NULL,
			sizeof (GstSHVideoBuffer),
			0,
			(GInstanceInitFunc) gst_sh_video_buffer_init,
			NULL
		};
		gst_sh_video_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
				"GstSHVideoBuffer", &gst_sh_video_buffer_info, 0);
	}
	return gst_sh_video_buffer_type;
}

