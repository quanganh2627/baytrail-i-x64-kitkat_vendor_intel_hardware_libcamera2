/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ANDROID_LIBCAMERA_FACEDETECTOR_H
#define ANDROID_LIBCAMERA_FACEDETECTOR_H

#include <utils/threads.h>
#include <system/camera.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "ia_face.h"
#ifdef __cplusplus
}
#endif

namespace android {

/**
 * the maximum number of faces detectable at the same time
 */
#define MAX_FACES_DETECTABLE (32)
#define SMILE_THRESHOLD (70)
#define BLINK_THRESHOLD (30)

class FaceDetector {

// constructor/destructor
public:
    FaceDetector();
    ~FaceDetector();

    int getFaces(camera_face_t *faces, int width, int height);
    int faceDetect(ia_frame *frame);
    void eyeDetect(ia_frame *frame);
    bool smileDetect(ia_frame *frame);
    bool blinkDetect(ia_frame *frame);


private:
    ia_face_state* mContext;

}; // class FaceDetector

}; // namespace android

#endif // ANDROID_LIBCAMERA_FACEDETECTOR_H
