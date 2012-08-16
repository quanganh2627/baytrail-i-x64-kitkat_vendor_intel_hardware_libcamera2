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
#include <time.h>
#include "AtomAAA.h"
#include "MessageQueue.h"

namespace android {

    class Callbacks;

    class ICallbackAAA {
    public:
        ICallbackAAA() {}
        virtual ~ICallbackAAA() {}
        virtual void autoFocusDone() = 0;
        virtual void sceneDetected(int sceneMode, bool sceneHdr) = 0;
    };

class AtomAAA;

/**
 * \class AAAThread
 *
 * AAAThread runs the actual 3A process for preview frames. In video
 * mode it also handles DVS.
 *
 * The implementation is done using AtomAAA singleton class,
 * but note that AtomAAA offers a much wider set of features and not
 * just 3A, so the two classes should not be confused. Please refer to
 * AtomAAA class documentation for more information.
 *
 * TODO: In long term, the goal is to get rid of AtomAAA singleton
 *       and use instance of Intel 3A libraries directly from
 *       AAAThread. But this is not yet supported by the underlying
 *       libraries so for now AtomAAA is used.
 */
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
    status_t enableDVS(bool en);
    status_t autoFocus();
    status_t cancelAutoFocus();
    status_t newFrame(struct timeval capture_timestamp);
    status_t applyRedEyeRemoval(AtomBuffer *snapshotBuffer, AtomBuffer *postviewBuffer, int width, int height, int format);
    status_t setFaces(camera_frame_metadata_t *face_metadata, int zoom);
    void getCurrentSmartScene(int &sceneMode, bool &sceneHdr);

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
        MESSAGE_ID_FACES,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageEnable {
        bool enable;
    };

    struct MessagePicture {
        AtomBuffer snaphotBuf;
        AtomBuffer postviewBuf;
        int width;
        int height;
        int format;
    };

    // for MESSAGE_ID_NEW_FRAME
    struct MessageNewFrame {
        struct timeval capture_timestamp;
    };

    // union of all message data
    union MessageData {
        MessageEnable enable;
        MessagePicture picture;
        MessageNewFrame frame;
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
    status_t handleMessageEnableDVS(MessageEnable* msg);
    status_t handleMessageAutoFocus();
    status_t handleMessageCancelAutoFocus();
    status_t handleMessageNewFrame(struct timeval capture_timestamp);
    status_t handleMessageRemoveRedEye(MessagePicture* msg);

    // main message function
    status_t waitForAndExecuteMessage();

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message, MessageId> mMessageQueue;
    bool mThreadRunning;
    AtomAAA *mAAA;
    Callbacks *mCallbacks;
    ICallbackAAA* mAAADoneCallback;

    bool m3ARunning;
    bool mDVSRunning;
    bool mStartAF;
    bool mStopAF;
    bool mAfAeWasLocked;
    bool mAfAwbWasLocked;
    size_t mFramesTillAfComplete; // used for debugging only
    int mSmartSceneMode; // Current detected scene mode, as defined in ia_aiq_external_toolbox.h
    bool mSmartSceneHdr; // Indicates whether the detected scene is valid for HDR
    camera_frame_metadata_t mFaceMetadata; // face metadata for smart scene detection
    int mCurrentZoom; // current zoom level for smart scene detection
}; // class AAAThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_AAA_THREAD_H
