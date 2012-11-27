/*
 * Copyright (c) 2012 Intel Corporation.
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
        mInstance = new PlatformCtpRedhookBay();

#elif   DMERR_VV
        mInstance = new PlatformSaltBay();

#else   // take defaults from MFLD_PR2 for all others now
        mInstance = new PlatformBlackbay();

#endif

    }

    return mInstance;
}

SensorType PlatformData::sensorType(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return SENSOR_TYPE_NONE;
    }
    return i->mCameras[cameraId].sensorType;
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

const char* PlatformData::supportedVideoSizes(void)
{
    PlatformBase *i = getInstance();
    return i->mSupportedVideoSizes;
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

bool PlatformData::supportsContinuousCapture(void)
{
    PlatformBase *i = getInstance();
    return i->mContinuousCapture;
}

bool PlatformData::renderPreviewViaOverlay(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return false;
    }
    return i->mCameras[cameraId].mPreviewViaOverlay;

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

const char* PlatformData::supportedBurstFPS(int cameraId)
{
    PlatformBase *i = getInstance();

    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return NULL;
    }

    return i->mCameras[cameraId].supportedBurstFPS;
}

const char* PlatformData::supportedBurstLength(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return NULL;
    }

    return i->mCameras[cameraId].supportedBurstLength;
}

int PlatformData::getMaxBurstFPS(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return -1;
    }
    return i->mCameras[cameraId].maxBurstFPS;
}

const char* PlatformData::supportedMaxEV(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].maxEV;
}

const char* PlatformData::supportedMinEV(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].minEV;
}

const char* PlatformData::supportedDefaultEV(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultEV;
}

const char* PlatformData::supportedStepEV(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].stepEV;
}

const char* PlatformData::supportedAeMetering(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedAeMetering;
}

const char* PlatformData::defaultAeMetering(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultAeMetering;
}

const char* PlatformData::supportedMaxSaturation(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].maxSaturation;
}

const char* PlatformData::supportedMinSaturation(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].minSaturation;
}

const char* PlatformData::supportedDefaultSaturation(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultSaturation;
}

const char* PlatformData::supportedStepSaturation(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].stepSaturation;
}

const char* PlatformData::supportedMaxContrast(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].maxContrast;
}

const char* PlatformData::supportedMinContrast(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].minContrast;
}

const char* PlatformData::supportedDefaultContrast(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultContrast;
}

const char* PlatformData::supportedStepContrast(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].stepContrast;
}

const char* PlatformData::supportedMaxSharpness(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].maxSharpness;
}

const char* PlatformData::supportedMinSharpness(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].minSharpness;
}

const char* PlatformData::supportedDefaultSharpness(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultSharpness;
}

const char* PlatformData::supportedStepSharpness(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].stepSharpness;
}

const char* PlatformData::supportedFlashModes(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedFlashModes;
}

const char* PlatformData::defaultFlashMode(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultFlashMode;
}

const char* PlatformData::supportedIso(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedIso;
}

const char* PlatformData::defaultIso(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultIso;
}

const char* PlatformData::supportedSceneModes(int cameraId)
{
    // TODO: Figure out a way to do product-specific configuration properly
    // This is not actually a HW platform restriction as such, but a product config.

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }


    return i->mCameras[cameraId].supportedSceneModes;
}

const char* PlatformData::defaultSceneMode(int cameraId)
{
    // TODO: Figure out a way to do product-specific configuration properly
    // This is not actually a HW platform restriction as such, but a product config.

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].defaultSceneMode;
}

const char* PlatformData::supportedEffectModes(int cameraId)
{
    // TODO: Figure out a way to do product-specific configuration properly
    // This is not actually a HW platform restriction as such, but a product config.

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].supportedEffectModes;
}

const char* PlatformData::supportedIntelEffectModes(int cameraId)
{
    // TODO: Figure out a way to do product-specific configuration properly
    // This is not actually a HW platform restriction as such, but a product config.

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].supportedIntelEffectModes;
}

const char* PlatformData::defaultEffectMode(int cameraId)
{
    // TODO: Figure out a way to do product-specific configuration properly
    // This is not actually a HW platform restriction as such, but a product config.

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].defaultEffectMode;
}

const char* PlatformData::supportedAwbModes(int cameraId)
{
    // TODO: Figure out a way to do product-specific configuration properly
    // This is not actually a HW platform restriction as such, but a product config.

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }


    return i->mCameras[cameraId].supportedAwbModes;
}

const char* PlatformData::defaultAwbMode(int cameraId)
{
    // TODO: Figure out a way to do product-specific configuration properly
    // This is not actually a HW platform restriction as such, but a product config.

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].defaultAwbMode;
}

const char* PlatformData::supportedPreviewFrameRate(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewFrameRate;
}

const char* PlatformData::supportedPreviewFPSRange(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewFPSRange;
}

const char* PlatformData::defaultPreviewFPSRange(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultPreviewFPSRange;
}

const char* PlatformData::supportedPreviewSize(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewSize;
}

const char* PlatformData::productName(void)
{
    PlatformBase *i = getInstance();
    return i->mProductName;
}

int PlatformData::getMaxPanoramaSnapshotCount()
{
    PlatformBase *i = getInstance();
    return i->mPanoramaMaxSnapshotCount;
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

int PlatformData::getMaxZoomFactor(void)
{
    PlatformBase *i = getInstance();
    return i->mMaxZoomFactor;
}

}; // namespace android
