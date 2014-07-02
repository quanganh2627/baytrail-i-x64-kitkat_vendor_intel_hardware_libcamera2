/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (c) 2014 Intel Corporation
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
#include "UltraLowLight.h"
#include "MessageQueue.h"
#include "IAtomIspObserver.h"
#include "SensorThread.h"

namespace android {

    class Callbacks;

    class ICallbackAAA {
    public:
        ICallbackAAA() {}
        virtual ~ICallbackAAA() {}
        virtual void sceneDetected(String8 sceneMode, bool sceneHdr) = 0;
        virtual int getCameraID() = 0;
    };

/**
 * \class AAAThread
 *
 * AAAThread runs the actual 3A process for preview frames. In video
 * mode it also handles DVS.
 *
 * The implementation is done using I3AControls class, which has two
 * derived classes: AtomAIQ for RAW cameras and AtomSoc3A for SoC cameras.
 *
 * AAAThread also checks 3A variables and updates the trigger status for
 * Ultra Low Light algorithm
 *
 */
class AAAThread : public Thread, public IAtomIspObserver, public IOrientationListener {

// constructor destructor
public:
    AAAThread(ICallbackAAA *aaaDone, UltraLowLight *ull, I3AControls *aaaControls, sp<CallbacksThread> callbacksThread, int cameraId, bool extIsp = false);
    virtual ~AAAThread();

    enum FlashStage {
        FLASH_STAGE_NA,
        FLASH_STAGE_EXIT,
        FLASH_STAGE_PRE_START,
        FLASH_STAGE_PRE_PHASE1,
        FLASH_STAGE_PRE_PHASE2,
        FLASH_STAGE_PRE_WAITING,
        FLASH_STAGE_PRE_EXPOSED,
        FLASH_STAGE_SHOT_WAITING,
        FLASH_STAGE_SHOT_EXPOSED
    };

// prevent copy constructor and assignment operator
private:
    AAAThread(const AAAThread& other);
    AAAThread& operator=(const AAAThread& other);

// Thread overrides
public:
    status_t requestExitAndWait();

// IAtomIspObserver overrides
public:
    bool atomIspNotify(IAtomIspObserver::Message *msg, const ObserverState state);

// IOrientationListener
public:
    void orientationChanged(int orientation);

// public methods
public:

    status_t enable3A();
    status_t lockAe(bool en);
    status_t lockAwb(bool en);
    status_t autoFocus();
    status_t cancelAutoFocus();
    status_t enterFlashSequence(FlashStage blockForStage = FLASH_STAGE_NA);
    status_t exitFlashSequence();
    status_t newFrame(AtomBuffer* b);
    status_t newStats(timeval &t, unsigned int seqNo);
    status_t applyRedEyeRemoval(AtomBuffer *snapshotBuffer, AtomBuffer *postviewBuffer, int width, int height, int fourcc);
    status_t switchModeAndRate(AtomMode mode, float fps);
    status_t setFaces(const ia_face_state& faceState);
    int32_t getFaceNum(void) const;
    status_t getFaces(ia_face_state& faceState) const;
    void getCurrentSmartScene(String8 &sceneMode, bool &sceneHdr);
    void resetSmartSceneValues();

// private types
private:
    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_ENABLE_AAA,
        MESSAGE_ID_REMOVE_REDEYE,
        MESSAGE_ID_AUTO_FOCUS,
        MESSAGE_ID_CANCEL_AUTO_FOCUS,
        MESSAGE_ID_NEW_FRAME,
        MESSAGE_ID_NEW_STATS_READY,
        MESSAGE_ID_FACES,
        MESSAGE_ID_ENABLE_AE_LOCK,
        MESSAGE_ID_ENABLE_AWB_LOCK,
        MESSAGE_ID_FLASH_STAGE,
        MESSAGE_ID_SWITCH_MODE_AND_RATE,
        MESSAGE_ID_SET_ORIENTATION,
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
        int fourcc;
    };

    struct MessageFlashStage {
        FlashStage  value;
    };

    // for MESSAGE_ID_NEW_STATS_READY
    struct MessageNewStats {
        struct timeval capture_timestamp;
        unsigned int    sequence_number;
    };

    // for MESSAGE_ID_NEW_FRAME
    struct MessageNewFrame {
        const AtomBuffer *buff;
    };

    // for MESSAGE_ID_SWITCH_MODE_AND_RATE
    struct MessageSwitchInfo {
        AtomMode mode;
        float fps;
    };

    // for  MESSAGE_ID_SET_ORIENTATION
    struct MessageOrientation {
        int orientation;
    };

    // union of all message data
    union MessageData {
        MessageEnable enable;
        MessagePicture picture;
        MessageNewStats stats;
        MessageNewFrame frame;
        MessageFlashStage flashStage;
        MessageSwitchInfo switchInfo;
        MessageOrientation orientation;
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
    status_t handleMessageAutoFocus();
    status_t handleMessageCancelAutoFocus();
    status_t handleMessageNewStats(MessageNewStats *msg);
    status_t handleMessageNewFrame(MessageNewFrame *msg);
    status_t handleMessageRemoveRedEye(MessagePicture* msg);
    status_t handleMessageEnableAeLock(MessageEnable* msg);
    status_t handleMessageEnableAwbLock(MessageEnable* msg);
    status_t handleMessageFlashStage(MessageFlashStage* msg);
    status_t handleMessageSwitchModeAndRate(MessageSwitchInfo *msg);
    status_t handleMessageSetOrientation(MessageOrientation *msg);

    // Miscellaneous helper methods
    void updateULLTrigger(void);
    // External ISP "normal AF" status handler
    status_t handleAutoFocusExtIsp(const AtomBuffer *buff);
    AfStatus parseAfMeta(const AtomBuffer *buff);

    // flash sequence handler
    bool handleFlashSequence(FrameBufferStatus frameStatus);

    // main message function
    status_t waitForAndExecuteMessage();

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message, MessageId> mMessageQueue;
    bool mThreadRunning;
    I3AControls* m3AControls;
    ICallbackAAA* mAAADoneCallback;
    sp<CallbacksThread> mCallbacksThread;
    UltraLowLight *mULL;

    int mCameraId;
    bool m3ARunning;
    bool mStartAF;              // Indicates that autoFocus() command was issued by the app.
    bool mWaitForScanStart;     // Indicates the waiting for information of AF sequence start. (ATM for ext-ISP only).
    bool mStopAF;
    AfStatus mPreviousCafStatus;
    bool mPublicAeLock;
    bool mPublicAwbLock;
    size_t mFramesTillAfComplete; // used for debugging only
    String8 mSmartSceneMode; // Current detected scene mode, as defined in I3AControls.h
    bool mSmartSceneHdr; // Indicates whether the detected scene is valid for HDR
    ia_face_state mFaceState; // face metadata for 3A use
    int mPreviousFaceCount;
    FlashStage mFlashStage;
    size_t mFramesTillExposed;
    FlashStage mBlockForStage;
    unsigned int mSkipStatistics;
    unsigned int mSkipForEv; // for flash sequence
    bool mSensorEmbeddedMetaDataEnabled;
    int32_t mTrigger3A;
    bool mExtIsp;
    int mOrientation;
    IAtomIspObserver::Message mCachedStatsEventMsg;

}; // class AAAThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_AAA_THREAD_H
