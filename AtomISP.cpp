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
#define LOG_TAG "Atom_ISP"

#include "LogHelper.h"
#include "AtomISP.h"
#include "Callbacks.h"
#include "ColorConverter.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#define CLEAR(x) memset (&(x), 0, sizeof (x))
#define main_fd video_fds[V4L2_FIRST_DEVICE]

#define DEFAULT_SENSOR_FPS      15.0

#define RESOLUTION_14MP_WIDTH   4352
#define RESOLUTION_14MP_HEIGHT  3264
#define RESOLUTION_8MP_WIDTH    3264
#define RESOLUTION_8MP_HEIGHT   2448
#define RESOLUTION_5MP_WIDTH    2560
#define RESOLUTION_5MP_HEIGHT   1920
#define RESOLUTION_1080P_WIDTH  1920
#define RESOLUTION_1080P_HEIGHT 1088
#define RESOLUTION_720P_WIDTH   1280
#define RESOLUTION_720P_HEIGHT  720
#define RESOLUTION_480P_WIDTH   768
#define RESOLUTION_480P_HEIGHT  480
#define RESOLUTION_VGA_WIDTH    640
#define RESOLUTION_VGA_HEIGHT   480
#define RESOLUTION_POSTVIEW_WIDTH    320
#define RESOLUTION_POSTVIEW_HEIGHT   240

#define RESOLUTION_14MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1088,2048x1536,2560x1920,3264x2448,3648x2736,4096x3072,4352x3264"

#define RESOLUTION_8MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1088,2048x1536,2560x1920,3264x2448"

#define RESOLUTION_5MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1088,2048x1536,2560x1920"

#define RESOLUTION_1080P_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1088"

#define RESOLUTION_720P_TABLE   \
        "320x240,640x480,1280x720,1280x960"

#define RESOLUTION_VGA_TABLE   \
        "320x240,640x480"

#define MAX_BACK_CAMERA_PREVIEW_WIDTH   1280
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT  720
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH  4352
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT 3264
#define MAX_BACK_CAMERA_VIDEO_WIDTH   1920
#define MAX_BACK_CAMERA_VIDEO_HEIGHT  1088

#define MAX_FRONT_CAMERA_PREVIEW_WIDTH  1280
#define MAX_FRONT_CAMERA_PREVIEW_HEIGHT 720
#define MAX_FRONT_CAMERA_SNAPSHOT_WIDTH 1920
#define MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT    1088
#define MAX_FRONT_CAMERA_VIDEO_WIDTH   1920
#define MAX_FRONT_CAMERA_VIDEO_HEIGHT  1088

#define MAX_ZOOM_LEVEL          150     // How many levels we have from 1x -> max zoom
#define MIN_ZOOM_LEVEL          0
#define MIN_SUPPORT_ZOOM        100     // Support 1x at least
#define MAX_SUPPORT_ZOOM        1600    // Support upto 16x and should not bigger than 99x
#define ZOOM_RATIO              100     // Conversion between zoom to really zoom effect

/**
 * Platform specific defines
 * */
#ifdef MFLD_PR2

#define PREFERRED_PREVIEW_SIZE_FOR_VIDEO    "1024x580"
#define FRONT_CAMERA_ROTATION    90
#define BACK_CAMERA_ROTATION     90

#elif MFLD_DV10

#define PREFERRED_PREVIEW_SIZE_FOR_VIDEO    "720x576"
#define FRONT_CAMERA_ROTATION    180
#define BACK_CAMERA_ROTATION     180

#else

#define PREFERRED_PREVIEW_SIZE_FOR_VIDEO    "640x480"
#define FRONT_CAMERA_ROTATION    0
#define BACK_CAMERA_ROTATION     0
#endif

namespace android {

////////////////////////////////////////////////////////////////////
//                          STATIC DATA
////////////////////////////////////////////////////////////////////

static const char *dev_name_array[3] = {"/dev/video0",
                                  "/dev/video1",
                                  "/dev/video2"};

AtomISP::cameraInfo AtomISP::camInfo[MAX_CAMERAS];
int AtomISP::numCameras = 0;

const camera_info AtomISP::mCameraInfo[MAX_CAMERAS] = {
    {
        CAMERA_FACING_BACK, BACK_CAMERA_ROTATION
    },
    {
        CAMERA_FACING_FRONT,FRONT_CAMERA_ROTATION
    }
};

static const char *resolution_tables[] = {
    RESOLUTION_VGA_TABLE,
    RESOLUTION_720P_TABLE,
    RESOLUTION_1080P_TABLE,
    RESOLUTION_5MP_TABLE,
    RESOLUTION_8MP_TABLE,
    RESOLUTION_14MP_TABLE
};

// Generated the string like "100,110,120, ...,1580,1590,1600"
// The string is determined by MAX_ZOOM_LEVEL and MAX_SUPPORT_ZOOM
static void computeZoomRatios(char *zoom_ratio, int max_count){

    //set up zoom ratio according to MAX_ZOOM_LEVEL
    int zoom_step = (MAX_SUPPORT_ZOOM - MIN_SUPPORT_ZOOM)/MAX_ZOOM_LEVEL;
    int ratio = MIN_SUPPORT_ZOOM;
    int pos = 0;
    int i = 0;

    //Get zoom from MIN_SUPPORT_ZOOM to MAX_SUPPORT_ZOOM
    while((ratio <= MAX_SUPPORT_ZOOM) && (pos < max_count)){
        sprintf(zoom_ratio + pos,"%d,",ratio);
        if (ratio < 1000)
            pos += 4;
        else
            pos += 5;
        ratio += zoom_step;
    }

    //Overwrite the last ',' with '\0'
    if (pos > 0)
        *(zoom_ratio + pos -1 ) = '\0';
}

////////////////////////////////////////////////////////////////////
//                          PUBLIC METHODS
////////////////////////////////////////////////////////////////////

AtomISP::AtomISP(int camera_id) :
    mMode(MODE_NONE)
    ,mCallbacks(Callbacks::getInstance())
    ,mNumBuffers(NUM_DEFAULT_BUFFERS)
    ,mPreviewBuffers(NULL)
    ,mRecordingBuffers(NULL)
    ,mClientRecordingBuffers(NULL)
    ,mUsingClientRecordingBuffers(false)
    ,mClientSnapshotBuffers(NULL)
    ,mUsingClientSnapshotBuffers(false)
    ,mNumPreviewBuffersQueued(0)
    ,mNumRecordingBuffersQueued(0)
    ,mNumCapturegBuffersQueued(0)
    ,mPreviewDevice(V4L2_FIRST_DEVICE)
    ,mRecordingDevice(V4L2_FIRST_DEVICE)
    ,mSessionId(0)
    ,mCameraId(0)
    ,mAAA(AtomAAA::getInstance())
    ,mLowLight(false)
    ,mZoomRatios(NULL)
{
    LOG1("@%s", __FUNCTION__);
    int camera_idx = -1;

    video_fds[V4L2_FIRST_DEVICE] = -1;
    video_fds[V4L2_SECOND_DEVICE] = -1;
    video_fds[V4L2_THIRD_DEVICE] = -1;

    mConfig.fps = 30;
    mConfig.num_snapshot = 1;
    mConfig.zoom = 0;

    // Open the main device first, this device will remain open during object life span
    int ret = openDevice(V4L2_FIRST_DEVICE);
    if (ret < 0) {
        LOGE("Failed to open first device!");
        return;
    }

    size_t numCameras = setupCameraInfo();

    for (size_t i = 0; i < numCameras; i++) {
        if ((camera_id == CAMERA_FACING_BACK  && camInfo[i].port == ATOMISP_CAMERA_PORT_PRIMARY) ||
                (camera_id == CAMERA_FACING_FRONT && camInfo[i].port == ATOMISP_CAMERA_PORT_SECONDARY)) {
            camera_idx = i;
            break;
        }
    }
    if (camera_idx == -1) {
        LOGE("Didn't find %s camera. Using default camera!",
                camera_id == CAMERA_FACING_BACK ? "back" : "front");
        camera_idx = 0;
    }
    mCameraId = camera_idx;

    mSensorType = (camInfo[mCameraId].port == ATOMISP_CAMERA_PORT_PRIMARY)?SENSOR_TYPE_RAW:SENSOR_TYPE_SOC;

    LOG1("Sensor type detected: %s", (mSensorType == SENSOR_TYPE_RAW)?"RAW":"SOC");

    if (selectCameraSensor() == NO_ERROR) {
        if (mSensorType == SENSOR_TYPE_RAW) {
            if (mAAA->init(camInfo[mCameraId].name, main_fd) == NO_ERROR) {
                LOG1("3A initialized");
            } else {
                LOGE("Error initializing 3A on RAW sensor!");
            }
        }
    } else {
        LOGE("Could not select camera: %s (sensor ID: %d)", camInfo[mCameraId].name, mCameraId);
    }

    ret = detectDeviceResolutions();
    if (ret) {
        LOGE("Failed to detect camera %s, resolution! Use default settings", camInfo[mCameraId].name);
        switch (camInfo[mCameraId].port) {
        case ATOMISP_CAMERA_PORT_SECONDARY:
            mConfig.snapshot.maxWidth  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
            mConfig.snapshot.maxHeight = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
            break;
        case ATOMISP_CAMERA_PORT_PRIMARY:
            mConfig.snapshot.maxWidth  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
            mConfig.snapshot.maxHeight = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
            break;
        }
    }
    else {
        LOG1("Camera %s: Max-resolution detected: %dx%d", camInfo[mCameraId].name,
                mConfig.snapshot.maxWidth,
                mConfig.snapshot.maxHeight);
    }

    switch (camInfo[mCameraId].port) {
    case ATOMISP_CAMERA_PORT_SECONDARY:
        mConfig.preview.maxWidth   = MAX_FRONT_CAMERA_PREVIEW_WIDTH;
        mConfig.preview.maxHeight  = MAX_FRONT_CAMERA_PREVIEW_HEIGHT;
        mConfig.recording.maxWidth = MAX_FRONT_CAMERA_VIDEO_WIDTH;
        mConfig.recording.maxHeight = MAX_FRONT_CAMERA_VIDEO_HEIGHT;
        break;
    case ATOMISP_CAMERA_PORT_PRIMARY:
        mConfig.preview.maxWidth   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
        mConfig.preview.maxHeight  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
        mConfig.recording.maxWidth = MAX_BACK_CAMERA_VIDEO_WIDTH;
        mConfig.recording.maxHeight = MAX_BACK_CAMERA_VIDEO_HEIGHT;
        break;
    default:
        LOGE("Invalid camera id: %d", mCameraId);
    }

    // Initialize the frame sizes
    setPreviewFrameFormat(RESOLUTION_VGA_WIDTH, RESOLUTION_VGA_HEIGHT, V4L2_PIX_FMT_NV12);
    setPostviewFrameFormat(RESOLUTION_POSTVIEW_WIDTH, RESOLUTION_POSTVIEW_HEIGHT, V4L2_PIX_FMT_NV12);
    setSnapshotFrameFormat(RESOLUTION_5MP_WIDTH, RESOLUTION_5MP_HEIGHT, V4L2_PIX_FMT_NV12);
    setVideoFrameFormat(RESOLUTION_VGA_WIDTH, RESOLUTION_VGA_HEIGHT, V4L2_PIX_FMT_NV12);

    /*
       Zoom is describled as 100, 200, each level has less memory than 5 bytes
       We don't support zoom bigger than 9999
       The last byte is used to store '\0'
     */
    static const int zoomBytes = MAX_ZOOM_LEVEL * 5 + 1;
    mZoomRatios = new char[zoomBytes];
    computeZoomRatios(mZoomRatios, zoomBytes);
}

AtomISP::~AtomISP()
{
    LOG1("@%s", __FUNCTION__);
    /*
     * The destructor is called when the hw_module close mehod is called. The close method is called
     * in general by the camera client when it's done with the camera device, but it is also called by
     * System Server when the camera application crashes. System Server calls close in order to release
     * the camera hardware module. So, if we are not in MODE_NONE, it means that we are in the middle of
     * somthing when the close function was called. So it's our duty to stop first, then close the
     * camera device.
     */
    if (mMode != MODE_NONE) {
        stop();
    }
    mAAA->unInit();
    closeDevice(V4L2_FIRST_DEVICE);

    if (mZoomRatios)
        delete mZoomRatios;
}

void AtomISP::getDefaultParameters(CameraParameters *params)
{
    LOG2("@%s", __FUNCTION__);
    if (!params) {
        LOGE("params is null!");
        return;
    }

    /**
     * PREVIEW
     */
    params->setPreviewSize(mConfig.preview.width, mConfig.preview.height);
    params->setPreviewFrameRate(30);

    if (camInfo[mCameraId].port == ATOMISP_CAMERA_PORT_PRIMARY) {
        params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
            "1024x580,1024x576,800x600,720x576,720x480,640x480,640x360,416x312,352x288,320x240,176x144");
    } else {
        params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
            "1024x580,720x576,720x480,640x480,640x360,352x288,320x240,176x144");
    }

    params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,"30,15,10");
    params->set(CameraParameters::KEY_PREVIEW_FPS_RANGE,"10500,30304");
    params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,"(10500,30304),(11000,30304),(11500,30304)");

    /**
     * RECORDING
     */
    params->setVideoSize(mConfig.recording.width, mConfig.recording.height);
    params->set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO, PREFERRED_PREVIEW_SIZE_FOR_VIDEO);
    params->set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES, "176x144,320x240,352x288,640x480,720x480,720x576,1280x720,1920x1088");
    params->set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
                CameraParameters::PIXEL_FORMAT_YUV420SP);
