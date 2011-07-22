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

#ifndef  GSTSHV4L2SRC_H
#define  GSTSHV4L2SRC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SHV4L2SRC \
  (gst_shv4l2src_get_type())

#define GST_SHV4L2SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SHV4L2SRC, GstSHV4L2Src))

#define GST_SHV4L2SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SHV4L2SRC, GstSHV4L2Src))

#define GST_IS_SHV4L2SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SHV4L2SRC))

#define GST_IS_SHV4L2SRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SHV4L2SRC))

typedef struct _GstSHV4L2Src GstSHV4L2Src;

typedef struct _GstSHV4L2SrcClass GstSHV4L2SrcClass;


/** Get gst-sh-mobile-v4l2src object type
    @return object type
*/
GType gst_shv4l2src_get_type(void);

G_END_DECLS

#endif

