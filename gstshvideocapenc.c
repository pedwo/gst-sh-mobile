/**
 * gst-sh-mobile-camera-enc
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
 * Takashi Namiki <takashi.namiki@renesas.com>
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/fb.h>
#include <linux/videodev2.h>	/* For pixel formats */
#include <uiomux/uiomux.h>
#include <shveu/shveu.h>

#include "gstshvideocapenc.h"
#include "cntlfile/capture.h"

typedef enum
{
  PREVIEW_OFF,
  PREVIEW_ON,
  PREVIEW_NONE
} GstCameraPreview;


/**
 * Define Gstreamer SH Video Encoder structure
 */
struct _GstshvideoEnc
{
  GstElement element;
  GstPad *sinkpad, *srcpad;
  GstBuffer *buffer_yuv;
  GstBuffer *buffer_cbcr;

  gint offset;
  SHCodecs_Format format;  
  SHCodecs_Encoder* encoder;
  gint width;
  gint height;
  gint fps_numerator;
  gint fps_denominator;

  APPLI_INFO ainfo;
  
  GstCaps* out_caps;
  gboolean caps_set;
  glong frame_number;

  GstClock* clock;
  GstClockTime start_time;

  pthread_t enc_thread;
  pthread_t capture_thread;
  pthread_t blit_thread;

  struct fb_var_screeninfo fbinfo;
  struct fb_fix_screeninfo finfo;
  pthread_mutex_t capture_start_mutex;
  pthread_mutex_t capture_end_mutex;
  pthread_mutex_t blit_mutex;
  pthread_mutex_t blit_vpu_end_mutex;
  pthread_mutex_t output_mutex;

  UIOMux * uiomux;
  int veu;

  int ceu_buf_size;
  int ceu_buf_num;
  unsigned char *ceu_ubuf;
  unsigned int enc_in_yaddr;
  unsigned int enc_in_caddr;
  GstCameraPreview preview;

  gboolean output_lock;
};


/**
 * Define Gstreamer SH Video Encoder Class structure
 */
struct _GstshvideoEncClass
{
  GstElementClass parent;
};


/**
 * Define capatibilities for the source factory
 */

static GstStaticPadTemplate src_factory = 
  GST_STATIC_PAD_TEMPLATE ("src",
			   GST_PAD_SRC,
			   GST_PAD_ALWAYS,
			   GST_STATIC_CAPS ("video/mpeg,"
					    "width = (int) [16, 1280],"
					    "height = (int) [16, 720],"
					    "framerate = (fraction) [0, 30],"
					    "mpegversion = (int) 4"
					    "; "
					    "video/x-h264,"
					    "width = (int) [16, 1280],"
					    "height = (int) [16, 720],"
					    "framerate = (fraction) [0, 30]"
					    )
			   );

GST_DEBUG_CATEGORY_STATIC (gst_sh_mobile_debug);
#define GST_CAT_DEFAULT gst_sh_mobile_debug

static GstElementClass *parent_class = NULL;

/* Forward declarations */
static void gst_shvideo_enc_init_class (gpointer g_class, gpointer data);
static void gst_shvideo_enc_base_init (gpointer klass);
static void gst_shvideo_enc_dispose (GObject * object);
static void gst_shvideo_enc_class_init (GstshvideoEncClass *klass);
static void gst_shvideo_enc_init (GstshvideoEnc *shvideoenc, GstshvideoEncClass *gklass);
static void gst_shvideo_enc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec * pspec);
static void gst_shvideo_enc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_shvideo_enc_src_query (GstPad * pad, GstQuery * query);
static int gst_shvideo_enc_get_input(SHCodecs_Encoder * encoder, void *user_data);
static int gst_shvideo_enc_write_output(SHCodecs_Encoder * encoder,	unsigned char *data, int length, void *user_data);
static void gst_shvideo_enc_init_camera_encoder(GstshvideoEnc * shvideoenc);
static void *launch_camera_encoder_thread(void *data);
static void *capture_thread(void *data);
static void *blit_thread(void *data);
static void capture_image_cb(sh_ceu * ceu, const unsigned char *frame_data, 
                             size_t length, void *user_data, int buffer_number);
