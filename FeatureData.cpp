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
#define LOG_TAG "Camera_FeatureData"
#include "LogHelper.h"

#include <assert.h>
#include "FeatureData.h"
#include "FeatureExtra.h"
#include <utils/Log.h>

namespace android {

FeatureBase* FeatureData::mInstance = 0;

FeatureBase* FeatureData::getInstance()
{

    if (mInstance == 0) {

#ifdef ENABLE_INTEL_EXTRAS
        mInstance = new FeatureExtra();
#else
        mInstance = new FeatureNotExtra();
#endif

    }

    return mInstance;
}

/**
 * Finding if CameraId is valid
 * return CameraId validity status.
 */
static bool checkCameraId(int cameraId, FeatureBase *i)
{
    if (cameraId < 0 || cameraId >= static_cast<int>(i->mCamFeature.size())) {
        LOGE("%s: Invalid cameraId %d", __FUNCTION__, cameraId);
        return false;
    }

    return true;
}

/**
 * getting hdr default value on current product
 * return default value
 */
const char* FeatureData::hdrDefault(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].hdrDefault : "");
}

/**
 * getting if hdr is supported or not on the current product
 * return possible options of hdr support
 */
const char* FeatureData::hdrSupported(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].hdrSupported : "");
}

/**
 * getting low light default value on current product
 * return default value
 */
const char* FeatureData::lowLightDefault(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].lowLightDefault : "");
}

/**
 * getting if low light is supported or not on the current product
 * return possible options of hdr support
 */
const char* FeatureData::lowLightSupported(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].lowLightSupported : "");
}

/**
 * getting face detection default value on current product
 * return default value
 */
const char* FeatureData::faceDetectionDefault(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].faceDetectionDefault : "");
}

/**
 * getting if face detection is supported or not on the current product
 * return possible options of face detection support
 */
const char* FeatureData::faceDetectionSupported(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].faceDetectionSupported : "");
}

/**
 * getting face recognition default value on current product
 * return default value
 */
const char* FeatureData::faceRecognitionDefault(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].faceRecognitionDefault : "");
}

/**
 * getting if face recognition is supported or not on the current product
 * return possible options of face recognition support
 */
const char* FeatureData::faceRecognitionSupported(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].faceRecognitionSupported : "");
}

/**
 * getting smile shutter default value on current product
 * return default value
 */
const char* FeatureData::smileShutterDefault(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].smileShutterDefault : "");
}

/**
 * getting if smile shutter is supported or not on the current product
 * return possible options of smile shutter support
 */
const char* FeatureData::smileShutterSupported(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].smileShutterSupported : "");
}

/**
 * getting blink shutter default value on current product
 * return default value
 */
const char* FeatureData::blinkShutterDefault(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].blinkShutterDefault : "");
}

/**
 * getting if blink shutter is supported or not on the current product
 * return possible options of blink shutter support
 */
const char* FeatureData::blinkShutterSupported(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].blinkShutterSupported : "");
}

/**
 * getting panorama default value on current product
 * return default value
 */
const char* FeatureData::panoramaDefault(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].panoramaDefault : "");
}

/**
 * getting if panorama is supported or not on the current product
 * return possible options of face panorama support
 */
const char* FeatureData::panoramaSupported(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].panoramaSupported : "");
}

/**
 * getting scene detection default value on current product
 * return default value
 */
const char* FeatureData::sceneDetectionDefault(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].sceneDetectionDefault : "");
}

/**
 * getting if scene detection is supported or not on the current product
 * return possible options of scene detection support
 */
const char* FeatureData::sceneDetectionSupported(int cameraId)
{
    FeatureBase *i = getInstance();
    return (checkCameraId(cameraId, i) ? i->mCamFeature[cameraId].sceneDetectionSupported : "");
}
}//namespace android
