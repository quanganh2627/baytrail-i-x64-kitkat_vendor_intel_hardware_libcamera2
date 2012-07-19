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

#ifndef ANDROID_LIBCAMERA_POSTPROC_THREAD_H
#define ANDROID_LIBCAMERA_POSTPROC_THREAD_H

#include <utils/threads.h>
#include <time.h>
#include <camera/CameraParameters.h>
#include "FaceDetector.h"
#include "MessageQueue.h"
#include "IFaceDetector.h"

namespace android {

class Callbacks;

class ICallbackPostProc {
public:
    ICallbackPostProc() {}
    virtual ~ICallbackPostProc() {}
};


class PostProcThread : public IFaceDetector,
                       public Thread
{

// constructor/destructor
public:
    PostProcThread(ICallbackPostProc *postProcDone);
    virtual ~PostProcThread();

// Common methods
    void getDefaultParameters(CameraParameters *params);

// Thread overrides
public:
    status_t requestExitAndWait();

// IFaceDetector overrides
public:
    virtual int getMaxFacesDetectable(){
        return MAX_FACES_DETECTABLE;
    };
    virtual void startFaceDetection();
    virtual void stopFaceDetection(bool wait=false);
    virtual int sendFrame(AtomBuffer *img, int width, int height);

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_FRAME,
        MESSAGE_ID_START_FACE_DETECTION,
        MESSAGE_ID_STOP_FACE_DETECTION,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //
    struct MessageFrame {
        AtomBuffer img;
        int width;
        int height;
    };

    // union of all message data
    union MessageData {
        // MESSAGE_ID_FRAME
        MessageFrame frame;
    };

    // message id and message data
    struct Message {
        MessageId id;
        MessageData data;
    };

// inherited from Thread
private:
    virtual bool threadLoop();

// private methods
private:
    status_t handleFrame(MessageFrame frame);
    status_t handleExit();
    status_t handleMessageStartFaceDetection();
    status_t handleMessageStopFaceDetection();

    // main message function
    status_t waitForAndExecuteMessage();

    void setFocusAreas(const CameraWindow* windows, size_t winCount);
    void useFacesForAAA(const camera_frame_metadata_t& face_metadata);

// private data
private:
    FaceDetector* mFaceDetector;
    MessageQueue<Message, MessageId> mMessageQueue;
    int mLastReportedNumberOfFaces;
    Callbacks *mCallbacks;
    ICallbackPostProc* mPostProcDoneCallback;
    bool mThreadRunning;
    bool mFaceDetectionRunning;
}; // class PostProcThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_POSTPROC_THREAD_H
