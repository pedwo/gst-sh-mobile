#ifndef	AVCBENCSMP_H
#define	AVCBENCSMP_H

#include "capture.h"

typedef struct {

	char ctrl_file_name_buf[256];
	char input_file_name_buf[256];
	char buf_input_yuv_file_with_path[256 + 8];
	char buf_input_yuv_file[64 + 8];

	long xpic;
	long ypic;
    long frame_rate;

	capture *ceu;

} APPLI_INFO;

#endif				/* AVCBENCSMP */
