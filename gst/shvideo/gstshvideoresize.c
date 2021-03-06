/**
 * "gst-sh-mobile-resize" element. Resizes video frames using the VEU hardware resizer
 * (via libshveu).
 *
 * In Gstreamer terminology, the element implements an out of place transform
 * (i.e. filter) on raw video frames.
 *
 * Example usage:
 * \code
 *     gst-launch \
 *       videotestsrc \
 *       ! "video/x-raw-rgb, bpp=16, width=160, height=120" \
 *       ! gst-sh-mobile-resize \
 *       ! "video/x-raw-yuv, width=320, height=240" \
 *       ! filesink location=out_qvga.yuv
 * \endcode
 *
 * This plugin supports the following formats on input and output:
 *       "video/x-raw-rgb, bpp=16"
 *       "video/x-raw-rgb, bpp=32"
 *       "video/x-raw-yuv, format=(fourcc)NV12"
 *       "video/x-raw-yuv, format=(fourcc)NV16"
 *
 * Note: You cannot use filesrc to provide the raw yuv/rgb input
 * as filesrc allocates it own buffers containing pagesize bytes.
 *
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>

#include <uiomux/uiomux.h>
#include <shveu/shveu.h>

#include "gstshvideoresize.h"
#include "gstshvideobuffer.h"

/* Declare variable used to categorize GST_LOG output */
GST_DEBUG_CATEGORY_STATIC (gst_shvidresize_debug);
#define GST_CAT_DEFAULT gst_shvidresize_debug

/* Use our size range. This will be expanded in GST_VIDEO_CAPS_* */
#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 16, 4092]"

#define MAX_SCALE_FACTOR 16

static void dbg(const char *str1, int l, const char *str2, const struct ren_vid_surface *s)
{
	GST_LOG("%s:%d: %s: (%dx%d) pitch=%d py=%p, pc=%p, pa=%p\n", str1, l, str2, s->w, s->h, s->pitch, s->py, s->pc, s->pa);
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS (
		GST_VIDEO_CAPS_YUV("NV12")";"
		GST_VIDEO_CAPS_YUV("NV16")";"
		GST_VIDEO_CAPS_RGB_16";"
		GST_VIDEO_CAPS_RGBx
	)
);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS (
		GST_VIDEO_CAPS_YUV("NV12")";"
		GST_VIDEO_CAPS_YUV("NV16")";"
		GST_VIDEO_CAPS_RGB_16";"
		GST_VIDEO_CAPS_RGBx
	)
);

/* Declare a global pointer to our element base class */
static GstElementClass *parent_class = NULL;


static gboolean get_spec (GstCaps *cap, gint *width,
	gint *height, int *ren_format)
{
	GstStructure *structure;
	guint32 fourcc;
	gint bpp;
	structure = gst_caps_get_structure(cap, 0);

	if (!gst_structure_get_int(structure, "width", width)) {
		GST_ERROR("Failed to get width");
		return FALSE;
	}

	if (!gst_structure_get_int(structure, "height", height)) {
		GST_ERROR("Failed to get height");
		return FALSE;
	}

	*ren_format = REN_UNKNOWN;

	if (gst_structure_get_fourcc(structure, "format", &fourcc)) {
		if (fourcc == GST_MAKE_FOURCC('N', 'V', '1', '2')) {
			*ren_format = REN_NV12;
		} else if (fourcc == GST_MAKE_FOURCC('N', 'V', '1', '6')) {
			*ren_format = REN_NV16;
		}
	} else {
		if (gst_structure_get_int(structure, "bpp", &bpp)) {
			if (bpp == 16)
				*ren_format = REN_RGB565;
			if (bpp == 32)
				*ren_format = REN_RGB32;
		}
	}

	if (*ren_format == REN_UNKNOWN) {
		GST_ERROR("failed to get format from cap");
		return FALSE;
	}

	return TRUE;
}

