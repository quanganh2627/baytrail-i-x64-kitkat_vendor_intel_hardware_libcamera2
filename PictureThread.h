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

#ifndef ANDROID_LIBCAMERA_PICTURE_THREAD_H
#define ANDROID_LIBCAMERA_PICTURE_THREAD_H

#include <utils/threads.h>
#include <camera.h>
#include <camera/CameraParameters.h>
#include "MessageQueue.h"
#include "AtomCommon.h"
#include "EXIFMaker.h"
#include "SkImageEncoder.h"

namespace android {

class Callbacks;

// callback for when Picture thread is done with yuv data
class ICallbackPicture {
public:
    ICallbackPicture() {}
    virtual ~ICallbackPicture() {}
    virtual void pictureDone(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf) = 0;
};

class PictureThread : public Thread {

// constructor destructor
public:
    PictureThread(ICallbackPicture *pictureDone);
    virtual ~PictureThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public methods
public:

    void setCallbacks(Callbacks *callbacks);

    status_t encode(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf);
    void setPictureFormat(int format)
    {
        mPictureFormat = format;
        mThumbFormat = format;
    }
    void getDefaultParameters(CameraParameters *params);
    void initialize(const CameraParameters &params, bool flashUsed);

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_ENCODE,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageEncode {
        AtomBuffer *snaphotBuf;
        AtomBuffer *postviewBuf;
    };

    // union of all message data
    union MessageData {

        // MESSAGE_ID_ENCODE
        MessageEncode encode;
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
    status_t handleMessageEncode(MessageEncode *encode);

    // main message function
    status_t waitForAndExecuteMessage();

    status_t convertRawImage(void* src, void** dst, int width, int height, int format);
    status_t encodeToJpeg(AtomBuffer *mainBuf, AtomBuffer *thumbBuf, AtomBuffer *destBuf);

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message> mMessageQueue;
    bool mThreadRunning;
    ICallbackPicture *mPictureDoneCallback;
    Callbacks *mCallbacks;
    SkImageEncoder* jpegEncoder;
    EXIFMaker exifMaker;

    int mPictureWidth;
    int mPictureHeight;
    int mPictureFormat;
    int mThumbWidth;
    int mThumbHeight;
    int mThumbFormat;
    int mPictureQuality;
    int mThumbnailQuality;

// public data
public:

}; // class PictureThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_PICTURE_THREAD_H
