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
#ifndef FEATUREDATA_H_
#define FEATUREDATA_H_

namespace android {

/**
 * \file FeatureData.h
 *
 * HAL internal interface for managing feature specific static
 * data.
 *
 * Design principles for Feature data mechanism:
 *
 * 1. Make it easy as possible to configure feature based on product.
 *
 * 2. Separate HW related feature from HW NOT related feature.
 *    HW related feature : PlatformData
 *    NOT HW related feature: FeatureData
 *
 * 3. Make it easy as possible to add new product and features.
 *
 * 4. Split implementations into separate files, to avoid
 *    version conflicts with parallel work targeting different
 *    platforms.
 *
 * 5. Focus on plain flat data and avoid defining new abstractions
 *    and relations.
 *
 * 6. If any #ifdefs are needed, put them in product files.
 *
 * 7. Keep the set of parameters to a minimum, and only add
 *    data that really varies from product to another.
 */

class FeatureBase {

    friend class FeatureData;

public:
    class CameraFeature {
    public:
        CameraFeature() {
        hdrDefault = "off";
        hdrSupported = "on,off";
        ultraLowLightDefault = "off";
        ultraLowLightSupported = "auto,on,off";
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
        //FIXME CONSTRUCTOR
        };
        const char *hdrDefault;
        const char *hdrSupported;
        const char *ultraLowLightDefault;
        const char *ultraLowLightSupported;
        const char *faceDetectionDefault;
        const char *faceDetectionSupported;
        const char *faceRecognitionDefault;
        const char *faceRecognitionSupported;
        const char *smileShutterDefault;
        const char *smileShutterSupported;
        const char *blinkShutterDefault;
        const char *blinkShutterSupported;
        const char *panoramaDefault;
        const char *panoramaSupported;
        const char *sceneDetectionDefault;
        const char *sceneDetectionSupported;
    };

    Vector<CameraFeature> mCamFeature;
};


class FeatureData {

    private:
        static FeatureBase* mInstance;
        static FeatureBase* getInstance();
    public:
        static const char* hdrDefault(int cameraId);
        static const char* hdrSupported(int cameraId);
        static const char* ultraLowLightDefault(int cameraId);
        static const char* ultraLowLightSupported(int cameraId);
        static const char* faceDetectionDefault(int cameraId);
        static const char* faceDetectionSupported(int cameraId);
        static const char* faceRecognitionDefault(int cameraId);
        static const char* faceRecognitionSupported(int cameraId);
        static const char* smileShutterDefault(int cameraId);
        static const char* smileShutterSupported(int cameraId);
        static const char* blinkShutterDefault(int cameraId);
        static const char* blinkShutterSupported(int cameraId);
        static const char* panoramaDefault(int cameraId);
        static const char* panoramaSupported(int cameraId);
        static const char* sceneDetectionDefault(int cameraId);
        static const char* sceneDetectionSupported(int cameraId);
};

} /* namespace android */
#endif /* FEATUREDATA_H_ */
