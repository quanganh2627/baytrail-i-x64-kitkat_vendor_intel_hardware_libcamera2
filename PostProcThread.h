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
#include "AtomAAA.h"
#include "FaceDetector.h"
#include "MessageQueue.h"
#include "IFaceDetector.h"
#include "PanoramaThread.h"

namespace android {

class Callbacks;

class ICallbackPostProc {
public:
    ICallbackPostProc() {}
    virtual ~ICallbackPostProc() {}
    virtual void facesDetected(camera_frame_metadata_t *face_metadata) = 0;
};


class PostProcThread : public IFaceDetector,
                       public Thread
{

// constructor/destructor
public:
    PostProcThread(ICallbackPostProc *postProcDone, PanoramaThread *panoramaThread);
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
    virtual void startSmartShutter();
    virtual void stopSmartShutter();

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_FRAME,
        MESSAGE_ID_START_FACE_DETECTION,
        MESSAGE_ID_STOP_FACE_DETECTION,
        MESSAGE_ID_START_SMART_SHUTTER,
        MESSAGE_ID_STOP_SMART_SHUTTER,

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
    status_t handleMessageStartSmartShutter();
    status_t handleMessageStopSmartShutter();

    // main message function
    status_t waitForAndExecuteMessage();

    void setFocusAreas(const CameraWindow* windows, size_t winCount);
    void useFacesForAAA(const camera_frame_metadata_t& face_metadata);

// private data
private:
    FaceDetector* mFaceDetector;
    PanoramaThread *mPanoramaThread;
    MessageQueue<Message, MessageId> mMessageQueue;
    int mLastReportedNumberOfFaces;
    Callbacks *mCallbacks;
    ICallbackPostProc* mPostProcDoneCallback;
    bool mThreadRunning;
    bool mFaceDetectionRunning;
    bool mSmartShutterRunning;
    AfMode mOldAfMode;
}; // class PostProcThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_POSTPROC_THREAD_H
