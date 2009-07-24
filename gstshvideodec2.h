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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 *
 * This library implements a GStreamer plug-in for video decode using Renesas
 * SH-Mobile Video Processing Unit (via libshcodecs).
 * 
 * @author Phil Edworthy <phil.edworthy@renesas.com>
 *
 */


#ifndef  GSTSHVIDEODEC_H
#define  GSTSHVIDEODEC_H

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/gstelement.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


G_BEGIN_DECLS
#define GST_TYPE_SHVIDEODEC \
  (gst_shm_videodec_get_type())
#define GST_SHVIDEODEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHVIDEODEC,Gstshvideodec))
#define GST_SHVIDEODEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SHVIDEODEC,Gstshvideodec))
#define GST_IS_SHVIDEODEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHVIDEODEC))
#define GST_IS_SHVIDEODEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHVIDEODEC))
typedef struct _Gstshvideodec Gstshvideodec;
typedef struct _GstshvideodecClass GstshvideodecClass;


/**
 * Define Gstreamer SH Video Decoder Class structure
 */
struct _GstshvideodecClass
{
  GstElementClass parent;
};


/** Get gst-sh-mobile-video-dec object type
    @return object type
*/
GType gst_shm_videodec_get_type (void);

/** Initialize the plugin, see GstPluginInitFunc.
    @param plugin Gstreamer plugin
    @return returns true if plugin initialized, else false
*/
gboolean gst_shm_videodec_plugin_init (GstPlugin *plugin);

G_END_DECLS
#endif