static GType gst_camera_preview_get_type (void);
static gboolean gst_shvideoenc_src_event (GstPad *pad, GstEvent *event);
static GstStateChangeReturn gst_shvideo_enc_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_shvideoenc_set_clock (GstElement *element, GstClock *clock);
static gboolean gst_shvideocameraenc_set_src_caps(GstPad *pad, GstCaps *caps);
static void gst_shvideocameraenc_read_src_caps(GstshvideoEnc * shvideoenc);
/**
 * Define encoder properties
 */

enum
{
  PROP_0,
  PROP_CNTL_FILE,
  PROP_PREVIEW,
  PROP_LAST
};

#define GST_TYPE_CAMERA_PREVIEW (gst_camera_preview_get_type())
static GType
gst_camera_preview_get_type (void)
{
  static GType camera_preview_type = 0;
  static const GEnumValue preview_method[] = {
    {PREVIEW_OFF, "No camera preview", "off"},
    {PREVIEW_ON, "Camera preview", "on"},
    {PREVIEW_NONE, "Camera preview is not specified", "none"},
    {0, NULL, NULL},
  };

  if (!camera_preview_type) {
    camera_preview_type = g_enum_register_static ("GstCameraPreview", preview_method);
  }
  return camera_preview_type;
}

/** ceu callback function
    @param sh_ceu 
    @param frame_data output buffer pointer
    @param length buffer size
    @param user_data user pointer
    @param buffer_number current buffer number
*/
static void
capture_image_cb(sh_ceu * ceu, const unsigned char *frame_data, size_t length,
		 void *user_data, int buffer_number)
{
  GstshvideoEnc *shvideoenc = (GstshvideoEnc *)user_data;
  unsigned int pixel_format;

  pixel_format = sh_ceu_get_pixel_format (ceu);

  if (pixel_format == V4L2_PIX_FMT_NV12) {
    shvideoenc->ceu_buf_size = length;
    shvideoenc->ceu_buf_num = buffer_number;
    shvideoenc->ceu_ubuf = (unsigned char *)frame_data;
    shcodecs_encoder_get_input_physical_addr (shvideoenc->encoder, 
             (unsigned int *)&shvideoenc->enc_in_yaddr, (unsigned int *)&shvideoenc->enc_in_caddr);    
  }
}

static void *capture_thread(void *data)
{
  GstshvideoEnc *shvideoenc = (GstshvideoEnc *)data;

  while(1)
  {
    GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

    //This mutex is released by the VPU get_input call back, created locked
	pthread_mutex_lock(&shvideoenc->capture_start_mutex); 

    sh_ceu_capture_frame(shvideoenc->ainfo.ceu, (sh_process_callback)capture_image_cb, shvideoenc);

    //This mutex releases the VEU copy to the VPU input buffer and the framebuffer
	pthread_mutex_unlock(&shvideoenc->blit_mutex);
  }
}

//#define USE_UIOMUX_VIRT_TO_PHYS

static void *blit_thread(void *data)
{
  GstshvideoEnc *shvideoenc = (GstshvideoEnc *)data;
  unsigned long veu_base;
  unsigned long in_yaddr;
  unsigned long in_caddr;

#ifndef USE_UIOMUX_VIRT_TO_PHYS
  uiomux_get_mem (shvideoenc->uiomux, UIOMUX_SH_VEU, &veu_base, NULL, NULL);
#endif

  while(1)
  {
    GST_LOG_OBJECT(shvideoenc,"%s called preview %d",__FUNCTION__, shvideoenc->preview);
    pthread_mutex_lock(&shvideoenc->blit_mutex); 

#ifdef USE_UIOMUX_VIRT_TO_PHYS 
    veu_base = uiomux_virt_to_phys (shvideoenc->uiomux, UIOMUX_SH_VEU, shvideoenc->ceu_ubuf);
    in_yaddr = veu_base;
#else
    in_yaddr = veu_base+shvideoenc->ceu_buf_size*shvideoenc->ceu_buf_num;
#endif

    in_caddr = in_yaddr+shvideoenc->ceu_buf_size/2;
    /* memory copy from ceu output buffer to vpu input buffer */

#if 0
    fprintf (stderr, "Resizing input data from %lu from size %ld x %ld to size %d x %d\n",
             in_yaddr, shvideoenc->ainfo.xpic, shvideoenc->ainfo.ypic, shvideoenc->width, shvideoenc->height);
#endif

    shveu_operation(
      shvideoenc->veu, 
      in_yaddr,
      in_caddr,
      shvideoenc->ainfo.xpic,
      shvideoenc->ainfo.ypic,
      shvideoenc->ainfo.xpic,
      SHVEU_YCbCr420,
      shvideoenc->enc_in_yaddr,
      shvideoenc->enc_in_caddr,
      (long)shvideoenc->width,
      (long)shvideoenc->height,
      (long)shvideoenc->width,
      SHVEU_YCbCr420,
      SHVEU_NO_ROT);
    pthread_mutex_unlock(&shvideoenc->blit_vpu_end_mutex);

    if(shvideoenc->preview == PREVIEW_ON)
    {
      shveu_operation(
        shvideoenc->veu, 
        shvideoenc->enc_in_yaddr,
        shvideoenc->enc_in_caddr,
        (long)shvideoenc->width,
        (long)shvideoenc->height,
        (long)shvideoenc->width,
        SHVEU_YCbCr420,
        shvideoenc->finfo.smem_start,
        0UL,
        (long)shvideoenc->fbinfo.xres,
        (long)shvideoenc->fbinfo.yres,
        (long)shvideoenc->fbinfo.xres,
        SHVEU_RGB565,
        SHVEU_NO_ROT);
    }
	pthread_mutex_unlock(&shvideoenc->capture_end_mutex);
  }
}

