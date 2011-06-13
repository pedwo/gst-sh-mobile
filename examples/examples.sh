#!/bin/sh
# Here is some examples of audio/video GStreamer pipelines using the Renesas
# plugins.

# If you want to log the messages, replace gst-launch with
# gst-launch 2>debug.log --gst-debug-no-color --gst-debug=gst-sh-*:5

# When encoding, use the -e with gst-launch. This forces End Of Stream down the
# pipeline when Ctrl+C is pressed to terminate the pipeline. This is often
# needed to add the headers to particular file formats (e.g. mp4)


# Test ALSA audio output (single tone)
gst-launch audiotestsrc is-live=1 ! alsasink

# Test ALSA audio input (WAV file)
gst-launch -e \
 alsasrc ! audio/x-raw-int, rate=44100, channels=2 \
 ! wavenc ! filesink location=test.wav

# Test ALSA audio output (WAV file)
gst-launch filesrc location=./test.wav ! wavparse ! alsasink

# Record raw audio in avi container
gst-launch -e \
 alsasrc ! audio/x-raw-int, width=24,rate=44100,channels=2 \
 ! queue ! audioconvert ! avimux ! filesink location=file.avi

# Playback raw audio in avi container
gst-launch \
 filesrc location=file.avi ! avidemux name=demux demux.audio_00 \
 ! queue ! decodebin ! audioconvert ! audioresample ! autoaudiosink

# Encode microphone input as mp4 AAC file.
gst-launch -e \
 alsasrc ! audio/x-raw-int, rate=16000, channels=2 \
 ! queue ! audioconvert ! faac outputformat=1 profile=LC bitrate=192000 \
 ! queue ! mux. \
   mp4mux name=mux ! filesink location=./test.m4a

# mp4 AAC file playback
gst-launch \
 filesrc location=./test.m4a \
 ! queue ! faad ! audioconvert ! audioresample ! autoaudiosink

gst-launch \
 filesrc location=/sample_media/audio/RANewMovie1024_track2.aac \
 ! queue ! faad ! audioconvert ! audioresample ! autoaudiosink

# mp3 playback
gst-launch \
 filesrc location=./UTB.mp3 \
 ! mad ! audioconvert ! audioresample ! autoaudiosink

# Movie (audio & video) playback
gst-launch \
 filesrc location=/sample_media/movie/advert-h264-vga-25fps-2mbps_aac.avi \
 ! avidemux name=demux \
  demux.video_00 ! queue ! gst-sh-mobile-dec ! gst-sh-mobile-sink \
  demux.audio_00 ! queue ! decodebin ! audioconvert ! audioresample ! autoaudiosink

# Encode VGA camera to file with LCD preview - note that the ceu capture size specified
# in the cntl_file is used for capture, then scaled to the size specified here.
gst-launch \
   gst-sh-mobile-camera-enc cntl_file=ctl/h264-video0-vga-stream.ctl preview=1 \
 ! video/x-h264, width=640, height=480, framerate=10/1 \
 ! filesink location=encoded_video.h264

# Testsrc => resize => file
gst-launch \
 videotestsrc \
 ! "video/x-raw-rgb, bpp=16, width=160, height=120" \
 ! gst-sh-mobile-resize \
 ! "video/x-raw-yuv, width=320, height=240" \
 ! filesink location=tmp.yuv

# camera => file
gst-launch -e \
 v4l2src device=/dev/video0 ! "video/x-raw-yuv, format=(fourcc)NV12, width=320, height=240, framerate=15/1" \
 ! filesink location=tmp.yuv

# camera => encoder => file
gst-launch -e \
 v4l2src device=/dev/video0 ! "video/x-raw-yuv, format=(fourcc)NV12, width=320, height=240, framerate=15/1" \
 ! queue ! gst-sh-mobile-enc cntl_file=ctl/h264-video0-qvga-stream.ctl \
 ! filesink location=tmp.264

# camera => videorate => encoder => file
gst-launch -e \
 v4l2src device=/dev/video0 ! "video/x-raw-yuv, format=(fourcc)NV12, width=1280, height=720, framerate=30/1" \
 ! videorate ! "video/x-raw-yuv, framerate=15/1" \
 ! queue ! gst-sh-mobile-enc cntl_file=ctl/h264-video0-720p-stream.ctl ! "video/x-h264" \
 ! filesink location=tmp.264

# camera => resize => encoder => file
gst-launch -e \
 v4l2src device=/dev/video0 ! "video/x-raw-yuv, format=(fourcc)NV12, width=640, height=480, framerate=30/1" \
 ! gst-sh-mobile-resize \
 ! "video/x-raw-yuv, width=320, height=240, framerate=15/1" \
 ! gst-sh-mobile-enc cntl_file=ctl/h264-video0-qvga-stream.ctl \
 ! filesink location=tmp.264

# file => encoder => file
gst-launch \
 filesrc location=qvga.yuv ! "video/x-raw-yuv, format=(fourcc)NV12, width=320, height=240, framerate=15/1" \
 ! gst-sh-mobile-enc stream-type=h264 bitrate=250000 \
 ! filesink location=tmp.264

# file => decoder => file
gst-launch \
 filesrc location=qvga.264 ! "video/x-h264, width=320, height=240, framerate=15/1" \
 ! gst-sh-mobile-dec \
 ! filesink location=tmp.yuv

# file => decoder => encoder => file
gst-launch \
 filesrc location=qvga.264 ! "video/x-h264, width=320, height=240, framerate=15/1" \
 ! gst-sh-mobile-dec \
 ! gst-sh-mobile-enc stream-type=h264 bitrate=2000000 ratecontrol-skip-enable=0 \
 ! filesink location=tmp.264

