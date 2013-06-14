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
#include "CameraProfiles.h"
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

static const int spIdLength = 4;

PlatformBase* PlatformData::mInstance = 0;
int PlatformData::mActiveCameraId = -1;

AiqConf PlatformData::AiqConfig;
HalConf PlatformData::HalConfig;

PlatformBase* PlatformData::getInstance(void)
{
    if (mInstance == 0) {
        mInstance = new CameraProfiles();

        // add an extra camera which is copied from the first one as a fake camera
        // for file injection
        mInstance->mCameras.push(mInstance->mCameras[0]);
        mInstance->mFileInject = true;
    }

    return mInstance;
}

status_t PlatformData::readSpId(String8& spIdName, int& spIdValue)
{
        FILE *file;
        status_t ret = OK;
        String8 sysfsSpIdPath = String8("/sys/spid/");
        String8 fullPath;

        fullPath = sysfsSpIdPath;
        fullPath.append(spIdName);

        file = fopen(fullPath, "rb");
        if (!file) {
            LOGE("ERROR in opening file %s", fullPath.string());
            return NAME_NOT_FOUND;
        }
        ret = fscanf(file, "%x", &spIdValue);
        if (ret < 0) {
            LOGE("ERROR in reading %s", fullPath.string());
            spIdValue = 0;
            fclose(file);
            return UNKNOWN_ERROR;
        }
        fclose(file);
        return ret;
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

void PlatformData::setActiveCameraId(int cameraId)
{
    // Multiple active cameras not supported
    if ((mActiveCameraId >= 0) || (cameraId < 0)) {
      LOGE("%s: Activating multiple cameras (was %d, now trying %d)", __FUNCTION__, mActiveCameraId, cameraId);
    }
    mActiveCameraId = cameraId;
}

void PlatformData::freeActiveCameraId(int cameraId)
{
    // Multiple active cameras not supported
    if ((mActiveCameraId != cameraId) || (cameraId < 0)) {
      LOGE("%s: Freeing a wrong camera (was %d, now trying %d)", __FUNCTION__, mActiveCameraId, cameraId);
    }
    mActiveCameraId = -1;
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
    if (!HalConfig.getString(sPtr, CPF::PreviewSizeVideoDefault)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    return i->mVideoPreviewSizePref;
}

const char* PlatformData::supportedVideoSizes(int cameraId)
{
    const char *sPtr;
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::VideoSizes)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return NULL;
    }
    return i->mCameras[cameraId].supportedVideoSizes;
}

const char* PlatformData::supportedSnapshotSizes(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return NULL;
    }
    return i->mCameras[cameraId].supportedSnapshotSizes;
}

bool PlatformData::supportsBackFlash(void)
{
    bool boolean;
    if (!HalConfig.getBool(boolean, CPF::HasFlash)) {
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

bool PlatformData::supportsContinuousCapture(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return false;
    }
    return i->mCameras[cameraId].continuousCapture;
}

int PlatformData::maxContinuousRawRingBufferSize(int cameraId)
{
    PlatformBase *i = getInstance();
    if (PlatformData::supportsContinuousCapture(cameraId) == false)
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

bool PlatformData::resolutionSupportedByVFPP(int cameraId,
        int width, int height)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return true;
    }

    Vector<Size>::const_iterator it = i->mCameras[cameraId].mVFPPLimitedResolutions.begin();
    for (;it != i->mCameras[cameraId].mVFPPLimitedResolutions.end(); ++it) {
        if (it->width == width && it->height == height) {
            return false;
        }
    }
    return true;
}

bool PlatformData::snapshotResolutionSupportedByZSL(int cameraId,
        int width, int height)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return true;
    }

    Vector<Size>::const_iterator it = i->mCameras[cameraId].mZSLUnsupportedSnapshotResolutions.begin();
    for (;it != i->mCameras[cameraId].mZSLUnsupportedSnapshotResolutions.end(); ++it) {
        if (it->width == width && it->height == height) {
            return false;
        }
    }
    return true;
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

bool PlatformData::supportEV(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return false;
    }

    const char* minEV = i->mCameras[cameraId].minEV;
    const char* maxEV = i->mCameras[cameraId].maxEV;
    if(!strcmp(minEV, "0") && !strcmp(maxEV, "0")) {
        LOG1("@%s: not supported by current camera", __FUNCTION__);
        return false;
    }
    return true;
}