/** Initialize shvideoenc class plugin event handler
    @param g_class Gclass
    @param data user data pointer, unused in the function
*/
static void
gst_shvideo_enc_init_class (gpointer g_class, gpointer data)
{
  GST_LOG("%s called",__FUNCTION__);
  parent_class = g_type_class_peek_parent (g_class);
  gst_shvideo_enc_class_init ((GstshvideoEncClass *) g_class);
}

/** Initialize SH hardware video encoder
    @param klass Gstreamer element class
*/
static void
gst_shvideo_enc_base_init (gpointer klass)
{
  static const GstElementDetails plugin_details =
    GST_ELEMENT_DETAILS ("SH hardware camera capture & video encoder",
			 "Codec/Encoder/Video/Src",
			 "Encode mpeg-based video stream (mpeg4, h264)",
			 "Takashi Namiki <takashi.namiki@renesas.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_LOG("%s called",__FUNCTION__);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

/** Dispose encoder
    @param object Gstreamer element class
*/
static void
gst_shvideo_enc_dispose (GObject * object)
{
  GstshvideoEnc *shvideoenc = GST_SHVIDEOENC (object);
  int fd;
  void *iomem;
  GST_LOG("%s called",__FUNCTION__);

  sh_ceu_stop_capturing(shvideoenc->ainfo.ceu);

  if (shvideoenc->encoder!=NULL)
  {
    shcodecs_encoder_close(shvideoenc->encoder);
    shvideoenc->encoder=NULL;
  }

  if(shvideoenc->preview == PREVIEW_ON)
  {
    //Open the frame buffer device to clear frame buffer
    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0)
    {
      GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,("Error opening fb device to get the resolution"), (NULL));
    }
    iomem = mmap(0, shvideoenc->finfo.smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (iomem == MAP_FAILED) {
      GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,("mmap"), (NULL));
    }
    /* clear framebuffer */
    memset(iomem, 0, shvideoenc->finfo.line_length * shvideoenc->fbinfo.yres);
    munmap(iomem, shvideoenc->finfo.smem_len);
    close (fd);
  }

  shveu_close();
  sh_ceu_close(shvideoenc->ainfo.ceu);

  uiomux_close (shvideoenc->uiomux);

  pthread_cancel (shvideoenc->enc_thread);
  pthread_cancel (shvideoenc->capture_thread);
  pthread_cancel (shvideoenc->blit_thread);

  pthread_mutex_destroy(&shvideoenc->capture_start_mutex);
  pthread_mutex_destroy(&shvideoenc->capture_end_mutex);
  pthread_mutex_destroy(&shvideoenc->blit_mutex);
  pthread_mutex_destroy(&shvideoenc->blit_vpu_end_mutex);
  pthread_mutex_destroy(&shvideoenc->output_mutex);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static gboolean            
gst_shvideoenc_set_clock (GstElement *element, GstClock *clock)
{
  GstshvideoEnc *enc = (GstshvideoEnc *) element;
  
  GST_DEBUG_OBJECT(enc,"%s called",__FUNCTION__);

  if(!clock)
  {
    GST_DEBUG_OBJECT(enc,"Using system clock");
    enc->clock = gst_system_clock_obtain();
    return FALSE;
  }
  else
  {
    GST_DEBUG_OBJECT(enc,"Clock accepted");
    enc->clock = clock;
    return TRUE;
  }
}


/** Initialize the class for encoder
    @param klass Gstreamer SH video encoder class
*/
static void
gst_shvideo_enc_class_init (GstshvideoEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  GST_LOG("%s called",__FUNCTION__);
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_shvideo_enc_dispose;
  gobject_class->set_property = gst_shvideo_enc_set_property;
  gobject_class->get_property = gst_shvideo_enc_get_property;
  gstelement_class->set_clock = gst_shvideoenc_set_clock;
  gstelement_class->change_state = gst_shvideo_enc_change_state;

  GST_DEBUG_CATEGORY_INIT (gst_sh_mobile_debug, "gst-sh-mobile-camera-enc",
      0, "Encoder for H264/MPEG4 streams");

  g_object_class_install_property (gobject_class, PROP_CNTL_FILE,
      g_param_spec_string ("cntl-file", "Control file location", 
			"Location of the file including encoding parameters", 
			   NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PREVIEW,
      g_param_spec_enum ("preview", "Camera preview","camera preview",
      GST_TYPE_CAMERA_PREVIEW, PREVIEW_NONE, G_PARAM_READWRITE));
}

/** Initialize the encoder
    @param shvideoenc Gstreamer SH video element
    @param gklass Gstreamer SH video encode class
*/
static void
gst_shvideo_enc_init (GstshvideoEnc * shvideoenc,
    GstshvideoEncClass * gklass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (shvideoenc);

  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  shvideoenc->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
//  gst_pad_use_fixed_caps (shvideoenc->srcpad);
  gst_pad_set_setcaps_function(shvideoenc->srcpad, gst_shvideocameraenc_set_src_caps);
//gst_shvideocameraenc_set_src_caps(shvideoenc->srcpad, gst_pad_peer_get_caps(shvideoenc->srcpad));

  gst_pad_set_query_function (shvideoenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_shvideo_enc_src_query));
  gst_pad_set_event_function(shvideoenc->srcpad, GST_DEBUG_FUNCPTR(gst_shvideoenc_src_event));

  gst_element_add_pad (GST_ELEMENT (shvideoenc), shvideoenc->srcpad);

  shvideoenc->encoder=NULL;
  shvideoenc->caps_set=FALSE;
  shvideoenc->enc_thread = 0;
  shvideoenc->buffer_yuv = NULL;
  shvideoenc->buffer_cbcr = NULL;

  pthread_mutex_init (&shvideoenc->capture_start_mutex, NULL);
  pthread_mutex_init (&shvideoenc->capture_end_mutex, NULL);
  pthread_mutex_init(&shvideoenc->blit_mutex, NULL);
  pthread_mutex_init(&shvideoenc->blit_vpu_end_mutex, NULL);
  pthread_mutex_init(&shvideoenc->output_mutex, NULL);

  shvideoenc->format = SHCodecs_Format_NONE;
  shvideoenc->out_caps = NULL;
  shvideoenc->width = 0;
  shvideoenc->height = 0;
  shvideoenc->fps_numerator = 25;
  shvideoenc->fps_denominator = 1;
  shvideoenc->frame_number = 0;
  shvideoenc->preview = PREVIEW_NONE;
  shvideoenc->output_lock = TRUE;
}


static GstStateChangeReturn
gst_shvideo_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstshvideoEnc * shvideoenc = (GstshvideoEnc *) element;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
       GST_DEBUG_OBJECT(shvideoenc,"GST_STATE_CHANGE_NULL_TO_READY");
	shvideoenc->output_lock = TRUE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
	GST_DEBUG_OBJECT(shvideoenc,"GST_STATE_CHANGE_READY_TO_PAUSED");
  	shvideoenc->output_lock = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
	GST_DEBUG_OBJECT(shvideoenc,"GST_STATE_CHANGE_PAUSED_TO_PLAYING");
	shvideoenc->output_lock = FALSE;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_DEBUG_OBJECT(shvideoenc,"GST_STATE_CHANGE_PLAYING_TO_PAUSED");
      shvideoenc->output_lock = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT(shvideoenc,"GST_STATE_CHANGE_PAUSED_TO_READY");
      shvideoenc->output_lock = TRUE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_DEBUG_OBJECT(shvideoenc,"GST_STATE_CHANGE_READY_TO_NULL");
      shvideoenc->output_lock = TRUE;
      break;
    default:
      break;
  }
  return ret;
}