/*
 * GstBaseTransformClass::prepare_output_buffer
 * Optional. Subclasses can override this to do their own allocation of
 * output buffers. Elements that only do analysis can return a subbuffer or
 * even just increment the reference to the input buffer (if in passthrough
 * mode)
 */
static GstFlowReturn gst_shvidresize_prepare_output_buffer (GstBaseTransform
	*trans, GstBuffer *inBuf, gint size, GstCaps *caps, GstBuffer **outBuf)
{
	GstSHVidresize *vidresize = GST_SHVIDRESIZE(trans);
	gint width, height;
	int format;

	/* Get the width, height & format from the caps */
	if (!get_spec(caps, &width, &height, &format)) {
		GST_ERROR("Failed to get resolution");
		return FALSE;
	}

	GST_LOG("output size = %dx%d, format=%d", width, height, format);

	*outBuf = gst_sh_video_buffer_new(vidresize->uiomux, width, height, format);

	if (*outBuf == NULL) {
		GST_ELEMENT_ERROR(vidresize, RESOURCE, NO_SPACE_LEFT,
			("failed to allocate output buffer"), (NULL));
		return GST_FLOW_ERROR;
	}

	gst_buffer_set_caps(*outBuf, GST_PAD_CAPS(trans->srcpad));

	return GST_FLOW_OK;
}

static gboolean
gst_shvidresize_get_unit_size (GstBaseTransform *trans, GstCaps *caps, guint *size)
{
	gint height, width;
	int format;

	if (!get_spec(caps, &width, &height, &format)) {
		GST_ERROR("Failed to get resolution");
		return FALSE;
	}

	*size = size_y(format, width*height) + size_c(format, width*height);

	GST_LOG("size=%d", *size);

	return TRUE;
}

/*
 * GstBaseTransformClass::transform
 * Required if the element does not operate in-place. Transforms one incoming
 * buffer to one outgoing buffer. The function is allowed to change
 * size/timestamp/duration of the outgoing buffer.
 */
static GstFlowReturn gst_shvidresize_transform (GstBaseTransform *trans,
	GstBuffer *srcbuf, GstBuffer *dstbuf)
{
	GstSHVidresize *vidresize = GST_SHVIDRESIZE(trans);
	struct ren_vid_surface src;
	struct ren_vid_surface dst;

	/* Create resize handle */
	GST_LOG("scaling from %dx%d to %dx%d",
		vidresize->srcWidth, vidresize->srcHeight,
		vidresize->dstWidth, vidresize->dstHeight);

	src.format = vidresize->srcColorSpace;
	src.w = vidresize->srcWidth;
	src.h = vidresize->srcHeight;
	src.pitch = src.w;
	src.py = GST_BUFFER_DATA(srcbuf);
	src.pc = src.py + size_y(src.format, src.pitch * src.h);
	src.pa = NULL;

	dst.format = vidresize->dstColorSpace;
	dst.w = vidresize->dstWidth;
	dst.h = vidresize->dstHeight;
	dst.pitch = dst.w;
	dst.py = GST_BUFFER_DATA(dstbuf);
	dst.pc = dst.py + size_y(dst.format, dst.pitch * dst.h);
	dst.pa = NULL;

	dbg(__func__, __LINE__, "src", &src);
	dbg(__func__, __LINE__, "dst", &dst);

	/* Use VEU to resize buffer */
	if (shveu_resize(vidresize->veu, &src, &dst) < 0) {
		GST_ELEMENT_ERROR(vidresize, RESOURCE, FAILED,
			("failed to execute veu resize"), (NULL));
		return GST_FLOW_ERROR;
	}

	GST_LOG("scale complete");

	return GST_FLOW_OK;
}

/*
 * GstBaseTransformClass::transform_caps
 * Optional. Given the pad in this direction and the given caps, what caps are
 * allowed on the other pad in this element?
 */
