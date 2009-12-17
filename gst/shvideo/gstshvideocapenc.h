/**
 * gst-sh-mobile-enc
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
 * @author Johannes Lahti <johannes.lahti@nomovok.com>
 * @author Pablo Virolainen <pablo.virolainen@nomovok.com>
 * @author Aki Honkasuo <aki.honkasuo@nomovok.com>
 *
 */

#ifndef  GSTSHVIDEOCAPENC_H
#define  GSTSHVIDEOCAPENC_H

#include <gst/gst.h>
#include <shcodecs/shcodecs_encoder.h>
#include <pthread.h>

#include "cntlfile/ControlFileUtil.h"
#include "cntlfile/capture.h"

G_BEGIN_DECLS
#define GST_TYPE_SHVIDEOENC \
  (gst_shvideo_enc_get_type())
#define GST_SHVIDEOENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHVIDEOENC,GstshvideoEnc))
#define GST_SHVIDEOENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SHVIDEOENC,GstshvideoEnc))
#define GST_IS_SHVIDEOENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHVIDEOENC))
#define GST_IS_SHVIDEOENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHVIDEOENC))
typedef struct _GstshvideoEnc GstshvideoEnc;
typedef struct _GstshvideoEncClass GstshvideoEncClass;



/** Get gst-sh-mobile-enc object type
    @return object type
*/
GType gst_shvideo_enc_get_type(void);

/** Initialize the encoder plugin 
    @param plugin Gstreamer plugin
    @return returns true if the plugin initialized and registered gst-sh-mobile-enc, else false
*/
gboolean gst_shvideo_camera_enc_plugin_init(GstPlugin * plugin);

G_END_DECLS
#endif