/** Event handler for encoder src events, see GstPadEventFunction.
 */
static gboolean
gst_shvideoenc_src_event (GstPad *pad, GstEvent *event)
{
  GstshvideoEnc *enc = (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));
  gboolean ret=TRUE;

  GST_DEBUG_OBJECT(enc,"%s called event %i",__FUNCTION__,GST_EVENT_TYPE(event));

  switch (GST_EVENT_TYPE (event)) 
  {
    case GST_EVENT_LATENCY:
    {
      ret = TRUE;
      break;
    }
    default:
    {
      ret = FALSE;
      break;
    }
  }
  return ret;
}


/** The function will set the user defined control file name value for decoder
    @param object The object where to get Gstreamer SH video Encoder object
    @param prop_id The property id
    @param value In this case file name if prop_id is PROP_CNTL_FILE
    @param value In this case file name if prop_id is PROP_PREVIEW
    @param pspec not used in fuction
*/
static void
gst_shvideo_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  static int cntl = 0;
  static int preview = 0;
  GstshvideoEnc *shvideoenc = GST_SHVIDEOENC (object);
  
  GST_LOG("%s called",__FUNCTION__);
  switch (prop_id) 
  {
    case PROP_CNTL_FILE:
    {
      strcpy(shvideoenc->ainfo.ctrl_file_name_buf,g_value_get_string(value));
      cntl = 1;
      break;
    }
    case PROP_PREVIEW:
    {
      shvideoenc->preview = g_value_get_enum(value);
      preview = 1;
      break;
    }
    default:
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
  if((cntl==1) && (preview==1))
    gst_shvideo_enc_init_camera_encoder(shvideoenc);
}

