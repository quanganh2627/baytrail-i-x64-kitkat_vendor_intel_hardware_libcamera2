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
#include "JpegCompressor.h"

namespace android {

class Callbacks;
class CallbacksThread;


class PictureThread : public Thread {

// constructor destructor
public:
    PictureThread();
    virtual ~PictureThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public methods
public:

    status_t encode(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf = NULL);
    void setPictureFormat(int format)
    {
        mPictureFormat = format;
        mThumbFormat = format;
    }
    void getDefaultParameters(CameraParameters *params);
    void initialize(const CameraParameters &params, const atomisp_makernote_info &makerNote, bool flashUsed);
    void setNumberOfShots(int num);
    status_t getSharedBuffers(int width, int height, void** sharedBuffersPtr, int sharedBuffersNum);
    status_t allocSharedBuffers(int width, int height, int sharedBuffersNum);
    status_t wait(); // wait to finish queued messages (sync)
    status_t flushBuffers();

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_ENCODE,
        MESSAGE_ID_ALLOC_BUFS,
        MESSAGE_ID_WAIT,
        MESSAGE_ID_FLUSH,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageAllocBufs {
        int width;
        int height;
        int numBufs;
    };

    struct MessageEncode {
        AtomBuffer snaphotBuf;
        AtomBuffer postviewBuf;
    };

    // union of all message data
    union MessageData {

        // MESSAGE_ID_ENCODE
        MessageEncode encode;
        MessageAllocBufs alloc;
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
    status_t handleMessageAllocBufs(MessageAllocBufs *alloc);
    status_t handleMessageWait();
    status_t handleMessageFlush();

    // main message function
    status_t waitForAndExecuteMessage();

    status_t encodeToJpeg(AtomBuffer *mainBuf, AtomBuffer *thumbBuf, AtomBuffer *destBuf);

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message, MessageId> mMessageQueue;
    bool mThreadRunning;
    Callbacks *mCallbacks;
    CallbacksThread *mCallbacksThread;
    JpegCompressor compressor;
    EXIFMaker exifMaker;
    AtomBuffer mExifBuf;
    AtomBuffer mOutBuf;

    int mPictureWidth;
    int mPictureHeight;
    int mPictureFormat;
    int mThumbWidth;
    int mThumbHeight;
    int mThumbFormat;
    int mPictureQuality;
    int mThumbnailQuality;
    bool mUsingSharedBuffers;

// public data
public:

}; // class PictureThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_PICTURE_THREAD_H
