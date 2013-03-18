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

/* config files for SONY 13M settings */
static const SensorParams imx135ParamFiles = {
    {
        "/etc/atomisp/Preview_UserParameter_imx135.prm",
        "/etc/atomisp/Video_UserParameter_imx135.prm",
        "/etc/atomisp/Primary_UserParameter_imx135.prm",
    },
    "/system/lib/libSh3aParamsimx135.so",
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

AiqConf PlatformData::AiqConfig;
HalConf PlatformData::HalConfig;

PlatformBase* PlatformData::getInstance(void)
{

    // Note: While these are build-time options at the moment, these
    //       could be runtime-detected in the future.

    if (mInstance == 0) {

#if   CLVT
        mInstance = new PlatformCtpRedhookBay();

#elif   MERR_VV
        mInstance = new PlatformSaltBay();

#elif   BODEGABAY
        mInstance = new PlatformBodegaBay();

#else   // take defaults from CloverTrail
        mInstance = new PlatformCtpRedhookBay();

#endif

    }

    return mInstance;
}

SensorType PlatformData::sensorType(int cameraId)
{
    bool boolean;
    if (!HalConfig.getBool(boolean, CPF::NeedsIspB)) {
        if (CPF::NeedsIspB) {
            return SENSOR_TYPE_RAW;
        } else {
            return SENSOR_TYPE_SOC;
        }
    }

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
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::PreviewSizeVideoDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    return i->mVideoPreviewSizePref;
}

const char* PlatformData::supportedVideoSizes(void)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::VideoSizesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    return i->mSupportedVideoSizes;
}

void PlatformData::maxSnapshotSize(int cameraId, int* width, int* height)
{
    if (!HalConfig.getValue(*width, CPF::SizeActiveT, CPF::tag_width)
        && !HalConfig.getValue(*height, CPF::SizeActiveT, CPF::tag_height)) {
        return;
    }

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
    bool boolean;
    if (!HalConfig.getBool(boolean, CPF::HasFlashB)) {
        return boolean;
    }

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

int PlatformData::maxContinuousRawRingBufferSize(void)
{
    PlatformBase *i = getInstance();
    if (PlatformData::supportsContinuousCapture() == false)
        return 0;

    return i->mMaxContinuousRawRingBuffer;
}

int PlatformData::shutterLagCompensationMs(void)
{
    PlatformBase *i = getInstance();
    return i->mShutterLagCompensationMs;
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

unsigned int PlatformData::maxPreviewPixelCountForVFPP(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return 0xFFFFFFFF;
    }
    return i->mCameras[cameraId].maxPreviewPixelCountForVFPP;
}

int PlatformData::overlayRotation(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return false;
    }
    return i->mCameras[cameraId].overlayRelativeRotation;

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
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::EvMaxS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].maxEV;
}

const char* PlatformData::supportedMinEV(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::EvMinS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].minEV;
}

const char* PlatformData::supportedDefaultEV(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::EvDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultEV;
}

const char* PlatformData::supportedStepEV(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::EvStepS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].stepEV;
}

const char* PlatformData::supportedAeMetering(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::AeModesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedAeMetering;
}

const char* PlatformData::defaultAeMetering(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::AeModeDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultAeMetering;
}

const char* PlatformData::supportedMaxSaturation(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SaturationMaxS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].maxSaturation;
}

const char* PlatformData::supportedMinSaturation(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SaturationMinS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].minSaturation;
}

const char* PlatformData::defaultSaturation(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SaturationDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultSaturation;
}

const char* PlatformData::supportedSaturation(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SaturationsS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedSaturation;
}

const char* PlatformData::supportedStepSaturation(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SaturationStepS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].stepSaturation;
}

const char* PlatformData::supportedMaxContrast(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::ContrastMaxS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].maxContrast;
}

const char* PlatformData::supportedMinContrast(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::ContrastMinS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].minContrast;
}

const char* PlatformData::defaultContrast(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::ContrastDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultContrast;
}

const char* PlatformData::supportedContrast(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::ContrastsS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedContrast;
}

const char* PlatformData::supportedStepContrast(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::ContrastStepS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].stepContrast;
}

const char* PlatformData::supportedMaxSharpness(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SharpnessMaxS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].maxSharpness;
}

const char* PlatformData::supportedMinSharpness(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SharpnessMinS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].minSharpness;
}

const char* PlatformData::defaultSharpness(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SharpnessDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultSharpness;
}

const char* PlatformData::supportedSharpness(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SharpnessesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedSharpness;
}

const char* PlatformData::supportedStepSharpness(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SharpnessStepS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].stepSharpness;
}

const char* PlatformData::supportedFlashModes(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::FlashModesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedFlashModes;
}

const char* PlatformData::defaultFlashMode(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::FlashModeDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultFlashMode;
}

const char* PlatformData::supportedIso(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::IsoModesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedIso;
}

const char* PlatformData::defaultIso(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::IsoModeDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultIso;
}

const char* PlatformData::supportedSceneModes(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SceneModesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }


    return i->mCameras[cameraId].supportedSceneModes;
}

const char* PlatformData::defaultSceneMode(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::SceneModeDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].defaultSceneMode;
}

const char* PlatformData::supportedEffectModes(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::EffectModesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].supportedEffectModes;
}

const char* PlatformData::supportedIntelEffectModes(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::ExtendedEffectModesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].supportedIntelEffectModes;
}

const char* PlatformData::defaultEffectMode(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::EffectModeDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].defaultEffectMode;
}

const char* PlatformData::supportedAwbModes(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::AwbModesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }


    return i->mCameras[cameraId].supportedAwbModes;
}

const char* PlatformData::defaultAwbMode(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::AwbModeDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }

    return i->mCameras[cameraId].defaultAwbMode;
}

const char* PlatformData::supportedPreviewFrameRate(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::PreviewFpssS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewFrameRate;
}

const char* PlatformData::supportedPreviewFPSRange(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::PreviewFpsRangesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewFPSRange;
}

const char* PlatformData::defaultPreviewFPSRange(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::PreviewFpsRangeDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultPreviewFPSRange;
}

const char* PlatformData::supportedPreviewSize(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::PreviewSizesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewSize;
}

bool PlatformData::supportsSlowMotion(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return false;
    }
    return i->mCameras[cameraId].hasSlowMotion;
}

const char* PlatformData::supportedFocusModes(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::FocusModesS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedFocusModes;
}

const char* PlatformData::defaultFocusMode(int cameraId)
{
    const char *sPtr;
    if (!HalConfig.getString(sPtr, CPF::FocusModeDefaultS)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultFocusMode;
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
    } else if (strstr(sensorId, "imx135") || strstr(sensorId, "imx175")) {
        sensorParameters = &imx135ParamFiles;
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
    int value;
    if (!HalConfig.getValue(value, CPF::ZoomMax)) {
        return value;
    }

    PlatformBase *i = getInstance();
    return i->mMaxZoomFactor;
}

bool PlatformData::supportVideoSnapshot(void)
{
    PlatformBase *i = getInstance();
    return i->mSupportVideoSnapshot;
}

int PlatformData::getRecordingBufNum(void)
{
    PlatformBase *i = getInstance();
    return i->mNumRecordingBuffers;
}

bool PlatformData::supportAIQ(void)
{
    PlatformBase *i = getInstance();
    return i->mSupportAIQ;
}

}; // namespace android
