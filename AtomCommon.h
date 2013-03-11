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

// Unless LOG_TAG not defined by including files, use this log tag:
#ifndef LOG_TAG
#define LOG_TAG "Camera_AtomCommon"
#endif

#include <camera.h>
#include <linux/atomisp.h>
#include <linux/videodev2.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <ia_3a_types.h>

#include "LogHelper.h"

#define MAX_CAMERAS 2

//This file define the general configuration for the atomisp camera

#define BPP 2 // bytes per pixel
#define MAX_PARAM_VALUE_LENGTH 32
#define MAX_BURST_BUFFERS 32
#define MAX_BURST_FRAMERATE 15

// macro STRINGIFY to change a number in a string.
#define STRINGIFY(s) STRINGIFY_(s)
#define STRINGIFY_(s) #s
// macro CLIP is used to clip the Number value to between the Min and Max
#define CLIP(Number, Max, Min)    ((Number) > (Max) ? (Max) : ((Number) < (Min) ? (Min) : (Number)))
// macro ALIGN16 root value to value that is divisible by 16
#define ALIGN16(x) (((x) + 15) & ~15)
#define ALIGN64(x) (((x) + 63) & ~63)

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
    MODE_CONTINUOUS_CAPTURE = 3
};

/*!\enum FrameBufferStatus
 *
 * maps with kernel atomisp.h atomisp_frame_status and extends
 * for HAL's internal use
 */
enum FrameBufferStatus {
    FRAME_STATUS_NA = -1,
    FRAME_STATUS_OK = ATOMISP_FRAME_STATUS_OK,
    FRAME_STATUS_CORRUPTED = ATOMISP_FRAME_STATUS_CORRUPTED,
    FRAME_STATUS_FLASH_EXPOSED = ATOMISP_FRAME_STATUS_FLASH_EXPOSED,
    FRAME_STATUS_FLASH_PARTIAL = ATOMISP_FRAME_STATUS_FLASH_PARTIAL,
    FRAME_STATUS_FLASH_FAILED = ATOMISP_FRAME_STATUS_FLASH_FAILED,
    FRAME_STATUS_SKIPPED,
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
    ATOM_BUFFER_PANORAMA,           /*!< Buffer contains a panorama image */
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
    int frameCounter;       /*!< Monotonic frame counter set by AtomISP class. Used in performance traces. */
    int frameSequenceNbr;   /*!< V4L2 Frame sequence number set by kernel. Used in bracketing to detect frame drops in the driver.  */
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
    FrameBufferStatus status;            /*!< status information of carried frame buffer */
    IBufferOwner* owner;                /*!< owner who is responsible to enqueue back to AtomISP*/
    struct timeval  capture_timestamp;  /*!< system timestamp from when the frame was captured */
    void    *gfxData;                   /*!< pointer to the actual data mapped from the gfx buffer
                                             only used for PREVIEW_GFX type */
    buffer_handle_t *mNativeBufPtr;     /*!< native buffer handle from which the gfx data is derived by mapping */

};

struct AAAWindowInfo {
    unsigned width;
    unsigned height;
};

extern timeval AtomBufferFactory_AtomBufDefTS; // default timestamp
class AtomBufferFactory {
public:
    static AtomBuffer createAtomBuffer(AtomBufferType type = ATOM_BUFFER_PREVIEW_GFX,
                           int format = V4L2_PIX_FMT_NV12,
                           int width = 0,
                           int height = 0,
                           int stride = 0,
                           int size = 0,
                           IBufferOwner *owner = NULL,
                           camera_memory_t *buff = NULL,
                           camera_memory_t *metadata_buff = NULL,
                           int id = 0,
                           int frameCounter = 0,
                           int ispPrivate = 0,
                           bool shared = false,
                           struct timeval capture_timestamp = AtomBufferFactory_AtomBufDefTS,
                           void *gfxData = NULL,
                           buffer_handle_t *mNativeBufPtr = NULL);
};

enum SensorType {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_RAW,
    SENSOR_TYPE_SOC
};

enum AAAFlags {
    AAA_FLAG_NONE = 0x0,
    AAA_FLAG_AE = 0x1,
    AAA_FLAG_AF = 0x2,
    AAA_FLAG_AWB = 0x4,
    AAA_FLAG_ALL = AAA_FLAG_AE | AAA_FLAG_AF | AAA_FLAG_AWB
};

/*! Bit '1' means successful result of selected event */
const int ci_adv_init_state         = 0;            /*!< Init. state */
const int ci_adv_load_camera_1      = (1 << 1);     /*!< Loaded camera module #1 */
const int ci_adv_load_camera_2      = (1 << 2);     /*!< Loaded camera module #2 */
const int ci_adv_load_camera_3      = (1 << 3);     /*!< Loaded camera module #3 */
const int ci_adv_load_camera_4      = (1 << 4);     /*!< Loaded camera module #4 */
const int ci_adv_cam_sensor_data    = (1 << 5);     /*!< Sensor calibration data from camera presents */
const int ci_adv_file_sensor_data   = (1 << 6);     /*!< Sensor calibration data from file presents */
const int ci_adv_cam_motor_data     = (1 << 7);     /*!< Motor calibration data presents */

