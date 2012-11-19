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

/* config files for DIS14 and default settings */
static const SensorParams dis14mParameters ={
    {
        "/etc/atomisp/Preview_UserParameter_DIS14M.prm",
        "/etc/atomisp/Video_UserParameter_DIS14M.prm",
        "/etc/atomisp/Primary_UserParameter_DIS14M.prm",
    },
    "/system/lib/libSh3aParamsDIS14M.so",
    ci_adv_load_camera_4,
    {
      NULL,
      0,
    },
    false
};

/* config files for Liteon 8M settings */
static const SensorParams liteon8mParamFiles = {
    {
        "/etc/atomisp/Preview_UserParameter_LiteOn8M.prm",
        "/etc/atomisp/Video_UserParameter_LiteOn8M.prm",
        "/etc/atomisp/Primary_UserParameter_LiteOn8M.prm",
    },
    "/system/lib/libSh3aParamsLiteOn8M.so",
    ci_adv_load_camera_2,
    {
      NULL,
      0,
    },
    false
};

/* config files for Abico FI86A086 settings */
static const SensorParams abicoFi86a086Parameters = {
    {
        "/etc/atomisp/Preview_UserParameter_AbicoFI86A086.prm",
        "/etc/atomisp/Video_UserParameter_AbicoFI86A086.prm",
        "/etc/atomisp/Primary_UserParameter_AbicoFI86A086.prm",
    },
    "/system/lib/libSh3aParamsAbicoFI86A086.so",
    ci_adv_load_camera_3,
    {
      NULL,
      0,
    },
    false
};

/* config files for Semco lc898211 settings */
static const SensorParams semcoLc898211Parameters = {
    {
        "/etc/atomisp/Preview_UserParameter_SemcoLc898211.prm",
        "/etc/atomisp/Video_UserParameter_SemcoLc898211.prm",
        "/etc/atomisp/Primary_UserParameter_SemcoLc898211.prm",
    },
    "/system/lib/libSh3aParamsSemcoLc898211.so",
    ci_adv_load_camera_1,
    {
      NULL,
      0,
    },
    true
};

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

#elif   CLVT
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

void PlatformData::maxSnapshotSize(int cameraId, int* width, int* height)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return;
    }

    *width = i->mCameras[cameraId].maxSnapshotWidth;
    *height = i->mCameras[cameraId].maxSnapshotHeight;
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

const char* PlatformData::supportedBurstFPS(void)
{
    PlatformBase *i = getInstance();
    return i->mSupportedBurstFPS;
}

const char* PlatformData::supportedBurstLength(void)
{
    PlatformBase *i = getInstance();
    return i->mSupportedBurstLength;
}

int PlatformData::getMaxBurstFPS(void)
{
    PlatformBase *i = getInstance();
    return i->mMaxBurstFPS;
}

const char* PlatformData::productName(void)
{
    PlatformBase *i = getInstance();
    return i->mProductName;
}

const char* PlatformData::manufacturerName(void)
{
    PlatformBase *i = getInstance();
    return i->mManufacturerName;
}

//TODO: needs to be extended so that derived platforms can set the sensor
//param file
const SensorParams *PlatformData::getSensorParamsFile(char *sensorId)
{
    const SensorParams *sensorParameters = NULL;

    if (strstr(sensorId, "mt9e013")) {
        if (strstr(sensorId, "lc898211")) {
            sensorParameters = &semcoLc898211Parameters;
        } else {
            sensorParameters = &liteon8mParamFiles;
        }
    } else if (strstr(sensorId, "ov8830")) {
        sensorParameters = &abicoFi86a086Parameters;
    } else if (strstr(sensorId, "dis71430m")) {
        sensorParameters = &dis14mParameters;
    }

    return sensorParameters;
}

const char* PlatformData::getISPSubDeviceName(void)
{
    PlatformBase *i = getInstance();
    return i->mSubDevName;
}
}; // namespace android