#ifndef ANDROID_2036
    params->set(CameraParameters::KEY_VIDEO_SNAPSHOT_SUPPORTED, CameraParameters::FALSE);
#else
    params->set(CameraParameters::KEY_VIDEO_SNAPSHOT_SUPPORTED, CameraParameters::TRUE);
#endif

    /**
     * SNAPSHOT
     */
    const char *picSizes = getMaxSnapShotResolution();
    params->set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, picSizes);
    params->setPictureSize(mConfig.snapshot.width, mConfig.snapshot.height);
    params->set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH,"320");
    params->set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,"240");
    params->set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,"320x240,240x320,320x180,180x320,160x120,120x160,0x0");

    /**
     * ZOOM
     */
    params->set(CameraParameters::KEY_ZOOM, 0);
    params->set(CameraParameters::KEY_ZOOM_SUPPORTED, CameraParameters::TRUE);

    /**
     * FLASH
     */
    if (camInfo[mCameraId].port == ATOMISP_CAMERA_PORT_PRIMARY) {
        // For main back camera
        // flash mode option
        params->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
        char flashModes[100] = {0};
        if (snprintf(flashModes, sizeof(flashModes)
                ,"%s,%s,%s,%s"
                ,CameraParameters::FLASH_MODE_AUTO
                ,CameraParameters::FLASH_MODE_OFF
                ,CameraParameters::FLASH_MODE_ON
                ,CameraParameters::FLASH_MODE_TORCH) < 0) {
            LOGE("Could not generate %s string: %s", CameraParameters::KEY_SUPPORTED_FLASH_MODES, strerror(errno));
            return;
        }
        params->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, flashModes);
    } else {
        // For front camera
        // No flash present
        params->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
        params->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, CameraParameters::FLASH_MODE_OFF);
    }

    /**
     * FOCUS
     */
    if(camInfo[mCameraId].port == ATOMISP_CAMERA_PORT_PRIMARY) {
        params->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);

        char focusModes[100] = {0};
        if (snprintf(focusModes, sizeof(focusModes)
                ,"%s,%s,%s,%s"
                ,CameraParameters::FOCUS_MODE_AUTO
                ,CameraParameters::FOCUS_MODE_INFINITY
                ,CameraParameters::FOCUS_MODE_MACRO
                ,CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO) < 0) {
            LOGE("Could not generate %s string: %s",
                CameraParameters::KEY_SUPPORTED_FOCUS_MODES, strerror(errno));
            return;
        }
        params->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, focusModes);
    }
    else {
        params->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_FIXED);
        params->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, CameraParameters::FOCUS_MODE_FIXED);
    }

    /**
     * FOCAL LENGTH
     */
    atomisp_makernote_info makerNote;
    getMakerNote(&makerNote);
    float focal_length = ((float)((makerNote.focal_length>>16) & 0xFFFF)) /
        ((float)(makerNote.focal_length & 0xFFFF));
    char focalLength[100] = {0};
    if (snprintf(focalLength, sizeof(focalLength),"%f", focal_length) < 0) {
        LOGE("Could not generate %s string: %s",
            CameraParameters::KEY_FOCAL_LENGTH, strerror(errno));
        return;
    }
    params->set(CameraParameters::KEY_FOCAL_LENGTH,focalLength);

    /**
     * FOCUS DISTANCES
     */
    getFocusDistances(params);

    /**
     * MISCELLANEOUS
     */
    params->set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE,"42.5");
    params->set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE,"54.8");

    /**
     * XNR/ANR
     */
    params->set(CameraParameters::KEY_SUPPORTED_XNR, "true,false");
    params->set(CameraParameters::KEY_XNR, CameraParameters::FALSE);
    params->set(CameraParameters::KEY_SUPPORTED_ANR, "true,false");
    params->set(CameraParameters::KEY_ANR, CameraParameters::FALSE);

    /**
     * EXPOSURE
     */
    params->set(CameraParameters::KEY_EXPOSURE_COMPENSATION,0);
    params->set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION,0);
    params->set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION,0);
    params->set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP,0);

    // No Capture bracketing
    params->set(CameraParameters::KEY_CAPTURE_BRACKET, "none");
    params->set(CameraParameters::KEY_SUPPORTED_CAPTURE_BRACKET, "none");

    // No HDR imaging
    params->set(CameraParameters::KEY_HDR_IMAGING, "off");
    params->set(CameraParameters::KEY_SUPPORTED_HDR_IMAGING, "off");
    params->set(CameraParameters::KEY_HDR_VIVIDNESS, "none");
    params->set(CameraParameters::KEY_SUPPORTED_HDR_VIVIDNESS, "none");
    params->set(CameraParameters::KEY_HDR_SHARPENING, "none");
    params->set(CameraParameters::KEY_SUPPORTED_HDR_SHARPENING, "none");
    params->set(CameraParameters::KEY_HDR_SAVE_ORIGINAL, "off");
    params->set(CameraParameters::KEY_SUPPORTED_HDR_SAVE_ORIGINAL, "off");

    /**
     * Burst-mode
     */
    params->set(CameraParameters::KEY_BURST_FPS, "1");
    params->set(CameraParameters::KEY_SUPPORTED_BURST_FPS, "1,3,5,7,15");
    params->set(CameraParameters::KEY_SUPPORTED_BURST_LENGTH, "1,3,5,10");
    params->set(CameraParameters::KEY_BURST_LENGTH, "1");

    if(mAAA->is3ASupported()){
        // effect modes
        params->set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);
        char effectModes[100] = {0};
        if (snprintf(effectModes, sizeof(effectModes)
                ,"%s,%s,%s,%s"
                ,CameraParameters::EFFECT_NONE
                ,CameraParameters::EFFECT_MONO
                ,CameraParameters::EFFECT_NEGATIVE
                ,CameraParameters::EFFECT_SEPIA) < 0) {
            LOGE("Could not generate %s string: %s", CameraParameters::KEY_SUPPORTED_EFFECTS, strerror(errno));
            return;
        }
        params->set(CameraParameters::KEY_SUPPORTED_EFFECTS, effectModes);

        // white-balance mode
        params->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
        char wbModes[100] = {0};
        if (snprintf(wbModes, sizeof(wbModes)
                ,"%s,%s,%s,%s,%s"
                ,CameraParameters::WHITE_BALANCE_AUTO
                ,CameraParameters::WHITE_BALANCE_INCANDESCENT
                ,CameraParameters::WHITE_BALANCE_FLUORESCENT
                ,CameraParameters::WHITE_BALANCE_DAYLIGHT
                ,CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT) < 0) {
            LOGE("Could not generate %s string: %s", CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, strerror(errno));
            return;
        }
        params->set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, wbModes);

        // scene mode
        params->set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_AUTO);
        char sceneModes[100] = {0};
        if (snprintf(sceneModes, sizeof(sceneModes)
                ,"%s,%s,%s,%s,%s,%s"
                ,CameraParameters::SCENE_MODE_AUTO
                ,CameraParameters::SCENE_MODE_PORTRAIT
                ,CameraParameters::SCENE_MODE_SPORTS
                ,CameraParameters::SCENE_MODE_LANDSCAPE
                ,CameraParameters::SCENE_MODE_NIGHT
                ,CameraParameters::SCENE_MODE_FIREWORKS) < 0) {
            LOGE("Could not generate %s string: %s", CameraParameters::KEY_SUPPORTED_SCENE_MODES, strerror(errno));
            return;
        }
        params->set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, sceneModes);

#ifdef RED_EYE_MODE_SUPPORT
        // red-eye mode
        params->set(CameraParameters::KEY_RED_EYE_MODE, CameraParameters::RED_EYE_REMOVAL_OFF);
        char redEyeModes[100] = {0};
        if (snprintf(redEyeModes, sizeof(redEyeModes)
                ,"%s,%s"
                ,CameraParameters::RED_EYE_REMOVAL_ON
                ,CameraParameters::RED_EYE_REMOVAL_OFF) < 0) {
            LOGE("Could not generate %s string: %s", CameraParameters::KEY_SUPPORTED_RED_EYE_MODES, strerror(errno));
            return;
        }
        params->set(CameraParameters::KEY_SUPPORTED_RED_EYE_MODES, redEyeModes);