struct SensorParams
{
    ia_3a_prm_files prmFiles;
    const char *tuning3aFile;
    int bootEvent;
    ia_3a_private_data cpfData;
    bool hasMotorData;
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

/**
 * Calculates the frame stride following the limitations imposed by display subsystem

 * \param width [in]
 * \return stride following the Display subsystem stride requirement
 **/
static int SGXandDisplayStride(int width)
{
    if (width <= 512)
        return  512;
    else
        return ALIGN64(width);
}

static int bytesPerLineToWidth(int format, int bytesperline)
{
    int width = 0;
    switch (format) {
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_YUV411P:
    case V4L2_PIX_FMT_YUV422P:
        width = (bytesperline * 2 / 3);
        break;
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_Y41P:
    case V4L2_PIX_FMT_UYVY:
        width = (bytesperline / 2);
        break;
    case V4L2_PIX_FMT_RGB565:
        width = (bytesperline / 2);
        break;
    default:
        width = (bytesperline / 2);
    }

    return width;
}

static int paddingWidthNV12VED( int width, int height)
{
    int padding = 0;
    if (width <= 512) {
        padding = 512;
    } else if (width <= 1024) {
        padding = 1024;
    } else if (width <= 1280) {
        padding = 1280;
    } else if (width <= 2048) {
        padding = 2048;
    } else if (width <= 4096) {
        padding = 4096;
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


// Helpers for converting AE metering areas
// from Android coordinates to user coordinates for usage with AAA.
/**
 * Converts window from Android coordinate system [-1000, 1000] to user defined width
 * and height coordinates [0, width], [0, height].
 *
 * @param srcWindow the CameraWindow with Android coordinates
 * @param toWindow the destination CameraWindow with new coordinates
 * @param convWindow User defined conversion info: width and height will be converted
 * in ratio to the Android coordinate system
 */
inline static void convertFromAndroidCoordinates(const CameraWindow &srcWindow,
        CameraWindow &toWindow, const AAAWindowInfo& convWindow)
{
    // Ratios for width and height in reference to Android coordinates [-1000,1000]
    float ratioW = float(convWindow.width) / 2000.0;
    float ratioH = float(convWindow.height) / 2000.0;

    // Transform from the Android coordinates to the target coordinates.
    // The +1000 comes from translation to [0,2000] coordinates from [-1000,1000]
    float left = (srcWindow.x_left + 1000.0) * ratioW;
    float top = (srcWindow.y_top + 1000.0) * ratioH;
    float right = (srcWindow.x_right + 1000.0) * ratioW;
    float bottom = (srcWindow.y_bottom + 1000.0) * ratioH;

    // Calculate the width and height for the target window.
    // This is needed in case the transformation goes off the grid.
    float rectW = float(right - left) * ratioW;
    float rectH = float(bottom - top) * ratioH;

    // Right side of the window is off the grid, so use the
    // grid max. value for right side:
    if (right > int(convWindow.width - 1)) {
        LOG2("@%s: Right side of target window off the grid after conversion", __FUNCTION__);
        right = convWindow.width - 1;
        left = right - rectW;
    }

    // Window bottom-side off the grid; use grid max. value:
    if (bottom > int(convWindow.height - 1)) {
        LOG2("@%s: Bottom side of target window off the grid after conversion", __FUNCTION__);
        bottom = convWindow.height - 1;
        top = bottom - rectH;
    }

    toWindow.x_left = left;
    toWindow.x_right = right;
    toWindow.y_top = top;
    toWindow.y_bottom = bottom;
}

/**
 * Converts window from Android coordinate system [-1000, 1000] to user defined width
 * and height coordinates [0, width], [0, height], and google weight [1, 1000] to
 * user defined weight [minWeight, maxWeight]
 *
 * @param srcWindow the CameraWindow with Android coordinates
 * @param toWindow the destination CameraWindow with new coordinates
 * @param convWindow User defined conversion info: width and height will be converted
 * in ratio to the Android coordinate system
 * @param minWeight the minimum for weight
 * @param maxWeight the maximum for weight
 */
inline static void convertFromAndroidCoordinates(const CameraWindow &srcWindow,
        CameraWindow &toWindow, const AAAWindowInfo& convWindow, int minWeight, int maxWeight)
{
    convertFromAndroidCoordinates(srcWindow, toWindow, convWindow);
    int weightWidth = maxWeight - minWeight;
    toWindow.weight = minWeight + roundf(weightWidth * srcWindow.weight / 1000.0f);
}

}
#endif // ANDROID_LIBCAMERA_COMMON_H
