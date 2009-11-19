/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <uiomux/uiomux.h>
#include <shveu/shveu.h>
#include <asm/types.h>          /* for videodev2.h */

#include <linux/videodev2.h>
#include "capture.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))
#define PAGE_ALIGN(addr) (((addr) + getpagesize() - 1) & ~(getpagesize()-1))


struct buffer{
  void *start;
  size_t length;
};

static void errno_exit(const char *s)
{
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror (errno));
  exit(EXIT_FAILURE);
}



static int xioctl(int fd,int request, void *arg)
{
  int r;

  do r = ioctl(fd, request, arg);
  while (-1 == r && EINTR == errno);
  return r;
}

static int read_frame(sh_ceu * ceu, sh_process_callback cb, void * user_data)
{
  struct v4l2_buffer buf;
  unsigned int i;

  switch(ceu->io)
  {
  case IO_METHOD_READ:
    if(-1 == read (ceu->fd, ceu->buffers[0].start, ceu->buffers[0].length))
    {
      switch (errno)
      {
      case EAGAIN:
        return 0;
      case EIO:
        /* Could ignore EIO, see spec. */
        /* fall through */
      default:
        errno_exit ("read");
      }
    }
    //process_image (buffers[0].start);
	cb(ceu, ceu->buffers[0].start,ceu->buffers[0].length, user_data, 0);
    break;

  case IO_METHOD_MMAP:
    CLEAR (buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (-1 == xioctl (ceu->fd, VIDIOC_DQBUF, &buf)) 
    {
      switch (errno)
      {
      case EAGAIN:
        return 0;
      case EIO:
        /* Could ignore EIO, see spec. */
        /* fall through */
      default:
        errno_exit ("VIDIOC_DQBUF");
      }
    }
    assert(buf.index < ceu->n_buffers);
    cb(ceu, ceu->buffers[buf.index].start, ceu->buffers[buf.index].length, user_data, 0);
    if (-1 == xioctl (ceu->fd, VIDIOC_QBUF, &buf))
      errno_exit ("VIDIOC_QBUF");
    break;

  case IO_METHOD_USERPTR:
    CLEAR (buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_USERPTR;

    if (-1 == xioctl (ceu->fd, VIDIOC_DQBUF, &buf))
    {
      switch (errno)
      {
      case EAGAIN:
        return 0;
      case EIO:
        /* Could ignore EIO, see spec. */
        /* fall through */
      default:
        errno_exit ("VIDIOC_DQBUF");
      }
    }
    for (i = 0; i < ceu->n_buffers; ++i){
      /* TODO Work around the kernel - it sets the buffer size incorrectly */
      buf.length = ceu->buffers[i].length;

      if (buf.m.userptr == (unsigned long)ceu->buffers[i].start && buf.length == ceu->buffers[i].length)
        break;
    }
    assert(i < ceu->n_buffers);
    cb(ceu, (void *) buf.m.userptr, buf.length,user_data, i);
    if (-1 == xioctl (ceu->fd, VIDIOC_QBUF, &buf))
      errno_exit ("VIDIOC_QBUF");
    break;
  }
  return 1;
}


void sh_ceu_capture_frame(sh_ceu * ceu, sh_process_callback cb, void * user_data)
{
  for (;;)
  {
   fd_set fds;
   struct timeval tv;
   int r;
   FD_ZERO (&fds);
   FD_SET (ceu->fd, &fds);
   /* Timeout. */
   tv.tv_sec = 2;
   tv.tv_usec = 0;
   r = select (ceu->fd + 1, &fds, NULL, NULL, &tv);
   if (-1 == r)
   {
     if (EINTR == errno)
       continue;
     errno_exit ("select");
   }
   if (0 == r)
   {
     fprintf (stderr, "select timeout\n");
     exit (EXIT_FAILURE);
   }
   if (read_frame (ceu, cb, user_data))
     break;
   /* EAGAIN - continue select loop. */
  }
}

void sh_ceu_stop_capturing(sh_ceu * ceu)
{
  enum v4l2_buf_type type;

  switch (ceu->io) {
  case IO_METHOD_READ:
    /* Nothing to do. */
    break;
  case IO_METHOD_MMAP:
  case IO_METHOD_USERPTR:
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (ceu->fd, VIDIOC_STREAMOFF, &type))
      errno_exit ("VIDIOC_STREAMOFF");
    break;
  }
}

void sh_ceu_start_capturing(sh_ceu * ceu)
{
  unsigned int i;
  enum v4l2_buf_type type;

  switch (ceu->io) {
  case IO_METHOD_READ:
    /* Nothing to do. */
    break;

  case IO_METHOD_MMAP:
    for (i = 0; i < ceu->n_buffers; ++i) {
      struct v4l2_buffer buf;
      CLEAR (buf);
      buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory      = V4L2_MEMORY_MMAP;
      buf.index       = i;

      if (-1 == xioctl (ceu->fd, VIDIOC_QBUF, &buf))
        errno_exit ("VIDIOC_QBUF");
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (ceu->fd, VIDIOC_STREAMON, &type))
       errno_exit ("VIDIOC_STREAMON");
    break;

  case IO_METHOD_USERPTR:
    for (i = 0; i < ceu->n_buffers; ++i) {
      struct v4l2_buffer buf;
      CLEAR (buf);
      buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory      = V4L2_MEMORY_USERPTR;
      buf.index       = i;
      buf.m.userptr   = (unsigned long) ceu->buffers[i].start;
      buf.length      = ceu->buffers[i].length;
			
      if (-1 == xioctl (ceu->fd, VIDIOC_QBUF, &buf))
         errno_exit ("VIDIOC_QBUF");
      }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (ceu->fd, VIDIOC_STREAMON, &type))
    errno_exit ("VIDIOC_STREAMON");
    break;
  }
}