#endif

        // ae mode
        params->set(CameraParameters::KEY_AE_MODE, "auto");
        params->set(CameraParameters::KEY_SUPPORTED_AE_MODES, "auto,manual,shutter-priority,aperture-priority");

        // 3a lock: auto-exposure lock
        params->set(CameraParameters::KEY_AUTO_EXPOSURE_LOCK, CameraParameters::FALSE);
        params->set(CameraParameters::KEY_AUTO_EXPOSURE_LOCK_SUPPORTED, CameraParameters::TRUE);
        // 3a lock: auto-whitebalance lock
        params->set(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK, CameraParameters::FALSE);
        params->set(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK_SUPPORTED, CameraParameters::TRUE);

        // exposure compensation
        params->set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
        params->set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "6");
        params->set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-6");
        params->set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.33333333");

        // Intel/UMG parameters for 3A locks
        // TODO: only needed until upstream key is available for AF lock
        params->set(CameraParameters::KEY_AF_LOCK_MODE, "unlock");
        params->set(CameraParameters::KEY_SUPPORTED_AF_LOCK_MODES, "lock,unlock");
        // TODO: add UMG-style AE/AWB locking for Test Camera?

        // multipoint focus
        params->set(CameraParameters::KEY_MAX_NUM_FOCUS_AREAS, mAAA->getAfMaxNumWindows());
        // set empty area
        params->set(CameraParameters::KEY_FOCUS_AREAS, "");

        // metering areas
        params->set(CameraParameters::KEY_MAX_NUM_METERING_AREAS, mAAA->getAfMaxNumWindows());

        // Capture bracketing
        params->set(CameraParameters::KEY_CAPTURE_BRACKET, "none");
        params->set(CameraParameters::KEY_SUPPORTED_CAPTURE_BRACKET, "none,exposure,focus");

        params->set(CameraParameters::KEY_HDR_IMAGING, "off");
        params->set(CameraParameters::KEY_SUPPORTED_HDR_IMAGING, "on,off");
        params->set(CameraParameters::KEY_HDR_VIVIDNESS, "none");
        params->set(CameraParameters::KEY_SUPPORTED_HDR_VIVIDNESS, "none,gaussian,gamma");
        params->set(CameraParameters::KEY_HDR_SHARPENING, "none");
        params->set(CameraParameters::KEY_SUPPORTED_HDR_SHARPENING, "none,normal,strong");
        params->set(CameraParameters::KEY_HDR_SAVE_ORIGINAL, "off");
        params->set(CameraParameters::KEY_SUPPORTED_HDR_SAVE_ORIGINAL, "on,off");
    }
}

const char* AtomISP::getMaxSnapShotResolution()
{
    LOG1("@%s", __FUNCTION__);
    int index = RESOLUTION_14MP;

    if (mConfig.snapshot.maxWidth < RESOLUTION_14MP_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_14MP_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_8MP_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_8MP_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_5MP_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_5MP_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_1080P_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_1080P_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_720P_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_720P_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_VGA_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_VGA_HEIGHT)
            index--;
    if (index < 0)
            index = 0;

    return resolution_tables[index];
}

status_t AtomISP::updateLowLight()
{
    status_t status = NO_ERROR;
    if (mSensorType == SENSOR_TYPE_RAW) {
        if (atomisp_set_attribute(main_fd, V4L2_CID_ATOMISP_LOW_LIGHT, mLowLight, "Low Light") < 0) {
            LOGE("set low light failure");
            status = UNKNOWN_ERROR;
        }
    }
    return status;
}

status_t AtomISP::start(AtomMode mode)
{
    LOG1("@%s", __FUNCTION__);
    LOG1("mode = %d", mode);
    status_t status = NO_ERROR;

    switch (mode) {
    case MODE_PREVIEW:
        status = startPreview();
        break;

    case MODE_VIDEO:
        status = startRecording();
        break;

    case MODE_CAPTURE:
        status = startCapture();
        break;

    default:
        break;
    };

    if (status == NO_ERROR) {
        mMode = mode;
        mSessionId++;
    }

    return status;
}

status_t AtomISP::stop()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    switch (mMode) {
    case MODE_PREVIEW:
        status = stopPreview();
        break;

    case MODE_VIDEO:
        status = stopRecording();
        break;

    case MODE_CAPTURE:
        status = stopCapture();
        break;

    default:
        break;
    };

    if (status == NO_ERROR)
        mMode = MODE_NONE;

    return status;
}

status_t AtomISP::startPreview()
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    status_t status = NO_ERROR;

    mPreviewDevice = V4L2_FIRST_DEVICE;

    if ((status = allocatePreviewBuffers()) != NO_ERROR)
        return status;

    ret = configureDevice(
            mPreviewDevice,
            CI_MODE_PREVIEW,
            mConfig.preview.padding,
            mConfig.preview.height,
            mConfig.preview.format,
            false);
    if (ret < 0) {
        LOGE("Configure preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitClose;
    }

    if (mAAA->is3ASupported()) {
        if (mAAA->switchModeAndRate(MODE_PREVIEW, mConfig.fps) == NO_ERROR) {
            LOG1("Switched 3A to MODE_PREVIEW at %.2f fps",
                 mConfig.fps);
        } else {
            LOGW("Failed switching 3A to MODE_PREVIEW at %.2f fps",
                 mConfig.fps);
        }
    }

    // need to resend the current zoom value
    atomisp_set_zoom(main_fd, mConfig.zoom);

    ret = startDevice(mPreviewDevice, mNumBuffers);
    if (ret < 0) {
        LOGE("Start preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitClose;
    }

    mNumPreviewBuffersQueued = mNumBuffers;

    return status;

exitClose:
    stopDevice(mPreviewDevice);
exitFree:
    freePreviewBuffers();
    return status;
}

status_t AtomISP::stopPreview()
{
    LOG1("@%s", __FUNCTION__);

    stopDevice(mPreviewDevice);
    freePreviewBuffers();

    return NO_ERROR;
}

status_t AtomISP::startRecording() {
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    status_t status = NO_ERROR;

    mPreviewDevice = V4L2_SECOND_DEVICE;

    if ((status = allocateRecordingBuffers()) != NO_ERROR)
        return status;

    if ((status = allocatePreviewBuffers()) != NO_ERROR)
        goto exitFreeRec;

    ret = openDevice(mPreviewDevice);
    if (ret < 0) {
        LOGE("Open preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitFreePrev;
    }

    ret = configureDevice(
            mRecordingDevice,
            CI_MODE_VIDEO,
            mConfig.recording.padding,
            mConfig.recording.height,
            mConfig.recording.format,
            false);
    if (ret < 0) {
        LOGE("Configure recording device failed!");
        status = UNKNOWN_ERROR;
        goto exitClosePrev;
    }

    ret = configureDevice(
            mPreviewDevice,
            CI_MODE_VIDEO,
            mConfig.preview.padding,
            mConfig.preview.height,
            mConfig.preview.format,
            false);
    if (ret < 0) {
        LOGE("Configure recording device failed!");
        status = UNKNOWN_ERROR;
        goto exitClosePrev;
    }

    if (mAAA->is3ASupported()) {
        if (mAAA->switchModeAndRate(MODE_VIDEO, mConfig.fps) == NO_ERROR) {
            LOG1("Switched 3A to MODE_VIDEO at %.2f fps",
                 mConfig.fps);
        } else {
            LOGW("Failed switching 3A to MODE_VIDEO at %.2f fps",
                 mConfig.fps);
        }
    }

    ret = startDevice(mRecordingDevice, mNumBuffers);
    if (ret < 0) {
        LOGE("Start recording device failed");
        status = UNKNOWN_ERROR;
        goto exitClosePrev;
    }

    ret = startDevice(mPreviewDevice, mNumBuffers);
    if (ret < 0) {
        LOGE("Start preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitStopRec;
    }

    mNumPreviewBuffersQueued = mNumBuffers;
    mNumRecordingBuffersQueued = mNumBuffers;

    return status;

exitStopRec:
    stopDevice(mRecordingDevice);
exitClosePrev:
    closeDevice(mPreviewDevice);
exitFreePrev:
    freePreviewBuffers();
exitFreeRec:
    freeRecordingBuffers();
    return status;
}

status_t AtomISP::stopRecording()
{
    LOG1("@%s", __FUNCTION__);

    freeRecordingBuffers();
    stopDevice(mRecordingDevice);

    freePreviewBuffers();
    stopDevice(mPreviewDevice);
    closeDevice(mPreviewDevice);

    return NO_ERROR;
}

status_t AtomISP::startCapture()
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    status_t status = NO_ERROR;

    if ((status = allocateSnapshotBuffers()) != NO_ERROR)
        return status;

    updateLowLight();

    ret = configureDevice(
            V4L2_FIRST_DEVICE,
            CI_MODE_STILL_CAPTURE,
            mConfig.snapshot.width,
            mConfig.snapshot.height,
            mConfig.snapshot.format,
            false);
    if (ret < 0) {
        LOGE("configure first device failed!");
        status = UNKNOWN_ERROR;
        goto errorFreeBuf;
    }

    ret = openDevice(V4L2_SECOND_DEVICE);
    if (ret < 0) {
        LOGE("Open second device failed!");
        status = UNKNOWN_ERROR;
        goto errorFreeBuf;
    }

    ret = configureDevice(
            V4L2_SECOND_DEVICE,
            CI_MODE_STILL_CAPTURE,
            mConfig.postview.width,
            mConfig.postview.height,
            mConfig.postview.format,
            false);
    if (ret < 0) {
        LOGE("configure second device failed!");
        status = UNKNOWN_ERROR;
        goto errorCloseSecond;
    }

    if (mAAA->is3ASupported()) {
        if (mAAA->switchModeAndRate(MODE_CAPTURE, mConfig.fps) == NO_ERROR) {
            LOG1("Switched 3A to MODE_CAPTURE at %.2f fps",
                 mConfig.fps);
        } else {
            LOGW("Failed switching 3A to MODE_CAPTURE at %.2f fps",
                 mConfig.fps);
        }
    }

    // need to resend the current zoom value
    atomisp_set_zoom(main_fd, mConfig.zoom);

    ret = startDevice(V4L2_FIRST_DEVICE, mConfig.num_snapshot);
    if (ret < 0) {
        LOGE("start capture on first device failed!");
        status = UNKNOWN_ERROR;
        goto errorCloseSecond;
    }

    ret = startDevice(V4L2_SECOND_DEVICE, mConfig.num_snapshot);
    if (ret < 0) {
        LOGE("start capture on second device failed!");
        status = UNKNOWN_ERROR;
        goto errorStopFirst;
    }

    mNumCapturegBuffersQueued = mConfig.num_snapshot;

    return status;

errorStopFirst:
    stopDevice(V4L2_FIRST_DEVICE);
errorCloseSecond:
    closeDevice(V4L2_SECOND_DEVICE);
errorFreeBuf:
    freeSnapshotBuffers();
    return status;
}

status_t AtomISP::stopCapture()
{
    LOG1("@%s", __FUNCTION__);
    stopDevice(V4L2_SECOND_DEVICE);
    stopDevice(V4L2_FIRST_DEVICE);
    closeDevice(V4L2_SECOND_DEVICE);
    mUsingClientSnapshotBuffers = false;
    return NO_ERROR;
}

status_t AtomISP::releaseCaptureBuffers()
{
    LOG1("@%s", __FUNCTION__);
    return freeSnapshotBuffers();
}

int AtomISP::configureDevice(int device, int deviceMode, int w, int h, int format, bool raw)
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    LOG1("device: %d, width:%d, height:%d, deviceMode:%d format:%d raw:%d", device,
        w, h, deviceMode, format, raw);

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LOGE("Wrong device: %d", device);
        return -1;
    }

    if ((w <= 0) || (h <= 0)) {
        LOGE("Wrong Width %d or Height %d", w, h);
        return -1;
    }

    //Only update the configure for device
    int fd = video_fds[device];

    //Switch the Mode before set the format. This is the requirement of
    //atomisp
    ret = atomisp_set_capture_mode(deviceMode);
    if (ret < 0)
        return ret;

    //Set the format
    ret = v4l2_capture_s_format(fd, device, w, h, format, raw);
    if (ret < 0)
        return ret;

    v4l2_buf_pool[device].width = w;
    v4l2_buf_pool[device].height = h;
    v4l2_buf_pool[device].format = format;

    /* 3A related initialization*/
    //Reallocate the grid for 3A after format change
    if (device == V4L2_FIRST_DEVICE) {
        ret = v4l2_capture_g_framerate(fd, &mConfig.fps, w, h, format);
        if (ret < 0) {
        /*Error handler: if driver does not support FPS achieving,
                       just give the default value.*/
            mConfig.fps = DEFAULT_SENSOR_FPS;
            ret = 0;
        }
    }

    //We need apply all the parameter settings when do the camera reset
    return ret;
}

