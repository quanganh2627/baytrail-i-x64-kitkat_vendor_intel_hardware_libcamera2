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

#ifndef ANDROID_LIBCAMERA_CONTROL_THREAD_H
#define ANDROID_LIBCAMERA_CONTROL_THREAD_H

#include <utils/threads.h>
#include <limits.h>

#include <camera.h>
#include <camera/CameraParameters.h>
#include <utils/List.h>
#include "IntelCamera.h"
#include "MessageQueue.h"
#include "PreviewThread.h"
#include "PictureThread.h"
#include "VideoThread.h"
#include "AtomCommon.h"
#include "CallbacksThread.h"
#include "AAAThread.h"
#include "AtomAAA.h"
#include "IFaceDetectionListener.h"
namespace android {

#define FLASH_FRAME_TIMEOUT 5

class AtomISP;
class BufferShareRegistry;
class IFaceDetector;
//
// ControlThread implements most of the operations defined
// by camera_device_ops_t. Refer to hardware/camera.h
// for documentation on each operation.
//
class ControlThread :
    public Thread,
    public ICallbackPreview,
    public ICallbackPicture,
    public ICallbackAAA,
    public IBufferOwner{

// constructor destructor
public:
    ControlThread();
    virtual ~ControlThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public methods
public:

    status_t init(int cameraId);
    void deinit();

    status_t setPreviewWindow(struct preview_stream_ops *window);

    // message callbacks
    void setCallbacks(camera_notify_callback notify_cb,
                      camera_data_callback data_cb,
                      camera_data_timestamp_callback data_cb_timestamp,
                      camera_request_memory get_memory,
                      void* user);
    void enableMsgType(int32_t msg_type);
    void disableMsgType(int32_t msg_type);
    bool msgTypeEnabled(int32_t msg_type);

    // synchronous (blocking) state machine methods
    status_t startPreview();
    status_t stopPreview();
    status_t startRecording();
    status_t stopRecording();

    void sendCommand( int32_t cmd, int32_t arg1, int32_t arg2);

    // return true if preview or recording is enabled
    bool previewEnabled();
    bool recordingEnabled();

    // parameter APIs
    status_t setParameters(const char *params);
    char *getParameters();
    void putParameters(char *params);

    // snapshot (asynchronous)
    status_t takePicture();
    status_t cancelPicture();

    // autofocus commands (asynchronous)
    status_t autoFocus();
    status_t cancelAutoFocus();

    // return recording frame to driver (asynchronous)
    status_t releaseRecordingFrame(void *buff);

    // TODO: need methods to configure control thread
    // TODO: decide if configuration method should send a message

// callback methods
private:
    virtual void previewDone(AtomBuffer *buff);
    virtual void pictureDone(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf);
    virtual void autoFocusDone();
    virtual void returnBuffer(AtomBuffer *buff);

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_START_PREVIEW,
        MESSAGE_ID_STOP_PREVIEW,
        MESSAGE_ID_START_RECORDING,
        MESSAGE_ID_STOP_RECORDING,
        MESSAGE_ID_TAKE_PICTURE,
        MESSAGE_ID_CANCEL_PICTURE,
        MESSAGE_ID_AUTO_FOCUS,
        MESSAGE_ID_CANCEL_AUTO_FOCUS,
        MESSAGE_ID_RELEASE_RECORDING_FRAME,
        MESSAGE_ID_RELEASE_PREVIEW_FRAME,//This is only a callback from other
                                         // HAL threads to signal preview buffer
                                         // is not used and is free to queue back
                                         // AtomISP.
        MESSAGE_ID_PREVIEW_DONE,
        MESSAGE_ID_PICTURE_DONE,
        MESSAGE_ID_SET_PARAMETERS,
        MESSAGE_ID_GET_PARAMETERS,
        MESSAGE_ID_AUTO_FOCUS_DONE,
        MESSAGE_ID_COMMAND,
        MESSAGE_ID_FACES_DETECTED,
        MESSAGE_ID_STOP_CAPTURE,
        MESSAGE_ID_CONFIGURE_FILE_INJECT,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageReleaseRecordingFrame {
        void *buff;
    };
    struct MessageReleasePreviewFrame {
        AtomBuffer buff;
    };
    struct MessagePreviewDone {
        AtomBuffer buff;
    };

    struct MessagePicture {
        AtomBuffer snapshotBuf;
        AtomBuffer postviewBuf;
    };

    struct MessageSetParameters {
        char* params;
    };

    struct MessageGetParameters {
        char** params;
    };

    struct MessageCommand{
        int32_t cmd_id;
        int32_t arg1;
        int32_t arg2;
    };

    struct MessageFacesDetected {
        camera_frame_metadata_t* meta;
        AtomBuffer buf;
    };

    struct MessageConfigureFileInject {
        char fileName[PATH_MAX];
        int width;
        int height;
        int format;
        int bayerOrder;
    };

    // union of all message data
    union MessageData {

        // MESSAGE_ID_RELEASE_RECORDING_FRAME
        MessageReleaseRecordingFrame releaseRecordingFrame;

        // MESSAGE_ID_RELEASE_PREVIEW_FRAME
        MessageReleasePreviewFrame releasePreviewFrame;

        // MESSAGE_ID_PREVIEW_DONE
        MessagePreviewDone previewDone;

        // MESSAGE_ID_PICTURE_DONE
        MessagePicture pictureDone;

        // MESSAGE_ID_SET_PARAMETERS
        MessageSetParameters setParameters;

        // MESSAGE_ID_GET_PARAMETERS
        MessageGetParameters getParameters;

        // MESSAGE_ID_COMMAND
        MessageCommand command;
        //MESSAGE_ID_FACES_DETECTED
        MessageFacesDetected FacesDetected;

        // MESSAGE_ID_CONFIGURE_FILE_INJECT
        MessageConfigureFileInject configureFileInject;
    };

    // message id and message data
    struct Message {
        MessageId id;
        MessageData data;
    };

    // thread states
    enum State {
        STATE_STOPPED,
        STATE_PREVIEW_STILL,
        STATE_PREVIEW_VIDEO,
        STATE_RECORDING,
        STATE_CAPTURE,
    };

    // Buffer sharing states listed in the order of transition.
    // Once we have propagated through all states we cycle back
    // to the beginning
    //
    // The reason for all these states is that the encoder component is loaded
    // after the camera is put into video recording mode. However, the encoder
    // component is responsible for allocating the shared buffers. So, once the
    // encoder component is loaded, we have to do a handshake with the encoder
    // to establish buffer sharing. Once we have the shared buffers, we have to
    // stop and restart the ISP with the new buffers.
    enum BSState {
        // This is the initial state
        //
        // TRIGGER: None
        BS_STATE_DISABLED,

        // In this state we have enabled buffer sharing
        // Need to wait for encoder to enable buffer sharing to transition
        // to the next state.
        //
        // TRIGGER: BufferShareRegistry::sourceRequestToEnableSharingMode() succeeds
        BS_STATE_ENABLE,

        // In this state we have set buffer sharing. Need to wait for
        // encoder to set buffer sharing before we transition to the next
        // state
        //
        // TRIGGER: BufferShareRegistry::sourceEnterSharingMode() succeeds
        BS_STATE_SET,

        // In this state encoder has set buffer sharing. We should now
        // be able to send buffers back and forth. Consider this the
        // steady state. When encoder has unset buffer sharing we will
        // need to follow suit and transition to the next state.
        //
        // TRIGGER: BufferShareRegistry::isBufferSharingModeSet() returns true
        BS_STATE_STEADY,

        // In this state we have unset buffer sharing. The cycle has
        // been completed
        //
        // TRIGGER: BufferShareRegistry::isBufferSharingModeSet() returns false and
        //          BufferShareRegistry::sourceExitSharingMode() succeeds
        BS_STATE_UNSET,
    };

    struct CoupledBuffer {
        AtomBuffer previewBuff;
        AtomBuffer recordingBuff;
        bool previewBuffReturned;
        bool recordingBuffReturned;
        bool videoSnapshotBuff;
        bool videoSnapshotBuffReturned;
    };

    enum BracketingMode {
        BRACKET_NONE = 0,
        BRACKET_EXPOSURE,
        BRACKET_FOCUS,
    };

    struct BracketingType {
        BracketingMode mode;
        float minValue;
        float maxValue;
        float currentValue;
        float step;
    };

    struct HdrImaging {
        enum HdrSharpening {
            NO_SHARPENING = 0,
            NORMAL_SHARPENING,
            STRONG_SHARPENING
        };

        enum HdrVividness {
            NO_VIVIDNESS = 0,
            GAUSSIAN_VIVIDNESS,
            GAMMA_VIVIDNESS
        };

        BracketingMode bracketMode;
        int  bracketNum;
        bool enabled;
        bool appSaveOrig;
        bool appSaveOrigRequest;
        bool saveOrig;
        bool saveOrigRequest;
        HdrSharpening sharpening;
        HdrVividness vividness;
        AtomBuffer outMainBuf;
        AtomBuffer outPostviewBuf;
        CiUserBuffer ciBufIn;
        CiUserBuffer ciBufOut;
    };

// private methods
private:

    // state machine helper functions
    status_t restartPreview(bool videoMode);
    status_t startPreviewCore(bool videoMode);
    status_t stopPreviewCore();

    // thread message execution functions
    status_t handleMessageExit();
    status_t handleMessageStartPreview();
    status_t handleMessageStopPreview();
    status_t handleMessageStartRecording();
    status_t handleMessageStopRecording();
    status_t handleMessageTakePicture(bool clientRequest = true);
    status_t handleMessageCancelPicture();
    status_t handleMessageAutoFocus();
    status_t handleMessageCancelAutoFocus();
    status_t handleMessageReleaseRecordingFrame(MessageReleaseRecordingFrame *msg);
    status_t handleMessageReleasePreviewFrame(MessageReleasePreviewFrame *msg);
    status_t handleMessagePreviewDone(MessagePreviewDone *msg);
    status_t handleMessagePictureDone(MessagePicture *msg);
    status_t handleMessageSetParameters(MessageSetParameters *msg);
    status_t handleMessageGetParameters(MessageGetParameters *msg);
    status_t handleMessageAutoFocusDone();
    status_t handleMessageCommand(MessageCommand* msg);
    status_t handleMessageConfigureFileInject(MessageConfigureFileInject *msg);

    status_t startFaceDetection();
    status_t stopFaceDetection(bool wait=false);
    status_t handleMessageFacesDetected(MessageFacesDetected* msg);
    status_t handleMessageStopCapture();
    void releasePreviewFrame(AtomBuffer* buff);

    // main message function
    status_t waitForAndExecuteMessage();

    AtomBuffer* findRecordingBuffer(void *findMe);

    // dequeue buffers from driver and deliver them
    status_t dequeuePreview();
    status_t dequeueRecording();
    status_t queueCoupledBuffers(int coupledId);

    status_t skipFrames(size_t numFrames, size_t doBracket = 0);
    status_t initBracketing();
    status_t applyBracketing();

    bool runPreFlashSequence();

    // parameters handling functions
    bool isParameterSet(const char* param);
    String8 paramsReturnNewIfChanged(const CameraParameters *oldParams,
            CameraParameters *newParams,
            const char *key);

    // These are parameters that can be set while the ISP is running (most params can be
    // set while the isp is stopped as well).
    status_t processDynamicParameters(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamFlash(const CameraParameters *oldParams,
                CameraParameters *newParams);
    status_t processParamAELock(const CameraParameters *oldParams,
                CameraParameters *newParams);
    status_t processParamAFLock(const CameraParameters *oldParams,
                CameraParameters *newParams);
    status_t processParamAWBLock(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamEffect(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamSceneMode(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamXNR_ANR(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamAntiBanding(const CameraParameters *oldParams,
                                           CameraParameters *newParams);
    status_t processParamFocusMode(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamWhiteBalance(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamSetMeteringAreas(const CameraParameters * oldParams,
            CameraParameters * newParams);
    status_t processParamBracket(const CameraParameters *oldParams,
                CameraParameters *newParams);
    status_t processParamHDR(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamExposureCompensation(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamAutoExposureMode(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamAutoExposureMeteringMode(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamIso(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamShutter(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamBackLightingCorrectionMode(const CameraParameters *oldParams,
            CameraParameters *newParams);

    bool verifyCameraWindow(const CameraWindow &win);
    void preSetCameraWindows(CameraWindow* focusWindows, size_t winCount);


    // These are params that can only be set while the ISP is stopped. If the parameters
    // changed while the ISP is running, the ISP will need to be stopped, reconfigured, and
    // restarted. Static parameters will most likely affect buffer size and/or format so buffers
    // must be deallocated and reallocated accordingly.
    status_t processStaticParameters(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t validateParameters(const CameraParameters *params);

    // buffer sharing enable/disable methods
    bool recordingBSEncoderEnabled();
    status_t recordingBSEnable();
    status_t recordingBSDisable();

    // buffer sharing set/unset methods
    status_t recordingBSSet();
    status_t recordingBSUnset();
    bool recordingBSEncoderSet();

    // buffer sharing handshake. see comments for enum BSState
    status_t recordingBSHandshake();

    status_t stopCapture();


// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    AtomISP *mISP;
    AtomAAA *mAAA;
    sp<PreviewThread> mPreviewThread;
    sp<PictureThread> mPictureThread;
    sp<VideoThread> mVideoThread;
    sp<AAAThread>     m3AThread;

    MessageQueue<Message, MessageId> mMessageQueue;
    State mState;
    bool mThreadRunning;
    Callbacks *mCallbacks;
    sp<CallbacksThread> mCallbacksThread;

    CoupledBuffer *mCoupledBuffers;
    int mNumBuffers;

    CameraParameters mParameters;
    IFaceDetector* m_pFaceDetector;
    bool mIntelParamsAllowed;           /*<! Flag that signals whether the caller is allowed to use Intel extended paramters*/
    bool mFaceDetectionActive;
    bool mAutoFocusActive;
    bool mFlashAutoFocus;
    int  mBurstSkipFrames;
    int  mBurstLength;
    int  mBurstCaptureNum;
    BracketingType mBracketing;
    List<SensorParams> mBracketingParams;
    HdrImaging mHdr;
    AeMode mPublicAeMode;    /* AE mode set by application */
    AfMode mPublicAfMode;    /* AF mode set by application */

    sp<BufferShareRegistry> mBSInstance;

    BSState mBSState;

    int mLastRecordingBuffIndex;

}; // class ControlThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_CONTROL_THREAD_H