/** The function will return the control file name from decoder to value
    @param object The object where to get Gstreamer SH video Encoder object
    @param prop_id The property id
    @param value In this case file name if prop_id is PROP_CNTL_FILE
    @param pspec not used in fuction
*/
static void
gst_shvideo_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstshvideoEnc *shvideoenc = GST_SHVIDEOENC (object);

  GST_LOG("%s called",__FUNCTION__);
  switch (prop_id) 
  {
    case PROP_CNTL_FILE:
    {
      g_value_set_string(value,shvideoenc->ainfo.ctrl_file_name_buf);      
      break;
    }
    case PROP_PREVIEW:
    {
      g_value_set_enum(value, shvideoenc->preview);      
      break;
    }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

/** Callback function for the encoder input
    @param encoder shcodecs encoder
    @param user_data Gstreamer SH encoder object
    @return 0 if encoder should continue. 1 if encoder should pause.
*/
static int 
gst_shvideo_enc_get_input(SHCodecs_Encoder * encoder, void *user_data)
{
  GstshvideoEnc *shvideoenc = (GstshvideoEnc *)user_data;
  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  pthread_mutex_lock(&shvideoenc->capture_end_mutex); //This mutex is created unlocked, so the first time it falls through
  pthread_mutex_unlock(&shvideoenc->capture_start_mutex); //Start the next capture, then return to the Encoder	
  pthread_mutex_lock(&shvideoenc->blit_vpu_end_mutex); //wait untill the VEU has copied the data to the VPU input buffer
  GST_LOG_OBJECT(shvideoenc,"%s end",__FUNCTION__);

  return 0;
}

/** Callback function for the encoder output
    @param encoder shcodecs encoder
    @param data the encoded video frame
    @param length size the encoded video frame buffer
    @param user_data Gstreamer SH encoder object
    @return 0 if encoder should continue. 1 if encoder should pause.
*/
static int 
gst_shvideo_enc_write_output(SHCodecs_Encoder * encoder,
			unsigned char *data, int length, void *user_data)
{
  GstshvideoEnc *enc = (GstshvideoEnc *)user_data;
  GstBuffer* buf=NULL;
  gint ret=0;
  static gboolean first_set = FALSE;
  long long unsigned int time_diff, stamp_diff, sleep_time;
  GstClockTime time_now;

  GST_LOG_OBJECT(enc,"%s called. Got %d bytes data\n",__FUNCTION__, length);
  
  if(length)
  {

    buf = gst_buffer_new();
    gst_buffer_set_data(buf, data, length);

    time_now = gst_clock_get_time(enc->clock);
    if (first_set == FALSE)
    {
      enc->start_time = time_now;
      first_set = TRUE;
    }
    time_diff = GST_TIME_AS_MSECONDS(GST_CLOCK_DIFF(enc->start_time, time_now));
    stamp_diff = enc->frame_number * ( 1000 * enc->fps_denominator / enc->fps_numerator );

    GST_DEBUG_OBJECT(enc,"Frame number: %d time from start: %llu stamp diff: %llu",
	  	   enc->frame_number, time_diff, stamp_diff);
    if (stamp_diff > time_diff)
    {
      sleep_time = stamp_diff - time_diff;
      GST_DEBUG_OBJECT(enc, "sleeping for: %llums", sleep_time);
      usleep(sleep_time*1000);
    }  
    GST_BUFFER_DURATION(buf) = enc->fps_denominator*1000*GST_MSECOND/enc->fps_numerator;
    GST_BUFFER_TIMESTAMP(buf) = enc->frame_number*GST_BUFFER_DURATION(buf);
    enc->frame_number++;

    ret = gst_pad_push (enc->srcpad, buf);

    if (ret != GST_FLOW_OK)
    {
      GST_DEBUG_OBJECT (enc, "pad_push failed: %s", gst_flow_get_name (ret));
      return -1;
    }
  }
  return 0;
}

/** Gstreamer source pad query 
    @param pad Gstreamer source pad
    @param query Gsteamer query
    @returns Returns the value of gst_pad_query_default
*/
static gboolean
gst_shvideo_enc_src_query (GstPad * pad, GstQuery * query)
{
  GstshvideoEnc *enc = 
    (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));
  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);
  return gst_pad_query_default (pad, query);
}