static GstCaps *gst_shvidresize_transform_caps (GstBaseTransform *trans,
	GstPadDirection direction, GstCaps *caps)
{
	GstPad *otherpad;
	GstCaps *to, *ret;
	const GstCaps *templ;
	GstStructure *structure;
	int i, nr_caps;
	int w, h;
	int min = 16;
	int max = 4092;
	int w_min = min;
	int w_max = max;
	int h_min = min;
	int h_max = max;

	static const GstStaticCaps static_caps = GST_STATIC_CAPS (
		GST_VIDEO_CAPS_YUV("NV12")";"
		GST_VIDEO_CAPS_YUV("NV16")";"
		GST_VIDEO_CAPS_RGB_16";"
		GST_VIDEO_CAPS_RGBx
	);

	/* this function is always called with a simple caps */
	g_return_val_if_fail (GST_CAPS_IS_SIMPLE (caps), NULL);

	GST_LOG("begin (%s)", direction==GST_PAD_SRC ? "src" : "sink");

	/* Calculate scaling limits */
	structure = gst_caps_get_structure(caps, 0);
	GST_LOG("input=%s", gst_structure_to_string(structure));
	if (gst_structure_get_int(structure, "width", &w)) {
		w_min = GST_ROUND_UP_4(w / MAX_SCALE_FACTOR);
		w_max = GST_ROUND_UP_4(w * MAX_SCALE_FACTOR);
	}
	if (gst_structure_get_int(structure, "height", &h)) {
		h_min = GST_ROUND_UP_4(h / MAX_SCALE_FACTOR);
		h_max = GST_ROUND_UP_4(h * MAX_SCALE_FACTOR);
	}

	/* output caps */
	to = gst_caps_copy( (const GstCaps *)gst_static_caps_get(&static_caps));
	nr_caps = gst_caps_get_size(to);
	for (i=0; i<nr_caps; i++) {
		structure = gst_caps_get_structure(to, i);

		gst_structure_set(structure,
			"width", GST_TYPE_INT_RANGE, w_min, w_max,
			"height", GST_TYPE_INT_RANGE, h_min, h_max, NULL);
	}

	/* filter against set allowed caps on the pad */
	otherpad = (direction == GST_PAD_SINK) ? trans->srcpad : trans->sinkpad;
	templ = gst_pad_get_pad_template_caps (otherpad);
	ret = gst_caps_intersect (to, templ);
	gst_caps_unref (to);

	/* debug help */
	nr_caps = gst_caps_get_size(ret);
	for (i=0; i<nr_caps; i++)
		GST_LOG("output=%s", gst_structure_to_string(gst_caps_get_structure(ret, i)));

	return ret;
}

static void
gst_shvidresize_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
	GstStructure *ins, *outs;
	gint width, height;

	g_return_if_fail (gst_caps_is_fixed (caps));

	ins = gst_caps_get_structure (caps, 0);
	outs = gst_caps_get_structure (othercaps, 0);

	GST_LOG("caps=%s", gst_structure_to_string(ins));

	if (gst_structure_get_int (ins, "width", &width)) {
		if (gst_structure_has_field (outs, "width")) {
			width = GST_ROUND_UP_4(width);
			gst_structure_fixate_field_nearest_int (outs, "width", width);
		}
	}
	if (gst_structure_get_int (ins, "height", &height)) {
		if (gst_structure_has_field (outs, "height")) {
			height = GST_ROUND_UP_4(height);
			gst_structure_fixate_field_nearest_int (outs, "height", height);
		}
	}

	GST_LOG("othercaps=%s", gst_structure_to_string(outs));
}

/*
 * GstBaseTransformClass::set_caps
 * allows the subclass to be notified of the actual caps set.
 */
static gboolean gst_shvidresize_set_caps (GstBaseTransform *trans,
	GstCaps *in, GstCaps *out)
{
	GstSHVidresize *vidresize = GST_SHVIDRESIZE(trans);

	if (!get_spec(in, &vidresize->srcWidth, &vidresize->srcHeight, &vidresize->srcColorSpace)) {
		GST_ERROR("Failed to get resolution");
		return FALSE;
	}

	if (!get_spec(out, &vidresize->dstWidth, &vidresize->dstHeight, &vidresize->dstColorSpace)) {
		GST_ERROR("Failed to get resolution");
		return FALSE;
	}

	return TRUE;
}

