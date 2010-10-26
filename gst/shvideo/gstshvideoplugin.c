/**
 * \mainpage gst-sh-mobile - GStreamer plugins for Renesas devices.
 *
 * This software package provides GStreamer plugins for hardware
 * accelerated video operations. They have been tested on the SH7724
 * (SH-Mobile R2R) device with an MS7724 development board. However,
 * they will work with any Renesas device that is supported by the
 * libshveu, libshbeu and libshcodecs software.
 *
 * This package consists of 3 plugins:
 *
 * The plugin includes following elements:
 * - \subpage dec "gst-sh-mobile-dec - MPEG4/H264 HW decoder"
 * - \subpage enc "gst-sh-mobile-enc - MPEG4/H264 HW encoder"
 * - \subpage sink "gst-sh-mobile-sink - Image sink"
 * Optional elements:
 * - \subpage resize "gst-sh-mobile-resize - HW video resize/rotate"
 * - \subpage mixer "gst-sh-mobile-mixer - HW video blend/overlay"
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * \section main-license License
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
 * \author Phil Edworthy <phil.edworthy@renesas.com>
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstshvideosink.h"
#include "gstshvideoenc.h"
#include "gstshvideodec.h"
#include "gstshvideocapenc.h"
#ifdef ENABLE_SCALE
#include "gstshvideoresize.h"
#endif
#ifdef ENABLE_BLEND
#include "shvideomixer.h"
#endif

gboolean
gst_sh_video_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "gst-sh-mobile-sink", GST_RANK_NONE,
          GST_TYPE_SH_VIDEO_SINK))
    return FALSE;

  if (!gst_element_register (plugin, "gst-sh-mobile-dec", GST_RANK_PRIMARY,
          GST_TYPE_SH_VIDEO_DEC))
    return FALSE;

  if (!gst_element_register (plugin, "gst-sh-mobile-enc", GST_RANK_PRIMARY,
          GST_TYPE_SH_VIDEO_ENC))
    return FALSE;

  if (!gst_element_register (plugin, "gst-sh-mobile-camera-enc", GST_RANK_PRIMARY,
          GST_TYPE_SH_VIDEO_CAPENC))
    return FALSE;

#ifdef ENABLE_SCALE
  if (!gst_element_register (plugin, "gst-sh-mobile-resize", GST_RANK_PRIMARY,
          GST_TYPE_SHVIDRESIZE))
    return FALSE;
#endif

#ifdef ENABLE_BLEND
  if (!gst_element_register (plugin, "gst-sh-mobile-mixer", GST_RANK_PRIMARY,
          GST_TYPE_SH_VIDEO_MIXER))
    return FALSE;
#endif

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gst-sh-mobile",
    "SH HW video elements",
    gst_sh_video_plugin_init,
    VERSION, "LGPL", "Renesas SH Video", "")
