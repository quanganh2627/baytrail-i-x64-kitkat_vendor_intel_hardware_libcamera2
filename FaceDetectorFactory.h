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

#ifndef FACEDETECTORFACTORY_H_
#define FACEDETECTORFACTORY_H_
#include <utils/StrongPointer.h>
#include "IFaceDetector.h"
#include "IFaceDetectionListener.h"
#include "OlaFaceDetect.h"
namespace android
{
class FaceDetectorFactory {
public:
    /**
     * create a detector instance with given listener.
     * caller is responsible to delete the detector when it is done.
     */
    static IFaceDetector* createDetector(IFaceDetectionListener* listener)
    {
        if (theInstance == 0)
            theInstance = new OlaFaceDetect(listener);
        return theInstance.get();
    }

    static bool destroyDetector(IFaceDetector* d)
    {
        if (theInstance.get() == static_cast<OlaFaceDetect*>(d)) {
            theInstance.clear();
            return true;
        }
        return false;
    }
private:
    static sp<OlaFaceDetect> theInstance;
    FaceDetectorFactory(){};
    virtual ~FaceDetectorFactory(){};
};
sp<OlaFaceDetect> FaceDetectorFactory::theInstance =0;
}
#endif /* FACEDETECTORFACTORY_H_ */
