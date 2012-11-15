/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2012 Intel Corporation
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

#ifndef ANDROID_LIBCAMERA_PANORAMA_H
#define ANDROID_LIBCAMERA_PANORAMA_H

#include <utils/threads.h>
#include <system/camera.h>
#include <camera/CameraParameters.h>
#include "MessageQueue.h"
#include "AtomCommon.h"
#include "Callbacks.h"
#include "CallbacksThread.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "ia_panorama.h"
#ifdef __cplusplus
}
#endif

namespace android {

#define PANORAMA_MAX_COUNT 6
#define PANORAMA_MAX_BLURVALUE 12
// PREV_WIDTH & HEIGHT must be from the list CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES
#define PANORAMA_DEF_PREV_WIDTH 160
#define PANORAMA_DEF_PREV_HEIGHT 120

class ICallbackPanorama {
public:
    ICallbackPanorama() {}
    virtual ~ICallbackPanorama() {}
    virtual void panoramaCaptureTrigger(void) = 0;
    virtual void panoramaFinalized(AtomBuffer *img) = 0;
};

// public types
enum PanoramaState {
    PANORAMA_STOPPED = 0,
    PANORAMA_STARTED,
    PANORAMA_WAITING_FOR_SNAPSHOT,
    PANORAMA_DETECTING_OVERLAP
};

class PanoramaThread : public Thread, public IBufferOwner {
// constructor/destructor
public:
    PanoramaThread(ICallbackPanorama *panoramaCallback);
    ~PanoramaThread();

    void getDefaultParameters(CameraParameters *intel_params);
    void startPanorama(void);
    void stopPanorama(bool synchronous = false);
    void startPanoramaCapture(void);
    void stopPanoramaCapture(void);
    void panoramaStitch(AtomBuffer *img, AtomBuffer *pv);
    void returnBuffer(AtomBuffer *buff);

    status_t reInit();
    bool detectOverlap(ia_frame *frame);
    status_t stitch(AtomBuffer *img, AtomBuffer *pv);
    status_t cancelStitch();
    void finalize(void);
    void sendFrame(AtomBuffer &buf);
    PanoramaState getState(void);

// Thread overrides
public:
    status_t requestExitAndWait();
// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_STITCH,
        MESSAGE_ID_FRAME,
        MESSAGE_ID_START_PANORAMA,
        MESSAGE_ID_STOP_PANORAMA,
        MESSAGE_ID_START_PANORAMA_CAPTURE,
        MESSAGE_ID_STOP_PANORAMA_CAPTURE,
        MESSAGE_ID_FINALIZE,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //
    struct MessageStitch {
        AtomBuffer img;
        AtomBuffer pv;
    };

    struct MessageStopPanorama {
        bool synchronous;
    };

    struct MessageFrame {
        ia_frame frame;
    };

    // union of all message data
    union MessageData {
        // MESSAGE_ID_FRAME
        MessageStitch stitch;
        MessageStopPanorama stop;
        MessageFrame frame;
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
    status_t handleExit(void);
    status_t handleStitch(MessageStitch frame);
    status_t handleFrame(MessageFrame frame);
    status_t handleMessageStartPanorama(void);
    status_t handleMessageStopPanorama(MessageStopPanorama stop);
    status_t handleMessageStartPanoramaCapture(void);
    status_t handleMessageStopPanoramaCapture(void);
    status_t handleMessageFinalize(void);

    // main message function
    status_t waitForAndExecuteMessage();

    bool isBlurred(int width, int dx, int dy) const;

private:
    ICallbackPanorama *mPanoramaCallback;
    ia_panorama_state* mContext;
    MessageQueue<Message, MessageId> mMessageQueue;
    camera_panorama_metadata_t mCurrentMetadata;
    // counter for the entire panorama snapshots (to limit maximum nr. of snapshots)
    int mPanoramaTotalCount;
    bool mThreadRunning;
    bool mPanoramaWaitingForImage;
    CallbacksThread *mCallbacksThread;
    Callbacks *mCallbacks;
    AtomBuffer mPostviewBuf;
    PanoramaState mState;
    int mPreviewWidth;
    int mPreviewHeight;

}; // class Panorama

}; // namespace android

#endif // ANDROID_LIBCAMERA_PANORAMA_H

