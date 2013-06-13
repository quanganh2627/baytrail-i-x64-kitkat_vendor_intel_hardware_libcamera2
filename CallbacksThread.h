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
#include "intel_camera_extensions.h"

namespace android {

class Callbacks;

// callback for when callback thread is done with yuv data
class ICallbackPicture {
public:
    ICallbackPicture() {}
    virtual ~ICallbackPicture() {}
    virtual void encodingDone(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf) = 0;
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
    status_t compressedFrameDone(AtomBuffer* jpegBuf, AtomBuffer* snapshotBuf, AtomBuffer* postviewBuf);
    status_t previewFrameDone(AtomBuffer *aPreviewFrame);
    status_t videoFrameDone(AtomBuffer *buff, nsecs_t timstamp);
    status_t requestTakePicture(bool postviewCallback = false,
                                bool rawCallback = false, bool waitRendering = false);
    status_t flushPictures();
    size_t   getQueuedBuffersNum() { return mBuffers.size(); }
    virtual void facesDetected(camera_frame_metadata_t &face_metadata);
    status_t sceneDetected(int sceneMode, bool sceneHdr);
    void autofocusDone(bool status);
    void focusMove(bool start);
    void panoramaDisplUpdate(camera_panorama_metadata_t &metadata);
    void panoramaSnapshot(const AtomBuffer &livePreview);
    status_t requestULLPicture(int id);
    status_t ullTriggered(int id);
    status_t postviewRendered();
    status_t sendError(int id);

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_CALLBACK_SHUTTER,    // send the shutter callback
        MESSAGE_ID_JPEG_DATA_READY,     // we have a JPEG image ready
        MESSAGE_ID_JPEG_DATA_REQUEST,   // a JPEG image was requested
        MESSAGE_ID_AUTO_FOCUS_DONE,
        MESSAGE_ID_FOCUS_MOVE,
        MESSAGE_ID_FLUSH,
        MESSAGE_ID_FACES,
        MESSAGE_ID_SCENE_DETECTED,
        MESSAGE_ID_PREVIEW_DONE,
        MESSAGE_ID_VIDEO_DONE,
        MESSAGE_ID_POSTVIEW_RENDERED,

        // panorama callbacks
        MESSAGE_ID_PANORAMA_SNAPSHOT,
        MESSAGE_ID_PANORAMA_DISPL_UPDATE,

        // Ultra Low Light Callbacks
        MESSAGE_ID_ULL_JPEG_DATA_REQUEST,
        MESSAGE_ID_ULL_TRIGGERED,

        // Error callback
        MESSAGE_ID_ERROR_CALLBACK,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageFrame {
        AtomBuffer jpegBuff;
        AtomBuffer postviewBuff;
        AtomBuffer snapshotBuff;
    };

    struct MessagePreview {
        AtomBuffer frame;
    };

    struct MessageVideo {
        AtomBuffer  frame;
        nsecs_t timestamp;
    };

    struct MessageFaces {
        camera_frame_metadata_t meta_data;
    };

    struct MessageAutoFocusDone {
        bool status;
    };

    struct MessageFocusMove {
        bool start;
    };

    struct MessageDataRequest {
        bool postviewCallback;
        bool rawCallback;
        bool waitRendering;
    };

    struct MessageSceneDetected {
        int sceneMode;
        bool sceneHdr;
    };

    struct MessagePanoramaDisplUpdate {
        camera_panorama_metadata_t metadata;
    };

    struct MessagePanoramaSnapshot {
        AtomBuffer snapshot;
    };

    struct MessageULLSnapshot {
        AtomBuffer snapshot;
        int id;
    };

    struct MessageError {
        int id;
    };
    // union of all message data
    union MessageData {

        //MESSAGE_ID_JPEG_DATA_READY
        MessageFrame compressedFrame;

        //MESSAGE_ID_JPEG_DATA_REQUEST
        MessageDataRequest dataRequest;

        //MESSAGE_ID_AUTO_FOCUS_DONE
        MessageAutoFocusDone autoFocusDone;

        // MESSAGE_ID_FOCUS_MOVE
        MessageFocusMove focusMove;

        // MESSAGE_ID_FACES
        MessageFaces faces;

        // MESSAGE_ID_SCENE_DETECTED
        MessageSceneDetected    sceneDetected;

        // MESSAGE_ID_PREVIEW_DONE
        MessagePreview  preview;

        // MESSAGE_ID_VIDEO_DONE
        MessageVideo    video;

        // MESSAGE_ID_PANORAMA_SNAPSHOT
        MessagePanoramaSnapshot panoramaSnapshot;

        // MESSAGE_ID_PANORAMA_DISPL_UPDATE
        MessagePanoramaDisplUpdate panoramaDisplUpdate;

        // MESSAGE_ID_ULL_JPEG_DATA_REQUEST
        // MESSAGE_ID_ULL_TRIGGERED
        MessageULLSnapshot  ull;

        // MESSAGE_ID_ERROR_CALLBACK
        MessageError error;

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
    status_t handleMessageJpegDataRequest(MessageDataRequest *msg);
    status_t handleMessageAutoFocusDone(MessageAutoFocusDone *msg);
    status_t handleMessageFocusMove(MessageFocusMove *msg);
    status_t handleMessageFlush();
    status_t handleMessageFaces(MessageFaces *msg);
    status_t handleMessageSceneDetected(MessageSceneDetected *msg);
    status_t handleMessagePreviewDone(MessagePreview *msg);
    status_t handleMessageVideoDone(MessageVideo *msg);
    status_t handleMessagePanoramaDisplUpdate(MessagePanoramaDisplUpdate *msg);
    status_t handleMessagePanoramaSnapshot(MessagePanoramaSnapshot *msg);
    status_t handleMessagePostviewRendered();
    status_t handleMessageUllJpegDataRequest(MessageULLSnapshot *msg);
    status_t handleMessageUllTriggered(MessageULLSnapshot *msg);
    status_t handleMessageUllJpegDataReady(MessageFrame *msg);
    status_t handleMessageSendError(MessageError *msg);
    // main message function
    status_t waitForAndExecuteMessage();

    // Intialization of Ctrl thread callback
    void setPictureDoneCallback(ICallbackPicture *pictureDone) { mPictureDoneCallback = pictureDone; };
    void convertGfx2Regular(AtomBuffer* aGfxBuf, AtomBuffer* aRegularBuf);

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    ICallbackPicture *mPictureDoneCallback;
    MessageQueue<Message, MessageId> mMessageQueue;
    bool mThreadRunning;
    Callbacks *mCallbacks;
    unsigned mJpegRequested;
    unsigned mPostviewRequested;
    unsigned mRawRequested;
    unsigned mULLRequested;
    unsigned mULLid;
    bool mWaitRendering;
    Message mPostponedJpegReady;

    /*
     * This vector contains not only the JPEG buffers, but also their corresponding
     * MAIN and POSTVIEW raw buffers. They need to be returned back to ISP when the
     * JPEG, RAW and POSTIVEW callbacks are sent to the camera client.
     */
    Vector<MessageFrame> mBuffers;
    camera_frame_metadata_t mFaceMetadata;

// public data
public:

}; // class CallbacksThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_CALLBACKS_THREAD_H
