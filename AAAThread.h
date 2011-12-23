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
#include "MessageQueue.h"

namespace android {

class AtomAAA;

class AAAThread : public Thread {

// constructor destructor
public:
    AAAThread();
    virtual ~AAAThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public methods
public:

    status_t autoFocus();
    status_t cancelAutoFocus();
    status_t runAAA(); // TODO: make sure we want this message
    status_t runDVS(); // TODO: make sure we want this message

    // TODO: need methods to configure AAA
    // TODO: decide if configuration method should send a message

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_AUTO_FOCUS,
        MESSAGE_ID_CANCEL_AUTO_FOCUS,
        MESSAGE_ID_RUN_AAA,
        MESSAGE_ID_RUN_DVS,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    // union of all message data
    union MessageData {
        void *placeHolder; // TODO: add message data if necessary
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
    status_t handleMessageAutoFocus();
    status_t handleMessageCancelAutoFocus();
    status_t handleMessageRunAAA();
    status_t handleMessageRunDVS();

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

}; // class AAAThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_AAA_THREAD_H
