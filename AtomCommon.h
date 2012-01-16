/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_LIBCAMERA_COMMON_H
#define ANDROID_LIBCAMERA_COMMON_H

#include <camera.h>
#include <linux/atomisp.h>
#include <linux/videodev2.h>

#define MAX_CAMERAS 2

//This file define the general configuration for the atomisp camera
#define RESOLUTION_14MP_WIDTH   4352
#define RESOLUTION_14MP_HEIGHT  3264
#define RESOLUTION_8MP_WIDTH    3264
#define RESOLUTION_8MP_HEIGHT   2448
#define RESOLUTION_5MP_WIDTH    2560
#define RESOLUTION_5MP_HEIGHT   1920
#define RESOLUTION_1080P_WIDTH  1920
#define RESOLUTION_1080P_HEIGHT 1080
#define RESOLUTION_720P_WIDTH   1280
#define RESOLUTION_720P_HEIGHT  720
#define RESOLUTION_480P_WIDTH   768
#define RESOLUTION_480P_HEIGHT  480
#define RESOLUTION_VGA_WIDTH    640
#define RESOLUTION_VGA_HEIGHT   480

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

#define ATOM_PREVIEW_BUFFERS    4
#define ATOM_RECORDING_BUFFERS  4

#define MAX_SENSOR_NAME_LENGTH  32
#define SNAPSHOT_MAX_NUM_BUFFERS 32 // kernel driver's limitation

#define MAX_V4L2_BUFFERS    SNAPSHOT_MAX_NUM_BUFFERS

#define V4L2_FIRST_DEVICE   0
#define V4L2_SECOND_DEVICE  1
#define V4L2_THIRD_DEVICE   2
#define V4L2_DEVICE_NUM (V4L2_THIRD_DEVICE + 1)

#define DEFAULT_VIDEO_DEVICE "/dev/video0"
#define DEFAULT_CAMERA_SENSOR   0
#define DEFAULT_SENSOR_FPS      15.0

#define BPP 2 // bytes per pixel

/*
 * 3 seconds wait for regular ISP output
 * 20 seconds wait for file input mode
 */
#define ATOMISP_POLL_TIMEOUT (3 * 1000)
#define ATOMISP_FILEINPUT_POLL_TIMEOUT (20 * 1000)

#define MAX_ZOOM_LEVEL  63
#define MIN_ZOOM_LEVEL  0

struct AtomBuffer {
    camera_memory_t *buff;
    int id;    // id for debugging data flow path
};

 struct FrameInfo {
     int format;     // V4L2 format
     int width;      // Frame width
     int height;     // Frame height
     int padding;    // Frame padding width
     int maxWidth;   // Frame maximum width
     int maxHeight;  // Frame maximum height
     int size;       // Frame size in bytes
 };

 struct FrameSize {
     int width;
     int height;

     FrameSize(): width(0), height(0) {}
     FrameSize(int w, int h): width(w), height(h) {}
 };

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

enum raw_data_format {
    RAW_NONE = 0,
    RAW_YUV,
    RAW_RGB,
    RAW_BAYER,
};

enum resolution_index {
    RESOLUTION_VGA = 0,
    RESOLUTION_720P,
    RESOLUTION_1080P,
    RESOLUTION_5MP,
    RESOLUTION_8MP,
    RESOLUTION_14MP,
};

static int frameSize(int format, int width, int height)
{
    int size = 0;
    switch (format) {
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_YVU420:
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_YUV411P:
        case V4L2_PIX_FMT_YUV422P:
            size = (width * height * 3 / 2);
            break;
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_Y41P:
        case V4L2_PIX_FMT_UYVY:
            size = (width * height *  2);
            break;
        case V4L2_PIX_FMT_RGB565:
            size = (width * height * BPP);
            break;
        default:
            size = (width * height * 2);
    }

    return size;
}

static int paddingWidth(int format, int width, int height)
{
    int padding = 0;
    switch (format) {
    //64bit align for 1.5byte per pixel
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_YUV411P:
    case V4L2_PIX_FMT_YUV422P:
        padding = (width + 63) / 64 * 64;
        break;
    //32bit align for 2byte per pixel
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_Y41P:
    case V4L2_PIX_FMT_UYVY:
        padding = width;
        break;
    case V4L2_PIX_FMT_RGB565:
        padding = (width + 31) / 32 * 32;
        break;
    default:
        padding = (width + 63) / 64 * 64;
    }
    return padding;
}

#endif // ANDROID_LIBCAMERA_COMMON_H
