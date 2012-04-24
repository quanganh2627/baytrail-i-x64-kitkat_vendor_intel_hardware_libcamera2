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

#ifndef ANDROID_LIBCAMERA_CALLBACKS_THREAD_H
#define ANDROID_LIBCAMERA_CALLBACKS_THREAD_H

#include <utils/threads.h>
#include <utils/Vector.h>
#include "MessageQueue.h"
#include "AtomCommon.h"
#include "IFaceDetectionListener.h"

namespace android {

class Callbacks;

// callback for when callback thread is done with yuv data
class ICallbackPicture {
public:
    ICallbackPicture() {}
    virtual ~ICallbackPicture() {}
    virtual void pictureDone(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf) = 0;
};

class CallbacksThread :
    public Thread,
    public IFaceDetectionListener {

private:
    static CallbacksThread* mInstance;
    CallbacksThread();
// constructor destructor
public:
    static CallbacksThread* getInstance(ICallbackPicture *pictureDone = 0) {
        if (mInstance == NULL) {
            mInstance = new CallbacksThread();
        }
        if(mInstance && pictureDone)
            mInstance->setPictureDoneCallback(pictureDone);
        return mInstance;
    }
    virtual ~CallbacksThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public methods
public:

    status_t shutterSound();
    status_t compressedFrameDone(AtomBuffer* jpegBuf);
    status_t requestTakePicture();
    status_t flushPictures();
    status_t postCaptureFrames(AtomBuffer* postviewBuf, AtomBuffer* snapshotBuf);
    size_t   getQueuedBuffersNum() { return mJpegBuffers.size(); }
    virtual void facesDetected(camera_frame_metadata_t &face_metadata);

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_CALLBACK_SHUTTER,    // send the shutter callback
        MESSAGE_ID_JPEG_DATA_READY,     // we have a JPEG image ready
        MESSAGE_ID_JPEG_DATA_REQUEST,   // a JPEG image was requested
        MESSAGE_ID_POSTCAPTURE_READY,   // post view and raw ready to be offered to the user via callback
        MESSAGE_ID_FLUSH,
        MESSAGE_ID_FACES,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageFrame {
        AtomBuffer buff;
    };
    struct MessagePostCaptureFrame {
        AtomBuffer postView;
        AtomBuffer snapshot;
    };

    struct MessageFaces {
        camera_frame_metadata_t meta_data;
    };

    // union of all message data
    union MessageData {

        //MESSAGE_ID_JPEG_DATA_READY
        MessageFrame compressedFrame;

        //MESSAGE_ID_POSTCAPTURE_READY
        MessagePostCaptureFrame postCaptureFrame;

        // MESSAGE_ID_FACES
        MessageFaces faces;
    };

    // message id and message data
    struct Message {
        MessageId id;
        MessageData data;
    };

// private methods
private:

    // thread message execution functions
    status_t handleMessageExit();
    status_t handleMessageCallbackShutter();
    status_t handleMessageJpegDataReady(MessageFrame *msg);
    status_t handleMessagePostCaptureDataReady(MessagePostCaptureFrame *msg);
    status_t handleMessageJpegDataRequest();
    status_t handleMessageFlush();
    status_t handleMessageFaces(MessageFaces *msg);

    // main message function
    status_t waitForAndExecuteMessage();

    // Intialization of Ctrl thread callback
    void setPictureDoneCallback(ICallbackPicture *pictureDone) { mPictureDoneCallback = pictureDone; };

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    ICallbackPicture *mPictureDoneCallback;
    MessageQueue<Message, MessageId> mMessageQueue;
    bool mThreadRunning;
    Callbacks *mCallbacks;
    bool mJpegRequested;
    bool mPostviewRequested;
    bool mRawRequested;

    Vector<AtomBuffer> mJpegBuffers;
    Vector<AtomBuffer> mPostviewBuffers;
    Vector<AtomBuffer> mRawBuffers;

// public data
public:

}; // class CallbacksThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_CALLBACKS_THREAD_H
