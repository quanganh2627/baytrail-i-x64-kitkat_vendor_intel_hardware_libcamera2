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

class CallbacksThread :
    public Thread,
    public IFaceDetectionListener {

private:
    static CallbacksThread* mInstance;
    CallbacksThread();
// constructor destructor
public:
    static CallbacksThread* getInstance() {
        if (mInstance == NULL) {
            mInstance = new CallbacksThread();
        }
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
        MESSAGE_ID_FLUSH,
        MESSAGE_ID_FACES,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageCompressedFrame {
        AtomBuffer buff;
    };

    struct MessageFaces {
        camera_frame_metadata_t meta_data;
    };

    // union of all message data
    union MessageData {

        //MESSAGE_ID_JPEG_DATA_READY
        MessageCompressedFrame compressedFrame;

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
    status_t handleMessageJpegDataReady(MessageCompressedFrame *msg);
    status_t handleMessageJpegDataRequest();
    status_t handleMessageFlush();
    status_t handleMessageFaces(MessageFaces *msg);

    // main message function
    status_t waitForAndExecuteMessage();

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message, MessageId> mMessageQueue;
    bool mThreadRunning;
    Callbacks *mCallbacks;
    bool mJpegRequested;
    Vector<AtomBuffer> mJpegBuffers;

// public data
public:

}; // class CallbacksThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_CALLBACKS_THREAD_H