static void uninit_device(sh_ceu * ceu)
{
  unsigned int i;

  switch (ceu->io) {
  case IO_METHOD_READ:
    free (ceu->buffers[0].start);
    break;

  case IO_METHOD_MMAP:
    for (i = 0; i < ceu->n_buffers; ++i)
      if (-1 == munmap (ceu->buffers[i].start, ceu->buffers[i].length))
        errno_exit ("munmap");
    break;

  case IO_METHOD_USERPTR:
    for (i = 0; i < ceu->n_buffers; ++i)
      //  free (ceu->buffers[i].start);//this is the VEU UIO memory, not stuff we malloc'ed
      // uiomux_free (...);
      break;
  }
  free (ceu->buffers);
}

static void init_read (sh_ceu * ceu, unsigned int buffer_size)
{
  ceu->buffers = calloc (1, sizeof (*ceu->buffers));

  if (!ceu->buffers) {
    fprintf (stderr, "Out of memory\n");
    exit (EXIT_FAILURE);
  }
  ceu->buffers[0].length = buffer_size;
  ceu->buffers[0].start = malloc (buffer_size);

  if (!ceu->buffers[0].start) {
    fprintf (stderr, "Out of memory\n");
    exit (EXIT_FAILURE);
  }
}

static void init_mmap(sh_ceu * ceu)
{
  struct v4l2_requestbuffers req;
  CLEAR (req);
  req.count               = 4;
  req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory              = V4L2_MEMORY_MMAP;

  if (-1 == xioctl (ceu->fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf (stderr, "%s does not support " "memory mapping\n", ceu->dev_name);
      exit (EXIT_FAILURE);
    } else {
      errno_exit ("VIDIOC_REQBUFS");
    }
  }

  if (req.count < 2) {
    fprintf (stderr, "Insufficient buffer memory on %s\n", ceu->dev_name);
      exit (EXIT_FAILURE);
  }

  ceu->buffers = calloc (req.count, sizeof (*ceu->buffers));

  if (!ceu->buffers) {
    fprintf (stderr, "Out of memory\n");
    exit (EXIT_FAILURE);
  }

  for (ceu->n_buffers = 0; ceu->n_buffers < req.count; ++ceu->n_buffers) {
    struct v4l2_buffer buf;
    CLEAR (buf);

    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = ceu->n_buffers;

    if (-1 == xioctl (ceu->fd, VIDIOC_QUERYBUF, &buf))
      errno_exit ("VIDIOC_QUERYBUF");

    ceu->buffers[ceu->n_buffers].length = buf.length;
    ceu->buffers[ceu->n_buffers].start =
    mmap (NULL /* start anywhere */,
          buf.length,
          PROT_READ | PROT_WRITE /* required */,
          MAP_SHARED /* recommended */,
          ceu->fd, buf.m.offset);

    if (MAP_FAILED == ceu->buffers[ceu->n_buffers].start)
      errno_exit ("mmap");

  }
}


static void init_userp(sh_ceu * ceu, unsigned int buffer_size)
{
  struct v4l2_requestbuffers req;
  unsigned int page_size;

  page_size = getpagesize ();
  buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);

  CLEAR (req);

  req.count               = 2;
  req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory              = V4L2_MEMORY_USERPTR;

  if (-1 == xioctl (ceu->fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf (stderr, "%s does not support " "user pointer i/o\n", ceu->dev_name);
      exit (EXIT_FAILURE);
    } else {
      errno_exit ("VIDIOC_REQBUFS");
    }
  }

  if (req.count < 2) {
    fprintf (stderr, "Insufficient buffer memory on %s\n", ceu->dev_name);
    exit (EXIT_FAILURE);
  }

  ceu->buffers = calloc (req.count, sizeof (*ceu->buffers));

  if (!ceu->buffers) {
    fprintf (stderr, "Out of memory\n");
    exit (EXIT_FAILURE);
  }

  {
    fprintf (stderr, "Initializing for buffer size %u\n", buffer_size);

    // get veu's virtual address
    for (ceu->n_buffers = 0; ceu->n_buffers < req.count; ++ceu->n_buffers) {
      ceu->buffers[ceu->n_buffers].length = buffer_size;
      ceu->buffers[ceu->n_buffers].start = uiomux_malloc (ceu->uiomux, UIOMUX_SH_VEU, buffer_size, 32);
      if (!ceu->buffers[ceu->n_buffers].start) {
        fprintf (stderr, "Out of memory\n");
        exit (EXIT_FAILURE);
      }
    }
  }

}