int AtomISP::startDevice(int device, int buffer_count)
{
    LOG1("@%s", __FUNCTION__);
    LOG1("device = %d", device);

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LOGE("Wrong device: %d", device);
        return -1;
    }

    int i, ret;
    int fd = video_fds[device];
    LOG1(" startDevice fd = %d", fd);

    if (device == V4L2_FIRST_DEVICE &&
        mAAA->is3ASupported() &&
        mAAA->applyIspSettings() != NO_ERROR) {
        LOGE("Failed to apply 3A ISP settings. Disabling 3A!");
    } else {
        LOG1("Applied 3A ISP settings!");
    }

    //parameter intialized before the streamon
    //request, query and mmap the buffer and save to the pool
    ret = createBufferPool(device, buffer_count);
    if (ret < 0)
        return ret;

    //Qbuf
    ret = activateBufferPool(device);
    if (ret < 0)
        goto activate_error;

    //stream on
    ret = v4l2_capture_streamon(fd);
    if (ret < 0)
       goto streamon_failed;

    //we are started now
    return 0;

aaa_error:
    v4l2_capture_streamoff(fd);
streamon_failed:
activate_error:
    destroyBufferPool(device);
    return ret;
}

int AtomISP::activateBufferPool(int device)
{
    LOG1("@%s: device = %d", __FUNCTION__, device);

    int fd = video_fds[device];
    int ret;
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];

    for (int i = 0; i < pool->active_buffers; i++) {
        ret = v4l2_capture_qbuf(fd, i, &pool->bufs[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

int AtomISP::createBufferPool(int device, int buffer_count)
{
    LOG1("@%s: device = %d", __FUNCTION__, device);
    int i, ret;

    int fd = video_fds[device];
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];
    int num_buffers = v4l2_capture_request_buffers(device, buffer_count);
    LOG1("num_buffers = %d", num_buffers);

    if (num_buffers <= 0)
        return -1;

    pool->active_buffers = num_buffers;

    for (i = 0; i < num_buffers; i++) {
        pool->bufs[i].width = pool->width;
        pool->bufs[i].height = pool->height;
        pool->bufs[i].format = pool->format;
        ret = v4l2_capture_new_buffer(device, i, &pool->bufs[i]);
        if (ret < 0)
            goto error;
    }
    return 0;

error:
    for (int j = 0; j < i; j++)
        v4l2_capture_free_buffer(device, &pool->bufs[j]);
    pool->active_buffers = 0;
    return ret;
}

void AtomISP::stopDevice(int device)
{
    LOG1("@%s: device = %d", __FUNCTION__, device);
    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LOGE("Wrong device: %d", device);
        return;
    }
    int fd = video_fds[device];

    if (fd >= 0) {
        //stream off
        v4l2_capture_streamoff(fd);
        destroyBufferPool(device);
    }
}

void AtomISP::destroyBufferPool(int device)
{
    LOG1("@%s: device = %d", __FUNCTION__, device);

    int fd = video_fds[device];
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];

    for (int i = 0; i < pool->active_buffers; i++)
        v4l2_capture_free_buffer(device, &pool->bufs[i]);
    pool->active_buffers = 0;
    v4l2_capture_release_buffers(device);
}

int AtomISP::openDevice(int device)
{
    LOG1("@%s", __FUNCTION__);
    if (video_fds[device] > 0) {
        LOGW("MainDevice already opened!");
        return video_fds[device];
    }

    video_fds[device] = v4l2_capture_open(device);

    if (video_fds[device] < 0) {
        LOGE("V4L2: capture_open failed: %s", strerror(errno));
        return -1;
    }

    // Query and check the capabilities
    if (v4l2_capture_querycap(device, &cap) < 0) {
        LOGE("V4L2: capture_querycap failed: %s", strerror(errno));
        v4l2_capture_close(video_fds[device]);
        video_fds[device] = -1;
        return -1;
    }

    return video_fds[device];
}

void AtomISP::closeDevice(int device)
{
    LOG1("@%s", __FUNCTION__);

    if (video_fds[device] < 0) {
        LOG1("Device %d already closed. Do nothing.", device);
        return;
    }

    v4l2_capture_close(video_fds[device]);

    video_fds[device] = -1;
}

status_t AtomISP::selectCameraSensor()
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0, camera_idx = -1;
    int device = V4L2_FIRST_DEVICE;

    //Choose the camera sensor
    LOG1("Selecting camera sensor: %d", mCameraId);
    ret = v4l2_capture_s_input(video_fds[device], mCameraId);
    if (ret < 0) {
        LOGE("V4L2: capture_s_input failed: %s", strerror(errno));
        v4l2_capture_close(video_fds[device]);
        video_fds[device] = -1;
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

int AtomISP::detectDeviceResolutions()
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    struct v4l2_frmsizeenum frame_size;
    int device = V4L2_FIRST_DEVICE;

    //Switch the Mode before try the format.
    ret = atomisp_set_capture_mode(MODE_CAPTURE);
    if (ret < 0)
        return ret;

    int i = 0;
    while (true) {
        memset(&frame_size, 0, sizeof(frame_size));
        frame_size.index = i++;
        frame_size.pixel_format = mConfig.snapshot.format;
        /* TODO: Currently VIDIOC_ENUM_FRAMESIZES is returning with Invalid argument
         * Need to know why the driver is not supporting this V4L2 API call
         */
        if (ioctl(video_fds[V4L2_FIRST_DEVICE], VIDIOC_ENUM_FRAMESIZES, &frame_size) < 0) {
            break;
        }
        ret++;
        float fps = 0;
        v4l2_capture_g_framerate(
                video_fds[V4L2_FIRST_DEVICE],
                &fps,
                frame_size.discrete.width,
                frame_size.discrete.height,
                frame_size.pixel_format);
        LOG1("Supported frame size: %ux%u@%dfps",
                frame_size.discrete.width,
                frame_size.discrete.height,
                static_cast<int>(fps));
    }

    // Get the maximum format supported
    mConfig.snapshot.maxWidth = 0xffff;
    mConfig.snapshot.maxHeight = 0xffff;
    ret = v4l2_capture_try_format(V4L2_FIRST_DEVICE, &mConfig.snapshot.maxWidth, &mConfig.snapshot.maxHeight, &mConfig.snapshot.format);
    if (ret < 0)
        return ret;
    return 0;
}

status_t AtomISP::setPreviewFrameFormat(int width, int height, int format)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if(format == 0)
         format = mConfig.preview.format;
    if (width > mConfig.preview.maxWidth || width <= 0)
        width = mConfig.preview.maxWidth;
    if (height > mConfig.preview.maxHeight || height <= 0)
        height = mConfig.preview.maxHeight;
    mConfig.preview.width = width;
    mConfig.preview.height = height;
    mConfig.preview.format = format;
    mConfig.preview.padding = paddingWidth(format, width, height);
    mConfig.preview.size = frameSize(format, mConfig.preview.padding, height);
    LOG1("width(%d), height(%d), pad_width(%d), size(%d), format(%x)",
        width, height, mConfig.preview.padding, mConfig.preview.size, format);
    return status;
}

status_t AtomISP::setPostviewFrameFormat(int width, int height, int format)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    LOG1("width(%d), height(%d), format(%x)",
         width, height, format);
    if (width < 0 || height < 0) {
        LOGE("Invalid postview size requested!");
        return BAD_VALUE;
    }
    if (width == 0 || height == 0) {
        // No thumbnail requested, we should anyway use postview to dequeue frames from ISP
        width = RESOLUTION_POSTVIEW_WIDTH;
        height = RESOLUTION_POSTVIEW_HEIGHT;
    }
    mConfig.postview.width = width;
    mConfig.postview.height = height;
    mConfig.postview.format = format;
    mConfig.postview.padding = paddingWidth(format, width, height);
    mConfig.postview.size = frameSize(format, width, height);
    if (mConfig.postview.size == 0)
        mConfig.postview.size = mConfig.postview.width * mConfig.postview.height * BPP;
    LOG1("width(%d), height(%d), pad_width(%d), size(%d), format(%x)",
            width, height, mConfig.postview.padding, mConfig.postview.size, format);
    return status;
}

status_t AtomISP::setSnapshotFrameFormat(int width, int height, int format)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (width > mConfig.snapshot.maxWidth || width <= 0)
        width = mConfig.snapshot.maxWidth;
    if (height > mConfig.snapshot.maxHeight || height <= 0)
        height = mConfig.snapshot.maxHeight;
    mConfig.snapshot.width  = width;
    mConfig.snapshot.height = height;
    mConfig.snapshot.format = format;
    mConfig.snapshot.padding = paddingWidth(format, width, height);
    mConfig.snapshot.size = frameSize(format, width, height);;
    if (mConfig.snapshot.size == 0)
        mConfig.snapshot.size = mConfig.snapshot.width * mConfig.snapshot.height * BPP;
    LOG1("width(%d), height(%d), pad_width(%d), size(%d), format(%x)",
        width, height, mConfig.snapshot.padding, mConfig.snapshot.size, format);
    return status;
}

