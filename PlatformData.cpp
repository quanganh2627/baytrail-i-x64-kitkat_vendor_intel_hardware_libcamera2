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

//#define LOG_NDEBUG 0
#define LOG_TAG "Camera_PlatformData"
#include "LogHelper.h"

#include <assert.h>
#include <camera.h>
#include <camera/CameraParameters.h>
#include "PlatformData.h"
#include "PlatformMedfield.h"
#include "PlatformClovertrail.h"
#include "PlatformMerrifield.h"
#include <utils/Log.h>
namespace android {

PlatformBase* PlatformData::mInstance = 0;

PlatformBase* PlatformData::getInstance(void)
{

    // Note: While these are build-time options at the moment, these
    //       could be runtime-detected in the future.

    if (mInstance == 0) {

#if     MFLD_DV10
        mInstance = new PlatformRedridge();

#elif   MFLD_GI
        mInstance = new PlatformLexington();

#elif   defined(CTP_PR0) || defined(CTP_PR1) || defined(CTP_NOMODEM)
        mInstance = new PlatformCtp();


#else   // take defaults from MFLD_PR2 for all others now
        mInstance = new PlatformBlackbay();

#endif

    }

    return mInstance;
}

int PlatformData::cameraFacing(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return -1;
    }
    return i->mCameras[cameraId].facing;
}

int PlatformData::cameraOrientation(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return -1;
    }
    return i->mCameras[cameraId].orientation;
}

int PlatformData::sensorFlipping(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return -1;
    }
    return i->mCameras[cameraId].flipping;
}


int PlatformData::numberOfCameras(void)
{
    PlatformBase *i = getInstance();
    int res = i->mCameras.size();
    return res;
}

const char* PlatformData::preferredPreviewSizeForVideo(void)
{
    PlatformBase *i = getInstance();
    return i->mVideoPreviewSizePref;
}

bool PlatformData::supportsBackFlash(void)
{
    PlatformBase *i = getInstance();
    return i->mBackFlash;
}

bool PlatformData::supportsFileInject(void)
{
    PlatformBase *i = getInstance();
    return i->mFileInject;
}

bool PlatformData::supportsDVS(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return false;
    }
    return i->mCameras[cameraId].dvs;
}

String8 PlatformData::supportedSceneModes()
{
    // TODO: Figure out a way to do product-specific configuration properly
    // This is not actually a HW platform restriction as such, but a product config.

    PlatformBase *i = getInstance();
    int status = 0;
    char modes[100];

    // This is the basic set of scene modes, supported on all
    // platforms:
    status = snprintf(modes, sizeof(modes)
            ,"%s,%s,%s,%s,%s,%s,%s"
            ,CameraParameters::SCENE_MODE_AUTO
            ,CameraParameters::SCENE_MODE_PORTRAIT
            ,CameraParameters::SCENE_MODE_SPORTS
            ,CameraParameters::SCENE_MODE_LANDSCAPE
            ,CameraParameters::SCENE_MODE_NIGHT
            ,CameraParameters::SCENE_MODE_FIREWORKS
            ,CameraParameters::SCENE_MODE_BARCODE);
    if (status < 0 || static_cast<size_t>(status) >= sizeof(modes)) {
        LOGE("Could not generate scene mode string. status = %d, error: %s",
             status, strerror(errno));
        return String8::empty();
    }

    // Generally the flash is supported, so let's add the rest of the
    // supported scene modes that require flash:
    if (i->mBackFlash) {
        status = snprintf(modes, sizeof(modes)
                ,"%s,%s"
                ,modes
                ,CameraParameters::SCENE_MODE_NIGHT_PORTRAIT);
        if (status < 0 || static_cast<size_t>(status) >= sizeof(modes)) {
            LOGE("Could not generate scene mode string. status = %d, error: %s",
                 status, strerror(errno));
            return String8::empty();
        }
    }

    return String8(modes);
}

}; // namespace android
