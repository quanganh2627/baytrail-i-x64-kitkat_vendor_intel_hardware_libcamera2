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

/**
 * \file PlatformMerrifield.h
 *
 * Platform data for Intel Merrifield based products
 */

namespace android {

class PlatformSaltBay : public PlatformBase {

public:
    PlatformSaltBay(void) {
        CameraInfo cam;
        mSubDevName = "/dev/v4l-subdev7";
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
        cam.flipping = PlatformData::SENSOR_FLIP_NA;
        cam.maxSnapshotWidth = RESOLUTION_720P_WIDTH;
        cam.maxSnapshotHeight = RESOLUTION_720P_HEIGHT;
        mCameras.push(cam);

        // file inject device
        mCameras.push(mCameras[0]);
        mFileInject = true;

        // generic parameters
        mBackFlash = true;
        mVideoPreviewSizePref = "1024x576";
        mMaxBurstFPS = 15;
        mSupportedBurstFPS = "1,3,5,7,15";
        mSupportedBurstLength = "1,3,5,10";

        mProductName = "ExampleModel";
        mManufacturerName = "ExampleMaker";

        mMaxZoomFactor = 1024;
    }
};

}; // namespace android