/** Initializes the SH Hardware encoder
    @param shvideoenc encoder object
*/
static void
gst_shvideo_enc_init_camera_encoder(GstshvideoEnc * shvideoenc)
{
  int fd;
  void *iomem;
  gint ret = 0;
  glong fmt = 0;

  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  ret = GetFromCtrlFTop((const char *)
				shvideoenc->ainfo.ctrl_file_name_buf,
				&shvideoenc->ainfo,
				&fmt);
  if (ret < 0)
  {
    GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,
		      ("Error reading control file."), (NULL));
  }

  if(shvideoenc->preview == PREVIEW_ON)
  {
    //Open the frame buffer device /dev/fb0 and find out the resolution,
    //used to centre the image on the display, and the framebuffer address
    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0)
    {
      GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,("Error opening fb device to get the resolution"), (NULL));
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &shvideoenc->fbinfo) == -1)
    {
      GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,("ioctl(FBIOGET_VSCREENINFO)"), (NULL));
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &shvideoenc->finfo) == -1)
    {
      GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,("ioctl(FBIOGET_FSCREENINFO)"), (NULL));
    }
    iomem = mmap(0, shvideoenc->finfo.smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (iomem == MAP_FAILED) {
      GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,("mmap"), (NULL));
    }
    /* clear framebuffer */
    memset(iomem, 0, shvideoenc->finfo.line_length * shvideoenc->fbinfo.yres);
    munmap(iomem, shvideoenc->finfo.smem_len);
    close (fd);
  }

  //Initalise the mutexes;
  pthread_mutex_lock(&shvideoenc->capture_start_mutex);
  pthread_mutex_unlock(&shvideoenc->capture_end_mutex);
  pthread_mutex_lock(&shvideoenc->blit_mutex);
  pthread_mutex_lock(&shvideoenc->blit_vpu_end_mutex);

  if(!shvideoenc->enc_thread)
  {
    /* We'll have to launch the encoder in 
       a separate thread to keep the pipeline running */
    pthread_create( &shvideoenc->enc_thread, NULL, launch_camera_encoder_thread, shvideoenc);
  }
}

/** Launches the encoder in an own thread
    @param data encoder object
*/
static void *
launch_camera_encoder_thread(void *data)
{
  gint ret;
  GstshvideoEnc *enc = (GstshvideoEnc *)data;

  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);


