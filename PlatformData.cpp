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
#include "PlatformData.h"
#include "PlatformMedfield.h"
#include "PlatformClovertrail.h"
#include "PlatformMerrifield.h"

namespace android {

/**
 * \enum Define camera id assignment on Intel Atom platforms
 *
 * These numbers are not directly mapped to V4L2 input
 * index values, but are just arbitrarily chosen values in
 * the HAL.
 */
enum IntelCameraIds {
    INTEL_CAMERA_ID_BACK = 0,
    INTEL_CAMERA_ID_FRONT = 1,
    INTEL_CAMERA_ID_INJECT = 2
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

#elif   defined(CTP_PR0) || defined(CTP_PR1)
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
    int res;

    assert(cameraId < sMaxCameraIds);
    switch(cameraId)
    {
    case INTEL_CAMERA_ID_FRONT:
        res = CAMERA_FACING_FRONT;
        break;
    default:
        res = CAMERA_FACING_BACK;;
    }

    return res;
}

int PlatformData::cameraOrientation(int cameraId)
{
    PlatformBase *i = getInstance();
    int res;
    assert(cameraId < sMaxCameraIds);

    switch(cameraId)
    {
    case INTEL_CAMERA_ID_FRONT:
        res = i->mFrontRotation;
        break;
    default:
        res = i->mBackRotation;
    }

    return res;
}

int PlatformData::numberOfCameras(void)
{
    PlatformBase *i = getInstance();
    int res;
    if (i->mFileInject)
        res = INTEL_CAMERA_ID_INJECT + 1;
    else
        res = INTEL_CAMERA_ID_FRONT + 1;

    return res;
}

const char* PlatformData::preferredPreviewSizeForVideo(void)
{
    PlatformBase *i = getInstance();
    return i->mVideoPreviewSizePref;
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

}; // namespace android
