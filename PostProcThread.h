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
    void flushFrames();
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
    virtual int getSmileThreshold();
    virtual bool isBlinkRunning();
    virtual int getBlinkThreshold();
    virtual void captureOnTrigger();
    virtual void stopCaptureOnTrigger();
    virtual void enableFaceAAA(AAAFlags flags);
    virtual void disableFaceAAA(AAAFlags flags);
    virtual void startFaceRecognition();
    virtual void stopFaceRecognition();
    bool isFaceRecognitionRunning();

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
        MESSAGE_ID_GET_SMILE_THRESHOLD,
        MESSAGE_ID_IS_BLINK_RUNNING,
        MESSAGE_ID_GET_BLINK_THRESHOLD,
        MESSAGE_ID_ENABLE_FACE_AAA,
        MESSAGE_ID_DISABLE_FACE_AAA,
        MESSAGE_ID_START_FACE_RECOGNITION,
        MESSAGE_ID_STOP_FACE_RECOGNITION,
        MESSAGE_ID_IS_FACE_RECOGNITION_RUNNING,

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

    struct MessageFaceAAA {
        AAAFlags flags;
    };

    // union of all message data
    union MessageData {
        // MESSAGE_ID_FRAME
        MessageFrame frame;
        // MESSAGE_START_SMART_SHUTTER
        // MESSAGE_STOP_SMART_SHUTTER
        MessageSmartShutter smartShutterParam;
        //MESSAGE_ID_FACE_AAA
        MessageFaceAAA faceAAA;
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
    status_t handleMessageGetSmileThreshold();
    status_t handleMessageIsBlinkRunning();
    status_t handleMessageStartSmartShutter();
    status_t handleMessageStopSmartShutter();
    status_t handleMessageGetBlinkThreshold();
    status_t handleMessageEnableFaceAAA(const MessageFaceAAA& msg);
    status_t handleMessageDisableFaceAAA(const MessageFaceAAA& msg);
    status_t handleMessageStartFaceRecognition();
    status_t handleMessageStopFaceRecognition();
    status_t handleMessageIsFaceRecognitionRunning();

    // main message function
    status_t waitForAndExecuteMessage();

    void setFocusAreas(const CameraWindow* windows, size_t winCount);
    void setAeMeteringArea(const CameraWindow* window);
    void useFacesForAAA(const camera_frame_metadata_t& face_metadata);
    void resetToOldAAAValues();

// private data
private:
    sp<FaceDetector> mFaceDetector;
    PanoramaThread *mPanoramaThread;
    MessageQueue<Message, MessageId> mMessageQueue;
    int mLastReportedNumberOfFaces;
    Callbacks *mCallbacks;
    ICallbackPostProc* mPostProcDoneCallback;
    bool mThreadRunning;
    bool mFaceDetectionRunning;
    bool mFaceRecognitionRunning;
    bool mSmartShutterRunning;
    int mFaceAAAFlags;
    AfMode mOldAfMode;
    MeteringMode mOldAeMeteringMode;
    SmartShutterParams mSmartShutter;
}; // class PostProcThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_POSTPROC_THREAD_H
