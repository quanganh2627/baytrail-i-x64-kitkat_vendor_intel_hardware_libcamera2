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
        CameraInfo cam;

        // back camera
        cam.facing = CAMERA_FACING_BACK;
        cam.orientation = 90;
        cam.dvs = true;
        cam.flipping = PlatformData::SENSOR_FLIP_NA;
        cam.maxSnapshotWidth = RESOLUTION_8MP_WIDTH;
        cam.maxSnapshotHeight = RESOLUTION_8MP_HEIGHT;
        mCameras.push(cam);

        // front camera
        cam.facing = CAMERA_FACING_FRONT;
        cam.orientation = 270;
        cam.dvs = false;
        cam.flipping = PlatformData::SENSOR_FLIP_H |
                       PlatformData::SENSOR_FLIP_V;
        cam.maxSnapshotWidth = RESOLUTION_1_3MP_WIDTH;
        cam.maxSnapshotHeight = RESOLUTION_1_3MP_HEIGHT;
        mCameras.push(cam);

        // inject device
        mCameras.push(mCameras[0]);
        mFileInject = true;

        // other params
        mBackFlash = true;
        mVideoPreviewSizePref = "1024x576";
        mProductName = "ExampleModel";
        mManufacturerName = "ExampleMaker";
    }
};

/**
 * Platform data for Lexington/MFLD_GI (Medfield based)
 */
class PlatformLexington : public PlatformBlackbay {

public:
    PlatformLexington(void) {
        mBackFlash = false;
    }
};

/**
 * Platform data for Redridge/MFLD_DV (Medfield based)
 */
class PlatformRedridge : public PlatformBlackbay {

public:
    PlatformRedridge(void) {

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


}; // namespace android
