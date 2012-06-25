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
#include <stdio.h>
#include <time.h>

#define MAX_CAMERAS 2

//This file define the general configuration for the atomisp camera

#define BPP 2 // bytes per pixel
#define MAX_PARAM_VALUE_LENGTH 32
#define MAX_BURST_BUFFERS 32
#define MAX_BURST_FRAMERATE 15

namespace android {
struct AtomBuffer;
class IBufferOwner
{
public:
    virtual void returnBuffer(AtomBuffer* buff) =0;
    virtual ~IBufferOwner(){};
};

enum AtomMode {
    MODE_NONE = -1,
    MODE_PREVIEW = 0,
    MODE_CAPTURE = 1,
    MODE_VIDEO = 2,
};

/*!\enum AtomBufferType
 *
 * Different buffer types that AtomBuffer can encapsulate
 * Type relates to the usage of the buffer
 */
enum AtomBufferType {
    ATOM_BUFFER_PREVIEW_GFX,        /*!< Buffer contains a preview frame allocated from GFx HW */
    ATOM_BUFFER_PREVIEW,            /*!< Buffer contains a preview frame allocated from AtomISP */
    ATOM_BUFFER_SNAPSHOT,           /*!< Buffer contains a full resolution snapshot image (uncompressed) */
    ATOM_BUFFER_SNAPSHOT_JPEG,      /*!< Buffer contains a full resolution snapshot image compressed */
    ATOM_BUFFER_POSTVIEW,           /*!< Buffer contains a postview image (uncompressed) */
    ATOM_BUFFER_POSTVIEW_JPEG,      /*!< Buffer contains a postview image (JPEG) */
    ATOM_BUFFER_VIDEO,              /*!< Buffer contains a video frame  */
};

/*! \struct AtomBuffer
 *
 * Container struct for buffers passed to/from Atom ISP
 *
 * The buffer type determines how the actual data is accessed
 * for all buffer types except ATOM_BUFFER_PREVIEW_GFX data is in *buff
 * for ATOM_BUFFER_PREVIEW_GFX the data is accessed via gfxData
 *
 * Please note that this struct must be kept as POC type.
 * so not possible to add methods.
 *
 */
struct AtomBuffer {
    camera_memory_t *buff;  /*!< Pointer to the memory allocated via the client provided callback */
    camera_memory_t *metadata_buff; /*!< Pointer to the memory allocated by callback, used to store metadata info for recording */
    int id;                 /*!< id for debugging data flow path */
    int ispPrivate;         /*!< Private to the AtomISP class.
                                 No other classes should touch this */
    bool shared;            /*!< Flag signaling whether the data is allocated by other components to
                                 prevent ISP to de-allocate it */
    int width;
    int height;
    int format;
    int stride;             /*!< stride of the buffer*/
    int size;
    AtomBufferType type;                /*!< context in which the buffer is used */
    IBufferOwner* owner;                /*!< owner who is responsible to enqueue back to AtomISP*/
    struct timeval  capture_timestamp;  /*!< system timestamp from when the frame was captured */
    void    *gfxData;                   /*!< pointer to the actual data mapped from the gfx buffer
                                             only used for PREVIEW_GFX type */
    buffer_handle_t *mNativeBufPtr;     /*!< native buffer handle from which the gfx data is derived by mapping */

};

enum SensorType {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_RAW,
    SENSOR_TYPE_SOC
};

struct CameraWindow {
    int x_left;
    int x_right;
    int y_top;
    int y_bottom;
    int weight;
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

static const char* v4l2Fmt2Str(int format)
{
    static char fourccBuf[5];
    memset(&fourccBuf[0], 0, sizeof(fourccBuf));
    char *fourccPtr = (char*) &format;
    snprintf(fourccBuf, sizeof(fourccBuf), "%c%c%c%c", *fourccPtr, *(fourccPtr+1), *(fourccPtr+2), *(fourccPtr+3));
    return &fourccBuf[0];
}

}
#endif // ANDROID_LIBCAMERA_COMMON_H
