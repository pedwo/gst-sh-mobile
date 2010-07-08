/*
 * "SHVidResize" element. Resizes video frames using the VEU hardware resizer
 * (via libshveu).
 *
 * In Gstreamer terminology, the element implements an out of place transform
 * (i.e. filter) on raw video frames.
 *
 * Example usage:
 *     gst-launch videotestsrc ! 'video/x-raw-yuv,width=160,height=120' !
 *       SHVidResize ! 'video/x-raw-yuv,width=320,height=240' ! 
 *        fakesink silent=TRUE
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
#include "gstshvideosink.h"
#include "gstshvideobuffer.h"

/* Declare variable used to categorize GST_LOG output */
GST_DEBUG_CATEGORY_STATIC (gst_shvidresize_debug);
#define GST_CAT_DEFAULT gst_shvidresize_debug

// TODO add more formats

/**
 * \var sink_factory
 * Name: sink \n
 * Direction: sink \n
 * Available: always \n
 * Caps:
 * - video/x-raw-yuv, format=(fourcc)NV12, width=(int)[16, 4092], 
 *   height=(int)[16, 4092]
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS
	( "video/x-raw-yuv, "
	  "format = (fourcc) NV12,"
	  "width = (int) [16, 4092],"
	  "height = (int) [16, 4092]"
	)
);

/**
 * \var src_factory
 * Name: src \n
 * Direction: src \n
 * Available: always \n
 * Caps:
 * - video/x-raw-yuv, format=(fourcc)NV12, width=(int)[16, 4092], 
 *   height=(int)[16, 4092]
 */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS
	( "video/x-raw-yuv, "
	  "format = (fourcc) NV12,"
	  "width = (int) [16, 4092],"
	  "height = (int) [16, 4092]"
	)
);

/* Declare a global pointer to our element base class */
static GstElementClass *parent_class = NULL;

/* Static Function Declarations */
static void
 gst_shvidresize_base_init(gpointer g_class);
static void
 gst_shvidresize_class_init(GstSHVidresizeClass *g_class);
static void
 gst_shvidresize_init(GstSHVidresize *object);

static gboolean gst_shvidresize_exit_resize(GstSHVidresize *vidresize);
static gboolean gst_shvidresize_set_caps (GstBaseTransform *trans, 
 GstCaps *in, GstCaps *out);
static GstCaps *gst_shvidresize_transform_caps (GstBaseTransform *trans,
 GstPadDirection direction, GstCaps *caps);
static GstFlowReturn gst_shvidresize_transform (GstBaseTransform *trans,
 GstBuffer *inBuf, GstBuffer *outBuf);
static gboolean gst_shvidresize_get_unit_size (GstBaseTransform *trans,
 GstCaps *caps, guint *size);
static GstFlowReturn gst_shvidresize_prepare_output_buffer (GstBaseTransform
 *trans, GstBuffer *inBuf, gint size, GstCaps *caps, GstBuffer **outBuf);

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

		/* Initialize GST_LOG for this object */
		GST_DEBUG_CATEGORY_INIT(gst_shvidresize_debug,
			"gst-sh-mobile-resize", 0, "SH Video Resize");

		GST_LOG("initialized get_type");
	}

	return object_type;
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
	trans_class->set_caps       = GST_DEBUG_FUNCPTR(gst_shvidresize_set_caps);
	trans_class->transform      = GST_DEBUG_FUNCPTR(gst_shvidresize_transform);
	trans_class->get_unit_size  = GST_DEBUG_FUNCPTR(gst_shvidresize_get_unit_size);
	trans_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_shvidresize_prepare_output_buffer);
	trans_class->passthrough_on_same_caps = TRUE;
	parent_class = g_type_class_peek_parent (klass);

	GST_LOG("finished class init");
}

/*
 * gst_shvidresize_init
 */
static void gst_shvidresize_init (GstSHVidresize *vidresize)
{
	GST_LOG("begin");

	// TODO add fail checks
	vidresize->uiomux = uiomux_open();
	vidresize->veu = shveu_open();

	GST_LOG("end");
}


/*****************************************************************************/

/*
 * Helper: parse_caps
 */
static gboolean parse_caps (GstCaps *cap, gint *width,
	gint *height, guint32 *format)
{
	GstStructure *structure;
	structure = gst_caps_get_structure(cap, 0);

	GST_LOG("begin");

	if (!gst_structure_get_int(structure, "width", width)) {
		GST_ERROR("Failed to get width");
		return FALSE;
	}

	if (!gst_structure_get_int(structure, "height", height)) {
		GST_ERROR("Failed to get height");
		return FALSE;
	}

	if (!gst_structure_get_fourcc(structure, "format", format)) {
		GST_ERROR("failed to get fourcc from cap");
		return FALSE;
	}

	GST_LOG("end");

	return TRUE; 
}

