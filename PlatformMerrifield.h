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

#include "PlatformData.h"
#include <camera.h>

/**
 * \file PlatformMerrifield.h
 *
 * Platform data for Intel Merrifield based products
 */

namespace android {

class PlatformSaltBay : public PlatformBase {

public:
    PlatformSaltBay(void) {
        mSubDevName = "/dev/v4l-subdev8";

        /* Creating CameraInfo object (default constructor value applied)
         * HERE we only modify the value which are different than
         * in the constructor.
         * non-present value are taking constructor values.
         * See Default values in PlatformData.h
         * same in the code for front camera.
         */
        CameraInfo *pcam = new CameraInfo;
        if (!pcam) {
            LOGE("Cannot create CameraInfo!");
            return;
        }
        // back camera settings
        //ev
        pcam->maxEV = "6";
        pcam->minEV = "-6";

        // If the back flash is supported, let's add the rest of the
        // supported scene modes that require flash:
        snprintf(pcam->supportedSceneModes
            ,sizeof(pcam->supportedSceneModes)
            ,"%s,%s"
            ,pcam->supportedSceneModes
            ,CameraParameters::SCENE_MODE_NIGHT_PORTRAIT);
        //Preview size
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
        pcam->orientation = 90;
        pcam->dvs = false;
        pcam->flipping = PlatformData::SENSOR_FLIP_H;
        pcam->maxSnapshotWidth = RESOLUTION_720P_WIDTH;
        pcam->maxSnapshotHeight = RESOLUTION_720P_HEIGHT;
        pcam->defaultBurstLength = "";
        pcam->supportedBurstLength = "";
        strcpy(pcam->supportedSceneModes, pcam->defaultSceneMode);
        strcpy(pcam->defaultFlashMode, "");
        strcpy(pcam->supportedFlashModes,"");
        strcpy(pcam->supportedEffectModes, pcam->defaultEffectMode);
        strcpy(pcam->supportedIntelEffectModes, pcam->defaultEffectMode);
        strcpy(pcam->supportedAwbModes, pcam->defaultAwbMode);
        pcam->defaultIso = "";
        pcam->supportedIso = "";
        pcam->defaultAeMetering = "";
        pcam->supportedAeMetering = "";
        pcam->supportedPreviewSize = "1024x576,720x480,640x480,640x360,352x288,320x240,176x144";
        strcpy(pcam->defaultFocusMode, CameraParameters::FOCUS_MODE_FIXED);
        strcpy(pcam->supportedFocusModes, pcam->defaultFocusMode);
        mCameras.push(*pcam);
        delete pcam;

        // inject device
        mCameras.push(mCameras[0]);
        mFileInject = true;

        // other params
        mBackFlash = true;
        mContinuousCapture = false;
        mVideoPreviewSizePref = "1024x576";

        mProductName = "ExampleModel";
        mManufacturerName = "ExampleMaker";

        mMaxZoomFactor = 1024;
    }
};

}; // namespace android
