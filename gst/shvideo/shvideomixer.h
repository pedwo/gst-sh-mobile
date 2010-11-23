/* SH video mixer plugin.
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
 
#ifndef __GST_SH_VIDEO_MIXER_H__
#define __GST_SH_VIDEO_MIXER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <uiomux/uiomux.h>
#include <shbeu/shbeu.h>
#include "shvideomixerpad.h"

G_BEGIN_DECLS

#define GST_TYPE_SH_VIDEO_MIXER (gst_sh_videomixer_get_type())
#define GST_SH_VIDEO_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SH_VIDEO_MIXER, GstSHVideoMixer))
#define GST_SH_VIDEO_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SH_VIDEO_MIXER, GstSHVideoMixerClass))
#define GST_IS_VIDEO_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SH_VIDEO_MIXER))
#define GST_IS_VIDEO_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SH_VIDEO_MIXER))

typedef struct _GstSHVideoMixer GstSHVideoMixer;
typedef struct _GstSHVideoMixerClass GstSHVideoMixerClass;

/**
 * GstSHVideoMixer:
 *
 * The opaque #GstSHVideoMixer structure.
 */
struct _GstSHVideoMixer
{
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* Lock to prevent the state to change while blending */
  GMutex *state_lock;
  /* Sink pads using Collect Pads from core's base library */
  GstCollectPads *collect;
  /* sinkpads, a GSList of GstSHVideoMixerPads */
  GSList *sinkpads;

  gint numpads;

  GstClockTime last_ts;

  /* the master pad */
  GstSHVideoMixerPad *master;

  gint in_width, in_height;
  gint out_width, out_height;
  gint out_format;
  gboolean setcaps;
  gboolean sendseg;

  gint fps_n;
  gint fps_d;
  
  /* Next available sinkpad index */
  gint next_sinkpad;

  /* sink event handling */
  GstPadEventFunction collect_event;
  guint64	segment_position;
  gdouble	segment_rate;

  UIOMux *uiomux;
  SHBEU  *beu;
};

struct _GstSHVideoMixerClass
{
  GstElementClass parent_class;
};

GType gst_sh_videomixer_get_type (void);

G_END_DECLS
#endif /* __GST_SH_VIDEO_MIXER_H__ */