const char* PlatformData::supportedMaxEV(int cameraId)
{
    const char *sPtr;
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::EvMax)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::EvMin)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::EvDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::EvStep)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::AeModes)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::AeModeDefault)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultAeMetering;
}

const char* PlatformData::supportedAeLock(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return NULL;
    }
    return i->mCameras[cameraId].supportedAeLock;
}

const char* PlatformData::supportedMaxSaturation(int cameraId)
{
    const char *sPtr;
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SaturationMax)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SaturationMin)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SaturationDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::Saturations)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SaturationStep)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::ContrastMax)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::ContrastMin)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::ContrastDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::Contrasts)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::ContrastStep)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SharpnessMax)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SharpnessMin)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SharpnessDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::Sharpnesses)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SharpnessStep)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::FlashModes)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::FlashModeDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::IsoModes)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::IsoModeDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SceneModes)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::SceneModeDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::EffectModes)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::ExtendedEffectModes)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::EffectModeDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::AwbModes)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::AwbModeDefault)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::PreviewFpss)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewFrameRate;
}

const char* PlatformData::supportedAwbLock(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return NULL;
    }
    return i->mCameras[cameraId].supportedAwbLock;
}

const char* PlatformData::supportedPreviewFPSRange(int cameraId)
{
    const char *sPtr;
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::PreviewFpsRanges)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::PreviewFpsRangeDefault)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultPreviewFPSRange;
}

const char* PlatformData::supportedPreviewSizes(int cameraId)
{
    const char *sPtr;
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::PreviewSizes)) {
        return sPtr;
    }

    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewSizes;
}

const char* PlatformData::supportedPreviewUpdateModes(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].supportedPreviewUpdateModes;
}

const char* PlatformData::defaultPreviewUpdateMode(int cameraId)
{
    PlatformBase *i = getInstance();
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCameras.size())) {
      LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
      return "";
    }
    return i->mCameras[cameraId].defaultPreviewUpdateMode;
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::FocusModes)) {
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
    if (cameraId == mActiveCameraId && !HalConfig.getString(sPtr, CPF::FocusModeDefault)) {
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
    if (!HalConfig.getValue(value, CPF::ZoomDigital, CPF::Max)) {
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

int PlatformData::getPreviewFormat(void)
{
    PlatformBase *i = getInstance();
    return i->mPreviewFormat;
}

int PlatformData::getGFXHALPixelFormat(void)
{
    PlatformBase *i = getInstance();
    return i->mHALPixelFormat;
}

const char* PlatformData::getBoardName(void)
{
    PlatformBase *i = getInstance();
    return i->mBoardName;
}

status_t PlatformData::createVendorPlatformProductName(String8& name)
{
    int vendorIdValue;
    int platformFamilyIdValue;
    int productLineIdValue;

    name = "";

    String8 vendorIdName = String8("vendor_id");
    String8 platformFamilyIdName = String8("platform_family_id");
    String8 productLineIdName = String8("product_line_id");

    if (readSpId(vendorIdName, vendorIdValue) < 0) {
        LOGE("%s could not be read from sysfs", vendorIdName.string());
        return UNKNOWN_ERROR;
    }
    if (readSpId(platformFamilyIdName, platformFamilyIdValue) < 0) {
        LOGE("%s could not be read from sysfs", platformFamilyIdName.string());
        return UNKNOWN_ERROR;
    }
    if (readSpId(productLineIdName, productLineIdValue) < 0){
        LOGE("%s could not be read from sysfs", productLineIdName.string());
        return UNKNOWN_ERROR;
    }

    char vendorIdValueStr[spIdLength];
    char platformFamilyIdValueStr[spIdLength];
    char productLineIdValueStr[spIdLength];

    snprintf(vendorIdValueStr, spIdLength, "%#x", vendorIdValue);
    snprintf(platformFamilyIdValueStr, spIdLength, "%#x", platformFamilyIdValue);
    snprintf(productLineIdValueStr, spIdLength, "%#x", productLineIdValue);

    name = vendorIdValueStr;
    name += String8("-");
    name += platformFamilyIdValueStr;
    name += String8("-");
    name += productLineIdValueStr;

    return OK;
}

}; // namespace android
