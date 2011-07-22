/**
 * "SHVidResize" element. Resizes video frames using the VEU hardware resizer
 * (via libshveu).
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

#ifndef __GST_SHVIDRESIZE_H__
#define __GST_SHVIDRESIZE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include <uiomux/uiomux.h>
#include <shveu/shveu.h>

G_BEGIN_DECLS

/* Standard macros for manipulating SHVidresize objects */
#define GST_TYPE_SHVIDRESIZE \
  (gst_shvidresize_get_type())
#define GST_SHVIDRESIZE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHVIDRESIZE,GstSHVidresize))
#define GST_SHVIDRESIZE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SHVIDRESIZE,GstSHVidresizeClass))
#define GST_IS_SHVIDRESIZE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHVIDRESIZE))
#define GST_IS_SHVIDRESIZE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHVIDRESIZE))

typedef struct _GstSHVidresize      GstSHVidresize;
typedef struct _GstSHVidresizeClass GstSHVidresizeClass;

/* _GstSHVidresize object */
struct _GstSHVidresize
{
	/* GStreamer infrastructure */
	GstBaseTransform  element;
	GstPad            *sinkpad;
	GstPad            *srcpad;

	/* Element state */
	gint              srcWidth;
	gint              srcHeight;
	gint              dstWidth;
	gint              dstHeight;
	int               srcColorSpace;
	int               dstColorSpace;
	UIOMux           *uiomux;
	SHVEU            *veu;
};

/* _GstSHVidresizeClass object */
struct _GstSHVidresizeClass
{
	GstBaseTransformClass parent_class;
};

/* External function declarations */
GType gst_shvidresize_get_type(void);

G_END_DECLS

#endif /* __GST_SHVIDRESIZE_H__ */
