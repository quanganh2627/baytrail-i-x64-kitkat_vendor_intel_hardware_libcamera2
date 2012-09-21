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
#include "IntelParameters.h"
#include <utils/List.h>
#include "MessageQueue.h"
#include "PreviewThread.h"
#include "PictureThread.h"
#include "VideoThread.h"
#include "AtomCommon.h"
#include "CallbacksThread.h"
#include "AAAThread.h"
#include "AtomAAA.h"
#include "OlaService/HalProxyOla.h"
#include "PostProcThread.h"
#include "PanoramaThread.h"
#include "CameraDump.h"
#include "CameraAreas.h"
#include "AtomCP.h"

namespace android {

#define FLASH_FRAME_TIMEOUT 5

class AtomISP;
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
    public ICallbackPostProc,
    public ICallbackPanorama,
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
    status_t storeMetaDataInBuffers(bool enabled);
    bool recordingEnabled();

    // parameter APIs
    status_t setParameters(const char *params);
    char *getParameters();
    void putParameters(char *params);
    status_t updateParameterCache(void);

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
    virtual void postProcCaptureTrigger();
    virtual void returnBuffer(AtomBuffer *buff);
    virtual void sceneDetected(int sceneMode, bool sceneHdr);
    virtual void facesDetected(camera_frame_metadata_t *face_metadata);
    virtual void panoramaCaptureTrigger();
    virtual void panoramaFinalized(AtomBuffer *buff);

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
        MESSAGE_ID_SMART_SHUTTER_PICTURE,
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
        MESSAGE_ID_SET_PREVIEW_WINDOW,
        MESSAGE_ID_SCENE_DETECTED,
        MESSAGE_ID_PANORAMA_PICTURE,
        MESSAGE_ID_PANORAMA_CAPTURE_TRIGGER,
        MESSAGE_ID_POST_PROC_CAPTURE_TRIGGER,

        // Messages for the Acceleration API temporary HACK
        MESSAGE_ID_LOAD_FIRMWARE,
        MESSAGE_ID_UNLOAD_FIRMWARE,
        MESSAGE_ID_SET_FIRMWARE_ARGUMENT,
        MESSAGE_ID_UNSET_FIRMWARE_ARGUMENT,

        // Message for enabling metadata buffer mode
        MESSAGE_ID_STORE_METADATA_IN_BUFFER,

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

    struct MessagePreviewWindow {
        struct preview_stream_ops *window;
    };

    struct MessageLoadFirmware {
        void *fwData;
        size_t size;
        unsigned int *fwHandle;
    };

    struct MessageUnloadFirmware {
        unsigned int fwHandle;
    };

    struct MessageSetFwArg {
        unsigned int fwHandle;
        unsigned int argIndex;
        void *value;
        size_t size;
    };

    struct MessageStoreMetaDataInBuffers {
        bool enabled;
    };

    struct MessageSceneDetected {
        int sceneMode;
        bool sceneHdr;
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

        // MESSAGE_ID_SET_PREVIEW_WINDOW
        MessagePreviewWindow    previewWin;

        //MESSAGE_ID_LOAD_FIRMWARE
        MessageLoadFirmware     loadFW;

        //MESSAGE_ID_UNLOAD_FIRMWARE
        MessageUnloadFirmware     unloadFW;

        //MESSAGE_ID_SET_FIRMWARE_ARGUMENT
        MessageSetFwArg     setFwArg;

        // MESSAGE_ID_STORE_METADATA_IN_BUFFER
        MessageStoreMetaDataInBuffers storeMetaDataInBuffers;

        // MESSAGE_ID_SCENE_DETECTED
        MessageSceneDetected    sceneDetected;
    };

    // message id and message data
    struct Message {
        MessageId id;
        MessageData data;
    };

    // thread states
    enum State {
        STATE_STOPPED,
        STATE_PREVIEW_NO_WINDOW,
        STATE_PREVIEW_STILL,
        STATE_PREVIEW_VIDEO,
        STATE_RECORDING,
        STATE_CAPTURE
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
        BracketingMode bracketMode;
        int  bracketNum;
        bool enabled;
        bool saveOrig;
        HdrSharpening sharpening;
        HdrVividness vividness;
        AtomBuffer outMainBuf;
        AtomBuffer outPostviewBuf;
        CiUserBuffer ciBufIn;
        CiUserBuffer ciBufOut;
    };

    struct StillPicParamsCtx {
        int    snapshotWidth;
        int    snapshotHeight;
        int    thumbnailWidth;
        int    thumbnailHeigth;
        String8 supportedSnapshotSizes;
        String8 suportedThumnailSizes;
        void clear() {
            supportedSnapshotSizes.clear();
            suportedThumnailSizes.clear();
        };
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
    status_t handleMessageTakePicture();
    status_t handleMessageTakeSmartShutterPicture();
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
    status_t handleMessageSetPreviewWindow(MessagePreviewWindow *msg);
    status_t handleMessageStoreMetaDataInBuffers(MessageStoreMetaDataInBuffers *msg);
    status_t handleMessagePanoramaPicture();
    status_t handleMessagePanoramaCaptureTrigger();