void AtomISP::getVideoSize(int *width, int *height)
{
    if (width && height) {
        *width = mConfig.recording.width;
        *height = mConfig.recording.height;
    }
}

status_t AtomISP::setSnapshotNum(int num)
{
    LOG1("@%s", __FUNCTION__);
    mConfig.num_snapshot = num;
    LOG1("mConfig.num_snapshot = %d", mConfig.num_snapshot);
    return NO_ERROR;
}

status_t AtomISP::setVideoFrameFormat(int width, int height, int format)
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    status_t status = NO_ERROR;

    if(format == 0)
         format = mConfig.recording.format;
    if (mConfig.recording.width == width &&
        mConfig.recording.height == height &&
        mConfig.recording.format == format) {
        // Do nothing
        return status;
    }

    if (mMode == MODE_VIDEO) {
        LOGE("Reconfiguration in video mode unsupported. Stop the ISP first");
        return INVALID_OPERATION;
    }

    if (width > mConfig.recording.maxWidth || width <= 0) {
        LOGE("invalid recording width %d. override to %d", width, mConfig.recording.maxWidth);
        width = mConfig.recording.maxWidth;
    }
    if (height > mConfig.recording.maxHeight || height <= 0) {
        LOGE("invalid recording height %d. override to %d", height, mConfig.recording.maxHeight);
        height = mConfig.recording.maxHeight;
    }
    mConfig.recording.width = width;
    mConfig.recording.height = height;
    mConfig.recording.format = format;
    mConfig.recording.padding = paddingWidth(format, width, height);
    mConfig.recording.size = frameSize(format, width, height);
    if (mConfig.recording.size == 0)
        mConfig.recording.size = mConfig.recording.width * mConfig.recording.height * BPP;
    LOG1("width(%d), height(%d), pad_width(%d), format(%x)",
            width, height, mConfig.recording.padding, format);

    return status;
}

void AtomISP::getZoomRatios(AtomMode mode, CameraParameters *params)
{
    LOG1("@%s", __FUNCTION__);
    if (params) {
        if ((mode == MODE_PREVIEW) ||
                (mode == MODE_CAPTURE) ||
                (mode == MODE_VIDEO && mSensorType == SENSOR_TYPE_RAW)) {
            params->set(CameraParameters::KEY_MAX_ZOOM, MAX_ZOOM_LEVEL);
            params->set(CameraParameters::KEY_ZOOM_RATIOS, mZoomRatios);
        } else {
            // zoom is not supported. this is indicated by placing a single zoom ratio in params
            params->set(CameraParameters::KEY_MAX_ZOOM, "0"); // zoom index 0 indicates first (and only) zoom ratio
            params->set(CameraParameters::KEY_ZOOM_RATIOS, "100");
        }
    }
}

void AtomISP::getFocusDistances(CameraParameters *params)
{
    LOG1("@%s", __FUNCTION__);
    char focusDistance[100] = {0};
    float fDistances[3] = {0};  // 3 distances: near, optimal, and far

    // would be better if we could get these from driver instead of hard-coding
    if(camInfo[mCameraId].port == ATOMISP_CAMERA_PORT_PRIMARY) {
        fDistances[0] = 2.0;
        fDistances[1] = 2.0;
        fDistances[2] = INFINITY;
    }
    else {
        fDistances[0] = 0.3;
        fDistances[1] = 0.65;
        fDistances[2] = INFINITY;
    }

    for (int i = 0; i < (int) (sizeof(fDistances)/sizeof(fDistances[0])); i++) {
        int left = sizeof(focusDistance) - strlen(focusDistance);
        int res;

        // use CameraParameters::FOCUS_DISTANCE_INFINITY for value of infinity
        if (fDistances[i] == INFINITY) {
            res = snprintf(focusDistance + strlen(focusDistance), left, "%s%s",
                    i ? "," : "", CameraParameters::FOCUS_DISTANCE_INFINITY);
        } else {
            res = snprintf(focusDistance + strlen(focusDistance), left, "%s%g",
                    i ? "," : "", fDistances[i]);
        }
        if (res < 0) {
            LOGE("Could not generate %s string: %s",
                CameraParameters::KEY_FOCUS_DISTANCES, strerror(errno));
            return;
        }
    }
    params->set(CameraParameters::KEY_FOCUS_DISTANCES, focusDistance);
}

status_t AtomISP::setFlash(int numFrames)
{
    LOG1("@%s: numFrames = %d", __FUNCTION__, numFrames);
    if (camInfo[mCameraId].port != ATOMISP_CAMERA_PORT_PRIMARY) {
        LOGE("Flash is supported only for primary camera!");
        return INVALID_OPERATION;
    }
    if (atomisp_set_attribute(main_fd, V4L2_CID_REQUEST_FLASH, numFrames, "Request Flash") < 0)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

status_t AtomISP::setFlashIndicator(int intensity)
{
    LOG1("@%s: intensity = %d", __FUNCTION__, intensity);
    if (camInfo[mCameraId].port != ATOMISP_CAMERA_PORT_PRIMARY) {
        LOGE("Indicator intensity is supported only for primary camera!");
        return INVALID_OPERATION;
    }

    if (intensity) {
        if (atomisp_set_attribute(main_fd, V4L2_CID_FLASH_INDICATOR_INTENSITY, intensity, "Indicator Intensity") < 0)
            return UNKNOWN_ERROR;
        if (atomisp_set_attribute(main_fd, V4L2_CID_FLASH_MODE, ATOMISP_FLASH_MODE_INDICATOR, "Flash Mode") < 0)
            return UNKNOWN_ERROR;
    } else {
        if (atomisp_set_attribute(main_fd, V4L2_CID_FLASH_MODE, ATOMISP_FLASH_MODE_OFF, "Flash Mode") < 0)
            return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t AtomISP::setTorch(int intensity)
{
    LOG1("@%s: intensity = %d", __FUNCTION__, intensity);
    if (camInfo[mCameraId].port != ATOMISP_CAMERA_PORT_PRIMARY) {
        LOGE("Indicator intensity is supported only for primary camera!");
        return INVALID_OPERATION;
    }

    if (intensity) {
        if (atomisp_set_attribute(main_fd, V4L2_CID_FLASH_TORCH_INTENSITY, intensity, "Torch Intensity") < 0)
            return UNKNOWN_ERROR;
        if (atomisp_set_attribute(main_fd, V4L2_CID_FLASH_MODE, ATOMISP_FLASH_MODE_TORCH, "Flash Mode") < 0)
            return UNKNOWN_ERROR;
    } else {
        if (atomisp_set_attribute(main_fd, V4L2_CID_FLASH_MODE, ATOMISP_FLASH_MODE_OFF, "Flash Mode") < 0)
            return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t AtomISP::setColorEffect(v4l2_colorfx effect)
{
    LOG1("@%s: effect = %d", __FUNCTION__, effect);
    status_t status = NO_ERROR;
    if (mMode == MODE_CAPTURE)
        return INVALID_OPERATION;
    if (atomisp_set_attribute (main_fd, V4L2_CID_COLORFX, effect, "Colour Effect") < 0)
        return UNKNOWN_ERROR;
    if (mAAA->is3ASupported()) {
        switch(effect) {
        case V4L2_COLORFX_NEGATIVE:
            status = mAAA->setNegativeEffect(true);
            break;
        default:
            status = mAAA->setNegativeEffect(false);
        }
        if (status == NO_ERROR) {
            status = mAAA->applyIspSettings();
        }
    }
    return status;
}

status_t AtomISP::setZoom(int zoom)
{
    LOG1("@%s: zoom = %d", __FUNCTION__, zoom);
    if (zoom == mConfig.zoom)
        return NO_ERROR;
    if (mMode == MODE_CAPTURE)
        return NO_ERROR;

    int ret = atomisp_set_zoom(main_fd, zoom);
    if (ret < 0) {
        LOGE("Error setting zoom to %d", zoom);
        return UNKNOWN_ERROR;
    }
    mConfig.zoom = zoom;
    return NO_ERROR;
}

status_t AtomISP::getMakerNote(atomisp_makernote_info *info)
{
    LOG1("@%s: info = %p", __FUNCTION__, info);
    int fd = video_fds[V4L2_FIRST_DEVICE];

    if (fd < 0) {
        return INVALID_OPERATION;
    }
    info->focal_length = 0;
    info->f_number_curr = 0;
    info->f_number_range = 0;
    if (xioctl(fd, ATOMISP_IOC_ISP_MAKERNOTE, info) < 0) {
        LOGW("WARNING: get maker note from driver failed!");
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t AtomISP::setXNR(bool enable)
{
    LOG1("@%s: %d", __FUNCTION__, (int) enable);
    status_t status = NO_ERROR;
    if (mSensorType == SENSOR_TYPE_RAW) {
        int en = (int) enable;
        if (xioctl(main_fd, ATOMISP_IOC_S_XNR, &en) < 0) {
            LOGE("set XNR failure");
            status = UNKNOWN_ERROR;
        }
    }
    return status;
}

status_t AtomISP::setLowLight(bool enable)
{
    LOG1("@%s: %d", __FUNCTION__, (int) enable);
    mLowLight = enable;
    return NO_ERROR;
}

int AtomISP::atomisp_set_zoom (int fd, int zoom)
{
    LOG1("@%s", __FUNCTION__);
    if (fd < 0) {
        LOG1("Device not opened!");
        return 0;
    }

    int zoom_driver = 0;
    float zoom_real = 0.0;

    if (zoom != 0) {

        /*
           The zoom value passed to HAL is from 0 to MAX_ZOOM_LEVEL to match 1x
           to 16x of real zoom effect. The equation between zoom_real and zoom_hal is:

           (zoom_hal - MIN_ZOOM_LEVEL)                   MAX_ZOOM_LEVEL - MIN_ZOOM_LEVEL
           ------------------------------------------ = ------------------------------------
           zoom_real * ZOOM_RATIO - MIN_SUPPORT_ZOOM     MAX_SUPPORT_ZOOM - MIN_SUPPORT_ZOOM
         */

        float x = ((MAX_SUPPORT_ZOOM - MIN_SUPPORT_ZOOM) / (MAX_ZOOM_LEVEL - MIN_ZOOM_LEVEL)) *
            ((float) zoom - MIN_ZOOM_LEVEL);
        zoom_real = (x + MIN_SUPPORT_ZOOM) / ZOOM_RATIO;

        /*
           The real zoom effect is 64/(64-zoom_driver) in the driver.
           Add 0.5 to get the more accurate result
           Calculate the zoom value should set to driver using the equation
           We want to get 3 if the zoom_driver is 2.9, so add 0.5 for compensation
         */

        zoom_driver = (64.0 - (64.0 / zoom_real) + 0.5);

    }

    LOG1("set zoom %f to driver with %d", zoom_real, zoom_driver);
    return atomisp_set_attribute (fd, V4L2_CID_ZOOM_ABSOLUTE, zoom_driver, "zoom");
}

int AtomISP::atomisp_set_attribute (int fd, int attribute_num,
                                             const int value, const char *name)
{
    LOG1("@%s", __FUNCTION__);
    struct v4l2_control control;
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control ext_control;

    LOG1("setting attribute [%s] to %d", name, value);

    if (fd < 0)
        return -1;

    control.id = attribute_num;
    control.value = value;
    controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
    controls.count = 1;
    controls.controls = &ext_control;
    ext_control.id = attribute_num;
    ext_control.value = value;

    if (ioctl(fd, VIDIOC_S_CTRL, &control) == 0)
        return 0;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) == 0)
        return 0;

    controls.ctrl_class = V4L2_CTRL_CLASS_USER;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) == 0)
        return 0;

    LOGE("Failed to set value %d for control %s (%d) on device '%d', %s",
        value, name, attribute_num, fd, strerror(errno));
    return -1;
}

int AtomISP::xioctl(int fd, int request, void *arg)
{
    int ret;

    do {
        ret = ioctl (fd, request, arg);
    } while (-1 == ret && EINTR == errno);

    if (ret < 0)
        LOGW ("Request %d failed: %s", request, strerror(errno));

    return ret;
}

int AtomISP::v4l2_capture_streamon(int fd)
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOGE("VIDIOC_STREAMON returned: %d (%s)", ret, strerror(errno));
        return ret;
    }
    return ret;
}

int AtomISP::v4l2_capture_streamoff(int fd)
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (fd < 0){ //Device is closed
        LOGE("Device is closed!");
        return 0;
    }
    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LOGE("VIDIOC_STREAMOFF returned: %d (%s)", ret, strerror(errno));
        return ret;
    }

    return ret;
}

