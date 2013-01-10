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
#include <cutils/properties.h>
#include <utils/Log.h>

/**
 * \file PlatformMedfield.h
 *
 * Platform data for Intel Medfield based products
 */

namespace android {

/**
 * Platform data for Blackbay/MFLD_PR (Medfield based)
 */
class PlatformBlackbay : public PlatformBase {

public:
    PlatformBlackbay(void) {
        mSubDevName = "/dev/v4l-subdev7";

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
        pcam->flipping = PlatformData::SENSOR_FLIP_H |
                       PlatformData::SENSOR_FLIP_V;
        pcam->maxSnapshotWidth = RESOLUTION_1_3MP_WIDTH;
        pcam->maxSnapshotHeight = RESOLUTION_1_3MP_HEIGHT;
        pcam->supportedBurstLength = "";
        pcam->maxEV = "";
        pcam->minEV = "";
        pcam->stepEV = "";
        pcam->defaultEV = "";
        strcpy(pcam->supportedSceneModes,"");
        strcpy(pcam->supportedFlashModes,"");
        strcpy(pcam->supportedEffectModes,"");
        strcpy(pcam->supportedIntelEffectModes,"");
        strcpy(pcam->supportedAwbModes,"");
        pcam->supportedIso = "";
        pcam->supportedAeMetering = "";
        pcam->supportedPreviewSize = "1024x576,720x480,640x480,640x360,352x288,320x240,176x144";
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

        mMaxZoomFactor = 64;
    }
};

/**
 * Platform data for Lexington/MFLD_GI (Medfield based)
 */
class PlatformLexington : public PlatformBlackbay {

public:
    PlatformLexington(void) {
        mSubDevName = "/dev/v4l-subdev6";
        mBackFlash = false;
        mCameras.editItemAt(0).maxBurstFPS = 5;
        mCameras.editItemAt(0).supportedBurstLength = "1,3,5";
        mCameras.editItemAt(0).supportedBurstFPS = "1,3,5";
        mSupportVideoSnapshot = false;
        mNumRecordingBuffers = 6;
        mCameras.editItemAt(0).supportedPreviewSize = "1024x576,800x600,720x480,640x480,640x360,416x312,352x288,320x240,176x144";
        // NOTE: front camera uses supportedPreviewSize from PlatformBlackbay
        mSupportedVideoSizes = "176x144,320x240,352x288,416x312,640x480,720x480,720x576,1280x720,1920x1080";
    }
};

/**
 * Platform data for Redridge/MFLD_DV (Medfield based)
 */
class PlatformRedridge : public PlatformBlackbay {

public:
    PlatformRedridge(void) {
        mSubDevName = "/dev/v4l-subdev7";
        char bid[PROPERTY_VALUE_MAX] = "";
        property_get("ro.board.id", bid, "");

        mCameras.editItemAt(1).orientation = 0;

        if (!strcmp(bid, "redridge_dv10") || !strcmp(bid,"joki_ev20")) {
            mCameras.editItemAt(0).orientation = 180;
            mCameras.editItemAt(1).orientation = 180;
        }
        else if (!strcmp(bid,"redridge_dv20") || !strcmp(bid,"redridge_dv21")) {
            mCameras.editItemAt(0).orientation = 0;
            mCameras.editItemAt(1).orientation = 180;
        }
        mVideoPreviewSizePref = "1024x576";
    }
};

/**
 * Platform data for Yukka Beach (Lexington based)
 */
class PlatformYukka : public PlatformLexington {

public:
    PlatformYukka(void) {

        mCameras.editItemAt(0).orientation = 0;
        mCameras.editItemAt(0).sensorType = SENSOR_TYPE_SOC;
        mCameras.editItemAt(0).dvs = false;
        mCameras.editItemAt(0).maxSnapshotWidth = RESOLUTION_2MP_WIDTH;
        mCameras.editItemAt(0).maxSnapshotHeight = RESOLUTION_2MP_HEIGHT;

        mCameras.editItemAt(1).orientation = 0;
        mCameras.editItemAt(1).sensorType = SENSOR_TYPE_SOC;
        mCameras.editItemAt(1).maxSnapshotWidth = RESOLUTION_VGA_WIDTH;
        mCameras.editItemAt(1).maxSnapshotHeight = RESOLUTION_VGA_HEIGHT;

        mCameras.editItemAt(2).sensorType = SENSOR_TYPE_SOC;
    }
};

}; // namespace android