# Testsrc => encoder => file
gst-launch -e \
 videotestsrc \
 ! "video/x-raw-yuv, format=(fourcc)NV12, width=320, height=240, framerate=15/1" \
 ! gst-sh-mobile-enc stream-type=h264 bitrate=250000 ratecontrol-skip-enable=0 \
 ! filesink location=tmp.264

# Testsrc => resize => encoder => file
gst-launch -e \
 videotestsrc \
 ! "video/x-raw-rgb, bpp=16, width=160, height=120" \
 ! gst-sh-mobile-resize \
 ! "video/x-raw-yuv, width=320, height=240, framerate=15/1" \
 ! gst-sh-mobile-enc stream-type=h264 bitrate=250000 ratecontrol-skip-enable=0 \
 ! filesink location=tmp.264

# file => decoder => encoder => file
gst-launch \
 filesrc location=qvga.264 ! "video/x-h264, width=320, height=240, framerate=30/1" \
 ! gst-sh-mobile-dec \
 ! "video/x-raw-yuv, width=320, height=240, framerate=30/1" \
 ! gst-sh-mobile-enc stream-type=h264 bitrate=1000000 ratecontrol-skip-enable=0 \
 ! filesink location=tmp.264

# file => decoder => resize => encoder => file
gst-launch \
 filesrc location=/sample_media/movie/advert-h264-vga-25fps-2mbps.264 ! "video/x-h264, width=640, height=480, framerate=30/1" \
 ! gst-sh-mobile-dec \
 ! gst-sh-mobile-resize \
 ! "video/x-raw-yuv, width=320, height=240, framerate=30/1" \
 ! queue ! gst-sh-mobile-enc stream-type=h264 bitrate=1000000 ratecontrol-skip-enable=0 \
 ! filesink location=tmp.264

# Testsrc => encoder => file
gst-launch \
 videotestsrc \
 ! "video/x-raw-yuv, width=320, height=240" \
 ! gst-sh-mobile-enc stream-type=h264 bitrate=250000 \
 ! filesink location=tmp.264

# camera => videorate => encoder => MP4 mux => file
gst-launch -v -e \
 v4l2src device=/dev/video0 ! "video/x-raw-yuv, format=(fourcc)NV12, width=1280, height=720, framerate=30/1" \
 ! videorate ! "video/x-raw-yuv, framerate=15/1" \
 ! queue ! gst-sh-mobile-enc cntl_file=ctl/h264-video0-720p-stream.ctl ! "video/x-h264" \
 ! queue ! mp4mux \
 ! filesink location=tmp.mp4

# file => decode => display
gst-launch \
 filesrc location=qvga.264 ! "video/x-h264, width=320, height=240, framerate=15/1" \
 ! gst-sh-mobile-dec \
 ! gst-sh-mobile-sink

# Testsrc => mixer => file
# Testsrc =/
gst-launch \
 videotestsrc pattern=1 ! "video/x-raw-yuv, format=(fourcc)NV12, framerate=(fraction)10/1, width=320, height=240" ! queue ! mix. \
 videotestsrc           ! "video/x-raw-yuv, format=(fourcc)NV12, framerate=(fraction)5/1,  width=100, height=100" ! queue ! mix. \
 gst-sh-mobile-mixer name=mix sink_1::alpha=0.5 \
  ! "video/x-raw-yuv, format=(fourcc)NV12" \
  ! filesink location=tmp.yuv

# Testsrc => mixer => file
# Testsrc =/
gst-launch \
 videotestsrc pattern=1 ! "video/x-raw-rgb, bpp=32, framerate=(fraction)10/1, width=320, height=240" ! queue ! mix. \
 videotestsrc           ! "video/x-raw-yuv, format=(fourcc)NV12, framerate=(fraction)5/1,  width=100, height=100" ! queue ! mix. \
 gst-sh-mobile-mixer name=mix sink_1::alpha=0.5 sink_1::xpos=100 sink_1::ypos=50\
  ! "video/x-raw-yuv, format=(fourcc)NV12" \
  ! filesink location=tmp.yuv

# file => decoder => mixer => display
#          camera =/
gst-launch \
 filesrc location=/sample_media/movie/advert-h264-vga-25fps-2mbps.264 ! "video/x-h264, width=640, height=480, framerate=30/1" \
 ! gst-sh-mobile-dec ! mix. \
 v4l2src device=/dev/video0 ! "video/x-raw-yuv, format=(fourcc)NV12, width=320, height=240, framerate=15/1" ! mix. \
 gst-sh-mobile-mixer name=mix sink_1::alpha=0.8 sink_1::xpos=100 sink_1::ypos=0 \
 ! gst-sh-mobile-sink

# file => decoder => mixer => display
#         camera1 =/
#         camera2 =/
gst-launch \
 filesrc location=/sample_media/movie/advert-h264-vga-25fps-2mbps.264 ! "video/x-h264, width=640, height=480, framerate=30/1" \
 ! gst-sh-mobile-dec ! mix. \
 v4l2src device=/dev/video0 ! "video/x-raw-yuv, format=(fourcc)NV12, width=320, height=240, framerate=15/1" ! mix. \
 v4l2src device=/dev/video2 ! "video/x-raw-yuv, format=(fourcc)NV12, width=320, height=240, framerate=15/1" ! mix. \
 gst-sh-mobile-mixer name=mix sink_1::alpha=0.8 sink_1::xpos=160 sink_1::ypos=120 sink_2::alpha=0.8 \
 ! gst-sh-mobile-sink


