/*
 * Copyright (C) 2012 The Android Open Source Project
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
#ifndef PLATFORMDATA_H_
#define PLATFORMDATA_H_

#include <utils/String8.h>
#include <utils/Vector.h>

#define RESOLUTION_14MP_WIDTH   4352
#define RESOLUTION_14MP_HEIGHT  3264
#define RESOLUTION_8MP_WIDTH    3264
#define RESOLUTION_8MP_HEIGHT   2448
#define RESOLUTION_5MP_WIDTH    2560
#define RESOLUTION_5MP_HEIGHT   1920
#define RESOLUTION_1_3MP_WIDTH    1280
#define RESOLUTION_1_3MP_HEIGHT   960
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

#include <camera.h>
#include "AtomCommon.h"

namespace android {

/**
 * \file PlatformData.h
 *
 * HAL internal interface for managing platform specific static
 * data.
 *
 * Design principles for platform data mechanism:
 *
 * 1. Make it easy as possible to add new configurable data.
 *
 * 2. Make it easy as possible to add new platforms.
 *
 * 3. Allow inheriting platforms from one another (as we'll typically
 *    have many derived platforms).
 *
 * 4. Split implementations into separate files, to avoid
 *    version conflicts with parallel work targeting different
 *    platforms.
 *
 * 5. Focus on plain flat data and avoid defining new abstractions
 *    and relations.
 *
 * 6. If any #ifdefs are needed, put them in platform files.
 *
 * 7. Keep the set of parameters to a minimum, and only add
 *    data that really varies from platform to another.
 */

// Forward declarations
class PlatformData;

/**
 * \class PlatformBase
 *
 * Base class for defining static platform features and
 * related configuration data that is needed by rest of the HAL.
 *
 * Each platform will extend this class.
 */
class PlatformBase {

    friend class PlatformData;

 protected:

    /**
     * Camera feature info that is specific to camera id
     */
    class CameraInfo {
    public:
       int facing;
       int orientation;
       int flipping;
       bool dvs;
       int maxSnapshotWidth;
       int maxSnapshotHeight;
    };

    // note: Android NDK does not yet support C++11 and
    //       initializer lists, so avoiding structs for now
    //       in these definitions (2012/May)

    Vector<CameraInfo> mCameras;

    bool mBackFlash;
    bool mFileInject;

    const char* mVideoPreviewSizePref;

    /* For EXIF Metadata */
    const char* mProductName;
    const char* mManufacturerName;
};

/**
 * \class PlatformData
 *
 * This class is a singleton that contains all the static information
 * from the platform. It doesn't store any state. It is a data repository
 * for static data. It provides convenience methods to initialize
 * some parameters based on the HW limitations.
 */

class PlatformData {

 private:
    static PlatformBase* mInstance;

    /**
     * Get access to the platform singleton.
     *
     * Note: this is implemented in PlatformFactory.cpp
     */
    static PlatformBase* getInstance(void);

 public:

    enum SensorFlip {
        SENSOR_FLIP_NA     = -1,   // Support Not-Available
        SENSOR_FLIP_OFF    = 0x00, // Both flip ctrls set to 0
        SENSOR_FLIP_H      = 0x01, // V4L2_CID_HFLIP 1
        SENSOR_FLIP_V      = 0x02, // V4L2_CID_VFLIP 1
    };

    /**
     * Number of cameras
     *
     * Returns number of cameras that may be opened with
     * android.hardware.Camera.open().
     *
     * \return a non-negative integer
     */
    static int numberOfCameras(void);

    /**
     * Facing of camera id
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return int value as defined in Camera.CameraInfo
     */
    static int cameraFacing(int cameraId);

    /**
     * Whether the back camera has flash?
     *
     * \return true if flash is available
     */
    static bool supportsBackFlash(void);

    /**
     * Whether image data injection from file is supported?
     *
     * \return true if supported
     */
    static bool supportsFileInject(void);

    /**
     * Orientation of camera id
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return int value as defined in Camera.CameraInfo
     */
    static int cameraOrientation(int cameraId);

    /**
     * Returns string described preferred preview size for video
     *
     * \return string following getParameter value notation
     */
    static const char* preferredPreviewSizeForVideo(void);

    /**
     * Returns (via out params) maximal supported snapshot size
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \param pointer to variable to receive max width
     * \param pointer to variable to receive max height
     */
    static void maxSnapshotSize(int cameraId, int* width, int* height);

    /**
     * Whether the camera supports Digital Video Stabilization or not
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return true if it is supported
     */
    static bool supportsDVS(int cameraId);

    /**
     * Returns the supported scene modes for the platform
     * \return Supported scene mode string, or empty string (String8::isEmpty() == true)
     *  upon error.
     */
    static String8 supportedSceneModes();

    /**
     * Flipping controls to set for camera id
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return int value as defined in PlatformData::SENSOR_FLIP_FLAGS
     */
    static int sensorFlipping(int cameraId);

    /**
     * Returns the name of the product
     * This is meant to be used in the EXIF metadata
     *
     * \return string with product name
     */
    static const char* productName(void);

    /**
     * Returns the name of the manufacturer
     * This is meant to be used in the EXIF metadata
     *
     * \return string with product manufacturer
     */
    static const char* manufacturerName(void);

    /**
     * Returns sensor parameter files for input sensor
     *
     * \param sensor_id identifier to sensor
     * \return pointer to sensor parameter file
    */
    static const SensorParams *getSensorParamsFile(char *sensorId);

};

} /* namespace android */
#endif /* PLATFORMDATA_H_ */
