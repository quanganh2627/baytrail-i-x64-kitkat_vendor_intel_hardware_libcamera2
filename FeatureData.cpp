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
 * getting hdr default value on current product
 * return default value
 */
const char* FeatureData::hdrDefault()
{
    FeatureBase *i = getInstance();

    return i->hdrDefault;
}

/**
 * getting if hdr is supported or not on the current product
 * return possible options of hdr support
 */
const char* FeatureData::hdrSupported()
{
    FeatureBase *i = getInstance();

    return i->hdrSupported;
}

/**
 * getting face detection default value on current product
 * return default value
 */
const char* FeatureData::faceDetectionDefault()
{
    FeatureBase *i = getInstance();

    return i->faceDetectionDefault;
}

/**
 * getting if face detection is supported or not on the current product
 * return possible options of face detection support
 */
const char* FeatureData::faceDetectionSupported()
{
    FeatureBase *i = getInstance();

    return i->faceDetectionSupported;
}

/**
 * getting face recognition default value on current product
 * return default value
 */
const char* FeatureData::faceRecognitionDefault()
{
    FeatureBase *i = getInstance();

    return i->faceRecognitionDefault;
}

/**
 * getting if face recognition is supported or not on the current product
 * return possible options of face recognition support
 */
const char* FeatureData::faceRecognitionSupported()
{
    FeatureBase *i = getInstance();

    return i->faceRecognitionSupported;
}

/**
 * getting smile shutter default value on current product
 * return default value
 */
const char* FeatureData::smileShutterDefault()
{
    FeatureBase *i = getInstance();

    return i->smileShutterDefault;
}

/**
 * getting if smile shutter is supported or not on the current product
 * return possible options of smile shutter support
 */
const char* FeatureData::smileShutterSupported()
{
    FeatureBase *i = getInstance();

    return i->smileShutterSupported;
}

/**
 * getting blink shutter default value on current product
 * return default value
 */
const char* FeatureData::blinkShutterDefault()
{
    FeatureBase *i = getInstance();

    return i->blinkShutterDefault;
}

/**
 * getting if blink shutter is supported or not on the current product
 * return possible options of blink shutter support
 */
const char* FeatureData::blinkShutterSupported()
{
    FeatureBase *i = getInstance();

    return i->blinkShutterSupported;
}

/**
 * getting panorama default value on current product
 * return default value
 */
const char* FeatureData::panoramaDefault()
{
    FeatureBase *i = getInstance();

    return i->panoramaDefault;
}

/**
 * getting if panorama is supported or not on the current product
 * return possible options of face panorama support
 */
const char* FeatureData::panoramaSupported()
{
    FeatureBase *i = getInstance();

    return i->panoramaSupported;
}

/**
 * getting scene detection default value on current product
 * return default value
 */
const char* FeatureData::sceneDetectionDefault()
{
    FeatureBase *i = getInstance();

    return i->sceneDetectionDefault;
}

/**
 * getting if scene detection is supported or not on the current product
 * return possible options of scene detection support
 */
const char* FeatureData::sceneDetectionSupported()
{
    FeatureBase *i = getInstance();

    return i->sceneDetectionSupported;
}
}//namespace android