/* Unmap the buffer or free the userptr */
int AtomISP::v4l2_capture_free_buffer(int device, struct v4l2_buffer_info *buf_info)
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    void *addr = buf_info->data;
    size_t length = buf_info->length;

    if (device == V4L2_THIRD_DEVICE &&
        (ret = munmap(addr, length)) < 0) {
            LOGE("munmap returned: %d (%s)", ret, strerror(errno));
            return ret;
        }

    return ret;
}

int AtomISP::v4l2_capture_release_buffers(int device)
{
    LOG1("@%s", __FUNCTION__);
    return v4l2_capture_request_buffers(device, 0);
}

int AtomISP::v4l2_capture_request_buffers(int device, uint num_buffers)
{
    LOG1("@%s", __FUNCTION__);
    struct v4l2_requestbuffers req_buf;
    int ret;
    CLEAR(req_buf);

    int fd = video_fds[device];

    if (fd < 0)
        return 0;

    req_buf.memory = V4L2_MEMORY_USERPTR;
    req_buf.count = num_buffers;
    req_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (device == V4L2_THIRD_DEVICE) {
        req_buf.memory = V4L2_MEMORY_MMAP;
        req_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    }

    LOG1("VIDIOC_REQBUFS, count=%d", req_buf.count);
    ret = ioctl(fd, VIDIOC_REQBUFS, &req_buf);

    if (ret < 0) {
        LOGE("VIDIOC_REQBUFS(%d) returned: %d (%s)",
            num_buffers, ret, strerror(errno));
        return ret;
    }

    if (req_buf.count < num_buffers)
        LOGW("Got less buffers than requested!");

    return req_buf.count;
}

int AtomISP::v4l2_capture_new_buffer(int device, int index, struct v4l2_buffer_info *buf)
{
    LOG1("@%s", __FUNCTION__);
    void *data;
    int ret;
    int fd = video_fds[device];
    struct v4l2_buffer *vbuf = &buf->vbuffer;
    vbuf->flags = 0x0;

    if (device == V4L2_THIRD_DEVICE) {
        vbuf->index = index;
        vbuf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        vbuf->memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fd, VIDIOC_QUERYBUF, vbuf);
        if (ret < 0) {
            LOGE("VIDIOC_QUERYBUF failed: %s", strerror(errno));
            return -1;
        }

        data = mmap(NULL, vbuf->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    vbuf->m.offset);

        if (MAP_FAILED == data) {
            LOGE("mmap failed: %s", strerror(errno));
            return -1;
        }

        buf->data = data;
        buf->length = vbuf->length;

        memcpy(data, mFileImage.mapped_addr, mFileImage.size);
        return 0;
    }

    vbuf->memory = V4L2_MEMORY_USERPTR;

    vbuf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf->index = index;
    ret = ioctl(fd , VIDIOC_QUERYBUF, vbuf);

    if (ret < 0) {
        LOGE("VIDIOC_QUERYBUF failed: %s", strerror(errno));
        return ret;
    }

    vbuf->m.userptr = (unsigned int)(buf->data);

    buf->length = vbuf->length;
    LOG1("index %u", vbuf->index);
    LOG1("type %d", vbuf->type);
    LOG1("bytesused %u", vbuf->bytesused);
    LOG1("flags %08x", vbuf->flags);
    LOG1("memory %u", vbuf->memory);
    LOG1("userptr:  %lu", vbuf->m.userptr);
    LOG1("length %u", vbuf->length);
    LOG1("input %u", vbuf->input);
    return ret;
}

int AtomISP::v4l2_capture_g_framerate(int fd, float *framerate, int width,
                                         int height, int pix_fmt)
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    struct v4l2_frmivalenum frm_interval;

    if (NULL == framerate)
        return -EINVAL;

    assert(fd > 0);
    CLEAR(frm_interval);
    frm_interval.pixel_format = pix_fmt;
    frm_interval.width = width;
    frm_interval.height = height;
    *framerate = -1.0;

    ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frm_interval);
    if (ret < 0) {
        LOGW("ioctl failed: %s", strerror(errno));
        return ret;
    }

    assert(0 != frm_interval.discrete.denominator);

    *framerate = 1.0 / (1.0 * frm_interval.discrete.numerator / frm_interval.discrete.denominator);

    return 0;
}

int AtomISP::v4l2_capture_s_format(int fd, int device, int w, int h, int fourcc, bool raw)
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);

    if (device == V4L2_THIRD_DEVICE) {
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        v4l2_fmt.fmt.pix.width = mFileImage.width;
        v4l2_fmt.fmt.pix.height = mFileImage.height;
        v4l2_fmt.fmt.pix.pixelformat = mFileImage.format;
        v4l2_fmt.fmt.pix.sizeimage = mFileImage.size;
        v4l2_fmt.fmt.pix.priv = mFileImage.bayer_order;

        LOG1("VIDIOC_S_FMT: width: %d, height: %d, format: %x, size: %d, bayer_order: %d",
                mFileImage.width,
                mFileImage.height,
                mFileImage.format,
                mFileImage.size,
                mFileImage.bayer_order);
        ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
        if (ret < 0) {
            LOGE("VIDIOC_S_FMT failed: %s", strerror(errno));
            return -1;
        }
        return 0;
    }

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    LOG1("VIDIOC_G_FMT");
    ret = ioctl (fd,  VIDIOC_G_FMT, &v4l2_fmt);
    if (ret < 0) {
        LOGE("VIDIOC_G_FMT failed: %s", strerror(errno));
        return -1;
    }
    if (raw) {
        LOG1("Choose raw dump path");
        v4l2_fmt.type = V4L2_BUF_TYPE_PRIVATE;
    } else {
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    v4l2_fmt.fmt.pix.width = w;
    v4l2_fmt.fmt.pix.height = h;
    v4l2_fmt.fmt.pix.pixelformat = fourcc;
    v4l2_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    LOG1("VIDIOC_S_FMT: width: %d, height: %d, format: %d, field: %d",
                v4l2_fmt.fmt.pix.width,
                v4l2_fmt.fmt.pix.height,
                v4l2_fmt.fmt.pix.pixelformat,
                v4l2_fmt.fmt.pix.field);
    ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        LOGE("VIDIOC_S_FMT failed: %s", strerror(errno));
        return -1;
    }
    return 0;

}

int AtomISP::v4l2_capture_qbuf(int fd, int index, struct v4l2_buffer_info *buf)
{
    LOG2("@%s", __FUNCTION__);
    struct v4l2_buffer *v4l2_buf = &buf->vbuffer;
    int ret;

    if (fd < 0) //Device is closed
        return 0;
    ret = ioctl(fd, VIDIOC_QBUF, v4l2_buf);
    if (ret < 0) {
        LOGE("VIDIOC_QBUF index %d failed: %s",
             index, strerror(errno));
        return ret;
    }
    return ret;
}

status_t AtomISP::v4l2_capture_open(int device)
{
    LOG1("@%s", __FUNCTION__);
    int fd;
    struct stat st;

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_THIRD_DEVICE)) {
        LOGE("Wrong device node %d", device);
        return -1;
    }

    const char *dev_name = dev_name_array[device];
    LOG1("---Open video device %s---", dev_name);

    if (stat (dev_name, &st) == -1) {
        LOGE("Error stat video device %s: %s",
             dev_name, strerror(errno));
        return -1;
    }

    if (!S_ISCHR (st.st_mode)) {
        LOGE("%s is not a device", dev_name);
        return -1;
    }

    fd = open(dev_name, O_RDWR);

    if (fd <= 0) {
        LOGE("Error opening video device %s: %s",
            dev_name, strerror(errno));
        return -1;
    }

    return fd;
}

