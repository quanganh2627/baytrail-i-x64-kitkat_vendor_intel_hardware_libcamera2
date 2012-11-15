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

#ifndef FEATUREEXTRA_H_
#define FEATUREEXTRA_H_

#include "FeatureData.h"
#include <cutils/properties.h>
#include <utils/Log.h>

/** \file FeatureExtra.h
 *
 * Feature available or not in INTEL_CAMERA_EXTRAS flag set
 *
 */
namespace android {

/* TODO: ADD MORE PRODUCT HERE OR CREATE A TOTALLY NEW FILE FOR NEW PRODUCT */
/* class FeatureYourOwn : public FeatureBase */

class FeatureExtra : public FeatureBase {

    public:

    FeatureExtra(void) {
        hdrDefault = "off";
        hdrSupported = "on,off";
        faceDetectionDefault = "off";
        faceDetectionSupported = "on,off";
        faceRecognitionDefault = "off";
        faceRecognitionSupported = "on,off";
        smileShutterDefault = "off";
        smileShutterSupported = "on,off";
        blinkShutterDefault = "off";
        blinkShutterSupported = "on,off";
        panoramaDefault = "off";
        panoramaSupported = "on,off";
        sceneDetectionDefault = "off";
        sceneDetectionSupported = "on,off";
         /* TODO: ADD MORE HERE WHEN NEEDED*/
    }
};


class FeatureNotExtra : public FeatureBase {

    public:

    FeatureNotExtra(void) {
        hdrDefault = "";
        hdrSupported = "";
        faceDetectionDefault = "";
        faceDetectionSupported = "";
        faceRecognitionDefault = "";
        faceRecognitionSupported = "";
        smileShutterDefault = "";
        smileShutterSupported = "";
        blinkShutterDefault = "";
        blinkShutterSupported = "";
        panoramaDefault = "";
        panoramaSupported = "";
        sceneDetectionDefault = "";
        sceneDetectionSupported = "";
        /* TODO: ADD MORE HERE WHEN NEEDED */
    }
};
}//namespace android
#endif /* FEATUREDATA_H_ */