/*
 * Helper: get_v4l2format
 */
int get_v4l2format (guint32 fourcc)
{
	switch (fourcc) {
	case GST_MAKE_FOURCC('N', 'V', '1', '2'):
		return V4L2_PIX_FMT_NV12;
	default:
		GST_ERROR("failed to get colorspace");
		return 0;
	}
}

/*****************************************************************************/

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
	guint32 fourcc;
	int v4l2fmt;

	GST_LOG("begin");

	/* Get the width, height & format from the caps */
	if (!parse_caps(caps, &width, &height, &fourcc)) {
		GST_ERROR("Failed to get resolution");
		return FALSE;
	}
	v4l2fmt = get_v4l2format(fourcc);

	GST_LOG("output size = %dx%d, format=%d", width, height, v4l2fmt);

	*outBuf = gst_sh_video_buffer_new(vidresize->uiomux, width, height, v4l2fmt);

	if (*outBuf == NULL) {
		GST_ELEMENT_ERROR(vidresize, RESOURCE, NO_SPACE_LEFT,
			("failed to allocate output buffer"), (NULL));
		return GST_FLOW_ERROR;
	}

	gst_buffer_set_caps(*outBuf, GST_PAD_CAPS(trans->srcpad));

	GST_LOG("alloacted dst py=%p, pc=%p", GST_SH_VIDEO_BUFFER_Y_DATA(*outBuf), GST_SH_VIDEO_BUFFER_C_DATA(*outBuf));

	GST_LOG("end");   

	return GST_FLOW_OK;
}

/*
 * GstBaseTransformClass::get_unit_size
 * Required if the transform is not in-place. get the size in bytes of one unit
 * for the given caps.
 */
static gboolean gst_shvidresize_get_unit_size (GstBaseTransform *trans,
	GstCaps *caps, guint *size)
{
	gint height, width;
	guint32 fourcc;
	int v4l2fmt;

	GST_LOG("begin");

	if (!parse_caps(caps, &width, &height, &fourcc)) {
		GST_ERROR("Failed to get resolution");
		return FALSE;
	}
	v4l2fmt = get_v4l2format(fourcc);

	// TODO calc buf size
	*size =1;

	GST_LOG("setting unit_size = %d", *size);

	GST_LOG("end"); 
	return TRUE; 
}

/*
 * GstBaseTransformClass::transform
 * Required if the element does not operate in-place. Transforms one incoming
 * buffer to one outgoing buffer. The function is allowed to change
 * size/timestamp/duration of the outgoing buffer.
 */
static GstFlowReturn gst_shvidresize_transform (GstBaseTransform *trans,
	GstBuffer *src, GstBuffer *dst)
{
	GstSHVidresize *vidresize = GST_SHVIDRESIZE(trans);
	GstBuffer *src_sh = NULL;
	GstFlowReturn ret = GST_FLOW_ERROR;

	GST_LOG("begin");

	if (!GST_IS_SH_VIDEO_BUFFER(dst)) {
		/* Something has gone badly wrong... */
		GST_ELEMENT_ERROR(vidresize, RESOURCE, WRITE,
			("output buf isn't an SH buffer!"), (NULL));
		goto exit;
	}

	/* Get the input buffer handle */
	if (GST_IS_SH_VIDEO_BUFFER(src)) {
		/* Can be passed straight to VEU hardware */
		GST_LOG("Input buffer is SH type");
		src_sh = src;
	} else {
		GST_LOG("Input buffer is not SH type (will copy data)");

		/* If we are recieving data from a non-SH video buffer, allocate
		   one and copy the data over. */
		src_sh = gst_sh_video_buffer_new(
			vidresize->uiomux,
			vidresize->srcWidth,
			vidresize->srcHeight,
			vidresize->srcColorSpace);

		if (src_sh == NULL) {
			GST_ELEMENT_ERROR(vidresize, RESOURCE, NO_SPACE_LEFT,
				("failed to create input SH buffer"), (NULL));
			goto exit;
		}

		// TODO deal with SH chroma alignment...
		memcpy(GST_BUFFER_DATA(src_sh), GST_BUFFER_DATA(src), GST_BUFFER_SIZE(src));
	}

	/* Create resize handle */
	GST_LOG("scaling from=%dx%d -> to=%dx%d", 
		vidresize->srcWidth, vidresize->srcHeight,
		vidresize->dstWidth, vidresize->dstHeight);

	GST_LOG("src py=%p, pc=%p", GST_SH_VIDEO_BUFFER_Y_DATA(src_sh), GST_SH_VIDEO_BUFFER_C_DATA(src_sh));
	GST_LOG("dst py=%p, pc=%p", GST_SH_VIDEO_BUFFER_Y_DATA(dst), GST_SH_VIDEO_BUFFER_C_DATA(dst));

	/* Use VEU to resize buffer */
	if (shveu_rescale(vidresize->veu,
		(unsigned long)GST_SH_VIDEO_BUFFER_Y_DATA(src_sh),
		(unsigned long)GST_SH_VIDEO_BUFFER_C_DATA(src_sh),
		(unsigned long)vidresize->srcWidth,
		(unsigned long)vidresize->srcHeight,
		vidresize->srcColorSpace,
		(unsigned long)GST_SH_VIDEO_BUFFER_Y_DATA(dst),
		(unsigned long)GST_SH_VIDEO_BUFFER_C_DATA(dst),
		(unsigned long)vidresize->dstWidth,
		(unsigned long)vidresize->dstHeight,
		vidresize->dstColorSpace) < 0)
	{
		GST_ELEMENT_ERROR(vidresize, RESOURCE, FAILED,
			("failed to execute veu resize"), (NULL));
		goto exit;
	}

	ret = GST_FLOW_OK;

exit:
	if (src_sh && !GST_IS_SH_VIDEO_BUFFER(src)) {
		// TODO
		gst_buffer_unref(src_sh);
	}

	GST_LOG("end");
	return ret;
}

