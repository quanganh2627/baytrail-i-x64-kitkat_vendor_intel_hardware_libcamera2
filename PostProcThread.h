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
#include "IntelParameters.h"
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
    virtual void postProcCaptureTrigger() = 0;
};


class PostProcThread : public IFaceDetector,
                       public Thread
{

// constructor/destructor
public:
    PostProcThread(ICallbackPostProc *postProcDone, PanoramaThread *panoramaThread);
    virtual ~PostProcThread();

// Common methods
    void getDefaultParameters(CameraParameters *params, CameraParameters *intel_parameters);

// Thread overrides
public:
    status_t requestExitAndWait();

public:
    SmartShutterMode mode;
    int level;

// IFaceDetector overrides
public:
    virtual int getMaxFacesDetectable(){
        return MAX_FACES_DETECTABLE;
    };
    virtual void startFaceDetection();
    virtual void stopFaceDetection(bool wait=false);
    virtual int sendFrame(AtomBuffer *img);
    virtual void startSmartShutter(SmartShutterMode mode, int level);
    virtual void stopSmartShutter(SmartShutterMode mode);
    virtual bool isSmartRunning();
    virtual bool isSmileRunning();
    virtual bool isBlinkRunning();
    virtual void captureOnTrigger();
    virtual void stopCaptureOnTrigger();

// private types
private:

    //smart shutter parameters structure
    struct SmartShutterParams {
        bool smartRunning;
        bool smileRunning;
        bool blinkRunning;
        bool captureOnTrigger;
        int smileThreshold;
        int blinkThreshold;
    };

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_FRAME,
        MESSAGE_ID_START_FACE_DETECTION,
        MESSAGE_ID_STOP_FACE_DETECTION,
        MESSAGE_ID_START_SMART_SHUTTER,
        MESSAGE_ID_STOP_SMART_SHUTTER,
        MESSAGE_ID_CAPTURE_ON_TRIGGER,
        MESSAGE_ID_STOP_CAPTURE_ON_TRIGGER,
        MESSAGE_ID_IS_SMART_RUNNING,
        MESSAGE_ID_IS_SMILE_RUNNING,
        MESSAGE_ID_IS_BLINK_RUNNING,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //
    struct MessageFrame {
        AtomBuffer img;
    };

    struct MessageSmartShutter {
        int mode;
        int level;
    };

    // union of all message data
    union MessageData {
        // MESSAGE_ID_FRAME
        MessageFrame frame;

        // MESSAGE_START_SMART_SHUTTER
        // MESSAGE_STOP_SMART_SHUTTER
        MessageSmartShutter smartShutterParam;
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
    status_t handleMessageStartSmartShutter(MessageSmartShutter params);
    status_t handleMessageStopSmartShutter(MessageSmartShutter params);
    status_t handleMessageCaptureOnTrigger();
    status_t handleMessageStopCaptureOnTrigger();
    status_t handleMessageIsSmartRunning();
    status_t handleMessageIsSmileRunning();
    status_t handleMessageIsBlinkRunning();

    // main message function
    status_t waitForAndExecuteMessage();

    void setFocusAreas(const CameraWindow* windows, size_t winCount);
    void setAeMeteringArea(const CameraWindow* window);
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
    AfMode mOldAfMode;
    MeteringMode mOldAeMeteringMode;
    int mPreviewWidth;
    int mPreviewHeight;
    SmartShutterParams mSmartShutter;
}; // class PostProcThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_POSTPROC_THREAD_H
