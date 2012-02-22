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

#ifndef ANDROID_LIBCAMERA_AAA_THREAD_H
#define ANDROID_LIBCAMERA_AAA_THREAD_H

#include <utils/threads.h>
#include "AtomAAA.h"
#include "MessageQueue.h"

namespace android {

    class Callbacks;

// callback for when AAA thread is done with redeye removed data
    class ICallbackAAA {
    public:
        ICallbackAAA() {}
        virtual ~ICallbackAAA() {}
        virtual void redEyeRemovalDone(AtomBuffer *snapshotBuffer, AtomBuffer *postviewBuffer) = 0;
    };

class AtomAAA;

class AAAThread : public Thread {

// constructor destructor
public:
    AAAThread(ICallbackAAA *aaaDone);
    virtual ~AAAThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public methods
public:

    status_t enable3A();
    status_t enableDVS();
    status_t autoFocus();
    status_t cancelAutoFocus();
    status_t newFrame();
    status_t applyRedEyeRemoval(AtomBuffer *snapshotBuffer, AtomBuffer *postviewBuffer, int width, int height, int format);

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_ENABLE_AAA,
        MESSAGE_ID_ENABLE_DVS,
        MESSAGE_ID_REMOVE_REDEYE,
        MESSAGE_ID_AUTO_FOCUS,
        MESSAGE_ID_CANCEL_AUTO_FOCUS,
        MESSAGE_ID_NEW_FRAME,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessagePicture {
        AtomBuffer snaphotBuf;
        AtomBuffer postviewBuf;
        int width;
        int height;
        int format;
    };

    // union of all message data
    union MessageData {
        MessagePicture picture;
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
    status_t handleMessageEnable3A();
    status_t handleMessageEnableDVS();
    status_t handleMessageAutoFocus();
    status_t handleMessageCancelAutoFocus();
    status_t handleMessageNewFrame();
    status_t handleMessageRemoveRedEye(MessagePicture* msg);

    // main message function
    status_t waitForAndExecuteMessage();

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message> mMessageQueue;
    bool mThreadRunning;
    AtomAAA *mAAA;
    ICallbackAAA* mAAADoneCallback;

    bool m3ARunning;
    bool mDVSRunning;
}; // class AAAThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_AAA_THREAD_H
