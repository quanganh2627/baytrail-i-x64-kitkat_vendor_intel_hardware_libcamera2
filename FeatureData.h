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

    protected:
        const char *hdrDefault;
        const char *hdrSupported;
        const char *faceDetectionDefault;
        const char *faceDetectionSupported;
        const char *faceRecognitionDefault;
        const char *faceRecognitionSupported;
        const char *blinkShutterDefault;
        const char *blinkShutterSupported;
        const char *smileShutterDefault;
        const char *smileShutterSupported;
        const char *panoramaDefault;
        const char *panoramaSupported;
        const char *sceneDetectionDefault;
    const char *sceneDetectionSupported;
};


class FeatureData {

    private:
        static FeatureBase* mInstance;
        static FeatureBase* getInstance();
    public:
        static const char* hdrDefault();
        static const char* hdrSupported();
        static const char* faceDetectionDefault();
        static const char* faceDetectionSupported();
        static const char* faceRecognitionDefault();
        static const char* faceRecognitionSupported();
        static const char* smileShutterDefault();
        static const char* smileShutterSupported();
        static const char* blinkShutterDefault();
        static const char* blinkShutterSupported();
        static const char* panoramaDefault();
        static const char* panoramaSupported();
        static const char* sceneDetectionDefault();
        static const char* sceneDetectionSupported();
};

} /* namespace android */
#endif /* FEATUREDATA_H_ */