static void init_device(sh_ceu * ceu)
{
  struct v4l2_capability cap;
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;
  struct v4l2_format fmt;
  int min;

  if (-1 == xioctl (ceu->fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf (stderr, "%s is no V4L2 device\n", ceu->dev_name);
      exit (EXIT_FAILURE);
    } else {
      errno_exit ("VIDIOC_QUERYCAP");
    }
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf (stderr, "%s is no video capture device\n", ceu->dev_name);
    exit (EXIT_FAILURE);
  }

  switch (ceu->io) {
  case IO_METHOD_READ:
    if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
      fprintf (stderr, "%s does not support read i/o\n", ceu->dev_name);
      exit (EXIT_FAILURE);
    }
    break;

  case IO_METHOD_MMAP:
  case IO_METHOD_USERPTR:
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
      fprintf (stderr, "%s does not support streaming i/o\n", ceu->dev_name);
      exit (EXIT_FAILURE);
    }
    break;
  }

  /* Select video input, video standard and tune here. */
  CLEAR (cropcap);
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (0 == xioctl (ceu->fd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; /* reset to default */

    if (-1 == xioctl (ceu->fd, VIDIOC_S_CROP, &crop)) {
      switch (errno) {
      case EINVAL:
        /* Cropping not supported. */
        break;
      default:
        /* Errors ignored. */
        break;
      }
    }
  } else {        
    /* Errors ignored. */
  }

  CLEAR (fmt);

  fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width       = ceu->width; 
  fmt.fmt.pix.height      = ceu->height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
  fmt.fmt.pix.field       = V4L2_FIELD_ANY;

	if (-1 == xioctl(ceu->fd, VIDIOC_S_FMT, &fmt)) {
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
		if (-1 == xioctl(ceu->fd, VIDIOC_S_FMT, &fmt)) {
			fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
			if (-1 == xioctl(ceu->fd, VIDIOC_S_FMT, &fmt)) {
       			errno_exit("VIDIOC_S_FMT");
			}
		}
	}
	ceu->pixel_format = fmt.fmt.pix.pixelformat;
	/* Note VIDIOC_S_FMT may change width and height. */
	ceu->width = fmt.fmt.pix.width;
	ceu->height = fmt.fmt.pix.height;

	/* Buggy driver paranoia. */
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min)
		fmt.fmt.pix.sizeimage = min;

	/* TODO Work around the kernel - it sets the buffer size incorrectly */
	fmt.fmt.pix.sizeimage = PAGE_ALIGN(fmt.fmt.pix.sizeimage);

  switch (ceu->io) {
  case IO_METHOD_READ:
    init_read (ceu, fmt.fmt.pix.sizeimage);
    break;
  case IO_METHOD_MMAP:
    init_mmap (ceu);
    break;
  case IO_METHOD_USERPTR:
    init_userp (ceu, fmt.fmt.pix.sizeimage);
    break;
  }
}

static void close_device(sh_ceu * ceu)
{
  if (-1 == close (ceu->fd))
    errno_exit ("close");
  ceu->fd = -1;
}

static void open_device(sh_ceu * ceu)
{
  struct stat st; 

  if (-1 == stat (ceu->dev_name, &st)) {
    fprintf (stderr, "Cannot identify '%s': %d, %s\n", ceu->dev_name, errno, strerror (errno));
      exit (EXIT_FAILURE);
  }

  if (!S_ISCHR (st.st_mode)) {
    fprintf (stderr, "%s is no device\n", ceu->dev_name);
    exit (EXIT_FAILURE);
  }

  ceu->fd = open (ceu->dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

  if (-1 == ceu->fd) {
    fprintf (stderr, "Cannot open '%s': %d, %s\n", ceu->dev_name, errno, strerror (errno));
    exit (EXIT_FAILURE);
  }
}

void sh_ceu_close (sh_ceu * ceu)
{
  uninit_device (ceu);
  close_device (ceu);
  free (ceu);
}

sh_ceu *sh_ceu_open(const char * device_name, int width, int height, io_method io, UIOMux * uiomux)
{
  sh_ceu * ceu;

  ceu = malloc(sizeof(*ceu));

  ceu->io = io;
  ceu->dev_name = device_name;
  ceu->width = width;
  ceu->height = height;
  ceu->uiomux = (void *)uiomux;

  open_device (ceu);
  init_device (ceu);

  return ceu;
}

int sh_ceu_get_width(sh_ceu * ceu)
{
	return ceu->width;
}

int sh_ceu_get_height(sh_ceu * ceu)
{
	return ceu->height;
}

unsigned int sh_ceu_get_pixel_format(sh_ceu * ceu)
{
  return ceu->pixel_format;
}