status_t AtomISP::v4l2_capture_close(int fd)
{
    LOG1("@%s", __FUNCTION__);
    /* close video device */
    LOG1("----close device ---");
    if (fd < 0) {
        LOGW("Device not opened!");
        return INVALID_OPERATION;
    }

    if (close(fd) < 0) {
        LOGE("Close video device failed: %s", strerror(errno));
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t AtomISP::v4l2_capture_querycap(int device, struct v4l2_capability *cap)
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    int fd = video_fds[device];

    ret = ioctl(fd, VIDIOC_QUERYCAP, cap);

    if (ret < 0) {
        LOGE("VIDIOC_QUERYCAP returned: %d (%s)", ret, strerror(errno));
        return ret;
    }

    if (device == V4L2_THIRD_DEVICE) {
        if (!(cap->capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
            LOGE("No output devices");
            return -1;
        }
        return ret;
    }

    if (!(cap->capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOGE("No capture devices");
        return -1;
    }

    if (!(cap->capabilities & V4L2_CAP_STREAMING)) {
        LOGE("Is not a video streaming device");
        return -1;
    }

    LOG1( "driver:      '%s'", cap->driver);
    LOG1( "card:        '%s'", cap->card);
    LOG1( "bus_info:      '%s'", cap->bus_info);
    LOG1( "version:      %x", cap->version);
    LOG1( "capabilities:      %x", cap->capabilities);

    return ret;
}

status_t AtomISP::v4l2_capture_s_input(int fd, int index)
{
    LOG1("@%s", __FUNCTION__);
    struct v4l2_input input;
    int ret;

    LOG1("VIDIOC_S_INPUT");
    input.index = index;

    ret = ioctl(fd, VIDIOC_S_INPUT, &input);

    if (ret < 0) {
        LOGE("VIDIOC_S_INPUT index %d returned: %d (%s)",
            input.index, ret, strerror(errno));
        return ret;
    }
    return ret;
}

int AtomISP::atomisp_set_capture_mode(int deviceMode)
{
    LOG1("@%s", __FUNCTION__);
    struct v4l2_streamparm parm;

    switch (deviceMode) {
    case CI_MODE_PREVIEW:
        LOG1("Setting CI_MODE_PREVIEW mode");
        break;;
    case CI_MODE_STILL_CAPTURE:
        LOG1("Setting CI_MODE_STILL_CAPTURE mode");
        break;
    case CI_MODE_VIDEO:
        LOG1("Setting CI_MODE_VIDEO mode");
        break;
    default:
        break;
    }

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.capturemode = deviceMode;
    if (ioctl(main_fd, VIDIOC_S_PARM, &parm) < 0) {
        LOGE("error %s", strerror(errno));
        return -1;
    }

    return 0;
}

int AtomISP::v4l2_capture_try_format(int device, int *w, int *h,
                                         int *fourcc)
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    int fd = video_fds[device];
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);

    if (device == V4L2_THIRD_DEVICE) {
        *w = mFileImage.width;
        *h = mFileImage.height;
        *fourcc = mFileImage.format;

        LOG1("width: %d, height: %d, format: %x, size: %d, bayer_order: %d",
             mFileImage.width,
             mFileImage.height,
             mFileImage.format,
             mFileImage.size,
             mFileImage.bayer_order);

        return 0;
    }

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    v4l2_fmt.fmt.pix.width = *w;
    v4l2_fmt.fmt.pix.height = *h;
    v4l2_fmt.fmt.pix.pixelformat = *fourcc;
    v4l2_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    ret = ioctl(fd, VIDIOC_TRY_FMT, &v4l2_fmt);
    if (ret < 0) {
        LOGE("VIDIOC_TRY_FMT returned: %d (%s)", ret, strerror(errno));
        return -1;
    }

    *w = v4l2_fmt.fmt.pix.width;
    *h = v4l2_fmt.fmt.pix.height;
    *fourcc = v4l2_fmt.fmt.pix.pixelformat;

    return 0;
}

status_t AtomISP::getPreviewFrame(AtomBuffer *buff, atomisp_frame_status *frameStatus)
{
    LOG2("@%s", __FUNCTION__);
    struct v4l2_buffer buf;

    if (mMode == MODE_NONE)
        return INVALID_OPERATION;

    int index = grabFrame(mPreviewDevice, &buf);
    if(index < 0){
        LOGE("Error in grabbing frame!");
        return BAD_INDEX;
    }
    LOG2("Device: %d. Grabbed frame of size: %d", mPreviewDevice, buf.bytesused);
    mPreviewBuffers[index].id = index;
    mPreviewBuffers[index].ispPrivate = mSessionId;
    mPreviewBuffers[index].capture_timestamp = buf.timestamp;
    *buff = mPreviewBuffers[index];

    if (frameStatus)
        *frameStatus = (atomisp_frame_status)buf.reserved;

    mNumPreviewBuffersQueued--;

    return NO_ERROR;
}

status_t AtomISP::putPreviewFrame(AtomBuffer *buff)
{
    LOG2("@%s", __FUNCTION__);
    if (mMode == MODE_NONE)
        return INVALID_OPERATION;

    if (buff->ispPrivate != mSessionId)
        return DEAD_OBJECT;

    if (v4l2_capture_qbuf(video_fds[mPreviewDevice],
                      buff->id,
                      &v4l2_buf_pool[mPreviewDevice].bufs[buff->id]) < 0) {
        return UNKNOWN_ERROR;
    }

    mNumPreviewBuffersQueued++;

    return NO_ERROR;
}

status_t AtomISP::setRecordingBuffers(SharedBufferType *buffs, int numBuffs)
{
    LOG1("@%s: buffs = %p, numBuffs = %d", __FUNCTION__, buffs, numBuffs);
    if (buffs == NULL || numBuffs <= 0)
        return BAD_VALUE;

    mClientRecordingBuffers = new void*[numBuffs];
    if (mClientRecordingBuffers == NULL)
        return NO_MEMORY;

    for (int i = 0; i < numBuffs; i++)
        mClientRecordingBuffers[i] = (void *) buffs[i].pointer;

    mUsingClientRecordingBuffers = true;
    mNumBuffers = numBuffs;

    return NO_ERROR;
}

void AtomISP::unsetRecordingBuffers()
{
    LOG1("@%s", __FUNCTION__);
    delete [] mClientRecordingBuffers;
    mUsingClientRecordingBuffers = false;
    mNumBuffers = NUM_DEFAULT_BUFFERS;
}

status_t AtomISP::getRecordingFrame(AtomBuffer *buff, nsecs_t *timestamp)
{
    LOG2("@%s", __FUNCTION__);
    struct v4l2_buffer buf;

    if (mMode != MODE_VIDEO)
        return INVALID_OPERATION;

    int index = grabFrame(mRecordingDevice, &buf);
    LOG2("index = %d", index);
    if(index < 0) {
        LOGE("Error in grabbing frame!");
        return BAD_INDEX;
    }
    LOG2("Device: %d. Grabbed frame of size: %d", mRecordingDevice, buf.bytesused);
    mRecordingBuffers[index].id = index;
    mRecordingBuffers[index].ispPrivate = mSessionId;
    mRecordingBuffers[index].capture_timestamp = buf.timestamp;
    *buff = mRecordingBuffers[index];
    *timestamp = systemTime();

    mNumRecordingBuffersQueued--;

    return NO_ERROR;
}

status_t AtomISP::putRecordingFrame(AtomBuffer *buff)
{
    LOG2("@%s", __FUNCTION__);
    if (mMode != MODE_VIDEO)
        return INVALID_OPERATION;

    if (buff->ispPrivate != mSessionId)
        return DEAD_OBJECT;

    if (v4l2_capture_qbuf(video_fds[mRecordingDevice],
            buff->id,
            &v4l2_buf_pool[mRecordingDevice].bufs[buff->id]) < 0) {
        return UNKNOWN_ERROR;
    }

    mNumRecordingBuffersQueued++;

    return NO_ERROR;
}

status_t AtomISP::setSnapshotBuffers(void *buffs, int numBuffs)
{
    LOG1("@%s: buffs = %p, numBuffs = %d", __FUNCTION__, buffs, numBuffs);
    if (buffs == NULL || numBuffs <= 0)
        return BAD_VALUE;

    mClientSnapshotBuffers = (void**)buffs;
    mConfig.num_snapshot = numBuffs;
    mUsingClientSnapshotBuffers = true;
    for (int i = 0; i < numBuffs; i++) {
        LOG1("Snapshot buffer %d = %p", i, mClientSnapshotBuffers[i]);
    }

    return NO_ERROR;
}

status_t AtomISP::getSnapshot(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf)
{
    LOG1("@%s", __FUNCTION__);
    struct v4l2_buffer buf;
    int snapshotIndex, postviewIndex;

    if (mMode != MODE_CAPTURE)
        return INVALID_OPERATION;

    snapshotIndex = grabFrame(V4L2_FIRST_DEVICE, &buf);
    if (snapshotIndex < 0) {
        LOGE("Error in grabbing frame from 1'st device!");
        return BAD_INDEX;
    }
    LOG1("Device: %d. Grabbed frame of size: %d", V4L2_FIRST_DEVICE, buf.bytesused);
    mSnapshotBuffers[snapshotIndex].capture_timestamp = buf.timestamp;

    postviewIndex = grabFrame(V4L2_SECOND_DEVICE, &buf);
    if (postviewIndex < 0) {
        LOGE("Error in grabbing frame from 2'nd device!");
        // If we failed with the second device, return the frame to the first device
        v4l2_capture_qbuf(video_fds[V4L2_FIRST_DEVICE], snapshotIndex,
                &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[snapshotIndex]);
        return BAD_INDEX;
    }
    LOG1("Device: %d. Grabbed frame of size: %d", V4L2_SECOND_DEVICE, buf.bytesused);
    mPostviewBuffers[postviewIndex].capture_timestamp = buf.timestamp;

    if (snapshotIndex != postviewIndex ||
            snapshotIndex >= MAX_V4L2_BUFFERS) {
        LOGE("Indexes error! snapshotIndex = %d, postviewIndex = %d", snapshotIndex, postviewIndex);
        // Return the buffers back to driver
        v4l2_capture_qbuf(video_fds[V4L2_FIRST_DEVICE], snapshotIndex,
                &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[snapshotIndex]);
        v4l2_capture_qbuf(video_fds[V4L2_SECOND_DEVICE], postviewIndex,
                &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[postviewIndex]);
        return BAD_INDEX;
    }

    mSnapshotBuffers[snapshotIndex].id = snapshotIndex;
    mSnapshotBuffers[snapshotIndex].ispPrivate = mSessionId;
    *snapshotBuf = mSnapshotBuffers[snapshotIndex];
    snapshotBuf->width = mConfig.snapshot.width;
    snapshotBuf->height = mConfig.snapshot.height;
    snapshotBuf->format = mConfig.snapshot.format;
    snapshotBuf->size = mConfig.snapshot.size;

    mPostviewBuffers[postviewIndex].id = postviewIndex;
    mPostviewBuffers[postviewIndex].ispPrivate = mSessionId;
    *postviewBuf = mPostviewBuffers[postviewIndex];
    postviewBuf->width = mConfig.postview.width;
    postviewBuf->height = mConfig.postview.height;
    postviewBuf->format = mConfig.postview.format;
    postviewBuf->size = mConfig.postview.size;

    mNumCapturegBuffersQueued--;

    return NO_ERROR;
}

status_t AtomISP::putSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf)
{
    LOG1("@%s", __FUNCTION__);
    int ret0, ret1;

    if (mMode != MODE_CAPTURE)
        return INVALID_OPERATION;

    if (snaphotBuf->ispPrivate != mSessionId || postviewBuf->ispPrivate != mSessionId)
        return DEAD_OBJECT;

    ret0 = v4l2_capture_qbuf(video_fds[V4L2_FIRST_DEVICE], snaphotBuf->id,
                      &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[snaphotBuf->id]);

    ret1 = v4l2_capture_qbuf(video_fds[V4L2_SECOND_DEVICE], postviewBuf->id,
                      &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[postviewBuf->id]);
    if (ret0 < 0 || ret1 < 0)
        return UNKNOWN_ERROR;

    mNumCapturegBuffersQueued++;

    return NO_ERROR;
}

