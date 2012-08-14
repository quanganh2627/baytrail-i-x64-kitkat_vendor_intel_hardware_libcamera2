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

    // note: Android NDK does not yet support C++11 and
    //       initializer lists, so avoiding structs for now
    //       in these definitions (2012/May)

    int mBackRotation;
    int mFrontRotation;

    bool mBackFlash;
    bool mFileInject;

    const char* mVideoPreviewSizePref;

    bool mBackDVS;
    bool mFrontDVS;
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
};

} /* namespace android */
#endif /* PLATFORMDATA_H_ */
