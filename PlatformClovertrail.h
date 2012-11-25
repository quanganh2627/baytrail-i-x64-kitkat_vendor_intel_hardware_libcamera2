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
        CameraInfo cam;
        mSubDevName = "/dev/v4l-subdev8";
        mPreviewViaOverlay = true;

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
        cam.maxSnapshotWidth = RESOLUTION_1_3MP_WIDTH;
        cam.maxSnapshotHeight = RESOLUTION_1_3MP_HEIGHT;
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

        mContinuousCapture = (deviceOnContinuousCaptureBlackList() == false);
    }
};

}; // namespace android
