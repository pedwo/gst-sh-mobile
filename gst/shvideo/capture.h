/*
 * libshcodecs: A library for controlling SH-Mobile hardware codecs
 * Copyright (C) 2009 Renesas Technology Corp.
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
 */

#ifndef __CAPTURE_H__
#define __CAPTURE_H__

typedef enum {
	IO_METHOD_READ,
	IO_METHOD_MMAP,
	IO_METHOD_USERPTR,
} io_method;

typedef struct _capture {
	const char *dev_name;
	int fd;
	io_method io;
	struct buffer *buffers;
	unsigned int n_buffers;
	int width;
	int height;
	unsigned int pixel_format;
	void *uiomux;
} capture;

typedef void (*sh_process_callback) (capture * ceu, const void *frame_data, size_t length,
				     void *user_data);

capture *capture_open(const char *device_name, int width, int height, io_method io, void *uiomux);

void capture_close(capture * ceu);

void capture_start_capturing(capture * ceu);

void capture_stop_capturing(capture * ceu);

void capture_capture_frame(capture * ceu, sh_process_callback cb, void *user_data);

/* Get the properties of the captured frames 
 * The v4l device may not support the request size
 */
int capture_get_width(capture * ceu);
int capture_get_height(capture * ceu);
unsigned int capture_get_pixel_format(capture * ceu);

#endif				/* __CAPTURE_H__ */