bool AtomISP::dataAvailable()
{
    LOG2("@%s", __FUNCTION__);

    // For video/recording, make sure isp has a preview and a recording buffer
    if (mMode == MODE_VIDEO)
        return mNumRecordingBuffersQueued > 0 && mNumPreviewBuffersQueued > 0;

    // For capture, just make sure isp has a capture buffer
    if (mMode == MODE_CAPTURE)
        return mNumCapturegBuffersQueued > 0;

    // For preview, just make sure isp has a preview buffer
    if (mMode == MODE_PREVIEW)
        return mNumPreviewBuffersQueued > 0;

    LOGE("Query for data in invalid mode");

    return false;
}

bool AtomISP::isBufferValid(const AtomBuffer* buffer) const
{
    return buffer->ispPrivate == this->mSessionId;
}

int AtomISP::grabFrame(int device, struct v4l2_buffer *buf)
{
    LOG2("@%s", __FUNCTION__);
    int ret;
    //Must start first
    if (main_fd < 0)
        return -1;

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LOGE("Wrong device %d", device);
        return -1;
    }

    ret = v4l2_capture_dqbuf(video_fds[device], buf);

    if (ret < 0)
        return ret;

    return buf->index;
}

int AtomISP::v4l2_capture_dqbuf(int fd, struct v4l2_buffer *buf)
{
    LOG2("@%s", __FUNCTION__);
    int ret;

    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_USERPTR;

    ret = ioctl(fd, VIDIOC_DQBUF, buf);

    if (ret < 0) {
        LOGE("error dequeuing buffers");
        return ret;
    }

    return buf->index;
}

////////////////////////////////////////////////////////////////////
//                          PRIVATE METHODS
////////////////////////////////////////////////////////////////////

status_t AtomISP::allocatePreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int allocatedBufs = 0;

    int size = frameSize(mConfig.preview.format,
            mConfig.preview.padding,
            mConfig.preview.height);

    mPreviewBuffers = new AtomBuffer[mNumBuffers];
    if (!mPreviewBuffers) {
        LOGE("Not enough mem for preview buffer array");
        status = NO_MEMORY;
        goto errorFree;
    }

    LOG1("Allocating %d buffers of size %d", mNumBuffers, size);
    for (int i = 0; i < mNumBuffers; i++) {
         mPreviewBuffers[i].buff = NULL;
         mCallbacks->allocateMemory(&mPreviewBuffers[i],  mConfig.preview.size);
         if (mPreviewBuffers[i].buff == NULL) {
             LOGE("Error allocation memory for preview buffers!");
             status = NO_MEMORY;
             goto errorFree;
         }
         allocatedBufs++;
         v4l2_buf_pool[mPreviewDevice].bufs[i].data = mPreviewBuffers[i].buff->data;
        mPreviewBuffers[i].shared = false;
    }
    return status;

errorFree:
    // On error, free the allocated buffers
    for (int i = 0 ; i < allocatedBufs; i++) {
        if (mRecordingBuffers[i].buff != NULL) {
            mRecordingBuffers[i].buff->release(mRecordingBuffers[i].buff);
            mRecordingBuffers[i].buff = NULL;
        }
    }
    if (mPreviewBuffers)
        delete [] mPreviewBuffers;
    return status;
}

status_t AtomISP::allocateRecordingBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int allocatedBufs = 0;
    int size;

    if (mUsingClientRecordingBuffers)
        size = sizeof(void *);
    else
        size = mConfig.recording.padding * mConfig.recording.height * 3 / 2;

    mRecordingBuffers = new AtomBuffer[mNumBuffers];
    if (!mRecordingBuffers) {
        LOGE("Not enough mem for recording buffer array");
        status = NO_MEMORY;
        goto errorFree;
    }

    for (int i = 0; i < mNumBuffers; i++) {
        mRecordingBuffers[i].buff = NULL;
        mCallbacks->allocateMemory(&mRecordingBuffers[i], size);
        LOG1("allocate recording buffer[%d] shared=%d, buff=%p size=%d",
                i, (int) mUsingClientRecordingBuffers,
                mRecordingBuffers[i].buff->data,
                mRecordingBuffers[i].buff->size);
        if (mRecordingBuffers[i].buff == NULL) {
            LOGE("Error allocation memory for recording buffers!");
            status = NO_MEMORY;
            goto errorFree;
        }
        allocatedBufs++;
        if (mUsingClientRecordingBuffers) {
            v4l2_buf_pool[mRecordingDevice].bufs[i].data = mClientRecordingBuffers[i];
            memcpy(mRecordingBuffers[i].buff->data, &mClientRecordingBuffers[i], sizeof(void *));
            mRecordingBuffers[i].shared = true;
        } else {
            v4l2_buf_pool[mRecordingDevice].bufs[i].data = mRecordingBuffers[i].buff->data;
            mRecordingBuffers[i].shared = false;
        }
    }
    return status;

errorFree:
    // On error, free the allocated buffers
    for (int i = 0 ; i < allocatedBufs; i++) {
        if (mRecordingBuffers[i].buff != NULL) {
            mRecordingBuffers[i].buff->release(mRecordingBuffers[i].buff);
            mRecordingBuffers[i].buff = NULL;
        }
    }
    if (mRecordingBuffers)
        delete [] mRecordingBuffers;
    return status;
}

status_t AtomISP::allocateSnapshotBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int allocatedSnaphotBufs = 0;
    int allocatedPostviewBufs = 0;
    int snapshotSize = mConfig.snapshot.size;

    if (mUsingClientSnapshotBuffers)
        snapshotSize = sizeof(void*);

    LOG1("Allocating %d buffers of size: %d (snapshot), %d (postview)",
            mConfig.num_snapshot,
            snapshotSize,
            mConfig.postview.size);
    for (int i = 0; i < mConfig.num_snapshot; i++) {
        mSnapshotBuffers[i].buff = NULL;
        mCallbacks->allocateMemory(&mSnapshotBuffers[i], snapshotSize);
        if (mSnapshotBuffers[i].buff == NULL) {
            LOGE("Error allocation memory for snapshot buffers!");
            status = NO_MEMORY;
            goto errorFree;
        }
        allocatedSnaphotBufs++;
        if (mUsingClientSnapshotBuffers) {
            v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[i].data = mClientSnapshotBuffers[i];
            memcpy(mSnapshotBuffers[i].buff->data, &mClientSnapshotBuffers[i], sizeof(void *));
            mSnapshotBuffers[i].shared = true;

        } else {
            v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[i].data = mSnapshotBuffers[i].buff->data;
            mSnapshotBuffers[i].shared = false;
        }

        mPostviewBuffers[i].buff = NULL;
        mCallbacks->allocateMemory(&mPostviewBuffers[i], mConfig.postview.size);
        if (mPostviewBuffers[i].buff == NULL) {
            LOGE("Error allocation memory for postview buffers!");
            status = NO_MEMORY;
            goto errorFree;
        }
        allocatedPostviewBufs++;
        v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[i].data = mPostviewBuffers[i].buff->data;
        mPostviewBuffers[i].shared = false;
    }
    return status;

errorFree:
    // On error, free the allocated buffers
    for (int i = 0 ; i < allocatedSnaphotBufs; i++) {
        if (mSnapshotBuffers[i].buff != NULL) {
            mSnapshotBuffers[i].buff->release(mSnapshotBuffers[i].buff);
            mSnapshotBuffers[i].buff = NULL;
        }
    }
    for (int i = 0 ; i < allocatedPostviewBufs; i++) {
        if (mPostviewBuffers[i].buff != NULL) {
            mPostviewBuffers[i].buff->release(mPostviewBuffers[i].buff);
            mPostviewBuffers[i].buff = NULL;
        }
    }
    return status;
}

status_t AtomISP::freePreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    for (int i = 0 ; i < mNumBuffers; i++) {
        if (mPreviewBuffers[i].buff != NULL) {
            mPreviewBuffers[i].buff->release(mPreviewBuffers[i].buff);
            mPreviewBuffers[i].buff = NULL;
        }
    }
    delete [] mPreviewBuffers;
    return NO_ERROR;
}

status_t AtomISP::freeRecordingBuffers()
{
    LOG1("@%s", __FUNCTION__);
    for (int i = 0 ; i < mNumBuffers; i++) {
        if (mRecordingBuffers[i].buff != NULL) {
            mRecordingBuffers[i].buff->release(mRecordingBuffers[i].buff);
            mRecordingBuffers[i].buff = NULL;
        }
    }
    delete [] mRecordingBuffers;
    return NO_ERROR;
}

status_t AtomISP::freeSnapshotBuffers()
{
    LOG1("@%s", __FUNCTION__);
    for (int i = 0 ; i < mConfig.num_snapshot; i++) {
        if (mSnapshotBuffers[i].buff != NULL) {
            mSnapshotBuffers[i].buff->release(mSnapshotBuffers[i].buff);
            mSnapshotBuffers[i].buff = NULL;
        }
        if (mPostviewBuffers[i].buff != NULL) {
            mPostviewBuffers[i].buff->release(mPostviewBuffers[i].buff);
            mPostviewBuffers[i].buff = NULL;
        }
    }
    return NO_ERROR;
}

int AtomISP::getNumberOfCameras()
{
    LOG1("@%s", __FUNCTION__);
    return sizeof(mCameraInfo)/sizeof(struct camera_info);
}

size_t AtomISP::setupCameraInfo()
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    size_t numCameras = 0;
    struct v4l2_input input;

    if (main_fd < 0)
        return numCameras;

    for (size_t i = 0; i < MAX_CAMERAS; i++) {
        memset(&input, 0, sizeof(input));
        input.index = i;
        ret = ioctl(main_fd, VIDIOC_ENUMINPUT, &input);
        if (ret < 0) {
            break;
        }
        camInfo[i].port = input.reserved[1];
        /*
         * Workaround for current libmfldadvci.so library which needs the sensor name
         * in init function. But that function looks only to the first word of the sensor name.
         */
        char *name = (char*)input.name;
        char *pos = strchr(name, ' ');
        if (pos > name) {
            name[pos - name] = '\0';
        }
        strncpy(camInfo[i].name, (const char *)input.name, MAX_SENSOR_NAME_LENGTH);
        LOG1("Detected sensor %s", camInfo[i].name);
        numCameras++;
    }
    return numCameras;
}

status_t AtomISP::getCameraInfo(int cameraId, camera_info *cameraInfo)
{
    LOG1("@%s: cameraId = %d", __FUNCTION__, cameraId);
    if (cameraId >= MAX_CAMERAS)
        return BAD_VALUE;

    memcpy(cameraInfo, &mCameraInfo[cameraId], sizeof(camera_info));
    return NO_ERROR;
}

} // namespace android