#if 0
  while(gst_pad_get_peer (enc->srcpad) == NULL)
  {
    usleep(10);

  }
#endif

  /* watit for  READY status */
  while (enc->output_lock == TRUE){
    usleep(10);
  }

  gst_shvideocameraenc_read_src_caps(enc);
  GST_LOG_OBJECT(enc,"set caps fps numerator %x fps denominator %x \n",enc->fps_numerator,enc->fps_denominator);

  if(enc->format == SHCodecs_Format_NONE)
  {
    enc->format = 0;
  }

  if(!enc->width)
  {
    enc->width = enc->ainfo.xpic;
  }

  if(!enc->height)
  {
    enc->height = enc->ainfo.ypic;
  }
  snprintf(enc->ainfo.input_file_name_buf, 256, "%s/%s",
		 enc->ainfo.buf_input_yuv_file_with_path,
		 enc->ainfo.buf_input_yuv_file);

  /* uiomux open */
  enc->uiomux = uiomux_open ();

  /* veu oopen */
  enc->veu = shveu_open();

  /* ceu oopen */
  enc->ainfo.ceu = sh_ceu_open(enc->ainfo.input_file_name_buf,
                            enc->ainfo.xpic,
                            enc->ainfo.ypic,
                            IO_METHOD_USERPTR,
                            enc->uiomux);

  enc->encoder = shcodecs_encoder_init(enc->width, 
					      enc->height, 
					      enc->format);

  shcodecs_encoder_set_input_callback(enc->encoder, 
				      gst_shvideo_enc_get_input, 
				      enc);
  shcodecs_encoder_set_output_callback(enc->encoder, 
				       gst_shvideo_enc_write_output, 
				       enc);

  ret = GetFromCtrlFtoEncParam(enc->encoder, &enc->ainfo);
  if (ret < 0) 
  {
    GST_ELEMENT_ERROR((GstElement*)enc,CORE,FAILED,
		      ("Error reading control file."), (NULL));
  }

  if(enc->fps_numerator && enc->fps_denominator)
  {
    shcodecs_encoder_set_frame_rate(enc->encoder,
				  (enc->fps_numerator/enc->fps_denominator)*10);

    if(enc->format == SHCodecs_Format_H264)
    {
      shcodecs_encoder_set_h264_sps_frame_rate_info(enc->encoder, enc->fps_numerator, enc->fps_denominator);
    }
  }
  shcodecs_encoder_set_xpic_size(enc->encoder,enc->width);
  shcodecs_encoder_set_ypic_size(enc->encoder,enc->height);

  shcodecs_encoder_set_frame_no_increment(enc->encoder,
      shcodecs_encoder_get_frame_num_resolution(enc->encoder) /
      (shcodecs_encoder_get_frame_rate(enc->encoder) / 10)); 

  GST_DEBUG_OBJECT(enc,"Encoder init: %ldx%ld %ldfps format:%ld",
		   shcodecs_encoder_get_xpic_size(enc->encoder),
		   shcodecs_encoder_get_ypic_size(enc->encoder),
		   shcodecs_encoder_get_frame_rate(enc->encoder),
		   shcodecs_encoder_get_frame_rate(enc->encoder)/10);

  //Create the threads
  if(!enc->capture_thread)
  {
    pthread_create(&enc->capture_thread, NULL, capture_thread, enc);
  }
  if(!enc->blit_thread)
  {
    pthread_create(&enc->blit_thread, NULL, blit_thread, enc);
  }

  sh_ceu_start_capturing(enc->ainfo.ceu);

  ret = shcodecs_encoder_run(enc->encoder);

  GST_DEBUG_OBJECT (enc,"shcodecs_encoder_run returned %d\n",ret);

  gst_pad_push_event(enc->srcpad,gst_event_new_eos ());

  return NULL;
}


GType gst_shvideo_enc_get_type (void)
{
  static GType object_type = 0;

  GST_LOG("%s called",__FUNCTION__);
  if (object_type == 0) 
  {
    static const GTypeInfo object_info = {
      sizeof (GstshvideoEncClass),
      gst_shvideo_enc_base_init,
      NULL,
      gst_shvideo_enc_init_class,
      NULL,
      NULL,
      sizeof (GstshvideoEnc),
      0,
      (GInstanceInitFunc) gst_shvideo_enc_init
    };
    
    object_type = g_type_register_static (GST_TYPE_ELEMENT, "gst-sh-mobile-camera-enc", 
			        &object_info, (GTypeFlags) 0);
  }
  return object_type;
}

