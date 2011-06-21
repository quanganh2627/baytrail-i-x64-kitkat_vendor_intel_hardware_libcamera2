#ifndef _ATOMISP_CONFIG__
#define _ATOMISP_CONFIG__

#include <linux/videodev2.h>
//This file define the general configuration for the atomisp camera

#define MAX_BACK_CAMERA_PREVIEW_WIDTH   640
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT  480
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH  4352
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT 3264
#define MAX_BACK_CAMERA_VIDEO_WIDTH   1920
#define MAX_BACK_CAMERA_VIDEO_HEIGHT  1080

#define MAX_FRONT_CAMERA_PREVIEW_WIDTH  640
#define MAX_FRONT_CAMERA_PREVIEW_HEIGHT 480
#define MAX_FRONT_CAMERA_SNAPSHOT_WIDTH 1920
#define MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT    1080
#define MAX_FRONT_CAMERA_VIDEO_WIDTH   1920
#define MAX_FRONT_CAMERA_VIDEO_HEIGHT  1080

#define PREVIEW_MODE            0
#define STILL_IMAGE_MODE        1
#define VIDEO_RECORDING_MODE      2

#define PREVIEW_NUM_BUFFERS 4
#define SNAPSHOT_NUM_BUFFERS 1
#define VIDEO_NUM_BUFFERS   4

#define MAX_V4L2_BUFFERS    (PREVIEW_NUM_BUFFERS + 1)

#define V4L2_FIRST_DEVICE   0
#define V4L2_SECOND_DEVICE   1
#define V4L2_THIRD_DEVICE   2
#define V4L2_DEVICE_NUM (V4L2_THIRD_DEVICE + 1)

#define DEFAULT_VIDEO_DEVICE "/dev/video0"

#define PRIMARY_CAMERA_SENSOR   0
#define SECOND_CAMERA_SENSOR   1

#define DEFAULT_CAMERA_SENSOR   0
#define DEFAULT_NUM_BUFFERS     4

#define DEFAULT_XNR             false
#define DEFAULT_TNR             false
#define DEFAULT_GDC             false
#define DEFAULT_NREE            true
#define DEFAULT_MACC            V4L2_COLORFX_NONE
#define DEFAULT_COLOR_EFFECT    V4L2_COLORFX_NONE

/*
 * 5 seconds wait for regular ISP output
 * 20 seconds wait for file input mode
 */
#define ATOMISP_POLL_TIMEOUT (5 * 1000)
#define ATOMISP_FILEINPUT_POLL_TIMEOUT (20 * 1000)

#define CAMERA_ID_FRONT 1
#define CAMERA_ID_BACK  0
#define DEFAULT_GAMMA_VALUE     2.2
#define DEFAULT_CONTRAST            256
#define DEFAULT_BRIGHTNESS        0
#define DEFAULT_INV_GAMMA           0
#define DEFAULT_SENSOR_FPS      15.0

#define LOG1(...) LOGD_IF(gLogLevel >= 1, __VA_ARGS__);
#define LOG2(...) LOGD_IF(gLogLevel >= 2, __VA_ARGS__);


static int32_t gLogLevel = 1;
static int need_dump_image = 0;
static int need_dump_recorder = 0;
static int need_dump_snapshot = 0;
static int memory_userptr = 1;
static int use_file_input = 0;

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
static int use_texture_streaming = 1;
#else
static int use_texture_streaming = 0;
#endif

#endif /*_CAMERA_CONFIG__*/
