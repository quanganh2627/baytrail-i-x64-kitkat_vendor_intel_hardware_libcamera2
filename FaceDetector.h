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
#include "MessageQueue.h"
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
#define MAX_FACES_DETECTABLE 32
#define SMILE_THRESHOLD_MAX 100
#define BLINK_THRESHOLD_MAX 100
#define SMILE_THRESHOLD 70
#define BLINK_THRESHOLD 30

#define SMILE_SHUTTER_SUPPORTED "on,off"
#define BLINK_SHUTTER_SUPPORTED "on,off"

// Smart Shutter Parameters
enum SmartShutterMode {
    SMILE_MODE = 0,
    BLINK_MODE
};

// TODO: Database path should be a system property
#define PERSONDB_PATH "/sdcard/DCIM/.PersonDB.db"

class FaceDetector : public Thread {

// constructor/destructor
public:
#ifdef ENABLE_INTEL_EXTRAS
    FaceDetector();
    ~FaceDetector();

    int getFaces(camera_face_t *faces, int width, int height);
    int faceDetect(ia_frame *frame);
    void eyeDetect(ia_frame *frame);
    void setSmileThreshold(int threshold);
    bool smileDetect(ia_frame *frame);
    void setBlinkThreshold(int threshold);
    bool blinkDetect(ia_frame *frame);
    status_t startFaceRecognition();
    status_t stopFaceRecognition();
    void faceRecognize(ia_frame *frame);

// Thread overrides
public:
    status_t requestExitAndWait();

private:
    status_t loadFaceDb();
    status_t handleExit();
    status_t handleMessageStartFaceRecognition();
    status_t handleMessageStopFaceRecognition();

    // main message function
    status_t waitForAndExecuteMessage();

// inherited from Thread
private:
    virtual bool threadLoop();

// private types
private:
    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_START_FACE_RECOGNITION,
        MESSAGE_ID_STOP_FACE_RECOGNITION,

        // max number of messages
        MESSAGE_ID_MAX
    };

    // message id and message data
    struct Message {
        MessageId id;
    };

// private data
private:
    ia_face_state* mContext;
    MessageQueue<Message, MessageId> mMessageQueue;
    int mSmileThreshold;
    int mBlinkThreshold;
    bool mFaceRecognitionRunning;
    bool mThreadRunning;


// function stubs for building without Intel extra features
#else
    FaceDetector() {}
    ~FaceDetector() {}

    int getFaces(camera_face_t *faces, int width, int height) { return 0; }
    int faceDetect(ia_frame *frame) { return 0; }
    void eyeDetect(ia_frame *frame) {}
    void setSmileThreshold(int threshold) {}
    bool smileDetect(ia_frame *frame) { return false; }
    bool blinkDetect(ia_frame *frame) { return true; }
    void setBlinkThreshold(int threshold) {}
    status_t startFaceRecognition() { return UNKNOWN_ERROR; }
    status_t stopFaceRecognition() { return UNKNOWN_ERROR; }
    void faceRecognize(ia_frame *frame) {}

// Thread overrides
public:
    status_t requestExitAndWait() { return UNKNOWN_ERROR; }

// inherited from Thread
private:
    virtual bool threadLoop() { return false; }
#endif

}; // class FaceDetector

}; // namespace android

#endif // ANDROID_LIBCAMERA_FACEDETECTOR_H
