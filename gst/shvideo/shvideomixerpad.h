/**
 * SH video mixer plugin.
 * Blends frames using the BEU hardware (via libshbeu)
 *
 * Based on the Generic video mixer plugin (gst-plugins-good)
 * Copyright (C) 2008 Wim Taymans <wim@fluendo.com>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_SH_VIDEO_MIXER_PAD_H__
#define __GST_SH_VIDEO_MIXER_PAD_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

G_BEGIN_DECLS

#define GST_TYPE_SH_VIDEO_MIXER_PAD (gst_sh_videomixer_pad_get_type())
#define GST_SH_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SH_VIDEO_MIXER_PAD, GstSHVideoMixerPad))
#define GST_SH_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SH_VIDEO_MIXER_PAD, GstSHVideoMixerPadiClass))
#define GST_IS_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SH_VIDEO_MIXER_PAD))
#define GST_IS_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SH_VIDEO_MIXER_PAD))

typedef struct _GstSHVideoMixerPad GstSHVideoMixerPad;
typedef struct _GstSHVideoMixerPadClass GstSHVideoMixerPadClass;
typedef struct _GstSHVideoMixerCollect GstSHVideoMixerCollect;

struct _GstSHVideoMixerCollect
{
  GstCollectData collect;       /* we extend the CollectData */

  GstBuffer *buffer;            /* the queued buffer for this pad */

  GstSHVideoMixerPad *mixpad;
};

/* all information needed for one video stream */
struct _GstSHVideoMixerPad
{
  GstPad parent;                /* subclass the pad */

  gint64 queued;

  guint in_width, in_height;
  gint fps_n;
  gint fps_d;

  gint xpos, ypos;
  guint zorder;
  gint blend_mode;
  gdouble alpha;

  GstSHVideoMixerCollect *mixcol;
};

struct _GstSHVideoMixerPadClass
{
  GstPadClass parent_class;
};

G_END_DECLS
#endif /* __GST_SH_VIDEO_MIXER_PAD_H__ */
