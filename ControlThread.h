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

#ifndef ANDROID_LIBCAMERA_CONTROL_THREAD_H
#define ANDROID_LIBCAMERA_CONTROL_THREAD_H

#include <utils/threads.h>
#include <camera.h>
#include <camera/CameraParameters.h>
#include "MessageQueue.h"
#include "PreviewThread.h"
#include "PictureThread.h"
#include "AtomCommon.h"

namespace android {

class Callbacks;
class AtomISP;

//
// ControlThread implements most of the operations defined
// by camera_device_ops_t. Refer to hardware/camera.h
// for documentation on each operation.
//
class ControlThread :
    public Thread,
    public ICallbackPreview,
    public ICallbackPicture {

// constructor destructor
public:
    ControlThread(int cameraId);
    virtual ~ControlThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public methods
public:

    status_t setPreviewWindow(struct preview_stream_ops *window);

    // message callbacks
    void setCallbacks(camera_notify_callback notify_cb,
                      camera_data_callback data_cb,
                      camera_data_timestamp_callback data_cb_timestamp,
                      camera_request_memory get_memory,
                      void* user);
    void enableMsgType(int32_t msg_type);
    void disableMsgType(int32_t msg_type);
    bool msgTypeEnabled(int32_t msg_type);

    // synchronous (blocking) state machine methods
    status_t startPreview();
    status_t stopPreview();
    status_t startRecording();
    status_t stopRecording();

    // return true if preview or recording is enabled
    bool previewEnabled();
    bool recordingEnabled();

    // parameter APIs
    status_t setParameters(const char *params);
    char *getParameters();
    void putParameters(char *params);

    // snapshot (asynchronous)
    status_t takePicture();
    status_t cancelPicture();

    // autofocus commands (asynchronous)
    status_t autoFocus();
    status_t cancelAutoFocus();

    // return recording frame to driver (asynchronous)
    status_t releaseRecordingFrame(void *buff);

    // TODO: need methods to configure control thread
    // TODO: decide if configuration method should send a message

// callback methods
private:
    virtual void previewDone(AtomBuffer *buff);
    virtual void pictureDone(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf);

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_START_PREVIEW,
        MESSAGE_ID_STOP_PREVIEW,
        MESSAGE_ID_START_RECORDING,
        MESSAGE_ID_STOP_RECORDING,
        MESSAGE_ID_TAKE_PICTURE,
        MESSAGE_ID_CANCEL_PICTURE,
        MESSAGE_ID_AUTO_FOCUS,
        MESSAGE_ID_CANCEL_AUTO_FOCUS,
        MESSAGE_ID_RELEASE_RECORDING_FRAME,
        MESSAGE_ID_PREVIEW_DONE,
        MESSAGE_ID_PICTURE_DONE,
        MESSAGE_ID_SET_PARAMETERS,
        MESSAGE_ID_GET_PARAMETERS,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageReleaseRecordingFrame {
        void *buff;
    };

    struct MessagePreviewDone {
        AtomBuffer *buff;
    };

    struct MessagePictureDone {
        AtomBuffer *snapshotBuf;
        AtomBuffer *postviewBuf;
    };

    struct MessageSetParameters {
        char* params;
    };

    struct MessageGetParameters {
        char** params;
    };

    // union of all message data
    union MessageData {

        // MESSAGE_ID_RELEASE_RECORDING_FRAME
        MessageReleaseRecordingFrame releaseRecordingFrame;

        // MESSAGE_ID_PREVIEW_DONE
        MessagePreviewDone previewDone;

        // MESSAGE_ID_PICTURE_DONE
        MessagePictureDone pictureDone;

        // MESSAGE_ID_SET_PARAMETERS
        MessageSetParameters setParameters;

        // MESSAGE_ID_GET_PARAMETERS
        MessageGetParameters getParameters;
    };

    // message id and message data
    struct Message {
        MessageId id;
        MessageData data;
    };

    // thread states
    enum State {
        STATE_STOPPED,
        STATE_PREVIEW_STILL,
        STATE_PREVIEW_VIDEO,
        STATE_RECORDING,
    };

    struct CoupledBuffer {
        AtomBuffer *previewBuff;
        AtomBuffer *recordingBuff;
        bool previewBuffReturned;
        bool recordingBuffReturned;
    };

// private methods
private:

    // state machine helper functions
    status_t restartPreview(bool videoMode);
    status_t startPreviewCore(bool videoMode);
    status_t stopPreviewCore();

    // thread message execution functions
    status_t handleMessageExit();
    status_t handleMessageStartPreview();
    status_t handleMessageStopPreview();
    status_t handleMessageStartRecording();
    status_t handleMessageStopRecording();
    status_t handleMessageTakePicture();
    status_t handleMessageCancelPicture();
    status_t handleMessageAutoFocus();
    status_t handleMessageCancelAutoFocus();
    status_t handleMessageReleaseRecordingFrame(MessageReleaseRecordingFrame *msg);
    status_t handleMessagePreviewDone(MessagePreviewDone *msg);
    status_t handleMessagePictureDone(MessagePictureDone *msg);
    status_t handleMessageSetParameters(MessageSetParameters *msg);
    status_t handleMessageGetParameters(MessageGetParameters *msg);

    // main message function
    status_t waitForAndExecuteMessage();

    AtomBuffer* findRecordingBuffer(void *findMe);

    // dequeue buffers from driver and deliver them
    status_t dequeuePreview();
    status_t dequeueRecording();
    status_t queueCoupledBuffers(int coupledId);

    // parameters handling functions
    bool isParameterSet(const char* param);

    // These are parameters that can be set while the ISP is running (most params can be
    // set while the isp is stopped as well).
    status_t processDynamicParameters(const CameraParameters *params);

    // These are params that can only be set while the ISP is stopped. If the parameters
    // changed while the ISP is running, the ISP will need to be stopped, reconfigured, and
    // restarted. Static parameters will most likely affect buffer size and/or format so buffers
    // must be deallocated and reallocated accordingly.
    status_t processStaticParameters(const CameraParameters *oldParams,
            const CameraParameters *newParams);
    status_t validateParameters(const CameraParameters *params);

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    AtomISP *mISP;
    sp<PreviewThread> mPreviewThread;
    sp<PictureThread> mPictureThread;

    MessageQueue<Message> mMessageQueue;
    State mState;
    bool mThreadRunning;
    Callbacks *mCallbacks;

    CoupledBuffer mCoupledBuffers[NUM_ATOM_BUFFERS];

    CameraParameters mParameters;

}; // class ControlThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_CONTROL_THREAD_H