    status_t startFaceDetection();
    status_t stopFaceDetection(bool wait=false);
    status_t startSmartShutter(SmartShutterMode mode);
    status_t stopSmartShutter(SmartShutterMode mode);
    status_t cancelCaptureOnTrigger();
    status_t handleMessageFacesDetected(MessageFacesDetected* msg);
    status_t startSmartSceneDetection();
    status_t stopSmartSceneDetection();
    status_t handleMessageStopCapture();
    status_t handleMessageLoadFirmware(MessageLoadFirmware* msg);
    status_t handleMessageUnloadFirmware(MessageUnloadFirmware* msg);
    status_t handleMessageSetFirmwareArgument(MessageSetFwArg* msg);
    status_t handleMessageUnsetFirmwareArgument(MessageSetFwArg* msg);
    status_t enableIntelParameters();
    void releasePreviewFrame(AtomBuffer* buff);
    status_t handleMessageSceneDetected(MessageSceneDetected *msg);
    status_t startPanorama();
    status_t stopPanorama();
    status_t startFaceRecognition();
    status_t stopFaceRecognition();

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
    status_t skipPreviewFrames(int numFrames, AtomBuffer* buff);
    status_t setSmartSceneParams();

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
    status_t processParamGDC(const CameraParameters *oldParams,
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
    status_t processParamSmartShutter(const CameraParameters *oldParams,
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
    status_t processParamAwbMappingMode(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamTNR(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t processParamRawDataFormat(const CameraParameters *oldParams,
            CameraParameters *newParams);

    void processParamFileInject(CameraParameters *newParams);

    void preSetCameraWindows(CameraWindow* focusWindows, size_t winCount);


    // These are params that can only be set while the ISP is stopped. If the parameters
    // changed while the ISP is running, the ISP will need to be stopped, reconfigured, and
    // restarted. Static parameters will most likely affect buffer size and/or format so buffers
    // must be deallocated and reallocated accordingly.
    status_t processStaticParameters(const CameraParameters *oldParams,
            CameraParameters *newParams);
    status_t validateParameters(const CameraParameters *params);
    // validation helpers
    bool validateSize(int width, int height, Vector<Size> &supportedSizes) const;

    status_t stopCapture();

    // HDR helper functions
    status_t hdrInit(int size, int pvSize, int format,
                     int width, int height,
                     int pvWidth, int pvHeight);
    status_t hdrProcess(AtomBuffer * snapshotBuffer, AtomBuffer* postviewBuffer);
    status_t hdrCompose();
    void     hdrRelease();
    status_t allocateSnapshotBuffers();
    void     setExternalSnapshotBuffers(int format, int width, int heigth);

    // Capture Flow helpers
    status_t getFlashExposedSnapshot(AtomBuffer *snaphotBuffer, AtomBuffer *postviewBuffer);
    void     fillPicMetaData(PictureThread::MetaData &metadata, bool flashFired);

    status_t captureStillPic();
    status_t captureBurstPic(bool clientRequest);
    status_t capturePanoramaPic(AtomBuffer &snapshotBuffer, AtomBuffer &postviewBuffer);
    status_t captureVideoSnap(void);
    void     encodeVideoSnapshot(int buffId);

    status_t updateSpotWindow(const int &width, const int &height);

    MeteringMode aeMeteringModeFromString(const String8& modeStr);

    void storeCurrentPictureParams();
    void restoreCurrentPictureParams();

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    AtomISP *mISP;
    AtomAAA *mAAA;
    AtomDvs *mDvs;
    AtomCP  *mCP;
    sp<PreviewThread> mPreviewThread;
    sp<PictureThread> mPictureThread;
    sp<VideoThread> mVideoThread;
    sp<AAAThread>     m3AThread;
    sp<PostProcThread> mPostProcThread;
    sp<PanoramaThread> mPanoramaThread;
    sp<HalProxyOla>   mProxyToOlaService;
    friend class HalProxyOla;

    MessageQueue<Message, MessageId> mMessageQueue;
    State mState;
    bool mThreadRunning;
    Callbacks *mCallbacks;
    sp<CallbacksThread> mCallbacksThread;

    CoupledBuffer *mCoupledBuffers;
    int mNumBuffers;

    CameraParameters mParameters;
    CameraParameters mIntelParameters;
    bool mIntelParamsAllowed;           /*<! Flag that signals whether the caller is allowed to use Intel extended paramters*/

    bool mFaceDetectionActive;
    bool mAutoFocusActive;
    bool mFlashAutoFocus;
    int  mBurstSkipFrames;
    int  mBurstLength;
    int  mBurstCaptureNum;
    BracketingType mBracketing;
    List<SensorAeConfig> mBracketingParams;
    HdrImaging mHdr;
    bool mAELockFlashNeed;
    AeMode mPublicAeMode;       /* AE mode set by application */
    AfMode mPublicAfMode;       /* AF mode set by application */
    float mPublicShutter;       /* Shutter set by application */

    Mutex mParamCacheLock;
    char* mParamCache;

    int mLastRecordingBuffIndex;
    bool mStoreMetaDataInBuffers;

    bool mPreviewForceChanged;

    CameraDump *mCameraDump;

    CameraAreas mFocusAreas;
    CameraAreas mMeteringAreas;

    bool mIsPreviewStartComplete;   /*!< Flag that signals the completion of the start preview process
                                         set to false when we receive the start preview command
                                         set to true when the first preview frame is returned to
                                         ControlThread */

    int mVideoSnapshotrequested;    /*!< number of video snapshots requested */

    struct StillPicParamsCtx mStillPictContext; /*!< we store the current still image parameters
                                                    It is used when video recording starts so the settings
                                                    can be restore when video recording stops
                                                 */
    Vector<MessagePicture> mUnqueuedPicBuf; /* store the buffers that have not been returned to ISP in capturing*/

}; // class ControlThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_CONTROL_THREAD_H