/*
 * GstBaseTransformClass::transform_caps
 * Optional. Given the pad in this direction and the given caps, what caps are
 * allowed on the other pad in this element?
 */
static GstCaps *gst_shvidresize_transform_caps (GstBaseTransform *trans,
	GstPadDirection direction, GstCaps *caps)
{
	GstCaps *ret;

	GST_LOG("begin (%s)", direction==GST_PAD_SRC ? "src" : "sink");

	// TODO should limit scale up/down
	ret = gst_caps_new_simple ("video/x-raw-yuv",
		"format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N','V','1','2'),
		"width", GST_TYPE_INT_RANGE, 16, 4096,
		"height", GST_TYPE_INT_RANGE, 16, 4096, NULL);

	return ret;
}

/*
 * GstBaseTransformClass::set_caps
 * allows the subclass to be notified of the actual caps set.
 */
static gboolean gst_shvidresize_set_caps (GstBaseTransform *trans, 
	GstCaps *in, GstCaps *out)
{
	GstSHVidresize      *vidresize  = GST_SHVIDRESIZE(trans);
	gboolean            ret         = FALSE;
	guint32             fourcc;
//	guint               outBufSize;

	GST_LOG("begin");

	/* parse input cap */
	if (!parse_caps(in, &vidresize->srcWidth,
			&vidresize->srcHeight, &fourcc)) {
		GST_ELEMENT_ERROR(vidresize, RESOURCE, FAILED,
			("failed to get input resolution"), (NULL));
		goto exit;
	}

	GST_LOG("input: %dx%d %" GST_FOURCC_FORMAT, vidresize->srcWidth, vidresize->srcHeight, GST_FOURCC_ARGS(fourcc));

	/* map fourcc with its corresponding v4l2 colorspace type */ 
	vidresize->srcColorSpace = get_v4l2format(fourcc);


	/* parse output cap */
	if (!parse_caps(out, &vidresize->dstWidth,
			&vidresize->dstHeight, &fourcc)) {
		GST_ELEMENT_ERROR(vidresize, RESOURCE, FAILED,
			("failed to get output resolution"), (NULL));
		goto exit;
	}

	GST_LOG("output: %dx%d %" GST_FOURCC_FORMAT, vidresize->dstWidth, vidresize->dstHeight, GST_FOURCC_ARGS(fourcc));

	/* map fourcc with its corresponding v4l2 colorspace type */
	vidresize->dstColorSpace = get_v4l2format(fourcc);

	// TODO If allocating a buffer pool, here would be a good place

	ret = TRUE;

exit:
	GST_LOG("end");
	return ret;
}

/*
 * GObjectClass::finalize
 *    Shut down any running video resize, and reset the element state.
 */
static gboolean gst_shvidresize_exit_resize(GstSHVidresize *vidresize)
{
	GST_LOG("begin");

	/* Shut down remaining items */
	if (vidresize->veu) {
		shveu_close(vidresize->veu);
		vidresize->veu = NULL;
	}

	if (vidresize->uiomux) {
		uiomux_close(vidresize->uiomux);
		vidresize->uiomux = NULL;
	}

	GST_LOG("end");
	return TRUE;
}

