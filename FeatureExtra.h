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
        /* Creating CameraFeature object (default constructor value applied)
         * HERE we only modify the value which are different than
         * in the constructor.
         * non-present value are taking constructor values.
         * See Default values in FeatureData.h
         * same applies later in the code for front camera.
         */
        CameraFeature camFeat;
        // Back camera settings same as default.
        mCamFeature.push(camFeat);

        //Front camera setting
        camFeat.hdrDefault = "";
        camFeat.hdrSupported = "";
        camFeat.panoramaDefault = "";
        camFeat.panoramaSupported = "";
        camFeat.sceneDetectionDefault = "";
        camFeat.sceneDetectionSupported = "";
        mCamFeature.push(camFeat);

    };

};

// No support for any Intel Feature.
class FeatureNotExtra : public FeatureBase {

    public:

    FeatureNotExtra(void) {

        /* Creating CameraFeature object (default constructor value applied)
         * HERE we only modify the value which are different than
         * in the constructor.
         * non-present value are taking constructor values.
         * See Default values in FeatureData.h
         * same applies later in the code for front camera.
         */
        CameraFeature camFeat;

        //Back camera setting
        camFeat.hdrDefault = "";
        camFeat.hdrSupported = "";
        camFeat.faceDetectionDefault = "";
        camFeat.faceDetectionSupported = "";
        camFeat.faceRecognitionDefault = "";
        camFeat.faceRecognitionSupported = "";
        camFeat.smileShutterDefault = "";
        camFeat.smileShutterSupported = "";
        camFeat.blinkShutterDefault = "";
        camFeat.blinkShutterSupported = "";
        camFeat.panoramaDefault = "";
        camFeat.panoramaSupported = "";
        camFeat.sceneDetectionDefault = "";
        camFeat.sceneDetectionSupported = "";
        mCamFeature.push(camFeat);

        //Front camera setting
        // same as back camera
        mCamFeature.push(camFeat);
    };
};
}//namespace android
#endif /* FEATUREDATA_H_ */