/** Reads the capabilities of the peer element behind source pad
    @param shvideoenc encoder object
*/
static void
gst_shvideocameraenc_read_src_caps(GstshvideoEnc * shvideoenc)
{
  GstStructure *structure;

  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  // get the caps of the next element in chain
  shvideoenc->out_caps = gst_pad_peer_get_caps(shvideoenc->srcpad);
  
  // Any format is ok too
  if(!gst_caps_is_any(shvideoenc->out_caps))
  {
    structure = gst_caps_get_structure (shvideoenc->out_caps, 0);
    if (!strcmp (gst_structure_get_name (structure), "video/mpeg"))
    {
      shvideoenc->format = SHCodecs_Format_MPEG4;
    }
    else if (!strcmp (gst_structure_get_name (structure), "video/x-h264")) 
    {
      shvideoenc->format = SHCodecs_Format_H264;
    }

    gst_structure_get_int (structure, "width",  &shvideoenc->width);
    gst_structure_get_int (structure, "height", &shvideoenc->height);
    gst_structure_get_fraction (structure, "framerate", 
				  &shvideoenc->fps_numerator, 
				  &shvideoenc->fps_denominator);
  }
}


/** Sets the capabilities of the source pad
    @param shvideoenc encoder object
    @return TRUE if the capabilities could be set, otherwise FALSE
*/
static gboolean
gst_shvideocameraenc_set_src_caps(GstPad * pad, GstCaps * caps)
{
  GstStructure *structure = NULL;
  GstshvideoEnc *shvideoenc = (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));
  gboolean ret = TRUE;

  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

 if (shvideoenc->encoder!=NULL) 
  {
    GST_DEBUG_OBJECT(shvideoenc,"%s: Encoder already opened",__FUNCTION__);
    return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);

  if (!strcmp (gst_structure_get_name (structure), "video/x-h264")) 
  {
    GST_DEBUG_OBJECT(shvideoenc, "codec format is video/x-h264");
    shvideoenc->format = SHCodecs_Format_H264;
  }
  else if (!strcmp (gst_structure_get_name (structure), "video/mpeg"))
  {
    GST_DEBUG_OBJECT (shvideoenc, "codec format is video/mpeg");
    shvideoenc->format = SHCodecs_Format_MPEG4;
  } else
  {
    GST_DEBUG_OBJECT(shvideoenc,"%s failed (not supported: %s)",__FUNCTION__,gst_structure_get_name (structure));
    return FALSE;
  }

  if(!gst_structure_get_fraction (structure, "framerate", 
				  &shvideoenc->fps_numerator, 
				  &shvideoenc->fps_denominator))
  {
    GST_DEBUG_OBJECT(shvideoenc,"%s failed (no framerate)",__FUNCTION__);
    return FALSE;
  }

  if(!gst_structure_get_int (structure, "width",  &shvideoenc->width))
  {
    GST_DEBUG_OBJECT(shvideoenc,"%s failed (no width)",__FUNCTION__);
    return FALSE;
  }

  if(!gst_structure_get_int (structure, "height", &shvideoenc->height))
  {
    GST_DEBUG_OBJECT(shvideoenc,"%s failed (no height)",__FUNCTION__);
    return FALSE;
  }

  if(!gst_pad_set_caps(shvideoenc->srcpad,caps))
  {
    GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,NEGOTIATION,
		      ("Source pad not linked."), (NULL));
    ret = FALSE;
  }
  if(!gst_pad_set_caps(gst_pad_get_peer(shvideoenc->srcpad),caps))
  {
    GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,NEGOTIATION,
		      ("Source pad not linked."), (NULL));
    ret = FALSE;
  }
  gst_caps_unref(caps);

  return ret;
}

gboolean
gst_shvideo_camera_enc_plugin_init (GstPlugin * plugin)
{
  GST_LOG("%s called",__FUNCTION__);
  if (!gst_element_register (plugin, "gst-sh-mobile-camera-enc", GST_RANK_PRIMARY,
          GST_TYPE_SHVIDEOENC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gst-sh-mobile-camera-enc",
    "gst-sh-mobile",
    gst_shvideo_camera_enc_plugin_init,
    VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)