/*
 * GObjectClass::finalize
 *    Shut down any running video resize, and reset the element state.
 */
static gboolean gst_shvidresize_exit_resize(GstSHVidresize *vidresize)
{
	/* Shut down remaining items */
	if (vidresize->veu) {
		shveu_close(vidresize->veu);
		vidresize->veu = NULL;
	}

	if (vidresize->uiomux) {
		uiomux_close(vidresize->uiomux);
		vidresize->uiomux = NULL;
	}

	return TRUE;
}

/*
 * gst_shvidresize_base_init
 *    Initializes element base class.
 */
static void gst_shvidresize_base_init(gpointer gclass)
{
	static GstElementDetails element_details = {
		"SH video scale",
		"Filter/Resize",
		"Resize video using VEU hardware resizer",
		"Phil Edworthy; Renesas"
	};

	GstElementClass *element_class = GST_ELEMENT_CLASS(gclass);

	gst_element_class_add_pad_template(element_class,
		gst_static_pad_template_get (&src_factory));
	gst_element_class_add_pad_template(element_class,
		gst_static_pad_template_get (&sink_factory));
	gst_element_class_set_details(element_class, &element_details);
}

/*
 * gst_shvidresize_class_init
 *    Initializes the SHVidresize class.
 */
static void gst_shvidresize_class_init(GstSHVidresizeClass *klass)
{
	GObjectClass *gobject_class;
	GstBaseTransformClass *trans_class;

	gobject_class    = (GObjectClass*) klass;
	trans_class      = (GstBaseTransformClass *) klass;

	gobject_class->finalize = (GObjectFinalizeFunc)gst_shvidresize_exit_resize;

	trans_class->transform_caps = GST_DEBUG_FUNCPTR(gst_shvidresize_transform_caps);
	trans_class->fixate_caps    = GST_DEBUG_FUNCPTR(gst_shvidresize_fixate_caps);
	trans_class->set_caps       = GST_DEBUG_FUNCPTR(gst_shvidresize_set_caps);
	trans_class->transform      = GST_DEBUG_FUNCPTR(gst_shvidresize_transform);
	trans_class->get_unit_size  = GST_DEBUG_FUNCPTR(gst_shvidresize_get_unit_size);
	trans_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_shvidresize_prepare_output_buffer);
	trans_class->passthrough_on_same_caps = TRUE;
	parent_class = g_type_class_peek_parent (klass);

	GST_DEBUG_CATEGORY_INIT(gst_shvidresize_debug,
		"gst-sh-mobile-resize", 0, "SH Video Resize");
}

/*
 * gst_shvidresize_init
 */
static void gst_shvidresize_init (GstSHVidresize *vidresize)
{
	// TODO add fail checks
	vidresize->uiomux = uiomux_open();
	vidresize->veu = shveu_open_named("VEU");
}

/*
 * gst_shvidresize_get_type
 *    Defines function pointers for initialization routines for this element.
 */
GType gst_shvidresize_get_type(void)
{
	static GType object_type = 0;

	if (G_UNLIKELY(object_type == 0)) {
		static const GTypeInfo object_info = {
			sizeof(GstSHVidresizeClass),
			gst_shvidresize_base_init,
			NULL,
			(GClassInitFunc) gst_shvidresize_class_init,
			NULL,
			NULL,
			sizeof(GstSHVidresize),
			0,
			(GInstanceInitFunc) gst_shvidresize_init
		};

		object_type = g_type_register_static(GST_TYPE_BASE_TRANSFORM,
			"gst-sh-mobile-resize", &object_info, (GTypeFlags)0);
	}

	return object_type;
}
