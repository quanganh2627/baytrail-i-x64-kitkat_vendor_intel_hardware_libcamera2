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

#include <stdio.h>
#include "PlatformData.h"
#include <camera.h>

#include "LogHelper.h"

/**
 * \file PlatformClovertrail.h
 *
 * Platform data for Intel Clovertrail based products
 */

namespace android {

#define CTP_PLATFORM_ID            2
#define CTP_HARDWARE_ID_FIRST_B0   0xc

/**
 * Check using SPID information whether the device is recent
 * enough revision to support continuous capture (older revisions
 * have issues with memory access delays).
 *
 * This check is only applicable to Intel RHB CTP FFRD and
 * cannot be used as a generic capability check.
 */
static bool deviceOnContinuousCaptureBlackList()
{
    unsigned int pid = 0, hid = 0;
    FILE *f1 = fopen("/sys/spid/hardware_id", "rb");
    if (f1) {
        fscanf(f1,  "%04X", &hid);
        LOGD("SPID hardware_id %04X", hid);
        fclose(f1);
    }

    f1 = fopen("/sys/spid/platform_family_id", "rb");
    if (f1) {
        fscanf(f1, "%04X", &pid);
        LOGD("SPID platform_family_id %04X", pid);
        fclose(f1);
    }

    // Blacklist CLV+ A0 devices
    if (pid == CTP_PLATFORM_ID && hid < CTP_HARDWARE_ID_FIRST_B0)
        return true;

    return false;
}

/**
 * Platform data for RedhookBay (clovertrail based)
 */
class PlatformCtpRedhookBay : public PlatformBase {

public:
    PlatformCtpRedhookBay(void) {
        mSubDevName = "/dev/v4l-subdev8";

        /* Creating CameraInfo object (default constructor value applied)
         * HERE we only modify the value which are different than
         * in the constructor.
         * non-present value are taking constructor values.
         * See Default values in PlatformData.h
         * same applies later in the code for front camera.
         */
        CameraInfo *pcam = new CameraInfo;
        if (!pcam) {
            LOGE("Cannot create CameraInfo!");
            return;
        }
        // back camera settings
        pcam->flipping = PlatformData::SENSOR_FLIP_NA;
        //EV
        pcam->maxEV = "6";
        pcam->minEV = "-6";
        pcam->mPreviewViaOverlay = true;
        pcam->overlayRelativeRotation = 0;

        // If the back flash is supported, let's add the rest of the
        // supported scene modes that require flash:
        snprintf(pcam->supportedSceneModes
            ,sizeof(pcam->supportedSceneModes)
            ,"%s,%s"
            ,pcam->supportedSceneModes
            ,CameraParameters::SCENE_MODE_NIGHT_PORTRAIT);
        // Preview size
        pcam->supportedPreviewSize = "1024x576,800x600,720x480,640x480,640x360,352x288,320x240,176x144";
        mCameras.push(*pcam);
        delete pcam;

        // New CameraInfo for front camera see comment for back camera above
        pcam = new CameraInfo;
        if (!pcam) {
            LOGE("Cannot create CameraInfo!");
            return;
        }
        // front camera settings
        pcam->sensorType = SENSOR_TYPE_SOC;
        pcam->facing = CAMERA_FACING_FRONT;
        pcam->orientation = 270;
        pcam->dvs = false;
        pcam->mPreviewViaOverlay = true;
        pcam->overlayRelativeRotation = 0;
        pcam->flipping = PlatformData::SENSOR_FLIP_NA;
        pcam->maxSnapshotWidth = RESOLUTION_1_3MP_WIDTH;
        pcam->maxSnapshotHeight = RESOLUTION_1_3MP_HEIGHT;
        pcam->supportedBurstLength = "";
        pcam->maxEV = "";
        pcam->minEV = "";
        pcam->stepEV = "";
        pcam->defaultEV = "";
        strcpy(pcam->supportedFlashModes,"");
        pcam->supportedIso = "";
        strcpy(pcam->supportedSceneModes,"");
        strcpy(pcam->supportedEffectModes,"");
        strcpy(pcam->supportedIntelEffectModes,"");
        strcpy(pcam->supportedAwbModes,"");
        pcam->supportedAeMetering = "";
        pcam->supportedPreviewSize = "1024x576,720x480,640x480,640x360,352x288,320x240,176x144";
        mCameras.push(*pcam);
        delete pcam;

        // file inject device
        mCameras.push(mCameras[0]);
        mFileInject = true;

        // generic parameters
        mBackFlash = true;
        mVideoPreviewSizePref = "1024x576";

        mProductName = "ExampleModel";
        mManufacturerName = "ExampleMaker";

        mContinuousCapture = (deviceOnContinuousCaptureBlackList() == false);
        mMaxZoomFactor = 64;

    }
};
}; // namespace android
