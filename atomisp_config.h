#ifndef _ATOMISP_CONFIG__
#define _ATOMISP_CONFIG__

#include <linux/videodev2.h>
#include <ci_adv_pub.h>
//This file define the general configuration for the atomisp camera
#define RESOLUTION_14MP_WIDTH	4352
#define RESOLUTION_14MP_HEIGHT	3264
#define RESOLUTION_8MP_WIDTH	3264
#define RESOLUTION_8MP_HEIGHT	2448
#define RESOLUTION_5MP_WIDTH	2560
#define RESOLUTION_5MP_HEIGHT	1920
#define RESOLUTION_1080P_WIDTH	1920
#define RESOLUTION_1080P_HEIGHT	1080
#define RESOLUTION_720P_WIDTH	1280
#define RESOLUTION_720P_HEIGHT	720
#define RESOLUTION_480P_WIDTH	768
#define RESOLUTION_480P_HEIGHT	480
#define RESOLUTION_VGA_WIDTH	640
#define RESOLUTION_VGA_HEIGHT	480


#define MAX_BACK_CAMERA_PREVIEW_WIDTH   1280
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT  720
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH  4352
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT 3264
#define MAX_BACK_CAMERA_VIDEO_WIDTH   1920
#define MAX_BACK_CAMERA_VIDEO_HEIGHT  1080

#define MAX_FRONT_CAMERA_PREVIEW_WIDTH  1280
#define MAX_FRONT_CAMERA_PREVIEW_HEIGHT 720
#define MAX_FRONT_CAMERA_SNAPSHOT_WIDTH 1920
#define MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT    1080
#define MAX_FRONT_CAMERA_VIDEO_WIDTH   1920
#define MAX_FRONT_CAMERA_VIDEO_HEIGHT  1080

#define PREVIEW_MODE            0
#define STILL_IMAGE_MODE        1
#define VIDEO_RECORDING_MODE      2

#define PREVIEW_NUM_BUFFERS 4
#define SNAPSHOT_MAX_NUM_BUFFERS 32 // kernel driver's limitation
#define VIDEO_NUM_BUFFERS   4

#define MAX_V4L2_BUFFERS    SNAPSHOT_MAX_NUM_BUFFERS

#define MAX_BURST_CAPTURE_NUM   10
#define MAX_ZOOM_LEVEL  63
#define MIN_ZOOM_LEVEL  0

#define V4L2_FIRST_DEVICE   0
#define V4L2_SECOND_DEVICE   1
#define V4L2_THIRD_DEVICE   2
#define V4L2_DEVICE_NUM (V4L2_THIRD_DEVICE + 1)

#define DEFAULT_VIDEO_DEVICE "/dev/video0"

#define DEFAULT_CAMERA_SENSOR   0
#define DEFAULT_NUM_BUFFERS     4

#define DEFAULT_XNR             false
#define DEFAULT_TNR             false
#define DEFAULT_GDC             false
#define DEFAULT_DVS             false
#define DEFAULT_SHADING_CORRECTION             false
#define DEFAULT_NREE            true
#define DEFAULT_MACC            V4L2_COLORFX_NONE
#define DEFAULT_COLOR_EFFECT    V4L2_COLORFX_NONE

/*
 * 3 seconds wait for regular ISP output
 * 20 seconds wait for file input mode
 */
#define ATOMISP_POLL_TIMEOUT (3 * 1000)
#define ATOMISP_FILEINPUT_POLL_TIMEOUT (20 * 1000)

#define DEFAULT_GAMMA_VALUE     2.2
#define DEFAULT_CONTRAST            256
#define DEFAULT_BRIGHTNESS        0
#define DEFAULT_INV_GAMMA           0
#define DEFAULT_SENSOR_FPS      15.0
#define FOCUS_CANCELLED   2

#define TORCH_INTENSITY        20 /* 20% */
#define INDICATOR_INTENSITY    20 /* 20% */

#define MAX_SENSOR_NAME_LENGTH  32
#define CDK_PRIMARY_SENSOR_NAME "dis71430m"
#define CDK_SECOND_SENSOR_NAME  "ov2720"
#define PR2_PRIMARY_SENSOR_NAME "mt9e013"
#define PR2_SECOND_SENSOR_NAME  "mt9m114"

enum {
    SENSOR_TYPE_RAW = 1,
    SENSOR_TYPE_SOC
};

enum {
    UNKNOWN_PLATFORM = 0,
    MFLD_CDK_PLATFORM,
    MFLD_PR2_PLATFORM
};

typedef struct {
    int port;
    char name[MAX_SENSOR_NAME_LENGTH];
} cameraInfo;

#define LOG1(...) LOGD_IF(gLogLevel >= 1, __VA_ARGS__);
#define LOG2(...) LOGD_IF(gLogLevel >= 2, __VA_ARGS__);

static int32_t gLogLevel = 0;
static int need_dump_image = 0;
static int need_dump_recorder = 0;
static int need_dump_snapshot = 0;
static int memory_userptr = 1;
static int use_file_input = 0;

enum raw_data_format {
    RAW_NONE = 0,
    RAW_YUV,
    RAW_RGB,
    RAW_BAYER,
};

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
static int use_texture_streaming = 1;
#else
static int use_texture_streaming = 0;
#endif

#define RESOLUTION_14MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1080,2048x1536,2560x1920,3264x2448,3648x2736,4096x3072,4352x3264"

#define RESOLUTION_8MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1080,2048x1536,2560x1920,3264x2448"

#define RESOLUTION_5MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1080,2048x1536,2560x1920"

#define RESOLUTION_1080P_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1080"

#define RESOLUTION_720P_TABLE   \
        "320x240,640x480,1280x720"

#define RESOLUTION_VGA_TABLE   \
        "320x240,640x480"

enum resolution_index {
    RESOLUTION_VGA = 0,
    RESOLUTION_720P,
    RESOLUTION_1080P,
    RESOLUTION_5MP,
    RESOLUTION_8MP,
    RESOLUTION_14MP,
};

#endif /*_CAMERA_CONFIG__*/
