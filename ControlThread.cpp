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
#define LOG_TAG "Camera_ControlThread"

#include "ControlThread.h"
#include "LogHelper.h"
#include "PerformanceTraces.h"
#include "CameraConf.h"
#include "PreviewThread.h"
#include "ImageScaler.h"
#include "PictureThread.h"
#include "AtomISP.h"
#include "Callbacks.h"
#include "CallbacksThread.h"
#include "ColorConverter.h"
#include "PlatformData.h"
#include "IntelParameters.h"
#include <utils/Vector.h>
#include <math.h>
#include <cutils/properties.h>
#include <binder/IServiceManager.h>
#include "intel_camera_extensions.h"

namespace android {

/*
 * NUM_WARMUP_FRAMES: used for front camera only
 * Since front camera does not 3A, it actually has 2A (auto-exposure and auto-whitebalance),
 * it needs about 4 for internal 2A from driver to gather enough information and establish
 * the correct values for 2A.
 */
#define NUM_WARMUP_FRAMES 4
/*
 * NUM_BURST_BUFFERS: used for burst captures
 */
#define NUM_BURST_BUFFERS 10
/*
 * NUM_BURST_BUFFERS: used for single still capture
 */
#define NUM_SINGLE_STILL_BUFFERS 1
/*
 * MAX_JPEG_BUFFERS: the maximum numbers of queued JPEG buffers
 */
#define MAX_JPEG_BUFFERS 4
/*
 * FLASH_TIMEOUT_FRAMES: maximum number of frames to wait for
 * a correctly exposed frame
 */
#define FLASH_TIMEOUT_FRAMES 5
/*
 * ASPECT_TOLERANCE: the tolerance between aspect ratios to consider them the same
 */
#define ASPECT_TOLERANCE 0.001

/*
 * DEFAULT_HDR_BRACKETING: the number of bracketed captures to be made in order to compose
 * a HDR image.
 */
#define DEFAULT_HDR_BRACKETING 3

// Minimum value of our supported preview FPS
const int MIN_PREVIEW_FPS = 11;
// Max value of our supported preview fps:
// TODO: This value should be gotten from sensor dynamically, instead of hardcoding:
const int MAX_PREVIEW_FPS = 30;

ControlThread::ControlThread(const sp<CameraConf>& cfg) :
    Thread(true) // callbacks may call into java
    ,mCameraConf(cfg)
    ,mISP(NULL)
    ,mAAA(NULL)
    ,mDvs(NULL)
    ,mCP(NULL)
    ,mPreviewThread(NULL)
    ,mPictureThread(NULL)
    ,mVideoThread(NULL)
    ,m3AThread(NULL)
    ,mMessageQueue("ControlThread", (int) MESSAGE_ID_MAX)
    ,mState(STATE_STOPPED)
    ,mThreadRunning(false)
    ,mCallbacks(NULL)
    ,mCallbacksThread(NULL)
    ,mCoupledBuffers(NULL)
    ,mNumBuffers(0)
    ,mIntelParamsAllowed(false)
    ,mFaceDetectionActive(false)
    ,mFlashAutoFocus(false)
    ,mFpsAdaptSkip(0)
    ,mBurstLength(0)
    ,mBurstStart(0)
    ,mBurstCaptureNum(-1)
    ,mBurstCaptureDoneNum(-1)
    ,mBurstQbufs(0)
    ,mAELockFlashNeed(false)
    ,mPublicShutter(-1)
    ,mParamCache(NULL)
    ,mStoreMetaDataInBuffers(false)
    ,mPreviewForceChanged(false)
    ,mPreviewStartQueued(false)
    ,mCameraDump(NULL)
    ,mFocusAreas()
    ,mMeteringAreas()
    ,mPreviewFramesDone(0)
    ,mVideoSnapshotrequested(0)
    ,mSetFPS(MAX_PREVIEW_FPS)
{
    // DO NOT PUT ANY ALLOCATION CODE IN THIS METHOD!!!
    // Put all init code in the init() method.
    // This is a workaround for an issue with Thread reference counting.

    LOG1("@%s", __FUNCTION__);
}

ControlThread::~ControlThread()
{
    // DO NOT PUT ANY CODE IN THIS METHOD!!!
    // Put all deinit code in the deinit() method.
    // This is a workaround for an issue with Thread reference counting.
    LOG1("@%s", __FUNCTION__);
    if(mMessageQueue.size() > 0) {
        Message msg;
        LOGE("At this point Message Q should be empty, found %d message(s)",mMessageQueue.size());
        mMessageQueue.receive(&msg);
        LOGE(" Id of first message is %d",msg.id);
    }
}

status_t ControlThread::init()
{
    LOG1("@%s: cameraId = %d", __FUNCTION__, mCameraConf->cameraId());
    int cameraId;

    if (mCameraConf == 0) {
        LOGE("ERROR no CPF info given for Control Thread in %s", __FUNCTION__);
        return NO_MEMORY;
    }

    status_t status = UNKNOWN_ERROR;

    mISP = new AtomISP(mCameraConf);
    if (mISP == NULL) {
        LOGE("error creating ISP");
        goto bail;
    }

    status = mISP->init();
    if (status != NO_ERROR) {
        LOGE("Error initializing ISP");
        goto bail;
    }

    mDvs = new AtomDvs(mISP);
    if (mDvs == NULL) {
        LOGE("error creating DVS");
        goto bail;
    }
    PERFORMANCE_TRACES_LAUNCH2PREVIEW_STEP("ISP_Init_Done");

    mAAA = AtomAAA::getInstance();
    if (mAAA == NULL) {
        LOGE("error creating AAA");
        goto bail;
    }

    // Choose 3A interface based on the sensor type
    if (PlatformData::sensorType(mISP->getCurrentCameraId()) == SENSOR_TYPE_RAW) {
        m3AControls = mAAA;
    } else {
        m3AControls = mISP;
    }

    mCP = new AtomCP(mISP);
    if (mCP == NULL) {
        LOGE("error creating CP");
        goto bail;
    }

    CameraDump::setDumpDataFlag();
    if (CameraDump::isDumpImageEnable()) {
        mCameraDump = CameraDump::getInstance();
        if (mCameraDump == NULL) {
            LOGE("error creating CameraDump");
            goto bail;
        }
    }

    // we implement the ICallbackPreview interface, so pass
    // this as argument
    mPreviewThread = new PreviewThread(this);
    if (mPreviewThread == NULL) {
        LOGE("error creating PreviewThread");
        goto bail;
    }

    mPictureThread = new PictureThread();
    if (mPictureThread == NULL) {
        LOGE("error creating PictureThread");
        goto bail;
    }

    mVideoThread = new VideoThread();
    if (mVideoThread == NULL) {
        LOGE("error creating VideoThread");
        goto bail;
    }

    // we implement ICallbackAAA interface
    m3AThread = new AAAThread(this, mDvs);
    if (m3AThread == NULL) {
        LOGE("error creating 3AThread");
        goto bail;
    }

    mCallbacks = Callbacks::getInstance();
    if (mCallbacks == NULL) {
        LOGE("error creating Callbacks");
        goto bail;
    }

    // we implement ICallbackPicture interface
    mCallbacksThread = CallbacksThread::getInstance(this);
    if (mCallbacksThread == NULL) {
        LOGE("error creating CallbacksThread");
        goto bail;
    }

    mPanoramaThread = new PanoramaThread(this);
    if (mPanoramaThread == NULL) {
        LOGE("error creating PanoramaThread");
        goto bail;
    }

    mPostProcThread = new PostProcThread(this, mPanoramaThread.get());
    if (mPostProcThread == NULL) {
        LOGE("error creating PostProcThread");
        goto bail;
    }

    if (mPostProcThread->init((void*)mISP) != NO_ERROR) {
        LOGE("error initializing face engine");
        goto bail;
    }

    mBracketManager = new BracketManager(mISP);
    if (mBracketManager == NULL) {
        LOGE("error creating BracketManager");
        goto bail;
    }

    PERFORMANCE_TRACES_LAUNCH2PREVIEW_STEP("New_Other_Threads");

    cameraId = mISP->getCurrentCameraId();
    // get default params from AtomISP and JPEG encoder
    mISP->getDefaultParameters(&mParameters, &mIntelParameters);
    m3AControls->getDefaultParams(&mParameters, &mIntelParameters);
    mPictureThread->getDefaultParameters(&mParameters);
    mPreviewThread->getDefaultParameters(&mParameters);
    mPanoramaThread->getDefaultParameters(&mIntelParameters, cameraId);
    mPostProcThread->getDefaultParameters(&mParameters, &mIntelParameters, cameraId);
    mVideoThread->getDefaultParameters(&mIntelParameters, cameraId);
    updateParameterCache();

    status = m3AThread->run();
    if (status != NO_ERROR) {
        LOGE("Error starting 3A thread!");
        goto bail;
    }
    status = mPreviewThread->run();
    if (status != NO_ERROR) {
        LOGE("Error starting preview thread!");
        goto bail;
    }
    status = mPictureThread->run();
    if (status != NO_ERROR) {
        LOGW("Error starting picture thread!");
        goto bail;
    }
    status = mCallbacksThread->run();
    if (status != NO_ERROR) {
        LOGW("Error starting callbacks thread!");
        goto bail;
    }
    status = mVideoThread->run();
    if (status != NO_ERROR) {
        LOGW("Error starting video thread!");
        goto bail;
    }
    status = mPostProcThread->run();
    if (status != NO_ERROR) {
        LOGW("Error starting Post Processing thread!");
        goto bail;
    }
    status = mPanoramaThread->run();
    if (status != NO_ERROR) {
        LOGW("Error Starting Panorama Thread!");
        goto bail;
    }
    status = mBracketManager->run();
    if (status != NO_ERROR) {
        LOGW("Error Starting Bracketing Manager!");
        goto bail;
    }

    // Disable bracketing by default
    mBracketManager->setBracketMode(BRACKET_NONE);

    // Disable HDR by default
    mHdr.enabled = false;
    mHdr.savedBracketMode = BRACKET_NONE;
    mHdr.sharpening = NORMAL_SHARPENING;
    mHdr.vividness = GAUSSIAN_VIVIDNESS;
    mHdr.saveOrig = false;

    //default flash modes
    mSavedFlashSupported = PlatformData::supportedFlashModes(cameraId);
    mSavedFlashMode = PlatformData::defaultFlashMode(cameraId);

    // Set property to inform system what camera is in use
    char facing[PROPERTY_VALUE_MAX];
    snprintf(facing, PROPERTY_VALUE_MAX, "%d", mCameraConf->cameraId());
    property_set("media.camera.facing", facing);

    // Set default parameters so that settings propagate to 3A
    MessageSetParameters msg;
    msg.previewFormatChanged = false;
    msg.videoMode = false;
    handleMessageSetParameters(&msg);

    return NO_ERROR;

bail:

    // this should clean up only what NEEDS to be cleaned up
    deinit();

    return status;
}

void ControlThread::deinit()
{
    // NOTE: This method should clean up only what NEEDS to be cleaned up.
    //       Refer to ControlThread::init(). This method will be called if
    //       even if only partial or no initialization was successful.
    //       Therefore it is important that each specific deinit step
    //       is checked for successful initialization before proceeding
    //       with deinit (eg. check for NULL / non-NULL).

    LOG1("@%s", __FUNCTION__);

    if (mBracketManager != NULL) {
        mBracketManager->requestExitAndWait();
        mBracketManager.clear();
    }

    if (mPostProcThread != NULL) {
        mPostProcThread->requestExitAndWait();
        mPostProcThread.clear();
    }

    if (mPanoramaThread != NULL) {
        mPanoramaThread->requestExitAndWait();
        mPanoramaThread.clear();
    }

    if (mPreviewThread != NULL) {
        mPreviewThread->requestExitAndWait();
        mPreviewThread.clear();
    }

    if (mPictureThread != NULL) {
        mPictureThread->requestExitAndWait();
        mPictureThread.clear();
    }

    if (mCallbacksThread != NULL) {
        mCallbacksThread->requestExitAndWait();
        mCallbacksThread.clear();
    }

    if (mVideoThread != NULL) {
        mVideoThread->requestExitAndWait();
        mVideoThread.clear();
    }

    if (m3AThread != NULL) {
        m3AThread->requestExitAndWait();
        m3AThread.clear();
    }

    if (mParamCache != NULL)
        free(mParamCache);

    if (mISP != NULL) {
        delete mISP;
    }

    if (mAAA != NULL) {
        delete mAAA;
    }

    if (mCP != NULL) {
        delete mCP;
    }

    if (mCameraDump != NULL) {
        delete mCameraDump;
    }

    if (mDvs != NULL) {
        delete mDvs;
    }
    if (mCallbacks != NULL) {
        delete mCallbacks;
    }
}

status_t ControlThread::setPreviewWindow(struct preview_stream_ops *window)
{
    LOG1("@%s: window = %p, state %d", __FUNCTION__, window, mState);

    Message msg;
    msg.id = MESSAGE_ID_SET_PREVIEW_WINDOW;
    msg.data.previewWin.window = window;
    return mMessageQueue.send(&msg);
}

void ControlThread::setCallbacks(camera_notify_callback notify_cb,
                                 camera_data_callback data_cb,
                                 camera_data_timestamp_callback data_cb_timestamp,
                                 camera_request_memory get_memory,
                                 void* user)
{
    LOG1("@%s", __FUNCTION__);
    mCallbacks->setCallbacks(notify_cb,
            data_cb,
            data_cb_timestamp,
            get_memory,
            user);
}

void ControlThread::enableMsgType(int32_t msgType)
{
    LOG2("@%s", __FUNCTION__);
    mCallbacks->enableMsgType(msgType);
}

void ControlThread::disableMsgType(int32_t msgType)
{
    LOG2("@%s", __FUNCTION__);
    mCallbacks->disableMsgType(msgType);
}

bool ControlThread::msgTypeEnabled(int32_t msgType)
{
    LOG2("@%s", __FUNCTION__);
    return mCallbacks->msgTypeEnabled(msgType);
}

status_t ControlThread::startPreview()
{
    LOG1("@%s", __FUNCTION__);
    PERFORMANCE_TRACES_SHOT2SHOT_STEP_NOPARAM();
    PERFORMANCE_TRACES_LAUNCH2PREVIEW_STEP("startPreview_in");
    status_t status = mPreviewStartLock.tryLock();
    if (status != OK) {
        return status;
    }
    // send message
    Message msg;
    msg.id = MESSAGE_ID_START_PREVIEW;
    mPreviewStartQueued = true;
    status = mMessageQueue.send(&msg);
    mPreviewStartLock.unlock();
    return status;
}

status_t ControlThread::stopPreview()
{
    LOG1("@%s", __FUNCTION__);
    if (mState == STATE_STOPPED && mPreviewStartQueued == false) {
        return NO_ERROR;
    }
    // send message and block until thread processes message
    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT);
    PerformanceTraces::SwitchCameras::getOriginalMode(videoMode);

    Message msg;
    msg.id = MESSAGE_ID_STOP_PREVIEW;
    return mMessageQueue.send(&msg, MESSAGE_ID_STOP_PREVIEW);
}

status_t ControlThread::startRecording()
{
    LOG1("@%s", __FUNCTION__);
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_START_RECORDING;
    return mMessageQueue.send(&msg, MESSAGE_ID_START_RECORDING);
}

status_t ControlThread::stopRecording()
{
    LOG1("@%s", __FUNCTION__);
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_STOP_RECORDING;
    return mMessageQueue.send(&msg, MESSAGE_ID_STOP_RECORDING);
}

bool ControlThread::previewEnabled()
{
    LOG2("@%s", __FUNCTION__);

    PreviewThread::PreviewState state = mPreviewThread->getPreviewState();

    bool enabled = mPreviewStartQueued ||
                   (state != PreviewThread::STATE_STOPPED
                 && state != PreviewThread::STATE_ENABLED_HIDDEN);
    // Note: See PreviewThread::setPreviewState() for documentation
    return enabled;
}

bool ControlThread::recordingEnabled()
{
    LOG2("@%s", __FUNCTION__);
    return mState == STATE_RECORDING;
}

status_t ControlThread::setParameters(const char *params)
{
    LOG1("@%s: params = %p", __FUNCTION__, params);
    Mutex::Autolock mLock(mParamCacheLock);
    status_t status = NO_ERROR;
    CameraParameters newParams;
    CameraParameters oldParams;

    if (mParamCache == NULL) {
        String8 params = mParameters.flatten();
        int len = params.length();
        mParamCache = strndup(params.string(), sizeof(char) * len);
    }
    String8 strOldParams(mParamCache);
    oldParams.unflatten(strOldParams);
    const String8 str_params(params);
    newParams.unflatten(str_params);

    CameraAreas newFocusAreas;
    CameraAreas newMeteringAreas;

    // print all old and new params for comparison (debug)
    status = validateParameters(&newParams);
    if (status != NO_ERROR)
        goto exit;
    LOG1("scanning AF focus areas");
    status = newFocusAreas.scan(newParams.get(CameraParameters::KEY_FOCUS_AREAS),
                                mAAA->getAfMaxNumWindows());
    if (status != NO_ERROR) {
        LOGE("bad focus area");
        goto exit;
    }
    LOG1("scanning AE metering areas");
    status = newMeteringAreas.scan(newParams.get(CameraParameters::KEY_METERING_AREAS),
                                   mAAA->getAeMaxNumWindows());
    if (status != NO_ERROR) {
        LOGE("bad metering area");
        goto exit;
    }

    Message msg;
    msg.id = MESSAGE_ID_SET_PARAMETERS;
    // Take care of parameters that need to be set while the ISP is stopped
    status = processStaticParameters(&oldParams, &newParams, msg);
    if (status != NO_ERROR)
        goto exit;

    {
        // release AE lock, if AE mode changed (cts)
        String8 newVal = paramsReturnNewIfChanged(&oldParams, &newParams,
                                                  IntelCameraParameters::KEY_AE_MODE);
        if (!newVal.isEmpty()) {
            newParams.set(CameraParameters::KEY_AUTO_EXPOSURE_LOCK, CameraParameters::FALSE);
        }

        processParamSceneMode(&oldParams, &newParams, false); // for cts, we process, but do not apply yet

        // let app know if we support zoom in the preview mode indicated
        bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT, newParams);
        mISP->getZoomRatios(videoMode, &newParams);
        mISP->getFocusDistances(&newParams);

        // update param cache
        free(mParamCache);
        String8 finalParams = newParams.flatten();
        int len = finalParams.length();
        mParamCache = strndup(finalParams.string(), sizeof(char) * len);
    }

    return mMessageQueue.send(&msg);

exit:
    return status;
}

char* ControlThread::getParameters()
{
    LOG2("@%s", __FUNCTION__);

    // Fast path. Just return the static copy right away.
    //
    // This is needed as some applications call getParameters()
    // from various HAL callbacks, causing deadlocks like the following:
    //   A. HAL is flushing picture/video thread and message loop
    //      is blocked until the operation finishes
    //   B. one of the pending picture/video messages, which was
    //      processed just before the flush, has called an app
    //      callback, which again calls HAL getParameters()
    //   C. the app call to getParameters() is synchronous
    //   D. deadlock results, as HAL/ControlThread is blocked on the
    //      flush call of step (A), and cannot process getParameters()
    //
    // Solution: implement getParameters so that it can be called
    //           even when ControlThread's message loop is blocked.
    char *params = NULL;
    mParamCacheLock.lock();
    if (mParamCache)
        params = strdup(mParamCache);
    mParamCacheLock.unlock();

    // Slow path. If cache was empty, send a message.
    //
    // The above case will not get triggered when param cache is NULL
    // (only happens when initially starting).
    if (params == NULL) {
        Message msg;
        msg.id = MESSAGE_ID_GET_PARAMETERS;
        msg.data.getParameters.params = &params; // let control thread allocate and set pointer
        mMessageQueue.send(&msg, MESSAGE_ID_GET_PARAMETERS);
    }

    return params;
}

void ControlThread::putParameters(char* params)
{
    LOG2("@%s: params = %p", __FUNCTION__, params);
    if (params)
        free(params);
}

bool ControlThread::isParameterSet(const char *param, const CameraParameters &params)
{
    const char* strParam = params.get(param);
    int len = strlen(CameraParameters::TRUE);
    if (strParam != NULL && strncmp(strParam, CameraParameters::TRUE, len) == 0) {
        return true;
    }
    return false;
}

bool ControlThread::isParameterSet(const char* param)
{
    return isParameterSet(param, mParameters);
}

/**
 * Returns value of 'key' in newParams, but only if it is different
 * from its value, or not defined, in oldParams.
 */
String8 ControlThread::paramsReturnNewIfChanged(
        const CameraParameters *oldParams,
        CameraParameters *newParams,
        const char *key)
{
    // note: CameraParameters::get() returns a NULL, but internally it
    //       does not distinguish between a param that is not set,
    //       from a param that is zero length, so we do not make
    //       the disctinction either.

    const char* o = oldParams->get(key);
    const char* n = newParams->get(key);

    // note: String8 segfaults if given a NULL, so thus check
    //      for that here
    String8 oldVal (o, (o == NULL ? 0 : strlen(o)));
    String8 newVal (n, (n == NULL ? 0 : strlen(n)));

    if (oldVal != newVal || !mThreadRunning) // return if changed or if set during init() (thread not running yet)
        return newVal;

    return String8::empty();
}

status_t ControlThread::takePicture()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;

    PERFORMANCE_TRACES_TAKE_PICTURE_QUEUE();

    if (mPanoramaThread->getState() != PANORAMA_STOPPED)
        msg.id = MESSAGE_ID_PANORAMA_PICTURE;
    else if (mPostProcThread->isSmartRunning()) // delaying capture for smart shutter case
        msg.id = MESSAGE_ID_SMART_SHUTTER_PICTURE;
    else
        msg.id = MESSAGE_ID_TAKE_PICTURE;

    return mMessageQueue.send(&msg);
}

status_t ControlThread::cancelPicture()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_PICTURE;
    return mMessageQueue.send(&msg, MESSAGE_ID_CANCEL_PICTURE);
}

status_t ControlThread::autoFocus()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::cancelAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::releaseRecordingFrame(void *buff)
{
    LOG2("@%s: buff = %p", __FUNCTION__, buff);
    Message msg;
    msg.id = MESSAGE_ID_RELEASE_RECORDING_FRAME;
    msg.data.releaseRecordingFrame.buff = buff;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::storeMetaDataInBuffers(bool enabled)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STORE_METADATA_IN_BUFFER;
    msg.data.storeMetaDataInBuffers.enabled = enabled;
    return  mMessageQueue.send(&msg, MESSAGE_ID_STORE_METADATA_IN_BUFFER);
}

void ControlThread::previewDone(AtomBuffer *buff)
{
    LOG2("@%s: buff = %p, id = %d", __FUNCTION__, buff, buff->id);
    Message msg;
    msg.id = MESSAGE_ID_PREVIEW_DONE;
    msg.data.previewDone.buff = *buff;
    mMessageQueue.send(&msg);
}
void ControlThread::returnBuffer(AtomBuffer *buff)
{
    LOG2("@%s: buff = %p, id = %d", __FUNCTION__, buff, buff->id);

    if ((buff->type == ATOM_BUFFER_PREVIEW_GFX) ||
        (buff->type == ATOM_BUFFER_PREVIEW)) {
        buff->owner = 0;
        releasePreviewFrame (buff);
    }
}
void ControlThread::sceneDetected(int sceneMode, bool sceneHdr)
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SCENE_DETECTED;
    msg.data.sceneDetected.sceneMode = sceneMode;
    msg.data.sceneDetected.sceneHdr = sceneHdr;
    mMessageQueue.send(&msg);
}

void ControlThread::facesDetected(const ia_face_state *faceState)
{
    LOG2("@%s", __FUNCTION__);
    m3AThread->setFaces(*faceState);
}

void ControlThread::panoramaFinalized(AtomBuffer *buff, AtomBuffer *pvBuff)
{
    LOG1("panorama Finalized frame buffer data %p, id = %d", buff, buff->id);
    Message msg;
    msg.id = MESSAGE_ID_PANORAMA_FINALIZE;
    msg.data.panoramaFinalized.buff = *buff;
    if (pvBuff)
        msg.data.panoramaFinalized.pvBuff = *pvBuff;
    else
        msg.data.panoramaFinalized.pvBuff.buff = NULL;
    mMessageQueue.send(&msg);
}

status_t ControlThread::handleMessagePanoramaFinalize(MessagePanoramaFinalize *msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = mCallbacksThread->requestTakePicture(false, false);

    if (status != OK)
        return status;

    PictureThread::MetaData picMetaData;
    fillPicMetaData(picMetaData, false);

    // Initialize the picture thread with the size of the final stiched image
    CameraParameters tmpParam = mParameters;
    tmpParam.setPictureSize(msg->buff.width, msg->buff.height);
    mPictureThread->initialize(tmpParam);

    AtomBuffer *pPvBuff = msg->pvBuff.buff ? &(msg->pvBuff) : NULL;

    status = mPictureThread->encode(picMetaData, &(msg->buff), pPvBuff);
    return status;
}

void ControlThread::panoramaCaptureTrigger()
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_PANORAMA_CAPTURE_TRIGGER;
    mMessageQueue.send(&msg);
}

void ControlThread::releasePreviewFrame(AtomBuffer *buff)
{
    LOG2("release preview frame buffer data %p, id = %d", buff, buff->id);
    Message msg;
    msg.id = MESSAGE_ID_RELEASE_PREVIEW_FRAME;
    msg.data.releasePreviewFrame.buff = *buff;
    mMessageQueue.send(&msg);
}

void ControlThread::pictureDone(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf)
{
    LOG2("@%s: snapshotBuf = %p, postviewBuf = %p, id = %d",
            __FUNCTION__,
            (snapshotBuf->buff)?snapshotBuf->buff->data:snapshotBuf->gfxData,
            (postviewBuf->buff)?postviewBuf->buff->data:postviewBuf->gfxData,
            snapshotBuf->id);
    Message msg;
    msg.id = MESSAGE_ID_PICTURE_DONE;
    msg.data.pictureDone.snapshotBuf = *snapshotBuf;
    msg.data.pictureDone.postviewBuf = *postviewBuf;
    mMessageQueue.send(&msg);
}

void ControlThread::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2)
{
    Message msg;
    msg.id = MESSAGE_ID_COMMAND;
    msg.data.command.cmd_id = cmd;
    msg.data.command.arg1 = arg1;
    msg.data.command.arg2 = arg2;

    // App should wait here until ENABLE_INTEL_PARAMETERS command finish.
    if (cmd == CAMERA_CMD_ENABLE_INTEL_PARAMETERS)
        mMessageQueue.send(&msg, MESSAGE_ID_COMMAND);
    else
        mMessageQueue.send(&msg);
}

void ControlThread::autoFocusDone()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_AUTO_FOCUS_DONE;
    mMessageQueue.send(&msg);
}

void ControlThread::postProcCaptureTrigger()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_POST_PROC_CAPTURE_TRIGGER;
    mMessageQueue.send(&msg);
}

status_t ControlThread::handleMessageExit()
{
    LOG1("@%s state = %d", __FUNCTION__, mState);
    status_t status;
    mThreadRunning = false;

    switch (mState) {
    case STATE_CAPTURE:
        status = stopCapture();
        break;
    case STATE_PREVIEW_STILL:
    case STATE_PREVIEW_VIDEO:
    case STATE_CONTINUOUS_CAPTURE:
        handleMessageStopPreview();
        break;
    case STATE_RECORDING:
        handleMessageStopRecording();
        break;
    case STATE_STOPPED:
        // do nothing
        break;
    default:
        LOGW("Exiting in an invalid state, this should not happen!!!");
        break;
    }

    return NO_ERROR;
}

status_t ControlThread::handleContinuousPreviewBackgrounding()
{
    PreviewThread::PreviewState previewState;

    if (mThreadRunning == false)
        return INVALID_OPERATION;

    if (mState != STATE_CONTINUOUS_CAPTURE)
        return NO_INIT;

    previewState = mPreviewThread->getPreviewState();

    // Post-capture stopPreview case
    if (!mISP->isSharedPreviewBufferConfigured()
        && previewState == PreviewThread::STATE_ENABLED_HIDDEN) {
        // Using internal buffers => flush and release preview buffers
        // and leave the core running
        // It is special case to fake preview stopped state for Public API
        // to reach the target Shot2Shot in continuous-mode. We allow
        // entering preview stopped state from hidden (after capture)
        // without stopping the ISP.
        mPreviewThread->flushBuffers();
        mMessageQueue.remove(MESSAGE_ID_PREVIEW_DONE);
        mMessageQueue.remove(MESSAGE_ID_RELEASE_PREVIEW_FRAME);
        mPostProcThread->flushFrames();
        // return Gfx buffers
        mPreviewThread->returnPreviewBuffers();
        // return AtomISP buffers back to ISP
        mISP->returnPreviewBuffers();
        mPreviewThread->setPreviewState(PreviewThread::STATE_STOPPED);
        LOG1("Continuous-mode is left running in background");
    } else {
        LOG1("Continuous-mode needs to stop");
        return INVALID_OPERATION;
    }

    return NO_ERROR;
}

status_t ControlThread::handleContinuousPreviewForegrounding()
{
    PreviewThread::PreviewState previewState;

    if (mState != STATE_CONTINUOUS_CAPTURE)
        return NO_INIT;

    previewState = mPreviewThread->getPreviewState();
    // already in continuous-state, startPreview case
    if (mISP->isOfflineCaptureRunning()) {
        mISP->stopOfflineCapture();
        LOG1("Capture stopped, resuming continuous viewfinder");
    }
    if (previewState == PreviewThread::STATE_STOPPED) {
        // just re-configure previewThread
        int format, width, height, stride;
        format = V4L2Format(mParameters.getPreviewFormat());
        mISP->getPreviewSize(&width, &height,&stride);
        // Magic 3 here is to allow circulation of preview buffers inside
        // PreviewThread, while we circulate with AtomISP buffers for all
        // operations
        mPreviewThread->setPreviewConfig(width, height, stride, format, 3);
    } else if (previewState != PreviewThread::STATE_ENABLED_HIDDEN) {
        LOGE("Trying to resume continuous preview from unexpected state!");
        return INVALID_OPERATION;
    }
    if (mEnableFocusCbAtStart)
        enableMsgType(CAMERA_MSG_FOCUS);
    if (mEnableFocusMoveCbAtStart)
        enableMsgType(CAMERA_MSG_FOCUS_MOVE);
    mPreviewThread->setPreviewState(PreviewThread::STATE_ENABLED);
    LOG1("Continuous preview is resumed by foregrounding");
    return NO_ERROR;
}

/**
 * Configures the ISP ringbuffer size in continuous mode.
 *
 * This configuration must be done before preview pipeline
 * is started. During runtime, user-space may modify
 * capture configuration (number of captures, skip, offset),
 * but only to smaller values. If any number of captures or
 * offset needs be changed so that a larger ringbuffer would
 * be needed, then ISP needs to be restarted. The values set
 * here are thus the maximum values.
 */
status_t ControlThread::configureContinuousRingBuffer()
{
    LOG2("@%s", __FUNCTION__);
    int numCaptures = 1;
    int offset = -1;
    if (mBurstLength > 1) {
        numCaptures = mBurstLength;
        offset = mBurstStart;
    }

    mISP->setContCaptureNumCaptures(numCaptures);
    mISP->setContCaptureOffset(offset);
    LOG2("continous mode ringbuffer for max %d captures, %d offset",
         numCaptures, offset);
    return NO_ERROR;
}

/**
 * Configures parameters for continuous capture.
 *
 * In continuous capture mode, parameters for both capture
 * and preview need to be set up before starting the ISP.
 */
status_t ControlThread::initContinuousCapture()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    int format = mISP->getSnapshotPixelFormat();
    int width, height;
    mParameters.getPictureSize(&width, &height);

    int pvWidth;
    int pvHeight;

    if (mPanoramaThread->getState() == PANORAMA_STOPPED) {
        if (isParameterSet(IntelCameraParameters::KEY_PREVIEW_KEEP_ALIVE)) {
            mParameters.getPreviewSize(&pvWidth, &pvHeight);
        } else {
            pvWidth = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
            pvHeight = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
        }
    } else {
        IntelCameraParameters::getPanoramaLivePreviewSize(pvWidth, pvHeight, mParameters);
    }

    // Configure PictureThread
    mPictureThread->initialize(mParameters);

    mISP->setSnapshotFrameFormat(width, height, format);
    configureContinuousRingBuffer();
    mISP->setPostviewFrameFormat(pvWidth, pvHeight, format);

    burstStateReset();

    // TODO: potential launch2preview impact, we cannot use
    //       the lazy buffer allocation strategy in continuous mode
    allocateSnapshotBuffers();

    setExternalSnapshotBuffers(format, width, height);

    return status;
}

/**
 * Frees resources related to continuous capture
 *
 * \param flushPictures whether to flush the picture thread
 */
void ControlThread::releaseContinuousCapture(bool flushPictures)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mUnqueuedPicBuf.clear();
    mISP->releaseCaptureBuffers();
    if (flushPictures) {
        // This covers cases when we need to fallback from
        // continuous mode to online mode to do a capture.
        // As capture is not runnning in these cases, flush
        // is not needed.
        status = mPictureThread->flushBuffers();
        if (status != NO_ERROR) {
            LOGE("Error flushing PictureThread!");
        }
    }
}

/**
 * Selects which still preview mode to use.
 *
 * @return STATE_CONTINUOUS_CAPTURE or STATE_PREVIEW_STILL
 */
ControlThread::State ControlThread::selectPreviewMode(const CameraParameters &params)
{
    // Whether hardware (SoC, memories) supports continuous mode?
    if (PlatformData::supportsContinuousCapture() == false) {
        LOG1("@%s: Disabling continuous mode, not supported by platform", __FUNCTION__);
        return STATE_PREVIEW_STILL;
    }

    // Whether the loaded ISP firmware supports continuous mode?
    if (mISP->isOfflineCaptureSupported() == false) {
        LOG1("@%s: Disabling continuous mode, not supported", __FUNCTION__);
        return STATE_PREVIEW_STILL;
    }

    // Picture-sizes smaller than 1280x768 are not validated with
    // any ISP firmware.
    int picWidth = 0, picHeight = 0;
    params.getPictureSize(&picWidth, &picHeight);
    if (picWidth <= 1280 && picHeight <= 768) {
        // this is a limitation of current CSS stack
        LOG1("@%s: 1M or smaller picture-size, disabling continuous mode", __FUNCTION__);
        return STATE_PREVIEW_STILL;
    }

    // Low preview resolutions have known issues in continuous mode.
    // TODO: to be removed, tracked in BZ 81396
    int pWidth = 0, pHeight = 0;
    mParameters.getPreviewSize(&pWidth, &pHeight);
    if (pWidth < 640 && pHeight < 360) {
        LOG1("@%s: continuous mode not available for preview size %ux%u",
             __FUNCTION__, pWidth, pHeight);
        return STATE_PREVIEW_STILL;
    }

    // ISP will fail to start if aspect ratio of preview and
    // main output do not match.
    // TODO: A CSS1.5 bug, tracked in BZ: 72564
    float picRatio = 1.0 * picWidth / picHeight;
    float previewRatio = 1.0 * pWidth / pHeight;
    if  (fabsf(picRatio - previewRatio) > ASPECT_TOLERANCE) {
        LOG1("@%s: Different aspect ratio for preview and picture size, disabling continuous mode", __FUNCTION__);
        return STATE_PREVIEW_STILL;
    }

    if (mBurstLength > 1 && mBurstStart >= 0) {
        LOG1("@%s: Burst length of %d requested, disabling continuous mode",
             __FUNCTION__, mBurstLength);
        return STATE_PREVIEW_STILL;
    }

    if (mBurstStart < 0) {
        // One buffer in the raw ringbuffer is reserved for streaming
        // from sensor, so output frame count is limited to maxSize-1.
        int maxBufSize = PlatformData::maxContinuousRawRingBufferSize();
        if (mBurstLength > maxBufSize - 1) {
             LOG1("@%s: Burst length of %d with offset %d requested, disabling continuous mode",
                  __FUNCTION__, mBurstLength, mBurstStart);
            return STATE_PREVIEW_STILL;
        }

        // Bracketing not supported in continuous mode as the number
        // captures is not fixed.
        if (mBracketManager->getBracketMode() != BRACKET_NONE) {
            LOG1("@%s: Bracketing requested, disabling continuous mode",
                 __FUNCTION__);
            return STATE_PREVIEW_STILL;
        }
    }

    // The continuous mode depends on maintaining a RAW frame
    // buffer, so feature is not available SoC sensors.
    if (!mAAA->is3ASupported()) {
        LOG1("@%s: Non-RAW sensor, disabling continuous mode", __FUNCTION__);
        return STATE_PREVIEW_STILL;
    }

    LOG1("@%s: Selecting continuous still preview mode", __FUNCTION__);
    return STATE_CONTINUOUS_CAPTURE;
}

status_t ControlThread::startPreviewCore(bool videoMode)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int width;
    int height;
    int format;
    int stride;
    State state;
    AtomMode mode;
    bool isDVSActive = false;

    if (mState != STATE_STOPPED) {
        LOGE("Must be in STATE_STOPPED to start preview");
        return INVALID_OPERATION;
    }

    PerformanceTraces::SwitchCameras::called(videoMode);
    if (videoMode) {
        LOG1("Starting preview in video mode");
        state = STATE_PREVIEW_VIDEO;
        mode = MODE_VIDEO;
        if(isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION_SUPPORTED) &&
           isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION))
            isDVSActive = true;
    } else {
        LOG1("Starting preview in still mode");
        state = selectPreviewMode(mParameters);
        if (state == STATE_PREVIEW_STILL)
            mode = MODE_PREVIEW;
        else
            mode = MODE_CONTINUOUS_CAPTURE;
    }
    if (CameraDump::isDumpImageEnable(CAMERA_DEBUG_DUMP_3A_STATISTICS))
        mAAA->init3aStatDump("preview");

    // set preview frame config
    format = V4L2Format(mParameters.getPreviewFormat());
    if (format == -1) {
        LOGE("Bad preview format. Cannot start the preview!");
        return BAD_VALUE;
    }

    // set video frame config
    if (videoMode) {
        mParameters.getVideoSize(&width, &height);
        mISP->setVideoFrameFormat(width, height);
        if(width < MIN_DVS_WIDTH && height < MIN_DVS_HEIGHT)
            isDVSActive = false;
        mISP->setDVS(isDVSActive);
    }

    if (state == STATE_CONTINUOUS_CAPTURE) {
        if (initContinuousCapture() != NO_ERROR) {
            return BAD_VALUE;
        }
    }

    // Update focus areas for the proper window size
    if (!mFaceDetectionActive && !mFocusAreas.isEmpty()) {
        size_t winCount(mFocusAreas.numOfAreas());
        CameraWindow *focusWindows = new CameraWindow[winCount];
        mFocusAreas.toWindows(focusWindows);
        preSetCameraWindows(focusWindows, winCount);
        if (mAAA->setAfWindows(focusWindows, winCount) != NO_ERROR) {
            LOGE("Could not set AF windows. Resseting the AF to %d", CAM_AF_MODE_AUTO);
            mAAA->setAfMode(CAM_AF_MODE_AUTO);
        }
        delete[] focusWindows;
        focusWindows = NULL;
    }

    // Update the spot mode window for the proper window size.
    if (m3AControls->getAeMeteringMode() == CAM_AE_METERING_MODE_SPOT && mMeteringAreas.isEmpty()) {
        // Update for the "fixed" AE spot window (Intel extension):
        LOG1("%s: setting forced spot window.", __FUNCTION__);
        AAAWindowInfo aaaWindow;
        mAAA->getGridWindow(aaaWindow);
        updateSpotWindow(aaaWindow.width, aaaWindow.height);
    } else if (m3AControls->getAeMeteringMode() == CAM_AE_METERING_MODE_SPOT) {
        // This update is when the AE metering is internally set to
        // "spot" mode by the HAL, when user has set the AE metering window.
        LOG1("%s: setting metering area with spot window.", __FUNCTION__);
        size_t winCount(mMeteringAreas.numOfAreas());
        CameraWindow *meteringWindows = new CameraWindow[winCount];
        CameraWindow aeWindow;
        mMeteringAreas.toWindows(meteringWindows);

        AAAWindowInfo aaaWindow;
        mAAA->getGridWindow(aaaWindow);
        convertFromAndroidCoordinates(meteringWindows[0], aeWindow, aaaWindow, 5, 255);

        if (mAAA->setAeWindow(&aeWindow) != NO_ERROR) {
            LOGW("Error setting AE metering window. Metering will not work");
        }
        delete[] meteringWindows;
        meteringWindows = NULL;
    }

    LOG1("Using preview format: %s", v4l2Fmt2Str(format));
    mParameters.getPreviewSize(&width, &height);
    mISP->setPreviewFrameFormat(width, height);

    // start the data flow
    status = mISP->configure(mode);
    if (status != NO_ERROR) {
        LOGE("Error configuring ISP");
        return status;
    }

    // Load any ISP extensions before ISP is started
    mPostProcThread->loadIspExtensions(videoMode);

    mISP->getPreviewSize(&width, &height,&stride);
    mNumBuffers = mISP->getNumBuffers(videoMode);
    AtomBuffer *pvBufs;
    int count;
    if (mode == MODE_CONTINUOUS_CAPTURE && !mIntelParamsAllowed) {
        // using mIntelParamsAllowed to distinquish applications using public
        // API from ones using agreed sequences within continuous mode.
        // For API compliant continuous-mode we use internal preview buffers
        // to be able to release and re-acquire external buffers while keeping
        // continuous mode running.
        // TODO: support for fluent transitions recardless of buffer type
        //       transparently
        // Note: Magic 3 here is to allow circulation of preview buffers inside
        // PreviewThread, while we circulate with AtomISP buffers for all
        // operations
        mPreviewThread->setPreviewConfig(width, height, stride, format, 3);
    } else {
        mPreviewThread->setPreviewConfig(width, height, stride, format, mNumBuffers);
        status = mPreviewThread->fetchPreviewBuffers(&pvBufs, &count);
        if ((status == NO_ERROR) && (count == mNumBuffers)) {
            mISP->setGraphicPreviewBuffers(pvBufs, mNumBuffers);
        }
    }
    PERFORMANCE_TRACES_LAUNCH2PREVIEW_STEP("Set_Preview_Config");

    mCoupledBuffers = new CoupledBuffer[mNumBuffers];
    memset(mCoupledBuffers, 0, mNumBuffers * sizeof(CoupledBuffer));

    status = mISP->allocateBuffers(mode);
    if (status != NO_ERROR) {
        LOGE("Error allocate buffers in ISP");
        return status;
    }

    if (mAAA->is3ASupported()) {
        if (mAAA->switchModeAndRate(mode, mISP->getFrameRate()) != NO_ERROR)
            LOGE("Failed switching 3A at %.2f fps", mISP->getFrameRate());
        if (isDVSActive && mDvs->reconfigure() != NO_ERROR)
            LOGE("Failed to reconfigure DVS grid");
    }

    status = mISP->start();
    PERFORMANCE_TRACES_LAUNCH2PREVIEW_STEP("ISP_Start");
    if (status == NO_ERROR) {
        memset(mCoupledBuffers, 0, sizeof(mCoupledBuffers));
        mState = state;
        mPreviewThread->setPreviewState(PreviewThread::STATE_ENABLED);
        if (mAAA->is3ASupported()) {
            // Enable auto-focus by default
            mAAA->setAfEnabled(true);
            m3AThread->enable3A();
            m3AThread->enableDVS(isDVSActive);
        }
    } else {
        LOGE("Error starting ISP!");
        mPreviewThread->returnPreviewBuffers();
    }

    // ISP started so frame counter will be 1
    PERFORMANCE_TRACES_SHOT2SHOT_STEP("started preview", 1);
    mPreviewFramesDone = 0;
    return status;
}

/**
 * Dequeues preview buffers to display until certain timestamp
 *
 * ControlThread is handling both preview and capture. Current
 * CSS1.5 is not able to provide constant preview stream while
 * taking shots, but as the stream is kept running at AtomISP
 * level, the shortage in preview frames is shorter compared to
 * preview state changes in this level.
 *
 * To address behaviour of preview screen during continuous
 * shooting, this function is used to flush preview frames from
 * drivers output queue after taking a shot. This leaves preview
 * in state close to the shot taken (not necessarily into same
 * frame due to way how driver sets the timestamps) and makes it
 * stay alive during max-fps capturing.
 *
 * Selected logic always dequeues a single frame and continues
 * until the requested timestamp is reached. Single frame gets
 * always flushed to keep preview alive in continuous-shooting
 * mode.
 *
 * It is not ensured that last frame flushed is exactly the same
 * frame with snapshot, for this one wound need to render the
 * postview buffer. See, "preview-keep-alive" parameter.
 */
void ControlThread::flushContinuousPreviewToDisplay(nsecs_t snapshotTs)
{
    LOG1("@%s", __FUNCTION__);
    atomisp_frame_status frameStatus;
    nsecs_t previewTs = 0;
    AtomBuffer buff;
    status_t status;
    int count = 0;
    while (previewTs <= snapshotTs) {
        if (!mISP->dataAvailable())
            break;
        status = mISP->getPreviewFrame(&buff, &frameStatus);
        if (status != NO_ERROR)
            break;
        count++;
        previewTs =
            ((nsecs_t)(buff.capture_timestamp.tv_sec)*1000000LL +
             (nsecs_t)(buff.capture_timestamp.tv_usec));
        if (frameStatus != ATOMISP_FRAME_STATUS_CORRUPTED
            && mAAA->is3ASupported()) {
            m3AThread->newFrame(buff.capture_timestamp);
            if (count <= 1 || previewTs <= snapshotTs) {
                status = mPreviewThread->preview(&buff);
                if (status != NO_ERROR) {
                    LOGE("Error sending buffer to preview thread");
                    mISP->putPreviewFrame(&buff);
                }
                LOG1("%s: flushed preview frame %lld <= %lld", __FUNCTION__,
                        previewTs, snapshotTs);
            } else {
                mISP->putPreviewFrame(&buff);
            }
        } else {
             mISP->putPreviewFrame(&buff);
        }
    }
}

/**
 * Stops ISP and frees allocated resources
 *
 * \param flushPictures whether to flush the picture thread
 */
status_t ControlThread::stopPreviewCore(bool flushPictures)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if ((mState == STATE_PREVIEW_VIDEO || mState == STATE_RECORDING) && mAAA->is3ASupported()) {
        m3AThread->enableDVS(false);
    }

    // Before stopping the ISP, flush any buffers in picture
    // and video threads. This is needed as AtomISP::stop() may
    // deallocate buffers and the picture/video threads might
    // otherwise hold invalid references.
    mPreviewThread->flushBuffers();

    // Flush also the messages coming from PreviewThread to CtrlThread
    // with preview frames ready to be sent to PostProcThread
    mMessageQueue.remove(MESSAGE_ID_PREVIEW_DONE);
    mMessageQueue.remove(MESSAGE_ID_RELEASE_PREVIEW_FRAME);

    mPostProcThread->unloadIspExtensions();
    mPostProcThread->flushFrames();

    if (mState == STATE_PREVIEW_VIDEO ||
        mState == STATE_RECORDING) {
        status = mVideoThread->flushBuffers();
    }

    State oldState = mState;
    status = mISP->stop();
    if (status == NO_ERROR) {
        mState = STATE_STOPPED;
    } else {
        LOGE("Error stopping ISP in preview mode!");
    }
    status = mPreviewThread->returnPreviewBuffers();

    if (oldState == STATE_CONTINUOUS_CAPTURE)
        releaseContinuousCapture(flushPictures);

    delete [] mCoupledBuffers;
    // set to null because frames can be returned to hal in stop state
    // need to check for null in relevant locations
    mCoupledBuffers = NULL;

    if (CameraDump::isDumpImageEnable(CAMERA_DEBUG_DUMP_3A_STATISTICS))
        mAAA->deinit3aStatDump();

    mPreviewThread->setPreviewState(PreviewThread::STATE_STOPPED);

    LOG2("Preview stopped after %d frames", mPreviewFramesDone);

    return status;
}

status_t ControlThread::stopCapture()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mState != STATE_CAPTURE) {
        LOGE("Must be in STATE_CAPTURE to stop capture");
        return INVALID_OPERATION;
    }
    mUnqueuedPicBuf.clear();
    status = mPictureThread->flushBuffers();
    if (status != NO_ERROR) {
        LOGE("Error flushing PictureThread!");
        return status;
    }

    if (isParameterSet(IntelCameraParameters::KEY_PREVIEW_KEEP_ALIVE))
        mPreviewThread->flushBuffers();

    status = mISP->stop();
    if (status != NO_ERROR) {
        LOGE("Error stopping ISP!");
        return status;
    }
    status = mISP->releaseCaptureBuffers();

    mState = STATE_STOPPED;
    burstStateReset();

    // Reset AE and AF in case HDR/bracketing was used (these features
    // manually configure AE and AF during takePicture)
    if (mBracketManager->getBracketMode() == BRACKET_EXPOSURE) {
        AeMode publicAeMode = mAAA->getPublicAeMode();
        mAAA->setAeMode(publicAeMode);
    }

    if (mBracketManager->getBracketMode() == BRACKET_FOCUS) {
        AfMode publicAfMode = mAAA->getPublicAfMode();
        if (!mFocusAreas.isEmpty() &&
            (publicAfMode == CAM_AF_MODE_AUTO ||
             publicAfMode == CAM_AF_MODE_CONTINUOUS ||
             publicAfMode == CAM_AF_MODE_MACRO)) {
            mAAA->setAfMode(CAM_AF_MODE_TOUCH);
        } else {
            mAAA->setAfMode(publicAfMode);
        }
    }

    if (mHdr.enabled) {
        hdrRelease();
    }
    return status;
}

status_t ControlThread::restartPreview(bool videoMode)
{
    LOG1("@%s: mode = %s", __FUNCTION__, videoMode?"VIDEO":"STILL");
    Mutex::Autolock mLock(mPreviewStartLock);
    mPreviewStartQueued = true;
    bool faceActive = mFaceDetectionActive;
    stopFaceDetection(true);
    status_t status = stopPreviewCore();
    if (status == NO_ERROR)
        status = startPreviewCore(videoMode);
    if (faceActive)
        startFaceDetection();
    mPreviewStartQueued = false;
    return status;
}

/**
 * Starts rendering an output frame from the raw
 * ringbuffer.
 */
status_t ControlThread::startOfflineCapture()
{
    assert(mState == STATE_CONTINUOUS_CAPTURE);

    int skip = 0;
    int captures = 1;
    int offset = -1;

    if (mBurstLength > 1) {
        captures = mBurstLength;
        offset = mBurstStart;
    }

    // in case preview has just started, we need to limit
    // how long we can look back
    if (mPreviewFramesDone < -offset)
        offset = -mPreviewFramesDone;

    // Starting capture device will queue all buffers,
    // so we need to clear any references we have.
    mUnqueuedPicBuf.clear();

    mISP->setContCaptureNumCaptures(captures);
    mISP->setContCaptureOffset(offset);
    mISP->setContCaptureSkip(skip);
    mISP->startOfflineCapture();

    return NO_ERROR;
}

status_t ControlThread::handleMessageStartPreview()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Mutex::Autolock mLock(mPreviewStartLock);

    PERFORMANCE_TRACES_SHOT2SHOT_STEP("handle start preview", -1);

    if (mState == STATE_CAPTURE) {
        status = stopCapture();
        if (status != NO_ERROR) {
            LOGE("Could not stop capture before start preview!");
            mPreviewStartQueued = false;
            return status;
        }
    }

    if (mState == STATE_CONTINUOUS_CAPTURE) {
        // already in continuous-state
        status = handleContinuousPreviewForegrounding();
        if (status == NO_ERROR)
            goto preview_started;
    }

    if (mState == STATE_STOPPED) {
        // API says apps should call startFaceDetection when resuming preview
        // stop FD here to avoid accidental FD.
        stopFaceDetection();
        if (mPreviewThread->isWindowConfigured()) {
            bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT);
            status = startPreviewCore(videoMode);
        } else {
            LOGI("Preview window not set deferring start preview until then");
            mPreviewThread->setPreviewState(PreviewThread::STATE_NO_WINDOW);
        }
    } else {
        LOGE("Error starting preview. Invalid state!");
        status = INVALID_OPERATION;
    }
preview_started:
    PERFORMANCE_TRACES_SHOT2SHOT_STEP("preview started", -1);
    mPreviewStartQueued = false;

    return status;
}

status_t ControlThread::handleMessageStopPreview()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    PERFORMANCE_TRACES_SHOT2SHOT_STEP("stop preview", -1);

    // In STATE_CAPTURE, preview is already stopped, nothing to do
    if (mState != STATE_CAPTURE) {
        stopFaceDetection(true);
        if (mState == STATE_CONTINUOUS_CAPTURE) {
            status = handleContinuousPreviewBackgrounding();
            if (status == NO_ERROR)
                goto preview_stopped;
        }
        if (mState != STATE_STOPPED) {
            status = stopPreviewCore();
        } else {
            LOGE("Error stopping preview. Invalid state!");
            status = INVALID_OPERATION;
        }
    }

    // Loose our preview window handle and let service maintain
    // it between stop and start
    mPreviewThread->setPreviewWindow(NULL);

preview_stopped:
    PERFORMANCE_TRACES_SHOT2SHOT_STEP("preview stopped", -1);

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_STOP_PREVIEW, status);
    return status;
}

/**
 *  Message Handler for setPreviewWindow HAL call
 *  Actual configuration is taken care of by PreviewThread
 *  Preview restart is done if preview is enabled
 */
status_t ControlThread::handleMessageSetPreviewWindow(MessagePreviewWindow *msg)
{
    LOG1("@%s state = %d window %p", __FUNCTION__, mState, msg->window);
    status_t status = NO_ERROR;

    if (mPreviewThread == NULL)
        return NO_INIT;

    status = mPreviewThread->setPreviewWindow(msg->window);

    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;

    // Only start preview if it was already requested by user
    if (mPreviewThread->getPreviewState() == PreviewThread::STATE_NO_WINDOW
        && (msg->window != NULL)) {
        startPreviewCore(videoMode);
    }

    return status;
}

status_t ControlThread::handleMessageStartRecording()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int width,height;
    char sizes[25];

    if (mState == STATE_PREVIEW_VIDEO) {
        mState = STATE_RECORDING;
    } else if (mState == STATE_PREVIEW_STILL ||
               mState == STATE_CONTINUOUS_CAPTURE) {
        /* We are in PREVIEW_STILL mode; in order to start recording
         * we first need to stop AtomISP and restart it with MODE_VIDEO
         */
        mISP->applyISPVideoLimitations(&mParameters,
                isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION)
                && isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION_SUPPORTED));
        status = restartPreview(true);
        if (status != NO_ERROR) {
            LOGE("Error restarting preview in video mode");
        }
        mState = STATE_RECORDING;
    } else {
        LOGE("Error starting recording. Invalid state!");
        status = INVALID_OPERATION;
    }

   /* Change the snapshot size and thumbnail size as per current video
    * snapshot limitations.
    * Only supported size is the size of the video
    * and thumbnail size is the size of preview.
    */
    storeCurrentPictureParams();
    /**
     * in video mode we may allocate bigger buffers to satisfy encoder or
     * ISP stride requirements. We need to be honest about the real size
     * towards the user.
     */
    int stride;
    mISP->getVideoSize(&width, &height, &stride);
    mParameters.setPictureSize(stride, height);
    allocateSnapshotBuffers();
    snprintf(sizes, 25, "%dx%d", stride,height);
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, sizes);

    LOG1("video snapshot size %dx%d", width, height);
    mParameters.getPreviewSize(&width, &height);
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, width);
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, height);
    snprintf(sizes, 25, "%dx%d,0x0", width,height);
    mParameters.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, sizes);
    updateParameterCache();

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_START_RECORDING, status);
    return status;
}

status_t ControlThread::handleMessageStopRecording()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mState == STATE_RECORDING) {
        /*
         * Even if startRecording was called from PREVIEW_STILL mode, we can
         * switch back to PREVIEW_VIDEO now since we got a startRecording
         */
        status = mVideoThread->flushBuffers();
        if (status != NO_ERROR)
            LOGE("Error flushing video thread");
        mState = STATE_PREVIEW_VIDEO;
    } else {
        LOGE("Error stopping recording. Invalid state!");
        status = INVALID_OPERATION;
    }

    // release buffers owned by encoder since it is not going to return them
    if (mCoupledBuffers) {
        for (int i = 0; i < mNumBuffers; i++) {
            if (!mCoupledBuffers[i].recordingBuffReturned) {
               mCoupledBuffers[i].recordingBuffReturned = true;
               queueCoupledBuffers(i);
            }
        }
    }

    /**
     * Restore the actual still picture parameters before we started video
     * In this way we lift the restrictions that we imposed because of
     * video snapshot implementation
     */
    restoreCurrentPictureParams();

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_STOP_RECORDING, status);
    return status;
}

status_t ControlThread::skipPreviewFrames(int numFrames, AtomBuffer* buff)
{
    LOG1("@%s: numFrames=%d", __FUNCTION__, numFrames);

    for (int i = 0; i < numFrames; i++) {
        status_t status = mISP->getPreviewFrame(buff);
        if (status == NO_ERROR)
            mISP->putPreviewFrame(buff);
        else
            return INVALID_OPERATION;
    }
    return NO_ERROR;
}

bool ControlThread::runPreFlashSequence()
{
    LOG2("@%s", __FUNCTION__);

    size_t framesTillFlashComplete = 0;
    AtomBuffer buff;
    bool ret = false;
    status_t status = NO_ERROR;
    atomisp_frame_status frameStatus = ATOMISP_FRAME_STATUS_OK;

    // hued images fix (BZ: 72908)
    status = skipPreviewFrames(2, &buff);

    // Stage 1
    status = mISP->getPreviewFrame(&buff);
    if (status == NO_ERROR) {
        mISP->putPreviewFrame(&buff);
        mAAA->applyPreFlashProcess(CAM_FLASH_STAGE_NONE);
    } else {
        return ret;
    }

    // Stage 1.5: Skip 2 frames to get exposure from Stage 1.
    //            First frame is for sensor to pick up the new value
    //            and second for sensor to apply it.
    status = skipPreviewFrames(2, &buff);
    if (status != NO_ERROR)
        return ret;

    // Stage 2
    status = mISP->getPreviewFrame(&buff);
    if (status == NO_ERROR) {
        mISP->putPreviewFrame(&buff);
        mAAA->applyPreFlashProcess(CAM_FLASH_STAGE_PRE);
    } else {
        return ret;
    }

    // Stage 2.5: Same as above, but for Stage 2.
    status = skipPreviewFrames(2, &buff);
    if (status != NO_ERROR)
        return ret;

    // Stage 3: get the flash-exposed preview frame
    // and let the 3A library calculate the exposure
    // settings for the flash-exposed still capture.
    // We check the frame status to make sure we use
    // the flash-exposed frame.
    status = mISP->setFlash(1);

    if (status != NO_ERROR) {
        LOGE("Failed to request pre-flash frame");
        return false;
    }

    while (++framesTillFlashComplete < FLASH_FRAME_TIMEOUT) {
        status = mISP->getPreviewFrame(&buff, &frameStatus);
        if (status == NO_ERROR) {
            mISP->putPreviewFrame(&buff);
        } else {
            return ret;
        }
        if (frameStatus == ATOMISP_FRAME_STATUS_FLASH_EXPOSED) {
            LOG1("PreFlash@Frame %d: SUCCESS    (stopping...)", framesTillFlashComplete);
            ret = true;
            break;
        }
        if (frameStatus == ATOMISP_FRAME_STATUS_FLASH_FAILED) {
            LOG1("PreFlash@Frame %d: FAILED     (stopping...)", framesTillFlashComplete);
            break;
        }
    }

    // Set flash off
    mISP->setFlash(0);

    if (ret) {
        mAAA->applyPreFlashProcess(CAM_FLASH_STAGE_MAIN);
    } else {
        mAAA->apply3AProcess(true, buff.capture_timestamp);
    }

    return ret;
}

status_t ControlThread::skipFrames(size_t numFrames)
{
    LOG1("@%s: numFrames=%d", __FUNCTION__, numFrames);
    status_t status = NO_ERROR;
    AtomBuffer snapshotBuffer, postviewBuffer;

    for (size_t i = 0; i < numFrames; i++) {
        if ((status = mISP->getSnapshot(&snapshotBuffer, &postviewBuffer)) != NO_ERROR) {
            LOGE("Error in grabbing warm-up frame %d!", i);
            return status;
        }
        status = mISP->putSnapshot(&snapshotBuffer, &postviewBuffer);
        if (status == DEAD_OBJECT) {
            LOG1("Stale snapshot buffer returned to ISP");
        } else if (status != NO_ERROR) {
            LOGE("Error in putting skip frame %d!", i);
            return status;
        }
    }
    return status;
}

/* If smart scene detection is enabled and user scene is set to "Auto",
 * change settings based on the detected scene
 */
status_t ControlThread::setSmartSceneParams(void)
{
    const char *scene_mode = mParameters.get(CameraParameters::KEY_SCENE_MODE);

    // Exit if IntelParams are not supported (xnr and anr)
    if (!mIntelParamsAllowed)
        return INVALID_OPERATION;

    if (scene_mode && !strcmp(scene_mode, CameraParameters::SCENE_MODE_AUTO)) {
        if (mAAA->is3ASupported() && mAAA->getSmartSceneDetection()) {
            int sceneMode = 0;
            bool sceneHdr = false;
            m3AThread->getCurrentSmartScene(sceneMode, sceneHdr);
            // Force XNR and ANR in case of lowlight scene
            if (sceneMode == ia_aiq_scene_mode_lowlight_portrait ||
                sceneMode == ia_aiq_scene_mode_low_light) {
                LOG1("Low-light scene detected, forcing XNR and ANR");
                mISP->setXNR(true);
                // Forcing mParameters to true, to be in sync with app update.
                mParameters.set(IntelCameraParameters::KEY_XNR, "true");

                mISP->setLowLight(true);
                // Forcing mParameters to true, to be in sync with app update.
                mParameters.set(IntelCameraParameters::KEY_ANR, "true");
            }
        }
    }
    return NO_ERROR;
}

status_t ControlThread::handleMessagePanoramaCaptureTrigger()
{
    LOG1("@%s:", __FUNCTION__);
    status_t status = NO_ERROR;
    AtomBuffer snapshotBuffer, postviewBuffer;

    PERFORMANCE_TRACES_SHOT2SHOT_TAKE_PICTURE_HANDLE();

    status = capturePanoramaPic(snapshotBuffer, postviewBuffer);
    if (status != NO_ERROR) {
        LOGE("Error %d capturing panorama picture.", status);
        return status;
    }

    mPanoramaThread->stitch(&snapshotBuffer, &postviewBuffer); // synchronous

    if (mState != STATE_CONTINUOUS_CAPTURE) {
        // we can return buffers now that panorama has (synchronously) processed
        // (copied) the buffers
        status = mISP->putSnapshot(&snapshotBuffer, &postviewBuffer);
        if (status != NO_ERROR)
            LOGE("error returning panorama capture buffers");

        //restart preview
        Message msg;
        msg.id = MESSAGE_ID_START_PREVIEW;
        mMessageQueue.send(&msg);
    }

    return status;
}

status_t ControlThread::handleMessagePanoramaPicture() {
    LOG1("@%s:", __FUNCTION__);
    status_t status = NO_ERROR;
    if (mPanoramaThread->getState() == PANORAMA_STARTED) {
        mPanoramaThread->startPanoramaCapture();
    } else {
        mPanoramaThread->finalize();
    }

    return status;
}

/**
 * Is a burst capture sequence ongoing?
 *
 * Returns true until the last burst picture has been
 * delivered to application.
 *
 * @see burstMoreCapturesNeeded()
 */
bool ControlThread::isBurstRunning()
{
    if (mBurstCaptureDoneNum != -1 &&
        mBurstLength > 1 &&
        mBurstCaptureDoneNum < mBurstLength)
        return true;

    return false;
}

/**
 * Do we need to request more pictures from ISP to
 * complete the capture burst.
 *
 * Returns true until the last burst picture has been
 * requested from application.
 *
 * @see isBurstRunnning()
 */
bool ControlThread::burstMoreCapturesNeeded()
{
    if (isBurstRunning() &&
        mBurstCaptureNum < mBurstLength)
        return true;

    return false;
}

/**
 * Resets the burst state managed in control thread.
 */
void ControlThread::burstStateReset()
{
    mBurstCaptureNum = -1;
    mBurstCaptureDoneNum = -1;
    mBurstQbufs = 0;
}


status_t ControlThread::handleMessageTakePicture() {
    LOG1("@%s:", __FUNCTION__);
    status_t status = NO_ERROR;

    switch(mState) {

        case STATE_PREVIEW_STILL:
        case STATE_PREVIEW_VIDEO:
            status = captureStillPic();
            break;
        case STATE_CONTINUOUS_CAPTURE:
            if (isBurstRunning())
                status = captureFixedBurstPic(true);
            else
                status = captureStillPic();
            break;
        case STATE_CAPTURE:
            status = captureBurstPic(true);
            break;
        case STATE_RECORDING:
            status = captureVideoSnap();
            break;

        default:
            LOGE("Taking picture when recording is not supported!");
            status = INVALID_OPERATION;
            break;
    }

    return status;
}

/**
 * Gets a snapshot/postview frame pair from ISP when
 * using flash.
 *
 * To ensure flash sync, the function fetches frames in
 * a loop until a properly exposed frame is available.
 */
status_t ControlThread::getFlashExposedSnapshot(AtomBuffer *snapshotBuffer, AtomBuffer *postviewBuffer)
{
    LOG2("@%s:", __FUNCTION__);
    status_t status = NO_ERROR;
    for (int cnt = 0;;) {
        enum atomisp_frame_status stat;

        status = mISP->getSnapshot(snapshotBuffer, postviewBuffer, &stat);
        if (status != NO_ERROR) {
            LOGE("%s: Error in grabbing snapshot!", __FUNCTION__);
            break;
        }

        if (stat == ATOMISP_FRAME_STATUS_FLASH_EXPOSED) {
            LOG2("flash exposed, frame %d", cnt);
            break;
        }
        else if (stat == ATOMISP_FRAME_STATUS_FLASH_FAILED) {
            LOGE("%s: flash fail, frame %d", __FUNCTION__, cnt);
            break;
        }

        if (cnt++ == FLASH_TIMEOUT_FRAMES) {
            LOGE("%s: unexpected flash timeout, frame %d", __FUNCTION__, cnt);
            break;
        }

        mISP->putSnapshot(snapshotBuffer, postviewBuffer);;
    }

    return status;
}

/**
 * Fetches meta data from 3A, ISP and sensors and fills
 * the data into struct that can be sent to PictureThread.
 *
 * The caller is responsible for freeing the data.
 */
void ControlThread::fillPicMetaData(PictureThread::MetaData &metaData, bool flashFired)
{
    LOG1("@%s: ", __FUNCTION__);

    ia_3a_mknote *aaaMkNote = 0;
    atomisp_makernote_info *atomispMkNote = 0;
    SensorAeConfig *aeConfig = 0;

    if (mAAA->is3ASupported()) {
        aeConfig = new SensorAeConfig;
        mAAA->getExposureInfo(*aeConfig);
        if (m3AControls->getEv(&aeConfig->evBias) != NO_ERROR) {
            aeConfig->evBias = EV_UPPER_BOUND;
        }
    }
    // TODO: for SoC/secondary camera, we have no means to get
    //       SensorAeConfig information, so setting as NULL on purpose
    mBracketManager->getNextAeConfig(aeConfig);
    if (mAAA->is3ASupported()) {
        // TODO: add support for raw mknote
        aaaMkNote = mAAA->get3aMakerNote(ia_3a_mknote_mode_jpeg);
        if (!aaaMkNote)
            LOGW("No 3A makernote data available");
    }

    atomisp_makernote_info tmp;
    status_t status = mISP->getMakerNote(&tmp);
    if (status == NO_ERROR) {
        atomispMkNote = new atomisp_makernote_info;
        *atomispMkNote = tmp;
    }
    else {
        LOGW("Could not get AtomISP makernote information!");
    }

    metaData.flashFired = flashFired;
    // note: the following may be null, if info not available
    metaData.aeConfig = aeConfig;
    metaData.ia3AMkNote = aaaMkNote;
    metaData.atomispMkNote = atomispMkNote;
}

status_t ControlThread::capturePanoramaPic(AtomBuffer &snapshotBuffer, AtomBuffer &postviewBuffer)
{
    LOG1("@%s: ", __FUNCTION__);
    status_t status = NO_ERROR;
    int format, size, width, height;
    int lpvWidth, lpvHeight, lpvSize;
    int thumbnailWidth, thumbnailHeight;

    postviewBuffer.owner = this;
    stopFaceDetection();

    if (mState == STATE_CONTINUOUS_CAPTURE) {
        assert(mBurstLength <= 1);
        mISP->setContCaptureNumCaptures(1);
        mISP->startOfflineCapture();
    }
    else {
        status = stopPreviewCore();
        if (status != NO_ERROR) {
            LOGE("Error stopping preview!");
            return status;
        }
        mState = STATE_CAPTURE;
    }
    mBurstCaptureNum = 0;

    // Get the current params
    mParameters.getPictureSize(&width, &height);
    IntelCameraParameters::getPanoramaLivePreviewSize(lpvWidth, lpvHeight, mParameters);
    format = mISP->getSnapshotPixelFormat();
    size = frameSize(format, width, height);
    lpvSize = frameSize(format, lpvWidth, lpvHeight);

    // Configure PictureThread
    mPictureThread->initialize(mParameters);

    // configure thumbnail size
    thumbnailWidth = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    thumbnailHeight= mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    mPanoramaThread->setThumbnailSize(thumbnailWidth, thumbnailHeight);

    if (mState != STATE_CONTINUOUS_CAPTURE) {
        // Configure and start the ISP
        mISP->setSnapshotFrameFormat(width, height, format);
        mISP->setPostviewFrameFormat(lpvWidth, lpvHeight, format);

        if ((status = mISP->configure(MODE_CAPTURE)) != NO_ERROR) {
            LOGE("Error configuring the ISP driver for CAPTURE mode");
            return status;
        }

        status = mISP->allocateBuffers(MODE_CAPTURE);
        if (status != NO_ERROR) {
            LOGE("Error allocate buffers in ISP");
            return status;
        }

        if (mAAA->is3ASupported())
            if (mAAA->switchModeAndRate(MODE_CAPTURE, mISP->getFrameRate()) != NO_ERROR)
                LOGE("Failed to switch 3A to capture mode at %.2f fps", mISP->getFrameRate());

        if ((status = mISP->start()) != NO_ERROR) {
            LOGE("Error starting the ISP driver in CAPTURE mode!");
            return status;
        }
    }

    /*
     *  If the current camera does not have 3A, then we should skip the first
     *  frames in order to allow the sensor to warm up.
     */
    if (!mAAA->is3ASupported()) {
        if ((status = skipFrames(NUM_WARMUP_FRAMES)) != NO_ERROR) {
            LOGE("Error skipping warm-up frames!");
            return status;
        }
    }

    // Turn off flash
    mISP->setFlashIndicator(0);

    // Get the snapshot
    if ((status = mISP->getSnapshot(&snapshotBuffer, &postviewBuffer)) != NO_ERROR) {
        LOGE("Error in grabbing snapshot!");
        return status;
    }

    if (mState == STATE_CONTINUOUS_CAPTURE) {
        stopOfflineCapture();
        // workaround for broken postview images - downscale in software
        // TODO REMOVE THIS WHEN ZSL POSTVIEW STARTS TO WORK PROPERLY!!! THIS CAUSES 22-60ms OF DELAY!
        ImageScaler::downScaleImage(&snapshotBuffer, &postviewBuffer);
    }

    snapshotBuffer.owner = this;

    mCallbacksThread->shutterSound();

    return status;
}

void ControlThread::stopOfflineCapture()
{
    LOG1("@%s: ", __FUNCTION__);
    if (mState == STATE_CONTINUOUS_CAPTURE &&
            mISP->isOfflineCaptureRunning()) {
        mISP->stopOfflineCapture();
    }
}

/**
 * Dequeues preview frames until capture frame is
 * available for reading from ISP.
 */
status_t ControlThread::waitForCaptureStart()
{
    LOG2("@%s: ", __FUNCTION__);
    status_t status = NO_ERROR;
    const int maxWaitMs = 2000;
    const int previewTimeoutMs = 20;
    const int maxCycles = maxWaitMs / previewTimeoutMs;
    int n;

    for(n = 0; n < maxCycles; n++) {
        // Check if capture frame is availble (no wait)
        int res = mISP->pollCapture(0);
        if (res > 0)
            break;

        res = mISP->pollPreview(previewTimeoutMs);
        if (res < 0) {
            status = UNKNOWN_ERROR;
            break;
        }
        else if (res > 0) {
            AtomBuffer buff;
            LOG2("handling preview while waiting for capture");
            status = skipPreviewFrames(1, &buff);
            if (status != NO_ERROR)
                break;
        }
    }

    if (n == maxCycles)
        status = UNKNOWN_ERROR;

    return status;
}

/**
 * Skips initial snapshot frames if target FPS is lower
 * than the ISP burst frame rate.
 */
status_t ControlThread::burstCaptureSkipFrames()
{
    LOG2("@%s: ", __FUNCTION__);
    status_t status = NO_ERROR;

    // In continuous mode the output frame count is fixed, so
    // we cannot arbitrarily skip frames. We return NO_ERROR as
    // this function is used to hide differences between
    // capture modes.
    if (mState == STATE_CONTINUOUS_CAPTURE)
        return NO_ERROR;

    if (mBurstLength > 1 &&
            mFpsAdaptSkip > 0 &&
            mBracketManager->getBracketMode() == BRACKET_NONE) {
        LOG1("Skipping %d burst frames", mFpsAdaptSkip);
        if ((status = skipFrames(mFpsAdaptSkip)) != NO_ERROR) {
            LOGE("Error skipping burst frames!");
        }
    }
    return status;
}

/**
 * Starts the capture process in continuous capture mode.
 */
status_t ControlThread::continuousStartStillCapture(bool useFlash)
{
    LOG2("@%s: ", __FUNCTION__);
    status_t status = NO_ERROR;
    if (useFlash == false) {
        mCallbacksThread->shutterSound();
        startOfflineCapture();
        mPreviewThread->setPreviewState(PreviewThread::STATE_ENABLED_HIDDEN);
    }
    else {
        // Flushing pictures will also clear counters for
        // requested pictures, which would break the
        // flash-fallback, so we need to avoid the flush (this
        // is ok as we have just run preflash sequence).
        LOG1("Fallback from continuous to normal mode for flash");
        bool flushPicThread = false;
        status = stopPreviewCore(flushPicThread);
        if (status == NO_ERROR)
            mState = STATE_CAPTURE;
        else
            LOGE("Error stopping preview!");
    }
    return status;
}

status_t ControlThread::captureStillPic()
{
    LOG1("@%s: ", __FUNCTION__);
    status_t status = NO_ERROR;
    AtomBuffer snapshotBuffer, postviewBuffer;
    int width, height, format, size;
    int pvWidth, pvHeight, pvSize;
    FlashMode flashMode = mAAA->getAeFlashMode();
    bool flashOn = (flashMode == CAM_AE_FLASH_MODE_TORCH ||
                    flashMode == CAM_AE_FLASH_MODE_ON);
    bool flashFired = false;
    bool previewKeepAlive =
        isParameterSet(IntelCameraParameters::KEY_PREVIEW_KEEP_ALIVE);
    nsecs_t snapshotTs = 0;

    PERFORMANCE_TRACES_SHOT2SHOT_TAKE_PICTURE_HANDLE();

    bool requestPostviewCallback = true;
    bool requestRawCallback = true;

    // TODO: Fix the TestCamera application bug and remove this workaround
    // WORKAROUND BEGIN: Due to a TesCamera application bug send the POSTVIEW and RAW callbacks only for single shots
    if ( mBurstLength > 1) {
        requestPostviewCallback = false;
        requestRawCallback = false;
    }
    // WORKAROUND END
    // Notify CallbacksThread that a picture was requested, so grab one from queue
    mCallbacksThread->requestTakePicture(requestPostviewCallback, requestRawCallback);

    stopFaceDetection();

    if (mBurstLength <= 1) {
        if (mAAA->is3ASupported()) {
            // If flash mode is not ON or TORCH, check for other
            // modes: AUTO, DAY_SYNC, SLOW_SYNC

            if (!flashOn && DetermineFlash(flashMode)) {
                // note: getAeFlashNecessary() should not be called when
                //       assist light (or TORCH) is on.
                if (mFlashAutoFocus)
                    LOGW("Assist light on when running pre-flash sequence");

                if (mAAA->getAeLock()) {
                    LOG1("AE was locked in %s, using old flash decision from AE "
                         "locking time (%s)", __FUNCTION__, mAELockFlashNeed ? "ON" : "OFF");
                    flashOn = mAELockFlashNeed;
                }
                else
                    flashOn = mAAA->getAeFlashNecessary();
            }

            if (flashOn) {
                if (mAAA->getAeMode() != CAM_AE_MODE_MANUAL &&
                        flashMode != CAM_AE_FLASH_MODE_TORCH) {
                    flashOn = runPreFlashSequence();
                }
            }
        }
    }

    if (mState == STATE_CONTINUOUS_CAPTURE) {
        bool useFlash = flashOn && flashMode != CAM_AE_FLASH_MODE_TORCH;
        status = continuousStartStillCapture(useFlash);

        // Snapshot timestamp in continuous-mode doesn't met with actual
        // frame taken time, but the time when frame is ready to be queueud.
        // Until better timestamp available, using the systemTime() when we
        // turn the offline capture mode on.
        snapshotTs = systemTime()/1000;
    } else {
        status = stopPreviewCore();
        if (status != NO_ERROR) {
            LOGE("Error stopping preview!");
            return status;
        }
        mState = STATE_CAPTURE;
    }
    mBurstCaptureNum = 0;
    mBurstCaptureDoneNum = 0;
    mBurstQbufs = 0;

    // Get the current params
    mParameters.getPictureSize(&width, &height);
    mParameters.getPreviewSize(&pvWidth, &pvHeight);
    if (pvWidth > width || pvHeight > height) {
        // we can't configure postview to be bigger than picture size,
        // the same driver/ISP limitation as with video-sizes
        pvWidth = width;
        pvHeight = height;
        // no support for postview2preview when size differs
        previewKeepAlive = false;
    }
    format = mISP->getSnapshotPixelFormat();
    size = frameSize(format, width, height);
    pvSize = frameSize(format, pvWidth, pvHeight);

    // Configure PictureThread
    mPictureThread->initialize(mParameters);

    if (mState != STATE_CONTINUOUS_CAPTURE) {

        // Possible smart scene parameter changes (XNR, ANR)
        if ((status = setSmartSceneParams()) != NO_ERROR)
            LOG1("set smart scene parameters failed");

        // Configure and start the ISP
        mISP->setSnapshotFrameFormat(width, height, format);
        mISP->setPostviewFrameFormat(pvWidth, pvHeight, format);
        if (mHdr.enabled) {
            mHdr.outMainBuf.buff = NULL;
            mHdr.outPostviewBuf.buff = NULL;
        }

        setExternalSnapshotBuffers(format, width, height);

        // Initialize bracketing manager before streaming starts
        if (mBurstLength > 1 && mBracketManager->getBracketMode() != BRACKET_NONE) {
            mBracketManager->initBracketing(mBurstLength, mFpsAdaptSkip);
        }

        if ((status = mISP->configure(MODE_CAPTURE)) != NO_ERROR) {
            LOGE("Error configuring the ISP driver for CAPTURE mode");
            return status;
        }

        PERFORMANCE_TRACES_SHOT2SHOT_STEP("start ISP", -1);

        status = mISP->allocateBuffers(MODE_CAPTURE);
        if (status != NO_ERROR) {
            LOGE("Error allocate buffers in ISP");
            return status;
        }

        if (mAAA->is3ASupported())
            if (mAAA->switchModeAndRate(MODE_CAPTURE, mISP->getFrameRate()) != NO_ERROR)
                LOGE("Failed to switch 3A to capture mode at %.2f fps", mISP->getFrameRate());
        if ((status = mISP->start()) != NO_ERROR) {
            LOGE("Error starting the ISP driver in CAPTURE mode");
            return status;
        }
    }

    // Start the actual bracketing sequence
    if (mBurstLength > 1 && mBracketManager->getBracketMode() != BRACKET_NONE) {
        mBracketManager->startBracketing();
    }

    // HDR init
    if (mHdr.enabled &&
       (status = hdrInit( size, pvSize, format, width, height, pvWidth, pvHeight)) != NO_ERROR) {
        LOGE("Error initializing HDR!");
        return status;
    }

    /*
     *  If the current camera does not have 3A, then we should skip the first
     *  frames in order to allow the sensor to warm up.
     */
    if (!mAAA->is3ASupported()) {
        if ((status = skipFrames(NUM_WARMUP_FRAMES)) != NO_ERROR) {
            LOGE("Error skipping warm-up frames!");
            return status;
        }
    }

    // Turn on flash. If flash mode is torch, then torch is already on
    if (flashOn && flashMode != CAM_AE_FLASH_MODE_TORCH && mBurstLength <= 1) {
        LOG1("Requesting flash");
        if (mISP->setFlash(1) != NO_ERROR) {
            LOGE("Failed to enable the Flash!");
        }
        else {
            flashFired = true;
        }
    } else if (DetermineFlash(flashMode)) {
        mISP->setFlashIndicator(TORCH_INTENSITY);
    }

    status = burstCaptureSkipFrames();
    if (status != NO_ERROR) {
        LOGE("Error skipping burst frames!");
        return status;
    }

    PERFORMANCE_TRACES_SHOT2SHOT_STEP("get frame", 1);

    if (mState == STATE_CONTINUOUS_CAPTURE) {
        // TODO: to be removed once preview data flow is moved fully to
        //       a separate thread
        if (mBurstLength > 1)
            mBurstQbufs = mISP->getSnapshotNum();
        status = waitForCaptureStart();
        if (status != NO_ERROR) {
            LOGE("Error while waiting for capture to start");
            return status;
        }
    }

    // Get the snapshot
    if (flashFired) {
        status = getFlashExposedSnapshot(&snapshotBuffer, &postviewBuffer);
        // Set flash off only if torch is not used
        if (flashMode != CAM_AE_FLASH_MODE_TORCH)
            mISP->setFlash(0);
    } else {
        if (mBurstLength > 1 && mBracketManager->getBracketMode() != BRACKET_NONE) {
            status = mBracketManager->getSnapshot(snapshotBuffer, postviewBuffer);
        } else {
            status = mISP->getSnapshot(&snapshotBuffer, &postviewBuffer);
        }
    }

    if (status != NO_ERROR) {
        LOGE("Error in grabbing snapshot!");
        return status;
    }

    PerformanceTraces::ShutterLag::snapshotTaken(&snapshotBuffer.capture_timestamp);

    PERFORMANCE_TRACES_SHOT2SHOT_STEP("got frame",
                                       snapshotBuffer.frameCounter);

    PictureThread::MetaData picMetaData;
    fillPicMetaData(picMetaData, flashFired);

    // HDR Processing
    if (mHdr.enabled &&
       (status = hdrProcess(&snapshotBuffer, &postviewBuffer)) != NO_ERROR) {
        LOGE("HDR: Error in compute CDF for capture %d in HDR sequence!", mBurstCaptureNum);
        picMetaData.free();
        return status;
    }

    mBurstCaptureNum++;

    if (mState != STATE_CONTINUOUS_CAPTURE &&
            (!mHdr.enabled || (mHdr.enabled && mBurstCaptureNum == 1))) {
        // Send request to play the Shutter Sound: in single shots or when burst-length is specified
        mCallbacksThread->shutterSound();
    }

    // Turn off flash
    if (!flashOn && DetermineFlash(flashMode) && mBurstLength <= 1) {
        mISP->setFlashIndicator(0);
    }

    // Do jpeg encoding in other cases except HDR. Encoding HDR will be done later.
    bool doEncode = false;
    if (!mHdr.enabled) {
        LOG1("TEST-TRACE: starting picture encode: Time: %lld", systemTime());
        status = mPictureThread->encode(picMetaData, &snapshotBuffer, &postviewBuffer);
        if (status == NO_ERROR) {
            doEncode = true;
        }
    }

    if (doEncode == false) {
        // normally this is done by PictureThread, but as no
        // encoding was done, free the allocated metadata
        picMetaData.free();
    }

    if (mState == STATE_CONTINUOUS_CAPTURE && mBurstLength <= 1)
        stopOfflineCapture();

    if (previewKeepAlive && !mHdr.enabled) {
        mPreviewThread->postview(&postviewBuffer);
    } else if (mState == STATE_CONTINUOUS_CAPTURE) {
        // Continuous mode will keep running keeping 3A active but
        // preview hidden, disable AF callbacks to act API compatible
        // Note: lens shouldn't be moving either, but we need that for
        // better AF behaviour in continuous-shooting mode
        mEnableFocusCbAtStart = msgTypeEnabled(CAMERA_MSG_FOCUS);
        mEnableFocusMoveCbAtStart = msgTypeEnabled(CAMERA_MSG_FOCUS_MOVE);
        disableMsgType(CAMERA_MSG_FOCUS_MOVE);
        disableMsgType(CAMERA_MSG_FOCUS);
        // flush preview buffers queued during & right after transition to
        // offline capture mode
        flushContinuousPreviewToDisplay(snapshotTs);
    }

    return status;
}

status_t ControlThread::captureBurstPic(bool clientRequest = false)
{
    LOG1("@%s: ", __FUNCTION__);
    status_t status = NO_ERROR;
    AtomBuffer snapshotBuffer, postviewBuffer;
    int width, height, format, size;
    int pvWidth, pvHeight, pvSize;
    bool previewKeepAlive =
        isParameterSet(IntelCameraParameters::KEY_PREVIEW_KEEP_ALIVE);

    if (clientRequest) {
        bool requestPostviewCallback = true;
        bool requestRawCallback = true;

        // Notify CallbacksThread that a picture was requested, so grab one from queue
        mCallbacksThread->requestTakePicture(requestPostviewCallback, requestRawCallback);

        /*
         *  If the CallbacksThread has already JPEG buffers in queue, make sure we use them, before
         *  continuing to dequeue frames from ISP and encode them
         */

        if (mCallbacksThread->getQueuedBuffersNum() > MAX_JPEG_BUFFERS) {
            return NO_ERROR;
        }
        // Check if ISP has free buffers we can use
        if (mBracketManager->getBracketMode() == BRACKET_NONE && !mISP->dataAvailable()) {
            // If ISP has no data, do nothing and return
            return NO_ERROR;
        }

        // If burst length was specified stop capturing when reached the requested burst captures
        if (mBurstLength > 1 && mBurstCaptureNum >= mBurstLength) {
            return NO_ERROR;
        }
    }

    PERFORMANCE_TRACES_SHOT2SHOT_TAKE_PICTURE_HANDLE();

    /**
     * Time to return the used frames to ISP, we do not do this in the function
     * "handleMessagePictureDone".
     * If HDR is enabled, don't return the buffers. we need them to compose HDR
     * image. The buffers will be discarded after HDR is done in stopCapture().
     * When HAL is in still single burst capture or burst capture, no need to
     * return picture frames back to ISP, because the buffers allocated are enough,
     * but for continuous capture, ControlThread will qbuf back to ISP before next capture.
     */
    if ((!mHdr.enabled) && (mBurstLength < 1) && (!mUnqueuedPicBuf.isEmpty())) {
        // Return the last picture frames back to ISP
        LOG1("return snapshot buffer to ISP");
        for (size_t i = 0; i < mUnqueuedPicBuf.size(); i++) {
            AtomBuffer snapshotBuf = mUnqueuedPicBuf[i].snapshotBuf;
            AtomBuffer postviewBuf = mUnqueuedPicBuf[i].postviewBuf;
            status = mISP->putSnapshot(&snapshotBuf, &postviewBuf);
            if (status == NO_ERROR) {
                mUnqueuedPicBuf.removeAt(i);
            } else if (status == DEAD_OBJECT) {
                mUnqueuedPicBuf.removeAt(i);
                LOG1("Stale snapshot buffer returned to ISP");
            } else if (status != NO_ERROR) {
                LOGE("Error in putting snapshot!");
            }
        }
    }

    // Get the current params
    mParameters.getPictureSize(&width, &height);
    mParameters.getPreviewSize(&pvWidth, &pvHeight);
    if (pvWidth > width || pvHeight > height) {
        // we can't configure postview to be bigger than picture size,
        // the same driver/ISP limitation as with video-sizes
        pvWidth = width;
        pvHeight = height;
        // no support for postview2preview when size differs
        previewKeepAlive = false;
    }
    format = mISP->getSnapshotPixelFormat();
    size = frameSize(format, width, height);
    pvSize = frameSize(format, pvWidth, pvHeight);

    // note: flash is not supported in burst and continuous shooting
    //       modes (this would be the place to enable it)

    status = burstCaptureSkipFrames();
    if (status != NO_ERROR) {
        LOGE("Error skipping burst frames!");
        return status;
    }

    // Get the snapshot
    if (mBurstLength > 1 && mBracketManager->getBracketMode() != BRACKET_NONE) {
        status = mBracketManager->getSnapshot(snapshotBuffer, postviewBuffer);
    } else {
        status = mISP->getSnapshot(&snapshotBuffer, &postviewBuffer);
    }

    if (status != NO_ERROR) {
        LOGE("Error in grabbing snapshot!");
        return status;
    }

    PERFORMANCE_TRACES_SHOT2SHOT_STEP("got frame",
                                       snapshotBuffer.frameCounter);

    if (previewKeepAlive && !mHdr.enabled)
        mPreviewThread->postview(&postviewBuffer);

    PictureThread::MetaData picMetaData;
    fillPicMetaData(picMetaData, false);

   // HDR Processing
    if ( mHdr.enabled &&
        (status = hdrProcess(&snapshotBuffer, &postviewBuffer)) != NO_ERROR) {
        LOGE("Error processing HDR!");
        picMetaData.free();
        return status;
    }

    mBurstCaptureNum++;

    // Do jpeg encoding

    bool doEncode = false;
    if (!mHdr.enabled || (mHdr.enabled && mHdr.saveOrig && picMetaData.aeConfig->evBias == 0)) {
        doEncode = true;
        mCallbacksThread->shutterSound();
        LOG1("TEST-TRACE: starting picture encode: Time: %lld", systemTime());
        status = mPictureThread->encode(picMetaData, &snapshotBuffer, &postviewBuffer);
    }

    if (mHdr.enabled && mBurstCaptureNum == mHdr.bracketNum) {
        // This was the last capture in HDR sequence, compose the final HDR image
        LOG1("HDR: last capture, composing HDR image...");

        status = hdrCompose();
        if (status != NO_ERROR)
            LOGE("Error composing HDR picture");
    }

    if (doEncode == false) {
        // normally this is done by PictureThread, but as no
        // encoding was done, free the allocated metadata
        picMetaData.free();
    }

    if (mBurstLength > 1 && mBracketManager->getBracketMode() != BRACKET_NONE && (mBurstCaptureNum == mBurstLength)) {
        LOG1("@%s: Bracketing done, got all %d snapshots", __FUNCTION__, mBurstLength);
        mBracketManager->stopBracketing();
    }

    return status;
}

/**
 * Notifies CallbacksThread that a picture was requested by
 * the application.
 */
void ControlThread::requestTakePicture()
{
    bool requestPostviewCallback = true;
    bool requestRawCallback = true;

    // Notify CallbacksThread that a picture was requested, so grab one from queue
    mCallbacksThread->requestTakePicture(requestPostviewCallback, requestRawCallback);
}

/**
 * Whether the JPEG/compressed frame queue in CallbacksThread is
 * already full?
 */
bool ControlThread::compressedFrameQueueFull()
{
    return mCallbacksThread->getQueuedBuffersNum() > MAX_JPEG_BUFFERS;
}

/**
 * Queues unused snapshot buffers to ISP.
 *
 * Note: in certain use-cases like single captures,
 * this step can be omitted to save in capture time.
 */
status_t ControlThread::queueSnapshotBuffers()
{
    LOG2("@%s:", __FUNCTION__);
    status_t status = NO_ERROR;
    for (size_t i = 0; i < mUnqueuedPicBuf.size(); i++) {
        AtomBuffer snapshotBuf = mUnqueuedPicBuf[i].snapshotBuf;
        AtomBuffer postviewBuf = mUnqueuedPicBuf[i].postviewBuf;
        LOG2("return snapshot buffer %u to ISP", i);
        status = mISP->putSnapshot(&snapshotBuf, &postviewBuf);
        if (status == NO_ERROR) {
            ++mBurstQbufs;
        }
        else if (status == DEAD_OBJECT) {
            LOG1("Stale snapshot buffer returned to ISP");
        } else if (status != NO_ERROR) {
            LOGE("Error in putting snapshot!");
        }
    }
    mUnqueuedPicBuf.clear();
    return status;
}

/**
 * Starts capture of the next picture of the ongoing fixed-size burst.
 */
status_t ControlThread::captureFixedBurstPic(bool clientRequest = false)
{
    LOG1("@%s: ", __FUNCTION__);
    status_t status = NO_ERROR;
    AtomBuffer snapshotBuffer, postviewBuffer;

    assert(mState == STATE_CONTINUOUS_CAPTURE);

    if (clientRequest) {
        requestTakePicture();

        // Check whether more frames are needed
        if (compressedFrameQueueFull())
            return NO_ERROR;
    }

    if (mBurstCaptureNum != -1 &&
        mBurstLength > 1 &&
        mBurstCaptureNum >= mBurstLength) {
        // All frames of the burst have been requested (but not necessarily
        // yet all dequeued).
        return NO_ERROR;
    }

    PERFORMANCE_TRACES_SHOT2SHOT_TAKE_PICTURE_HANDLE();

    PictureThread::MetaData picMetaData;
    fillPicMetaData(picMetaData, false);

    // Get the snapshot
    status = mISP->getSnapshot(&snapshotBuffer, &postviewBuffer);

    if (status != NO_ERROR) {
        LOGE("Error in grabbing snapshot!");
        picMetaData.free();
        return status;
    }

    mBurstCaptureNum++;

    PERFORMANCE_TRACES_SHOT2SHOT_STEP("got frame",
                                       snapshotBuffer.frameCounter);

    // Do jpeg encoding
    LOG1("TEST-TRACE: starting picture encode: Time: %lld", systemTime());
    status = mPictureThread->encode(picMetaData, &snapshotBuffer, &postviewBuffer);

    // If all captures have been requested, ISP capture device
    // can be stopped. Otherwise requeue buffers back to ISP.
    if (mBurstCaptureNum == mBurstLength) {
        stopOfflineCapture();
    }
    else if (mBurstLength > mISP->getSnapshotNum() &&
             mBurstQbufs < mBurstLength) {
        // To save capture time, only requeue buffers if total
        // burst length exdeeds the ISP buffer queue size, and
        // more buffers are needed.
        queueSnapshotBuffers();
    }

    return status;
}

status_t ControlThread::captureVideoSnap()
{
    LOG1("@%s: ", __FUNCTION__);
    status_t status = NO_ERROR;

    mCallbacksThread->requestTakePicture(true, true);

    // Configure PictureThread
    mPictureThread->initialize(mParameters);

    /* Request a new video snapshot in the next capture cycle
     * In the next call of dequeuePreview we will send the preview
     * and recording frame to encode
     */
    mVideoSnapshotrequested++;

    return status;
}

void ControlThread::encodeVideoSnapshot(int buffId)
{
    LOG1("@%s: ", __FUNCTION__);
    PictureThread::MetaData aDummyMetaData;
    int pvWidth,pvHeight;
    AtomBuffer* postViewBuf;

    fillPicMetaData(aDummyMetaData, false);
    LOG1("Encoding a video snapshot couple buf id:%d", buffId);
    LOG2("snapshot size %dx%d stride %d format %d", mCoupledBuffers[buffId].recordingBuff.width
            ,mCoupledBuffers[buffId].recordingBuff.height
            ,mCoupledBuffers[buffId].recordingBuff.stride
            ,mCoupledBuffers[buffId].recordingBuff.format);

    mCoupledBuffers[buffId].videoSnapshotBuff = true;
    mCoupledBuffers[buffId].videoSnapshotBuffReturned = false;
    mCallbacksThread->shutterSound();

    pvWidth = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    pvHeight= mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    postViewBuf = &(mCoupledBuffers[buffId].previewBuff);

    mPictureThread->encode(aDummyMetaData,
                           &(mCoupledBuffers[buffId].recordingBuff),
                           postViewBuf);

}

status_t ControlThread::updateSpotWindow(const int &width, const int &height)
{
    LOG1("@%s", __FUNCTION__);
    // TODO: Check, if these window fractions are right. Copied off from libcamera1
    CameraWindow spotWin = { (int)width * 7.0 / 16.0, (int)width * 9.0 / 16.0, (int)height * 7.0 / 16.0, (int)height * 9.0 / 16.0, 255 };
    return mAAA->setAeWindow(&spotWin);
}

MeteringMode ControlThread::aeMeteringModeFromString(const String8& modeStr)
{
    LOG1("@%s", __FUNCTION__);
    MeteringMode mode(CAM_AE_METERING_MODE_AUTO);

    if (modeStr == "auto") {
        mode = CAM_AE_METERING_MODE_AUTO;
    } else if (modeStr == "center") {
        mode = CAM_AE_METERING_MODE_CENTER;
    } else if(modeStr == "spot") {
        mode = CAM_AE_METERING_MODE_SPOT;
    }

    return mode;
}

status_t ControlThread::handleMessageTakeSmartShutterPicture()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    // In case of smart shutter with HDR, we need to trigger save orig as a normal capture.
    if (mHdr.enabled && mHdr.saveOrig && mPostProcThread->isSmartCaptureTriggered()) {
        mPostProcThread->resetSmartCaptureTrigger();
        status = handleMessageTakePicture();
    } else {   //normal smart shutter capture
        mPostProcThread->captureOnTrigger();
        mState = selectPreviewMode(mParameters);
    }

    return status;
}

status_t ControlThread::handleMessageCancelPicture()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mBurstLength = 0;
    mPictureThread->flushBuffers();

    mMessageQueue.reply(MESSAGE_ID_CANCEL_PICTURE, status);
    return status;
}

status_t ControlThread::handleMessageAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    FlashMode flashMode = mAAA->getAeFlashMode();

    PERFORMANCE_TRACES_SHOT2SHOT_STEP_NOPARAM();

    // Implement pre auto-focus functions
    if (flashMode != CAM_AE_FLASH_MODE_TORCH && mAAA->is3ASupported() && mBurstLength <= 1) {
        if (!mFlashAutoFocus && (DetermineFlash(flashMode) || flashMode == CAM_AE_FLASH_MODE_ON)) {
            LOG1("Flash mode = %d", flashMode);
            if (mAAA->getAfNeedAssistLight()) {
                mFlashAutoFocus = true;
            }
        }

        if (mFlashAutoFocus) {
            LOG1("Using Torch for auto-focus");
            mISP->setTorch(TORCH_INTENSITY);
        }
    }

    //If the apps call autoFocus(AutoFocusCallback), the camera will stop sending face callbacks.
    // The last face callback indicates the areas used to do autofocus. After focus completes,
    // face detection will resume sending face callbacks.
    //If the apps call cancelAutoFocus(), the face callbacks will also resume.
    LOG2("auto focus is on");
    if (mFaceDetectionActive)
        disableMsgType(CAMERA_MSG_PREVIEW_METADATA);
    // Auto-focus should be done in AAAThread, so send a message directly to it
    status = m3AThread->autoFocus();

    // If start auto-focus failed and we enabled torch, disable it now
    if (status != NO_ERROR && mFlashAutoFocus) {
        mISP->setTorch(0);
        mFlashAutoFocus = false;
    }

    return status;
}

status_t ControlThread::handleMessageCancelAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    status = m3AThread->cancelAutoFocus();
    LOG2("auto focus is off");
    if (mFaceDetectionActive)
        enableMsgType(CAMERA_MSG_PREVIEW_METADATA);
    if (mFlashAutoFocus) {
        mISP->setTorch(0);
        mFlashAutoFocus = false;
    }
    /*
     * The normal autoFocus sequence is:
     * - camera client is calling autoFocus (we run the AF sequence and lock AF)
     * - camera client is calling:
     *     - takePicture: AF is locked, so the picture will have the focus established
     *       in previous step. In this case, we have to reset the auto-focus to enabled
     *       when the camera client will call startPreview.
     *     - cancelAutoFocus: AF is locked, camera client no longer wants this focus position
     *       so we should switch back to auto-focus in 3A library
     */
    if (mAAA->is3ASupported()) {
        mAAA->setAfEnabled(true);
    }
    return status;
}

status_t ControlThread::handleMessageReleaseRecordingFrame(MessageReleaseRecordingFrame *msg)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (mState == STATE_RECORDING) {
        AtomBuffer *recBuff = findRecordingBuffer(msg->buff);
        if (recBuff == NULL) {
            // This may happen with buffer sharing. When the omx component is stopped
            // it disables buffer sharing and deallocates its buffers. Internally we check
            // to see if sharing was disabled then we restart the ISP with new buffers. In
            // the mean time, the app is returning us shared buffers when we are no longer
            // using them.
            LOGE("Could not find recording buffer: %p", msg->buff);
            return DEAD_OBJECT;
        }
        int curBuff = recBuff->id;
        LOG2("Recording buffer released from encoder, buff id = %d", curBuff);
        if (mCoupledBuffers && curBuff < mNumBuffers) {
            mCoupledBuffers[curBuff].recordingBuffReturned = true;
            status = queueCoupledBuffers(curBuff);
        }
    }
    return status;
}

status_t ControlThread::handleMessagePreviewDone(MessagePreviewDone *msg)
{
    LOG2("@%s, buffer id = %d", __FUNCTION__, msg->buff.id);
    if (!mISP->isBufferValid(&msg->buff)) {
        LOGE("Invalid preview buffer returned by preview Thread");
        return DEAD_OBJECT;
    }

    if (mFaceDetectionActive || mPanoramaThread->getState() == PANORAMA_DETECTING_OVERLAP) {
        LOG2("@%s: face detection active", __FUNCTION__);
        msg->buff.rotation = mParameters.getInt(CameraParameters::KEY_ROTATION);
        msg->buff.owner = this;
        if (mPostProcThread->sendFrame(&msg->buff, AtomISP::zoomRatio(mParameters.getInt(CameraParameters::KEY_ZOOM))) < 0) {
            msg->buff.owner = 0;
            releasePreviewFrame(&msg->buff);
        }
    } else {
       releasePreviewFrame(&msg->buff);
    }

    ++mPreviewFramesDone;
    if (mPreviewFramesDone == 1 &&
           mState != STATE_CONTINUOUS_CAPTURE) {

        /**
         * First preview frame was rendered.
         * Now preview is ongoing. Complete now any initialization that is not
         * strictly needed to do, before preview is started so it doesn't
         * impact launch to preview time.
         *
         * In this case we send the request to the PictureThread to allocate
         * the snapshot buffers
         */
        allocateSnapshotBuffers();
    }
    return NO_ERROR;
}

status_t ControlThread::handleMessageReleasePreviewFrame(MessageReleasePreviewFrame *msg)
{
    LOG2("handle preview frame release buff id = %d", msg->buff.id);
    status_t status = NO_ERROR;
    if (mState == STATE_PREVIEW_STILL || mState == STATE_CONTINUOUS_CAPTURE) {
        status = mISP->putPreviewFrame(&msg->buff);
        if (status == DEAD_OBJECT) {
            LOG1("Stale preview buffer returned to ISP");
        } else if (status != NO_ERROR) {
            LOGE("Error putting preview frame to ISP");
        }
    } else if (mState == STATE_PREVIEW_VIDEO || mState == STATE_RECORDING) {
        int curBuff = msg->buff.id;
        if (mCoupledBuffers && curBuff < mNumBuffers) {
            mCoupledBuffers[curBuff].previewBuffReturned = true;
            status = queueCoupledBuffers(curBuff);
        }
    }
    return status;
}

status_t ControlThread::queueCoupledBuffers(int coupledId)
{
    LOG2("@%s: coupledId = %d", __FUNCTION__, coupledId);
    status_t status = NO_ERROR;

    CoupledBuffer *buff = &mCoupledBuffers[coupledId];

    if (!buff->previewBuffReturned || !buff->recordingBuffReturned ||
            (buff->videoSnapshotBuff && !buff->videoSnapshotBuffReturned))
        return NO_ERROR;
    LOG2("Putting buffer back to ISP, coupledId = %d",  coupledId);
    status = mISP->putRecordingFrame(&buff->recordingBuff);
    if (status == NO_ERROR) {
        status = mISP->putPreviewFrame(&buff->previewBuff);
        if (status == DEAD_OBJECT) {
            LOGW("Stale preview buffer returned to ISP");
        } else if (status != NO_ERROR) {
            LOGE("Error putting preview frame to ISP");
        }
    } else if (status == DEAD_OBJECT) {
        LOGW("Stale recording buffer returned to ISP");
    } else {
        LOGE("Error putting recording frame to ISP");
    }

    return status;
}

status_t ControlThread::handleMessagePictureDone(MessagePicture *msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (msg->snapshotBuf.type == ATOM_BUFFER_PANORAMA) {
        // panorama pictures are special, they use the panorama engine memory.
        // we return them to panorama for releasing
        msg->snapshotBuf.owner->returnBuffer(&msg->snapshotBuf);
        msg->snapshotBuf.owner->returnBuffer(&msg->postviewBuf);
    } else if (mState == STATE_RECORDING) {
        int curBuff = msg->snapshotBuf.id;
        if (mCoupledBuffers && curBuff < mNumBuffers) {
            mCoupledBuffers[curBuff].videoSnapshotBuffReturned = true;
            status = queueCoupledBuffers(curBuff);
            mCoupledBuffers[curBuff].videoSnapshotBuffReturned = false;
            mCoupledBuffers[curBuff].videoSnapshotBuff = false;
        }
    } else if (mState == STATE_CAPTURE || mState == STATE_CONTINUOUS_CAPTURE) {
        /*
         * Store the buffer that has not been returned to ISP, and it shall be returned
         * when next capturing happen
         * The reason is to save S2S time, don't need to qbuf back to ISP in still
         * and burst capture
         */
        mUnqueuedPicBuf.push(*msg);

        if (isBurstRunning()) {
            ++mBurstCaptureDoneNum;
            LOG2("Burst req %d done %d len %d",
                 mBurstCaptureNum, mBurstCaptureDoneNum, mBurstLength);
            if (mBurstCaptureDoneNum >= mBurstLength) {
                LOGW("Last pic in burst received, terminating");
                burstStateReset();
            }
        }

    } else {
        LOGW("Received a picture Done during invalid state %d; buf id:%d, ptr=%p", mState, msg->snapshotBuf.id, msg->snapshotBuf.buff);
    }


    return status;
}

status_t ControlThread::handleMessageAutoFocusDone()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (mFaceDetectionActive)
        enableMsgType(CAMERA_MSG_PREVIEW_METADATA);
    // Implement post auto-focus functions
    if (mFlashAutoFocus) {
        mISP->setTorch(0);
        mFlashAutoFocus = false;
    }

    return status;
}

bool ControlThread::validateSize(int width, int height, Vector<Size> &supportedSizes) const
{
    if (width < 0 || height < 0)
        return false;

    for (Vector<Size>::iterator it = supportedSizes.begin(); it != supportedSizes.end(); ++it)
        if (width == it->width && height == it->height)
            return true;

    return false;
}

bool ControlThread::validateString(const char* value,  const char* supportList) const{
    // value should not set if support list is empty
    if (value !=NULL && supportList == NULL) {
        return false;
    }

    if (value == NULL || supportList == NULL) {
        return true;
    }

    size_t len = strlen(value);
    const char* startPtr(supportList);
    const char* endPtr(supportList);
    int bracketLevel(0);

    // divide support list to values and compare those to given values.
    // values are separated with comma in support list, but commas also exist
    // part of values inside bracket.
    while (true) {
        if ( *endPtr == '(') {
            ++bracketLevel;
        } else if (*endPtr == ')') {
            --bracketLevel;
        } else if ( bracketLevel == 0 && ( *endPtr == '\0' || *endPtr == ',')) {
            if (((startPtr + len) == endPtr) &&
                (strncmp(value, startPtr, len) == 0)) {
                return true;
            }

            // bracket can use circle values in supported list
            if (((startPtr + len + 2 ) == endPtr) &&
                ( *startPtr == '(') &&
                (strncmp(value, startPtr + 1, len) == 0)) {
                return true;
            }
            startPtr = endPtr + 1;
        }

        if (*endPtr == '\0') {
            return false;
        }
        ++endPtr;
    }

    return false;
}

status_t ControlThread::validateParameters(const CameraParameters *params)
{
    LOG1("@%s: params = %p", __FUNCTION__, params);
    // PREVIEW
    int width, height;
    Vector<Size> supportedSizes;
    params->getSupportedPreviewSizes(supportedSizes);
    params->getPreviewSize(&width, &height);
    if (!validateSize(width, height, supportedSizes)) {
        LOGE("bad preview size");
        return BAD_VALUE;
    }

    int minFPS, maxFPS;
    params->getPreviewFpsRange(&minFPS, &maxFPS);
    if (minFPS == maxFPS || minFPS > maxFPS) {
        LOGE("invalid fps range [%d,%d]", minFPS, maxFPS);
        return BAD_VALUE;
    }

    // VIDEO
    params->getVideoSize(&width, &height);
    supportedSizes.clear();
    params->getSupportedVideoSizes(supportedSizes);
    if (!validateSize(width, height, supportedSizes)) {
        LOGE("bad video size %dx%d", width, height);
        return BAD_VALUE;
    }

    // SNAPSHOT
    params->getPictureSize(&width, &height);
    supportedSizes.clear();
    params->getSupportedPictureSizes(supportedSizes);
    if (!validateSize(width, height, supportedSizes)) {
        LOGE("bad picture size");
        return BAD_VALUE;
    }

    // JPEG QUALITY
    int jpegQuality = params->getInt(CameraParameters::KEY_JPEG_QUALITY);
    if (jpegQuality < 1 || jpegQuality > 100) {
        LOGE("bad jpeg quality: %d", jpegQuality);
        return BAD_VALUE;
    }

    // THUMBNAIL QUALITY
    int thumbQuality = params->getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if (thumbQuality < 1 || thumbQuality > 100) {
        LOGE("bad thumbnail quality: %d", thumbQuality);
        return BAD_VALUE;
    }

    // THUMBNAIL SIZE
    int thumbWidth = params->getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    int thumbHeight = params->getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    char* thumbnailSizes = (char*) params->get(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES);
    supportedSizes.clear();

    while (true) {
        int width = (int)strtol(thumbnailSizes, &thumbnailSizes, 10);
        int height = (int)strtol(thumbnailSizes+1, &thumbnailSizes, 10);
        supportedSizes.push(Size(width, height));
        if (*thumbnailSizes == '\0')
            break;
        ++thumbnailSizes;
    }

    if (!validateSize(thumbWidth, thumbHeight, supportedSizes)) {
        LOGE("bad thumbnail size: (%d,%d)", thumbWidth, thumbHeight);
        return BAD_VALUE;
    }

    // PICTURE FORMAT
    const char* picFormat = params->get(CameraParameters::KEY_PICTURE_FORMAT);
    const char* picFormats = params->get(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS);
    if (!validateString(picFormat, picFormats)) {
        LOGE("bad picture format: %s", picFormat);
        return BAD_VALUE;
    }

    // PREVIEW FORMAT
    const char* preFormat = params->get(CameraParameters::KEY_PREVIEW_FORMAT);
    const char* preFormats = params->get(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS);
    if (!validateString(preFormat, preFormats))  {
        LOGE("bad preview format: %s", preFormat);
        return BAD_VALUE;
    }

    // ROTATION, can only be 0 ,90, 180 or 270.
    int rotation = params->getInt(CameraParameters::KEY_ROTATION);
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        LOGE("bad rotation value: %d", rotation);
        return BAD_VALUE;
    }


    // WHITE BALANCE
    const char* whiteBalance = params->get(CameraParameters::KEY_WHITE_BALANCE);
    const char* whiteBalances = params->get(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE);
    if (!validateString(whiteBalance, whiteBalances)) {
        LOGE("bad white balance mode: %s", whiteBalance);
        return BAD_VALUE;
    }

    // ZOOM
    int zoom = params->getInt(CameraParameters::KEY_ZOOM);
    int maxZoom = params->getInt(CameraParameters::KEY_MAX_ZOOM);
    if (zoom > maxZoom || zoom < 0) {
        LOGE("bad zoom index: %d", zoom);
        return BAD_VALUE;
    }

    // FLASH
    const char* flashMode = params->get(CameraParameters::KEY_FLASH_MODE);
    const char* flashModes = params->get(CameraParameters::KEY_SUPPORTED_FLASH_MODES);
    if (!validateString(flashMode, flashModes)) {
        LOGE("bad flash mode");
        return BAD_VALUE;
    }

    // SCENE MODE
    const char* sceneMode = params->get(CameraParameters::KEY_SCENE_MODE);
    const char* sceneModes = params->get(CameraParameters::KEY_SUPPORTED_SCENE_MODES);
    if (!validateString(sceneMode, sceneModes)) {
        LOGE("bad scene mode: %s; supported: %s", sceneMode, sceneModes);
        return BAD_VALUE;
    }

    // FOCUS
    const char* focusMode = params->get(CameraParameters::KEY_FOCUS_MODE);
    const char* focusModes = params->get(CameraParameters::KEY_SUPPORTED_FOCUS_MODES);
    if (!validateString(focusMode, focusModes)) {
        LOGE("bad focus mode: %s; supported: %s", focusMode, focusModes);
        return BAD_VALUE;
    }

    // BURST LENGTH
    const char* burstLength = params->get(IntelCameraParameters::KEY_BURST_LENGTH);
    const char* burstLengths = params->get(IntelCameraParameters::KEY_SUPPORTED_BURST_LENGTH);
    if (!validateString(burstLength, burstLengths)) {
        LOGE("bad burst length: %s; supported: %s", burstLength, burstLengths);
        return BAD_VALUE;
    }
    int burstStart = params->getInt(IntelCameraParameters::KEY_BURST_START_INDEX);
    const char* captureBracket = params->get(IntelCameraParameters::KEY_CAPTURE_BRACKET);
    if (burstStart < 0 && captureBracket && captureBracket && String8(captureBracket) != "none") {
        LOGE("negative start-index and bracketing not supported concurrently");
        return BAD_VALUE;
    }

    // BURST FPS
    const char* burstFps = params->get(IntelCameraParameters::KEY_BURST_FPS);
    const char* burstFpss = params->get(IntelCameraParameters::KEY_SUPPORTED_BURST_FPS);
    if (!validateString(burstFps,burstFpss)) {
        LOGE("bad burst FPS: %s; supported: %s", burstFps, burstFpss);
        return BAD_VALUE;
    }

    // OVERLAY
    const char* overlaySupported = params->get(IntelCameraParameters::KEY_HW_OVERLAY_RENDERING_SUPPORTED);
    const char* overlay = params->get(IntelCameraParameters::KEY_HW_OVERLAY_RENDERING);
        if (!validateString(overlay, overlaySupported)) {
        LOGE("bad overlay rendering mode: %s; supported: %s", overlay, overlaySupported);
        return BAD_VALUE;
    }

    // MISCELLANEOUS
    const char *size = params->get(IntelCameraParameters::KEY_PANORAMA_LIVE_PREVIEW_SIZE);
    const char *livePreviewSizes = IntelCameraParameters::getSupportedPanoramaLivePreviewSizes(*params);
    if (!validateString(size, livePreviewSizes)) {
        LOGE("bad panorama live preview size");
        return BAD_VALUE;
    }

    // ANTI FLICKER
    const char* flickerMode = params->get(CameraParameters::KEY_ANTIBANDING);
    const char* flickerModes = params->get(CameraParameters::KEY_SUPPORTED_ANTIBANDING);
    if (!validateString(flickerMode, flickerModes)) {
        LOGE("bad anti flicker mode");
        return BAD_VALUE;
    }

    // COLOR EFFECT
    const char* colorEffect = params->get(CameraParameters::KEY_EFFECT);
    const char* colorEffects = params->get(CameraParameters::KEY_SUPPORTED_EFFECTS);
    if (!validateString(colorEffect, colorEffects)) {
        LOGE("bad color effect: %s", colorEffect);
        return BAD_VALUE;
    }

    // EXPOSURE COMPENSATION
    int exposure = params->getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    int minExposure = params->getInt(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);
    int maxExposure = params->getInt(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
    if (exposure > maxExposure || exposure < minExposure) {
        LOGE("bad exposure compensation value: %d", exposure);
        return BAD_VALUE;
    }

    //Note: here for Intel expand parameters, add additional validity check
    //for their supported list. when they're null, we return bad value for
    //these intel parameters setting. As "noise reduction and edge enhancement"
    //and "multi access color correction" are not supported yet.

    // NOISE_REDUCTION_AND_EDGE_ENHANCEMENT
    const char* noiseReductionAndEdgeEnhancement = params->get(IntelCameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
    const char* noiseReductionAndEdgeEnhancements = params->get(IntelCameraParameters::KEY_SUPPORTED_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
    if (!validateString(noiseReductionAndEdgeEnhancement, noiseReductionAndEdgeEnhancements)) {
        LOGE("bad noise reduction and edge enhancement value : %s", noiseReductionAndEdgeEnhancement);
        return BAD_VALUE;
    }

    // MULTI_ACCESS_COLOR_CORRECTION
    const char* multiAccessColorCorrection = params->get(IntelCameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION);
    const char* multiAccessColorCorrections = params->get(IntelCameraParameters::KEY_SUPPORTED_MULTI_ACCESS_COLOR_CORRECTIONS);
    if (!validateString(multiAccessColorCorrection, multiAccessColorCorrections)) {
        LOGE("bad multi access color correction value : %s", multiAccessColorCorrection);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

status_t ControlThread::ProcessOverlayEnable(const CameraParameters *oldParams,
                                                   CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int cameraId = mISP->getCurrentCameraId();
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_HW_OVERLAY_RENDERING);

    if (!newVal.isEmpty() && (mState == STATE_STOPPED))  {

        if (newVal == "true") {
            if (mPreviewThread->enableOverlay(true, PlatformData::overlayRotation(cameraId)) == NO_ERROR) {
                newParams->set(IntelCameraParameters::KEY_HW_OVERLAY_RENDERING, "true");
                LOG1("@%s: Preview Overlay rendering enabled!", __FUNCTION__);
            } else {
                LOGE("Could not configure Overlay preview rendering");
            }
        }
    } else {
        LOGW("Overlay cannot be enabled in other state than stop, ignoring request");
    }

    return status;
}

status_t ControlThread::processParamBurst(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // Burst mode
    // Get the burst length
    mBurstLength = newParams->getInt(IntelCameraParameters::KEY_BURST_LENGTH);
    mFpsAdaptSkip = 0;
    if (mBurstLength <= 0) {
        // Parameter not set, leave it as 0
         mBurstLength = 0;
    } else {
        // Get the burst framerate
        int fps = newParams->getInt(IntelCameraParameters::KEY_BURST_FPS);
        if (fps > MAX_BURST_FRAMERATE) {
            LOGE("Invalid value received for %s: %d", IntelCameraParameters::KEY_BURST_FPS, mFpsAdaptSkip);
            return BAD_VALUE;
        }
        if (fps > 0) {
            mFpsAdaptSkip = roundf(PlatformData::getMaxBurstFPS(mISP->getCurrentCameraId())/float(fps)) - 1;
            LOG1("%s, mFpsAdaptSkip:%d", __FUNCTION__, mFpsAdaptSkip);
        }
    }

    // Burst start-index (for Time Nudge et al)
    int burstStart = newParams->getInt(IntelCameraParameters::KEY_BURST_START_INDEX);
    if (burstStart != mBurstStart) {
        LOG1("Burst start-index set %d -> %d", mBurstStart, burstStart);
        mBurstStart = burstStart;
    }

    return status;
}

status_t ControlThread::processDynamicParameters(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    int newZoom = newParams->getInt(CameraParameters::KEY_ZOOM);
    bool zoomSupported = isParameterSet(CameraParameters::KEY_ZOOM_SUPPORTED) ? true : false;
    if (zoomSupported)
        status = mISP->setZoom(newZoom);
    else
        LOGD("not supported zoom setting");

    // Color effect
    if (status == NO_ERROR) {
        status = processParamEffect(oldParams, newParams);
    }

    // anti flicker
    if (status == NO_ERROR) {
        status = processParamAntiBanding(oldParams, newParams);
    }

    // raw data format for snapshot
    if (status == NO_ERROR) {
        status = processParamRawDataFormat(oldParams, newParams);
    }

    // preview framerate
    // NOTE: This is deprecated since Android API level 9, applications should use
    // setPreviewFpsRange()
    if (status == NO_ERROR) {
        status = processParamPreviewFrameRate(oldParams, newParams);
    }

    // Changing the scene may change many parameters, including
    // flash, awb. Thus the order of how processParamFoo() are
    // called is important for the parameter changes to take
    // effect, and processParamSceneMode needs to be called first.
    if (status == NO_ERROR) {
        // Scene Mode
        status = processParamSceneMode(oldParams, newParams);
    }

    // slow motion value settings in high speed recording mode
    if (status == NO_ERROR) {
        status = processParamSlowMotionRate(oldParams, newParams);
    }

    if (status == NO_ERROR) {
        // white balance
        status = processParamWhiteBalance(oldParams, newParams);
    }

    if (status == NO_ERROR) {
        // exposure compensation
        status = processParamExposureCompensation(oldParams, newParams);
    }

    if (status == NO_ERROR) {
        // ISO manual setting (Intel extension)
        status = processParamIso(oldParams, newParams);
    }

    if (!mFaceDetectionActive && status == NO_ERROR) {
        // customize metering
        status = processParamSetMeteringAreas(oldParams, newParams);
    }

    if (mAAA->is3ASupported()) {

        if (status == NO_ERROR) {
            // flash settings
            status = processParamFlash(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            //Focus Mode
            status = processParamFocusMode(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // ae lock
            status = processParamAELock(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // af lock
            status = processParamAFLock(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // awb lock
            status = processParamAWBLock(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // xnr/anr
            status = processParamXNR_ANR(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // Capture bracketing
            status = processParamBracket(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // Smart Shutter Capture
            status = processParamSmartShutter(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // ae mode
            status = processParamAutoExposureMeteringMode(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // ae mode
            status = processParamAutoExposureMode(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // shutter manual setting (Intel extension)
            status = processParamShutter(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // back lighting correction (Intel extension)
            status = processParamBackLightingCorrectionMode(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // AWB mapping mode (Intel extension)
            status = processParamAwbMappingMode(oldParams, newParams);
        }

    }

    // In case of continuous-capture, the buffers are allocated
    // when restarting the preview (done if picture size changes).
    if (status == NO_ERROR &&
            mState != STATE_STOPPED &&
            mState != STATE_CONTINUOUS_CAPTURE) {
        // Request PictureThread to allocate snapshot buffers
        allocateSnapshotBuffers();
    }

    return status;
}

status_t ControlThread::allocateSnapshotBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int picWidth, picHeight;
    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;

    mParameters.getPictureSize(&picWidth, &picHeight);
    if(videoMode){
       /**
        * In video mode we configure the Picture thread not to pre-allocate
        * the snapshot buffers. This means that there will be no active libVA
        * context created. we cannot have more than one libVA (encoder) context active
        * and in video mode the video encoder already creates one.
        */
       status = mPictureThread->allocSharedBuffers(picWidth, picHeight, 0);
    } else if (mBurstLength > 1){
       int bufCount = mBurstLength > NUM_BURST_BUFFERS ? NUM_BURST_BUFFERS : mBurstLength;
       status = mPictureThread->allocSharedBuffers(picWidth, picHeight, bufCount);
    } else {
       status = mPictureThread->allocSharedBuffers(picWidth, picHeight, NUM_SINGLE_STILL_BUFFERS);
    }

    if (status != NO_ERROR) {
       LOGW("Could not pre-allocate picture buffers!");
    }

    return status;
}
void ControlThread::processParamFileInject(CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);

    unsigned int width = 0, height = 0, bayerOrder = 0, format = 0;
    const char *fileName = newParams->get(IntelCameraParameters::KEY_FILE_INJECT_FILENAME);

    if (!fileName || !strncmp(fileName, "off", sizeof("off")))
        return;

    width = newParams->getInt(IntelCameraParameters::KEY_FILE_INJECT_WIDTH);
    height = newParams->getInt(IntelCameraParameters::KEY_FILE_INJECT_HEIGHT);
    bayerOrder = newParams->getInt(IntelCameraParameters::KEY_FILE_INJECT_BAYER_ORDER);
    format = newParams->getInt(IntelCameraParameters::KEY_FILE_INJECT_FORMAT);

    LOG1("FILE INJECTION new parameter dumping:");
    LOG1("file name=%s,width=%d,height=%d,format=%d,bayer-order=%d.",
          fileName, width, height, format, bayerOrder);
    mISP->configureFileInject(fileName, width, height, format, bayerOrder);

}
status_t ControlThread::processParamAFLock(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // af lock mode
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_AF_LOCK_MODE);
    if (!newVal.isEmpty()) {
        bool af_lock;
        // TODO: once available, use the definitions in Intel
        //       parameter namespace, see UMG BZ26264
        const char* PARAM_LOCK = "lock";
        const char* PARAM_UNLOCK = "unlock";

        if(newVal == PARAM_LOCK) {
            af_lock = true;
        } else if(newVal == PARAM_UNLOCK) {
            af_lock = false;
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_AF_LOCK_MODE, newVal.string());
            return INVALID_OPERATION;
        }
        status = mAAA->setAfLock(af_lock);

        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_AF_LOCK_MODE, newVal.string());
        }
    }

    return status;
}

status_t ControlThread::processParamAWBLock(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // awb lock mode
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK);

    if (!newVal.isEmpty()) {
        bool awb_lock;

        if(newVal == CameraParameters::TRUE) {
            awb_lock = true;
        } else if(newVal == CameraParameters::FALSE) {
            awb_lock = false;
        } else {
            LOGE("Invalid value received for %s: %s", CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK, newVal.string());
            return INVALID_OPERATION;
        }
        status = m3AThread->lockAwb(awb_lock);
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK, newVal.string());
        }
    }

    return status;
}

status_t ControlThread::processParamXNR_ANR(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // XNR
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_XNR);
    LOG2("XNR value new %s ", newVal.string());
    if (!newVal.isEmpty()) {
        if (newVal == CameraParameters::TRUE)
            status = mISP->setXNR(true);
        else
            status = mISP->setXNR(false);
    }

    // ANR
    newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_ANR);
    LOG2("ANR value new %s ", newVal.string());
    if (!newVal.isEmpty()) {
        if (newVal == CameraParameters::TRUE)
            status = mISP->setLowLight(true);
        else
            status = mISP->setLowLight(false);
    }

    return status;
}

/**
 * Processing of antibanding parameters
 * it checks if the parameter changed and then it selects the correct
 * FlickerMode
 * If 3A is supported by the sensor (i.e is a raw sensor) then configure
 * 3A library,
 * if it is a SOC sensor then the auto-exposure is controled via the sensor driver
 * so configure ISP
 * @param oldParams old parameters
 * @param newParams new parameters
 * @return NO_ERROR: everything went fine. settigns are applied
 * @return UNKNOWN_ERROR: error configuring 3A or V4L2
 */
status_t ControlThread::processParamAntiBanding(const CameraParameters *oldParams,
                                                      CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    FlickerMode lightFrequency;

    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              CameraParameters::KEY_ANTIBANDING);
    if (!newVal.isEmpty()) {

        if (newVal == CameraParameters::ANTIBANDING_50HZ)
            lightFrequency = CAM_AE_FLICKER_MODE_50HZ;
        else if (newVal == CameraParameters::ANTIBANDING_60HZ)
            lightFrequency = CAM_AE_FLICKER_MODE_60HZ;
        else if (newVal == CameraParameters::ANTIBANDING_AUTO)
            lightFrequency = CAM_AE_FLICKER_MODE_AUTO;
        else
            lightFrequency = CAM_AE_FLICKER_MODE_OFF;

        if(mAAA->is3ASupported()) {
            status = mAAA->setAeFlickerMode(lightFrequency);
        } else {
            status = mISP->setLightFrequency(lightFrequency);
        }

    }

    return status;
}

status_t ControlThread::processParamAELock(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // ae lock mode

    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              CameraParameters::KEY_AUTO_EXPOSURE_LOCK);
    if (!newVal.isEmpty()) {
        bool ae_lock;

        if(newVal == CameraParameters::TRUE) {
            ae_lock = true;
        } else  if(newVal == CameraParameters::FALSE) {
            ae_lock = false;
        } else {
            LOGE("Invalid value received for %s: %s", CameraParameters::KEY_AUTO_EXPOSURE_LOCK, newVal.string());
            return INVALID_OPERATION;
        }

        status = m3AThread->lockAe(ae_lock);
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_AUTO_EXPOSURE_LOCK, newVal.string());
            if (ae_lock) {
                mAELockFlashNeed = mAAA->getAeFlashNecessary();
                LOG1("AE locked, storing flash necessity decision (%s)", mAELockFlashNeed ? "ON" : "OFF");
            }
        }
    }

    return status;
}

status_t ControlThread::processParamFlash(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              CameraParameters::KEY_FLASH_MODE);

    if (!newVal.isEmpty()) {
        FlashMode flash = CAM_AE_FLASH_MODE_AUTO;
        if(newVal == CameraParameters::FLASH_MODE_AUTO)
            flash = CAM_AE_FLASH_MODE_AUTO;
        else if(newVal == CameraParameters::FLASH_MODE_OFF)
            flash = CAM_AE_FLASH_MODE_OFF;
        else if(newVal == CameraParameters::FLASH_MODE_ON)
            flash = CAM_AE_FLASH_MODE_ON;
        else if(newVal == CameraParameters::FLASH_MODE_TORCH)
            flash = CAM_AE_FLASH_MODE_TORCH;
        else if(newVal == IntelCameraParameters::FLASH_MODE_SLOW_SYNC)
            flash = CAM_AE_FLASH_MODE_SLOW_SYNC;
        else if(newVal == IntelCameraParameters::FLASH_MODE_DAY_SYNC)
            flash = CAM_AE_FLASH_MODE_DAY_SYNC;

        mSavedFlashMode = newVal;

        if (flash == CAM_AE_FLASH_MODE_TORCH && mAAA->getAeFlashMode() != CAM_AE_FLASH_MODE_TORCH) {
            mISP->setTorch(TORCH_INTENSITY);
        }

        if (flash != CAM_AE_FLASH_MODE_TORCH && mAAA->getAeFlashMode() == CAM_AE_FLASH_MODE_TORCH) {
            mISP->setTorch(0);
        }

        status = mAAA->setAeFlashMode(flash);
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_FLASH_MODE, newVal.string());
        }
    }
    return status;
}

status_t ControlThread::processParamEffect(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              CameraParameters::KEY_EFFECT);

    if (!newVal.isEmpty()) {
        status = m3AControls->set3AColorEffect(newVal.string());
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_EFFECT, newVal.string());
        }
    }
    return status;
}

status_t ControlThread::processParamBracket(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_CAPTURE_BRACKET);

    if (!newVal.isEmpty()) {
        if(newVal == "exposure") {
            mBracketManager->setBracketMode(BRACKET_EXPOSURE);
        } else if(newVal == "focus") {
            mBracketManager->setBracketMode(BRACKET_FOCUS);
        } else if(newVal == "none") {
            mBracketManager->setBracketMode(BRACKET_NONE);
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_CAPTURE_BRACKET, newVal.string());
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_CAPTURE_BRACKET, newVal.string());
        }
    }
    return status;
}

status_t ControlThread::processParamSmartShutter(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    //smile shutter threshold
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_SMILE_SHUTTER_THRESHOLD);
    if (!newVal.isEmpty()) {
        int value = newParams->getInt(IntelCameraParameters::KEY_SMILE_SHUTTER_THRESHOLD);
        if (value < 0 || value > SMILE_THRESHOLD_MAX) {
            LOGE("Invalid value received for %s: %d, set to default %d",
                IntelCameraParameters::KEY_SMILE_SHUTTER_THRESHOLD, value, SMILE_THRESHOLD);
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %d", IntelCameraParameters::KEY_SMILE_SHUTTER_THRESHOLD, value);
        }
    }

    //blink shutter threshold
    newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                      IntelCameraParameters::KEY_BLINK_SHUTTER_THRESHOLD);
    if (!newVal.isEmpty()) {
        int value = newParams->getInt(IntelCameraParameters::KEY_BLINK_SHUTTER_THRESHOLD);
        if (value < 0 || value > BLINK_THRESHOLD_MAX) {
            LOGE("Invalid value received for %s: %d, set to default %d",
                IntelCameraParameters::KEY_BLINK_SHUTTER_THRESHOLD, value, BLINK_THRESHOLD);
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %d", IntelCameraParameters::KEY_BLINK_SHUTTER_THRESHOLD, value);
        }
    }
    return status;
}

status_t ControlThread::processParamHDR(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    status_t localStatus = NO_ERROR;
    int newWidth, newHeight;
    int oldWidth, oldHeight;

    newParams->getPictureSize(&newWidth, &newHeight);
    oldParams->getPictureSize(&oldWidth, &oldHeight);

    // Check the HDR parameters
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_HDR_IMAGING);

    if (!newVal.isEmpty()) {
        if(newVal == "on") {
            mHdr.enabled = true;
            mHdr.bracketMode = BRACKET_EXPOSURE;
            mHdr.savedBracketMode = mBracketManager->getBracketMode();
            mHdr.bracketNum = DEFAULT_HDR_BRACKETING;
        } else if(newVal == "off") {
            mHdr.enabled = false;
            mBracketManager->setBracketMode(mHdr.savedBracketMode);
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_HDR_IMAGING, newVal.string());
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_HDR_IMAGING, newVal.string());
        }
    }

    if (mHdr.enabled) {
        // Dependency parameters
        mBurstLength = mHdr.bracketNum;
        mBracketManager->setBracketMode(mHdr.bracketMode);
    }

    newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_HDR_SHARPENING);
    if (!newVal.isEmpty()) {
        localStatus = NO_ERROR;
        if(newVal == "normal") {
            mHdr.sharpening = NORMAL_SHARPENING;
        } else if(newVal == "strong") {
            mHdr.sharpening = STRONG_SHARPENING;
        } else if(newVal == "none") {
            mHdr.sharpening = NO_SHARPENING;
        } else {
            LOGW("Invalid value received for %s: %s", IntelCameraParameters::KEY_HDR_SHARPENING, newVal.string());
            localStatus = BAD_VALUE;
        }
        if (localStatus == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_HDR_SHARPENING, newVal.string());
        }
    }

    newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_HDR_VIVIDNESS);
    if (!newVal.isEmpty()) {
        localStatus = NO_ERROR;
        if(newVal == "gaussian") {
            mHdr.vividness = GAUSSIAN_VIVIDNESS;
        } else if(newVal == "gamma") {
            mHdr.vividness = GAMMA_VIVIDNESS;
        } else if(newVal == "none") {
            mHdr.vividness = NO_VIVIDNESS;
        } else {
            // the default value is kept
            LOGW("Invalid value received for %s: %s", IntelCameraParameters::KEY_HDR_VIVIDNESS, newVal.string());
            localStatus = BAD_VALUE;
        }
        if (localStatus == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_HDR_VIVIDNESS, newVal.string());
        }
    }

    newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL);
    if (!newVal.isEmpty()) {
        localStatus = NO_ERROR;
        if(newVal == "on") {
            mHdr.saveOrig = true;
        } else if(newVal == "off") {
            mHdr.saveOrig = false;
        } else {
            // the default value is kept
            LOGW("Invalid value received for %s: %s", IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL, newVal.string());
            localStatus = BAD_VALUE;
        }
        if (localStatus == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL, newVal.string());
        }
    }

    return status;
}

/**
 * select flash mode for single or burst capture
 * in burst capture, the flash is forced to off, otherwise
 * saved single capture flash mode is applied.
 * \param newParams
 */
void ControlThread::selectFlashMode(CameraParameters *newParams)
{
    // !mBurstLength is only for CTS to pass
    LOG1("@%s", __FUNCTION__);
    if (mBurstLength == 1 || !mBurstLength) {
        newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, mSavedFlashSupported.string());
        newParams->set(CameraParameters::KEY_FLASH_MODE, mSavedFlashMode.string());
    } else {
        newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "off");
        newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
    }
}

status_t ControlThread::processParamSceneMode(const CameraParameters *oldParams,
        CameraParameters *newParams, bool applyImmediately)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newScene = paramsReturnNewIfChanged(oldParams, newParams, CameraParameters::KEY_SCENE_MODE);

    // we can't run this during init() because CTS mandates flash to be off. Thus we will initially be in auto
    // scene mode with flash off, thanks to CTS. Therefore we check mThreadRunning which is off during init().
    if (!newScene.isEmpty() && mThreadRunning) {
        SceneMode sceneMode = CAM_AE_SCENE_MODE_AUTO;
        if (newScene == CameraParameters::SCENE_MODE_PORTRAIT) {
            sceneMode = CAM_AE_SCENE_MODE_PORTRAIT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE);
            newParams->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto,continuous-picture");
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            if (PlatformData::supportsBackFlash()) {
                mSavedFlashSupported = String8("auto,off,on,torch");
                mSavedFlashMode = String8(CameraParameters::FLASH_MODE_AUTO);
                selectFlashMode(newParams);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_AE_METERING_MODES, "auto,center");
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else if (newScene == CameraParameters::SCENE_MODE_SPORTS || newScene == CameraParameters::SCENE_MODE_PARTY) {
            sceneMode = (newScene == CameraParameters::SCENE_MODE_SPORTS) ? CAM_AE_SCENE_MODE_SPORTS : CAM_AE_SCENE_MODE_PARTY;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_INFINITY);
            newParams->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "infinity");
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                mSavedFlashSupported = String8("off");
                mSavedFlashMode = String8(CameraParameters::FLASH_MODE_OFF);
                selectFlashMode(newParams);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else if (newScene == CameraParameters::SCENE_MODE_LANDSCAPE || newScene == CameraParameters::SCENE_MODE_SUNSET) {
            sceneMode = (newScene == CameraParameters::SCENE_MODE_LANDSCAPE) ? CAM_AE_SCENE_MODE_LANDSCAPE : CAM_AE_SCENE_MODE_CANDLELIGHT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_INFINITY);
            newParams->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "infinity");
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                mSavedFlashSupported = String8("off");
                mSavedFlashMode = String8(CameraParameters::FLASH_MODE_OFF);
                selectFlashMode(newParams);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_OUTDOOR);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else if (newScene == CameraParameters::SCENE_MODE_NIGHT) {
            sceneMode = CAM_AE_SCENE_MODE_NIGHT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_INFINITY);
            newParams->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "infinity");
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                mSavedFlashSupported = String8("off");
                mSavedFlashMode = String8(CameraParameters::FLASH_MODE_OFF);
                selectFlashMode(newParams);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::TRUE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "true");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::TRUE);
        } else if (newScene == CameraParameters::SCENE_MODE_NIGHT_PORTRAIT) {
            sceneMode = CAM_AE_SCENE_MODE_NIGHT_PORTRAIT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE);
            newParams->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto,continuous-picture");
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                mSavedFlashSupported = String8("on");
                mSavedFlashMode = String8(CameraParameters::FLASH_MODE_ON);
                selectFlashMode(newParams);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::TRUE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "true");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::TRUE);
        } else if (newScene == CameraParameters::SCENE_MODE_FIREWORKS) {
            sceneMode = CAM_AE_SCENE_MODE_FIREWORKS;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_INFINITY);
            newParams->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "infinity");
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                mSavedFlashSupported = String8("off");
                mSavedFlashMode = String8(CameraParameters::FLASH_MODE_OFF);
                selectFlashMode(newParams);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else if (newScene == CameraParameters::SCENE_MODE_BARCODE) {
            sceneMode = CAM_AE_SCENE_MODE_TEXT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_MACRO);
            newParams->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "macro,continuous-picture");
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            if (PlatformData::supportsBackFlash()) {
                mSavedFlashSupported = String8("auto,off,on,torch");
                mSavedFlashMode = String8(CameraParameters::FLASH_MODE_AUTO);
                selectFlashMode(newParams);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else {
            if (newScene == CameraParameters::SCENE_MODE_CANDLELIGHT) {
                sceneMode = CAM_AE_SCENE_MODE_CANDLELIGHT;
            } else {
                LOG1("Unsupported %s: %s. Using AUTO!", CameraParameters::KEY_SCENE_MODE, newScene.string());
                sceneMode = CAM_AE_SCENE_MODE_AUTO;
            }

            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE);
            newParams->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto,infinity,fixed,macro,continuous-video,continuous-picture");
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, "off,50hz,60hz,auto");
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            if (PlatformData::supportsBackFlash()) {
                mSavedFlashSupported = String8("auto,off,on,torch");
                mSavedFlashMode = String8(CameraParameters::FLASH_MODE_AUTO);
                selectFlashMode(newParams);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_AE_METERING_MODES, "auto,center,spot");
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "true,false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        }

        if (applyImmediately) {
            m3AControls->setAeSceneMode(sceneMode);
            if (status == NO_ERROR) {
                LOG1("Changed: %s -> %s", CameraParameters::KEY_SCENE_MODE, newScene.string());
            }
        }

        // If Intel params are not allowed,
        // we should update Intel params setting to HW, and remove them here.
        if (!mIntelParamsAllowed) {
            if (applyImmediately) {
                processParamBackLightingCorrectionMode(oldParams, newParams);
                processParamAwbMappingMode(oldParams, newParams);
                processParamXNR_ANR(oldParams, newParams);
            }
            newParams->remove(IntelCameraParameters::KEY_AWB_MAPPING_MODE);
            newParams->remove(IntelCameraParameters::KEY_SUPPORTED_AWB_MAPPING_MODES);
            newParams->remove(IntelCameraParameters::KEY_SUPPORTED_AE_METERING_MODES);
            newParams->remove(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE);
            newParams->remove(IntelCameraParameters::KEY_SUPPORTED_XNR);
            newParams->remove(IntelCameraParameters::KEY_XNR);
            newParams->remove(IntelCameraParameters::KEY_SUPPORTED_ANR);
            newParams->remove(IntelCameraParameters::KEY_ANR);
        }
    }

    return status;
}

void ControlThread::preSetCameraWindows(CameraWindow* focusWindows, size_t winCount)
{
    LOG1("@%s", __FUNCTION__);
    if (winCount > 0) {
        int width;
        int height;
        mParameters.getPreviewSize(&width, &height);
        AAAWindowInfo aaaWindow;
        mAAA->getGridWindow(aaaWindow);

        for (size_t i = 0; i < winCount; i++) {
            // Camera KEY_FOCUS_AREAS Coordinates range from -1000 to 1000. Let's convert..
            convertFromAndroidCoordinates(focusWindows[i], focusWindows[i], aaaWindow);
            LOG1("Preset camera window %d: (%d,%d,%d,%d)",
                    i,
                    focusWindows[i].x_left,
                    focusWindows[i].y_top,
                    focusWindows[i].x_right,
                    focusWindows[i].y_bottom);
        }
    }
}

status_t ControlThread::processParamFocusMode(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams, CameraParameters::KEY_FOCUS_MODE);
    AfMode afMode = CAM_AF_MODE_NOT_SET;

    if (!newVal.isEmpty()) {
        if (newVal == CameraParameters::FOCUS_MODE_AUTO) {
            afMode = CAM_AF_MODE_AUTO;
        } else if (newVal == CameraParameters::FOCUS_MODE_INFINITY) {
            afMode = CAM_AF_MODE_INFINITY;
        } else if (newVal == CameraParameters::FOCUS_MODE_FIXED) {
            afMode = CAM_AF_MODE_FIXED;
        } else if (newVal == CameraParameters::FOCUS_MODE_MACRO) {
            afMode = CAM_AF_MODE_MACRO;
        } else if (newVal == CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO ||
                   newVal == CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE) {
            afMode = CAM_AF_MODE_CONTINUOUS;
        } else {
            afMode = CAM_AF_MODE_MANUAL;
        }

        // If the focus mode was explicitly set to infinity or fixed, disable AF
        if (afMode == CAM_AF_MODE_INFINITY || afMode == CAM_AF_MODE_FIXED) {
            mPostProcThread->disableFaceAAA(AAA_FLAG_AF);
        } else {
            mPostProcThread->enableFaceAAA(AAA_FLAG_AF);
        }

        status = mAAA->setAfEnabled(true);
        if (status == NO_ERROR) {
            status = mAAA->setAfMode(afMode);
        }
        if (status == NO_ERROR) {
            mAAA->setPublicAfMode(afMode);
            LOG1("Changed: %s -> %s", CameraParameters::KEY_FOCUS_MODE, newVal.string());
        }
    }

    if (!mFaceDetectionActive) {

        AfMode publicAfMode = mAAA->getPublicAfMode();
        // Based on Google specs, the focus area is effective only for modes:
        // (framework side constants:) FOCUS_MODE_AUTO, FOCUS_MODE_MACRO, FOCUS_MODE_CONTINUOUS_VIDEO
        // or FOCUS_MODE_CONTINUOUS_PICTURE.
        if (publicAfMode == CAM_AF_MODE_AUTO ||
            publicAfMode == CAM_AF_MODE_CONTINUOUS ||
            publicAfMode == CAM_AF_MODE_MACRO) {

            afMode = publicAfMode;

            // See if any focus areas are set.
            // NOTE: CAM_AF_MODE_TOUCH is for HAL internal use only
            if (!mFocusAreas.isEmpty()) {
                LOG1("Focus areas set, using AF mode \"touch \"");
                afMode = CAM_AF_MODE_TOUCH;
            }

            // See if we have to change the actual mode (it could be correct already)
            AfMode curAfMode = mAAA->getAfMode();
            if (afMode != curAfMode) {
                mAAA->setAfMode(afMode);
            }

            // If in touch mode, we set the focus windows now
            if (afMode == CAM_AF_MODE_TOUCH) {
                size_t winCount(mFocusAreas.numOfAreas());
                CameraWindow *focusWindows = new CameraWindow[winCount];
                mFocusAreas.toWindows(focusWindows);
                preSetCameraWindows(focusWindows, winCount);
                if (mAAA->setAfWindows(focusWindows, winCount) != NO_ERROR) {
                    // If focus windows couldn't be set, previous AF mode is used
                    // (AfSetWindowMulti has its own safety checks for coordinates)
                    LOGE("Could not set AF windows. Resetting the AF back to %d", curAfMode);
                    mAAA->setAfMode(curAfMode);
                }
                delete[] focusWindows;
                focusWindows = NULL;
            }
        }
    }

    return status;
}

status_t ControlThread:: processParamSetMeteringAreas(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // TODO: Support for more windows. At the moment we only support one?
    if (!mMeteringAreas.isEmpty()) {
        //int w, h;
        size_t winCount(mMeteringAreas.numOfAreas());
        CameraWindow *meteringWindows = new CameraWindow[winCount];
        CameraWindow aeWindow;
        AAAWindowInfo aaaWindow;

        mMeteringAreas.toWindows(meteringWindows);

        mAAA->getGridWindow(aaaWindow);
        //in our AE bg weight is 1, max is 255, thus working values are inside [2, 255].
        //Google probably expects bg weight to be zero, therefore sending happily 1 from
        //default camera app. To have some kind of visual effect, we start our range from 5
        convertFromAndroidCoordinates(meteringWindows[0], aeWindow, aaaWindow, 5, 255);

        if (m3AControls->setAeMeteringMode(CAM_AE_METERING_MODE_SPOT) == NO_ERROR) {
            LOG1("@%s, Got metering area, and \"spot\" mode set. Setting window.", __FUNCTION__ );
            if (mAAA->setAeWindow(&aeWindow) != NO_ERROR) {
                LOGW("Error setting AE metering window. Metering will not work");
            }
        } else {
                LOGW("Error setting AE metering mode to \"spot\". Metering will not work");
        }

        delete[] meteringWindows;
        meteringWindows = NULL;
    } else {
        // Resetting back to previous AE metering mode, if it was set (Intel extension, so
        // standard app won't be using "previous mode")
        const char* modeStr = newParams->get(IntelCameraParameters::KEY_AE_METERING_MODE);
        MeteringMode oldMode = CAM_AE_METERING_MODE_AUTO;
        if (modeStr != NULL) {
            oldMode = aeMeteringModeFromString(String8(modeStr));
        }

        if (oldMode != m3AControls->getAeMeteringMode()) {
            LOG1("Resetting from \"spot\" to (previous) AE metering mode (%d).", oldMode);
            m3AControls->setAeMeteringMode(oldMode);
        }

        if (oldMode == CAM_AE_METERING_MODE_SPOT) {
            AAAWindowInfo aaaWindow;
            mAAA->getGridWindow(aaaWindow);
            updateSpotWindow(aaaWindow.width, aaaWindow.height);
        }
    }

    return status;
}

status_t ControlThread::processParamExposureCompensation(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              CameraParameters::KEY_EXPOSURE_COMPENSATION);
    if (!newVal.isEmpty()) {
        int exposure = newParams->getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
        float comp_step = newParams->getFloat(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP);
        status = m3AControls->setEv(exposure * comp_step);
        float ev = 0;
        m3AControls->getEv(&ev);
        LOGD("exposure compensation to \"%s\" (%d), ev value %f, res %d",
             newVal.string(), exposure, ev, status);
    }
    return status;
}

/**
 * Sets AutoExposure mode

 * Note, this is an Intel extension, so the values are not defined in
 * Android documentation.
 */
status_t ControlThread::processParamAutoExposureMode(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_AE_MODE);
    if (!newVal.isEmpty()) {
        AeMode ae_mode (CAM_AE_MODE_AUTO);

        if (newVal == "auto") {
            ae_mode = CAM_AE_MODE_AUTO;
        } else if (newVal == "manual") {
            ae_mode = CAM_AE_MODE_MANUAL;
        } else if (newVal == "shutter-priority") {
            ae_mode = CAM_AE_MODE_SHUTTER_PRIORITY;
            // antibanding cannot be supported when shutter-priority
            // is selected, so turning antibanding off (see BZ17480)
            newParams->set(CameraParameters::KEY_ANTIBANDING, "off");
        } else if (newVal == "aperture-priority") {
            ae_mode = CAM_AE_MODE_APERTURE_PRIORITY;
        } else {
            LOGW("unknown AE_MODE \"%s\", falling back to AUTO", newVal.string());
            ae_mode = CAM_AE_MODE_AUTO;
        }
        mAAA->setPublicAeMode(ae_mode);
        mAAA->setAeMode(ae_mode);
        LOGD("Changed ae mode to \"%s\" (%d)", newVal.string(), ae_mode);

        if (mPublicShutter >= 0 &&
                (ae_mode == CAM_AE_MODE_SHUTTER_PRIORITY ||
                ae_mode == CAM_AE_MODE_MANUAL)) {
            mAAA->setManualShutter(mPublicShutter);
            LOGD("Changed shutter to %f", mPublicShutter);
        }
    }
    return status;
}

/**
 * Sets Auto Exposure Metering Mode
 *
 * Note, this is an Intel extension, so the values are not defined in
 * Android documentation.
 */
status_t ControlThread::processParamAutoExposureMeteringMode(
        const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_AE_METERING_MODE);
    if (!newVal.isEmpty()) {
        MeteringMode mode = aeMeteringModeFromString(newVal);

        // The fixed "spot" metering mode (and area) should be set only when user has set the
        // AE metering area to null (isEmpty() == true)
        if (mode == CAM_AE_METERING_MODE_SPOT && mMeteringAreas.isEmpty()) {
            AAAWindowInfo aaaWindow;
            mAAA->getGridWindow(aaaWindow);
            // Let's set metering area to fixed position here. We will also get arbitrary area
            // when using touch AE, which is handled in processParamSetMeteringAreas().
            updateSpotWindow(aaaWindow.width, aaaWindow.height);
        } else if (mode == CAM_AE_METERING_MODE_SPOT) {
            LOGE("User trying to set AE metering mode \"spot\" with an AE metering area.");
        }

        m3AControls->setAeMeteringMode(mode);
        LOGD("Changed ae metering mode to \"%s\" (%d)", newVal.string(), mode);
    }

    return status;
}

/**
 * Sets manual ISO sensitivity value
 *
 * Note, this is an Intel extension, so the values are not defined in
 * Android documentation.
 */
status_t ControlThread::processParamIso(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                   IntelCameraParameters::KEY_ISO);
    if (newVal.isEmpty()) return status;
    // note: value format is 'iso-NNN'
    const size_t iso_prefix_len = 4;
    if (newVal.length() > iso_prefix_len) {
        IsoMode iso_mode(CAM_AE_ISO_MODE_AUTO);
        const char* isostr = newVal.string() + iso_prefix_len;
        if (strcmp("auto", isostr)) {
            iso_mode = CAM_AE_ISO_MODE_MANUAL;
            int iso = atoi(isostr);
            m3AControls->setManualIso(iso);
            LOGD("Changed manual iso to \"%s\" (%d)", newVal.string(), iso);
        }
        m3AControls->setIsoMode(iso_mode);
    }
    return status;
}

/**
 * Sets manual shutter time value
 *
 * Note, this is an Intel extension, so the values are not defined in
 * Android documentation.
 */
status_t ControlThread::processParamShutter(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_SHUTTER);
    if (!newVal.isEmpty()) {
        float shutter = -1;
        bool flagParsed = false;

        if (strchr(newVal.string(), 's') != NULL) {
            // ns: n seconds
            shutter = atof(newVal.string());
            flagParsed = true;
        } else if (strchr(newVal.string(), 'm') != NULL) {
            // nm: n minutes
            shutter = atof(newVal.string()) * 60;
            flagParsed = true;
        } else {
            // n: 1/n second
            float tmp = atof(newVal.string());
            if (tmp > 0) {
                shutter = 1.0 / atof(newVal.string());
                flagParsed = true;
            }
        }

        if (flagParsed) {
            mPublicShutter = shutter;
            if (mAAA->getAeMode() == CAM_AE_MODE_MANUAL ||
                (mAAA->getAeMode() == CAM_AE_MODE_SHUTTER_PRIORITY)) {
                mAAA->setManualShutter(mPublicShutter);
                LOGD("Changed shutter to \"%s\" (%f)", newVal.string(), shutter);
            }
        }
    }

    return status;
}

/**
 * Sets Back Lighting Correction Mode
 *
 * Note, this is an Intel extension, so the values are not defined in
 * Android documentation.
 */
status_t ControlThread::processParamBackLightingCorrectionMode(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
            IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE);
    if (!newVal.isEmpty()) {
        bool backlightCorrection;

        if (newVal == "on") {
            backlightCorrection= true;
        } else if (newVal == "off") {
            backlightCorrection= false;
        } else {
            backlightCorrection = true;
        }

        mAAA->setAeBacklightCorrection(backlightCorrection);
        LOGD("Changed ae backlight correction to \"%s\" (%d)",
             newVal.string(), backlightCorrection);
    }

    return status;
}
/**
 * Sets AWB Mapping Mode
 *
 * Note, this is an Intel extension, so the values are not defined in
 * Android documentation.
 */
status_t ControlThread::processParamAwbMappingMode(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status(NO_ERROR);
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
            IntelCameraParameters::KEY_AWB_MAPPING_MODE);
    if (!newVal.isEmpty()) {
        ia_3a_awb_map awbMappingMode(ia_3a_awb_map_auto);

        if (newVal == IntelCameraParameters::AWB_MAPPING_OUTDOOR) {
            mPostProcThread->disableFaceAAA(AAA_FLAG_AWB);
        } else {
            mPostProcThread->enableFaceAAA(AAA_FLAG_AWB);
        }

        if (newVal == IntelCameraParameters::AWB_MAPPING_AUTO) {
            awbMappingMode = ia_3a_awb_map_auto;
        } else if (newVal == IntelCameraParameters::AWB_MAPPING_INDOOR) {
            awbMappingMode = ia_3a_awb_map_indoor;
        } else if (newVal == IntelCameraParameters::AWB_MAPPING_OUTDOOR) {
            awbMappingMode = ia_3a_awb_map_outdoor;
        } else {
            awbMappingMode = ia_3a_awb_map_auto;
        }

        status = mAAA->setAwbMapping(awbMappingMode);
        if (status ==  NO_ERROR) {
            LOGD("Changed AWB mapping mode to \"%s\" (%d)",
                 newVal.string(), awbMappingMode);
        } else {
            LOGE("Error setting AWB mapping mode (\"%s\" (%d))",
                 newVal.string(), awbMappingMode);
        }
    }

    return status;
}

status_t ControlThread::processParamWhiteBalance(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              CameraParameters::KEY_WHITE_BALANCE);
    if (!newVal.isEmpty()) {
        AwbMode wbMode = CAM_AWB_MODE_AUTO;
        // TODO: once available, use the definitions in Intel
        //       parameter namespace, see UMG BZ26264
        const char* PARAM_MANUAL = "manual";

        if(newVal == CameraParameters::WHITE_BALANCE_AUTO) {
            wbMode = CAM_AWB_MODE_AUTO;
        } else if(newVal == CameraParameters::WHITE_BALANCE_INCANDESCENT) {
            wbMode = CAM_AWB_MODE_WARM_INCANDESCENT;
        } else if(newVal == CameraParameters::WHITE_BALANCE_FLUORESCENT) {
            wbMode = CAM_AWB_MODE_FLUORESCENT;
        } else if(newVal == CameraParameters::WHITE_BALANCE_WARM_FLUORESCENT) {
            wbMode = CAM_AWB_MODE_WARM_FLUORESCENT;
        } else if(newVal == CameraParameters::WHITE_BALANCE_DAYLIGHT) {
            wbMode = CAM_AWB_MODE_DAYLIGHT;
        } else if(newVal == CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT) {
            wbMode = CAM_AWB_MODE_CLOUDY;
        } else if(newVal == CameraParameters::WHITE_BALANCE_TWILIGHT) {
            wbMode = CAM_AWB_MODE_SUNSET;
        } else if(newVal == CameraParameters::WHITE_BALANCE_SHADE) {
            wbMode = CAM_AWB_MODE_SHADOW;
        } else if(newVal == PARAM_MANUAL) {
            wbMode = CAM_AWB_MODE_MANUAL_INPUT;
        }

        status = m3AControls->setAwbMode(wbMode);

        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_WHITE_BALANCE, newVal.string());
        }
    }
    return status;
}

status_t ControlThread::processParamRawDataFormat(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_RAW_DATA_FORMAT);
    if (!newVal.isEmpty()) {
        if (newVal == "bayer") {
            CameraDump::setDumpDataFlag(CAMERA_DEBUG_DUMP_RAW);
            mCameraDump = CameraDump::getInstance();
        } else if (newVal == "yuv") {
            CameraDump::setDumpDataFlag(CAMERA_DEBUG_DUMP_YUV);
            mCameraDump = CameraDump::getInstance();
        } else
            CameraDump::setDumpDataFlag(RAW_NONE);
    }
    return NO_ERROR;
}

status_t ControlThread::processParamPreviewFrameRate(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s : NOTE: DEPRECATED", __FUNCTION__);

    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              CameraParameters::KEY_PREVIEW_FRAME_RATE);

    if (!newVal.isEmpty()) {
        LOGI("DEPRECATED: Got new preview frame rate: %s", newVal.string());
        int fps = newParams->getPreviewFrameRate();
        // Save the set FPS for doing frame dropping
        mSetFPS = fps;
    }

    return NO_ERROR;
}

/**
 * Sets slow motion rate value in high speed recording mode
 *
 * Note, this is an Intel extension, so the values are not defined in
 * Android documentation.
 */
status_t ControlThread::processParamSlowMotionRate(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);

    status_t status = NO_ERROR;
    String8 newVal = paramsReturnNewIfChanged(oldParams, newParams,
                                              IntelCameraParameters::KEY_SLOW_MOTION_RATE);
    if (!newVal.isEmpty()) {
        int slowMotionRate = 1;
        if(newVal == IntelCameraParameters::SLOW_MOTION_RATE_1X) {
            slowMotionRate = 1;
        } else if (newVal == IntelCameraParameters::SLOW_MOTION_RATE_2X) {
            slowMotionRate = 2;
        } else if (newVal == IntelCameraParameters::SLOW_MOTION_RATE_3X) {
            slowMotionRate = 3;
        } else if (newVal == IntelCameraParameters::SLOW_MOTION_RATE_4X) {
            slowMotionRate = 4;
        } else {
            return BAD_VALUE;
        }
        status = mVideoThread->setSlowMotionRate(slowMotionRate);
        if(status == NO_ERROR)
            LOG1("Changed hs value to \"%s\" (%d)", newVal.string(), slowMotionRate);
    }
    return status;
}




/*
 * NOTE: this function runs in camera service thread. Protect member accesses accordingly!
 *
 * @param[in] oldParams the previous parameters
 * @param[in] newParams the new parameters which are being set
 * @param[out] msg a message which will be sent for ControlThread for processing later,
 *             this function will return whether in video mode and whether preview
 *             format changed through in message structure.
 */
status_t ControlThread::processStaticParameters(const CameraParameters *oldParams,
        CameraParameters *newParams, Message &msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    bool previewFormatChanged = false;
    float previewAspectRatio = 0.0f;
    float videoAspectRatio = 0.0f;
    Vector<Size> sizes;
    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT, *newParams) ? true : false;
    bool dvsEnabled = isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION, *newParams) ?  true : false;

    int oldWidth, newWidth;
    int oldHeight, newHeight;
    int previewWidth, previewHeight;
    int oldFormat, newFormat;

    // see if preview params have changed
    newParams->getPreviewSize(&newWidth, &newHeight);
    oldParams->getPreviewSize(&oldWidth, &oldHeight);
    newFormat = V4L2Format(newParams->getPreviewFormat());
    oldFormat = V4L2Format(oldParams->getPreviewFormat());
    previewWidth = oldWidth;
    previewHeight = oldHeight;
    if (newWidth != oldWidth || newHeight != oldHeight ||
            oldFormat != newFormat) {
        previewWidth = newWidth;
        previewHeight = newHeight;
        previewAspectRatio = 1.0 * newWidth / newHeight;
        LOG1("Preview size/format is changing: old=%dx%d %s; new=%dx%d %s; ratio=%.3f",
                oldWidth, oldHeight, v4l2Fmt2Str(oldFormat),
                newWidth, newHeight, v4l2Fmt2Str(newFormat),
                previewAspectRatio);
        previewFormatChanged = true;
        mPreviewForceChanged = false;
    } else {
        previewAspectRatio = 1.0 * oldWidth / oldHeight;
        LOG1("Preview size/format is unchanged: old=%dx%d %s; ratio=%.3f",
                oldWidth, oldHeight, v4l2Fmt2Str(oldFormat),
                previewAspectRatio);
    }

    if(videoMode) {
        // see if video params have changed
        newParams->getVideoSize(&newWidth, &newHeight);
        oldParams->getVideoSize(&oldWidth, &oldHeight);
        if (newWidth != oldWidth || newHeight != oldHeight) {
            videoAspectRatio = 1.0 * newWidth / newHeight;
            LOG1("Video size is changing: old=%dx%d; new=%dx%d; ratio=%.3f",
                    oldWidth, oldHeight,
                    newWidth, newHeight,
                    videoAspectRatio);
            previewFormatChanged = true;
            /*
             *  Camera client requested a new video size, so make sure that requested
             *  video size matches requested preview size. If it does not, then select
             *  a corresponding preview size to match the aspect ratio with video
             *  aspect ratio. Also, the video size must be at least as preview size
             */
            if (fabsf(videoAspectRatio - previewAspectRatio) > ASPECT_TOLERANCE) {
                LOGW("Requested video (%dx%d) aspect ratio does not match preview \
                     (%dx%d) aspect ratio! The preview will be stretched!",
                        newWidth, newHeight,
                        previewWidth, previewHeight);
            }
        } else {
            videoAspectRatio = 1.0 * oldWidth / oldHeight;
            LOG1("Video size is unchanged: old=%dx%d; ratio=%.3f",
                    oldWidth, oldHeight,
                    videoAspectRatio);
            /*
             *  Camera client did not specify any video size, so make sure that
             *  requested preview size matches our default video size. If it does
             *  not, then select a corresponding video size to match the aspect
             *  ratio with preview aspect ratio.
             */
            if (fabsf(videoAspectRatio - previewAspectRatio) > ASPECT_TOLERANCE
                && !mPreviewForceChanged) {
                LOG1("Our video (%dx%d) aspect ratio does not match preview (%dx%d) aspect ratio!",
                      newWidth, newHeight, previewWidth, previewHeight);
                newParams->getSupportedVideoSizes(sizes);
                for (size_t i = 0; i < sizes.size(); i++) {
                    float thisSizeAspectRatio = 1.0 * sizes[i].width / sizes[i].height;
                    if (fabsf(thisSizeAspectRatio - previewAspectRatio) <= ASPECT_TOLERANCE) {
                        if (sizes[i].width < previewWidth || sizes[i].height < previewHeight) {
                            // This video size is smaller than preview, can't use it
                            continue;
                        }
                        newWidth = sizes[i].width;
                        newHeight = sizes[i].height;
                        LOG1("Forcing video to %dx%d to match preview aspect ratio!", newWidth, newHeight);
                        newParams->setVideoSize(newWidth, newHeight);
                        break;
                    }
                }
            }
        }
    }

    // Burst mode and HDR
    int oldBurstLength = mBurstLength;
    int oldFpsAdaptSkip = mFpsAdaptSkip;
    status = processParamBurst(oldParams, newParams);
    if (status == NO_ERROR) {
      status = processParamHDR(oldParams, newParams);
    }
    if (mBurstLength != oldBurstLength || mFpsAdaptSkip != oldFpsAdaptSkip) {
        LOG1("Burst configuration changed, restarting preview");
        previewFormatChanged = true;
    }

    /**
     * There are multiple workarounds related to what preview and video
     * size combinations can be supported by ISP (also impacted by
     * sensor configuration).
     *
     * Check the inline documentation for applyISPvideoLimitations()
     * in AtomISP.cpp to see detailed description of the limitations.
     *
     * NOTE: applyISPVideoLimitatiosn is const and the read access to
     * AtomISP member mCameraInput is safe after init, so we don't need
     * to lock in it.
     */
    if (videoMode && mISP->applyISPVideoLimitations(newParams, dvsEnabled)) {
        mPreviewForceChanged = true;
        previewFormatChanged = true;
    }

    msg.data.setParameters.previewFormatChanged = previewFormatChanged;
    msg.data.setParameters.videoMode = videoMode;

    return status;
}

/**
 * Update public parameter cache
 *
 * To implement a fast-path for GetParameters HAL call, update
 * a cached copy of parameters every time a modification is done.
 */
status_t ControlThread::updateParameterCache()
{
    status_t status = BAD_VALUE;

    mParamCacheLock.lock();

    // let app know if we support zoom in the preview mode indicated
    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;
    mISP->getZoomRatios(videoMode, &mParameters);
    mISP->getFocusDistances(&mParameters);

    String8 params = mParameters.flatten();
    int len = params.length();
    if (mParamCache)
        free(mParamCache);
    mParamCache = strndup(params.string(), sizeof(char) * len);
    status = NO_ERROR;

    mParamCacheLock.unlock();

    return status;
}

/**
 * Save the current context of camera parameters that describe:
 * - picture size
 * - thumbnail size
 * - supported picture sizes
 * - supported thumbnail sizes
 *
 * This is used when we start video recording because we need to impose restric
 * tions on these values to implement video snapshot feature
 * When recording is stopped a reciprocal call to restoreCurrentPictureParams
 * will be done
 */
void ControlThread::storeCurrentPictureParams()
{
    mStillPictContext.clear();

    mParameters.getPictureSize(&mStillPictContext.snapshotWidth,
                               &mStillPictContext.snapshotHeight);
    mStillPictContext.thumbnailWidth = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    mStillPictContext.thumbnailHeigth = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);

    const char* supportedSnapshotSizes = mParameters.get(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES);
    if (supportedSnapshotSizes) {
        mStillPictContext.supportedSnapshotSizes = supportedSnapshotSizes;
    } else {
        LOGE("Missing supported picture sizes");
        mStillPictContext.supportedSnapshotSizes = "";
    }

    const char* supportedThumbnailSizes = mParameters.get(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES);
    if (supportedThumbnailSizes) {
        mStillPictContext.suportedThumnailSizes = supportedThumbnailSizes;
    } else {
        LOGE("Missing supported thumbnail sizes");
        mStillPictContext.suportedThumnailSizes = "";
    }
}

/**
 * Restores from the member variable mStillPictContext the following camera
 * parameters:
 * - picture size
 * - thumbnail size
 * - supported picture sizes
 * - supported thumbnail sizes
 * This is used when video recording stops to restore the state before video
 * recording started and to lift the limitations of the current video snapshot
 */
void ControlThread::restoreCurrentPictureParams()
{
    mParameters.setPictureSize(mStillPictContext.snapshotWidth,
                               mStillPictContext.snapshotHeight);
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH,
                    mStillPictContext.thumbnailWidth);
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,
                    mStillPictContext.thumbnailHeigth);

    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                    mStillPictContext.supportedSnapshotSizes.string());
    mParameters.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
                    mStillPictContext.suportedThumnailSizes.string());

    mStillPictContext.clear();
    updateParameterCache();
    allocateSnapshotBuffers();
}

bool ControlThread::paramsHasPictureSizeChanged(const CameraParameters *oldParams,
                                                CameraParameters *newParams) const
{
    int newWidth, newHeight;
    int oldWidth, oldHeight;

    newParams->getPictureSize(&newWidth, &newHeight);
    oldParams->getPictureSize(&oldWidth, &oldHeight);

    if (newWidth != oldWidth || newHeight != oldHeight)
        return true;

    return false;
}

status_t ControlThread::handleMessageSetParameters(MessageSetParameters *msg)
{
    LOG1("@%s", __FUNCTION__);

    status_t status = NO_ERROR;
    CameraParameters newParams;
    CameraParameters oldParams = mParameters;

    bool needRestartPreview = msg->previewFormatChanged;
    bool videoMode = msg->videoMode;

    CameraAreas newFocusAreas;
    CameraAreas newMeteringAreas;

    mParamCacheLock.lock();
    // flush the rest of setParameters, if any - last parameters are in mParamCache
    mMessageQueue.remove(MESSAGE_ID_SET_PARAMETERS);
    // copy cached settings
    String8 str_params(mParamCache);
    newParams.unflatten(str_params);
    mParamCacheLock.unlock();

    CameraParamsLogger newParamLogger (str_params.string());
    CameraParamsLogger oldParamLogger (mParameters.flatten().string());

    // print all old and new params for comparison (debug)
    LOG1("----------BEGIN PARAM DIFFERENCE----------");
    newParamLogger.dumpDifference(oldParamLogger);
    LOG1("----------END PARAM DIFFERENCE----------");

    LOG2("----------- BEGIN OLD PARAMS -------- ");
    oldParamLogger.dump();
    LOG2("----------- END OLD PARAMS -------- ");

    LOG2("----------- BEGIN NEW PARAMS -------- ");
    newParamLogger.dump();
    LOG2("----------- END NEW PARAMS -------- ");

    LOG1("scanning AF focus areas");
    status = newFocusAreas.scan(newParams.get(CameraParameters::KEY_FOCUS_AREAS),
                                mAAA->getAfMaxNumWindows());
    if (status != NO_ERROR) {
        LOGE("bad focus area");
        goto exit;
    }
    LOG1("scanning AE metering areas");
    status = newMeteringAreas.scan(newParams.get(CameraParameters::KEY_METERING_AREAS),
                                   mAAA->getAeMaxNumWindows());
    if (status != NO_ERROR) {
        LOGE("bad metering area");
        goto exit;
    }

    if (paramsHasPictureSizeChanged(&oldParams, &newParams)) {
        LOG1("Picture size has changed while camera is active!");

        if (mState == STATE_CAPTURE) {
            status = stopCapture();
        }
        else if (mState == STATE_PREVIEW_STILL ||
                 mState == STATE_CONTINUOUS_CAPTURE) {

            // Preview needs to be restarted if the preview mode changes, or
            // with any picture size change when in continuous mode.
            if (selectPreviewMode(newParams) != mState ||
                mState == STATE_CONTINUOUS_CAPTURE) {
                needRestartPreview = true;
                videoMode = false;
                if (mState == STATE_CONTINUOUS_CAPTURE)
                  allocateSnapshotBuffers();
            }
        }
    }
    mParameters = newParams;
    mFocusAreas = newFocusAreas;
    mMeteringAreas = newMeteringAreas;

    ProcessOverlayEnable(&oldParams, &newParams);

    if (needRestartPreview == true) {
        // if preview is running and preview format has changed, then we need
        // to stop, reconfigure, and restart the isp and all threads.
        // Update the current params before we re-start
        switch (mState) {
            case STATE_PREVIEW_VIDEO:
            case STATE_PREVIEW_STILL:
            case STATE_CONTINUOUS_CAPTURE:
                status = restartPreview(videoMode);
                break;
            case STATE_STOPPED:
                break;
            default:
                LOGE("formats can only be changed while in preview or stop states");
                break;
        };
    }

    // if file injection is enabled, get file injection parameters and save
    // them in AtomISP
    if (mISP->isFileInjectionEnabled())
        processParamFileInject(&newParams);

    // Take care of parameters that can be set while ISP is running
    status = processDynamicParameters(&oldParams, &newParams);
    if (status != NO_ERROR)
        goto exit;

    mParameters = newParams;

exit:
    // return status
    return status;
}

status_t ControlThread::handleMessageGetParameters(MessageGetParameters *msg)
{
    status_t status = BAD_VALUE;

    if (msg->params) {
        // let app know if we support zoom in the preview mode indicated
        bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;
        mISP->getZoomRatios(videoMode, &mParameters);
        mISP->getFocusDistances(&mParameters);

        String8 params = mParameters.flatten();
        int len = params.length();
        *msg->params = strndup(params.string(), sizeof(char) * len);
        status = NO_ERROR;

    }
    mMessageQueue.reply(MESSAGE_ID_GET_PARAMETERS, status);
    return status;
}
status_t ControlThread::handleMessageCommand(MessageCommand* msg)
{
    status_t status = BAD_VALUE;
    switch (msg->cmd_id)
    {
    case CAMERA_CMD_START_FACE_DETECTION:
        status = startFaceDetection();
        break;
    case CAMERA_CMD_STOP_FACE_DETECTION:
        status = stopFaceDetection();
        break;
    case CAMERA_CMD_START_SCENE_DETECTION:
        status = startSmartSceneDetection();
        break;
    case CAMERA_CMD_STOP_SCENE_DETECTION:
        status = stopSmartSceneDetection();
        break;
    case CAMERA_CMD_START_SMILE_SHUTTER:
        status = startSmartShutter(SMILE_MODE);
        break;
    case CAMERA_CMD_START_BLINK_SHUTTER:
        status = startSmartShutter(BLINK_MODE);
        break;
    case CAMERA_CMD_STOP_SMILE_SHUTTER:
        status = stopSmartShutter(SMILE_MODE);
        break;
    case CAMERA_CMD_STOP_BLINK_SHUTTER:
        status = stopSmartShutter(BLINK_MODE);
        break;
    case CAMERA_CMD_CANCEL_TAKE_PICTURE:
        status = cancelCaptureOnTrigger();
        break;
    case CAMERA_CMD_ENABLE_INTEL_PARAMETERS:
        status = enableIntelParameters();
        mMessageQueue.reply(MESSAGE_ID_COMMAND, status);
        break;
    case CAMERA_CMD_START_PANORAMA:
        status = startPanorama();
        break;
    case CAMERA_CMD_STOP_PANORAMA:
        status = stopPanorama();
        break;
    case CAMERA_CMD_START_FACE_RECOGNITION:
        status = startFaceRecognition();
        break;
    case CAMERA_CMD_STOP_FACE_RECOGNITION:
        status = stopFaceRecognition();
        break;
    case CAMERA_CMD_ENABLE_FOCUS_MOVE_MSG:
        status = enableFocusMoveMsg(static_cast<bool>(msg->arg1));
    default:
        break;
    }

    if (status != NO_ERROR)
        LOGE("@%s command id %d failed", __FUNCTION__, msg->cmd_id);
    return status;
}

status_t ControlThread::handleMessageSceneDetected(MessageSceneDetected *msg)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    status = mCallbacksThread->sceneDetected(msg->sceneMode, msg->sceneHdr);
    return status;
}

/**
 * Start Smart scene detection. This should be called after preview is started.
 * The camera will notify Camera.SmartSceneDetectionListener when a new scene
 * is detected.
 */
status_t ControlThread::startSmartSceneDetection()
{
    LOG2("@%s", __FUNCTION__);
    if (mState == STATE_STOPPED || mAAA->getSmartSceneDetection()) {
        return INVALID_OPERATION;
    }
    enableMsgType(CAMERA_MSG_SCENE_DETECT);
    if (m3AThread != NULL)
        m3AThread->resetSmartSceneValues();
    mAAA->setSmartSceneDetection(true);
    return NO_ERROR;
}

status_t ControlThread::stopSmartSceneDetection()
{
    LOG2("@%s", __FUNCTION__);
    if (mState == STATE_STOPPED || !mAAA->getSmartSceneDetection()) {
        return INVALID_OPERATION;
    }
    disableMsgType(CAMERA_MSG_SCENE_DETECT);
    mAAA->setSmartSceneDetection(false);
    return NO_ERROR;
}

status_t ControlThread::handleMessageStoreMetaDataInBuffers(MessageStoreMetaDataInBuffers *msg)
{
    LOG1("@%s. state = %d", __FUNCTION__, mState);
    status_t status = NO_ERROR;
    //Prohibit to enable metadata mode if state of HAL isn't equal stopped or in preview
    if (mState != STATE_STOPPED &&
            mState != STATE_PREVIEW_VIDEO &&
            mState != STATE_PREVIEW_STILL &&
            mState != STATE_CONTINUOUS_CAPTURE) {
        LOGE("Cannot configure metadata buffers in this state: %d", mState);
        status = BAD_VALUE;
        mMessageQueue.reply(MESSAGE_ID_STORE_METADATA_IN_BUFFER, status);
        return status;
    }

    mStoreMetaDataInBuffers = msg->enabled;
    status = mISP->storeMetaDataInBuffers(msg->enabled);
    if(status == NO_ERROR)
        status = mCallbacks->storeMetaDataInBuffers(msg->enabled);
    else
        LOGE("Error configuring metadatabuffers in ISP!");

    mMessageQueue.reply(MESSAGE_ID_STORE_METADATA_IN_BUFFER, status);
    return status;
}

status_t ControlThread::hdrInit(int size, int pvSize, int format,
                                int width, int height,
                                int pvWidth, int pvHeight)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // Initialize the HDR output buffers
    // Main output buffer
    mCallbacks->allocateMemory(&mHdr.outMainBuf, size);
    if (mHdr.outMainBuf.buff == NULL) {
        LOGE("HDR: Error allocating memory for HDR main buffer!");
        return NO_MEMORY;
    }
    mHdr.outMainBuf.shared = false;
    // merging multiple images from ISP, so just set counter to 1
    mHdr.outMainBuf.frameCounter = 1;
    mHdr.outMainBuf.type = ATOM_BUFFER_SNAPSHOT;

    LOG1("HDR: using %p as HDR main output buffer", mHdr.outMainBuf.buff->data);
    // Postview output buffer
    mCallbacks->allocateMemory(&mHdr.outPostviewBuf, pvSize);
    if (mHdr.outPostviewBuf.buff == NULL) {
        LOGE("HDR: Error allocating memory for HDR postview buffer!");
        return NO_MEMORY;
    }
    mHdr.outPostviewBuf.shared = false;
    mHdr.outPostviewBuf.type = ATOM_BUFFER_POSTVIEW;

    LOG1("HDR: using %p as HDR postview output buffer", mHdr.outPostviewBuf.buff->data);

    // Initialize the CI input buffers (will be initialized later, when snapshots are taken)
    mHdr.ciBufIn.ciBufNum = mHdr.bracketNum;
    mHdr.ciBufIn.ciMainBuf = new ia_frame[mHdr.ciBufIn.ciBufNum];
    mHdr.ciBufIn.ciPostviewBuf = new ia_frame[mHdr.ciBufIn.ciBufNum];
    mHdr.ciBufIn.hist = new ia_cp_histogram[mHdr.ciBufIn.ciBufNum];

    // Initialize the CI output buffers
    mHdr.ciBufOut.ciBufNum = mHdr.bracketNum;
    mHdr.ciBufOut.ciMainBuf = new ia_frame[1];
    mHdr.ciBufOut.ciPostviewBuf = new ia_frame[1];
    mHdr.ciBufOut.hist = NULL;

    if (mHdr.ciBufIn.ciMainBuf == NULL ||
        mHdr.ciBufIn.ciPostviewBuf == NULL ||
        mHdr.ciBufIn.hist == NULL ||
        mHdr.ciBufOut.ciMainBuf == NULL ||
        mHdr.ciBufOut.ciPostviewBuf == NULL) {
        LOGE("HDR: Error allocating memory for HDR CI buffers!");
        return NO_MEMORY;
    }

    status = AtomCP::setIaFrameFormat(&mHdr.ciBufOut.ciMainBuf[0], format);
    if (status != NO_ERROR) {
        LOGE("HDR: pixel format %d not supported", format);
        return status;
    }

    mHdr.ciBufOut.ciMainBuf->data = mHdr.outMainBuf.buff->data;
    mHdr.ciBufOut.ciMainBuf[0].width = mHdr.outMainBuf.width = width;
    mHdr.ciBufOut.ciMainBuf[0].stride = mHdr.outMainBuf.stride = width;
    mHdr.ciBufOut.ciMainBuf[0].height = mHdr.outMainBuf.height = height;
    mHdr.outMainBuf.format = format;
    mHdr.ciBufOut.ciMainBuf[0].size = mHdr.outMainBuf.size = size;

    LOG1("HDR: Initialized output CI main     buff @%p: (data=%p, size=%d, width=%d, height=%d, format=%d)",
            &mHdr.ciBufOut.ciMainBuf[0],
            mHdr.ciBufOut.ciMainBuf[0].data,
            mHdr.ciBufOut.ciMainBuf[0].size,
            mHdr.ciBufOut.ciMainBuf[0].width,
            mHdr.ciBufOut.ciMainBuf[0].height,
            mHdr.ciBufOut.ciMainBuf[0].format);

    mHdr.ciBufOut.ciPostviewBuf[0].data = mHdr.outPostviewBuf.buff->data;
    mHdr.ciBufOut.ciPostviewBuf[0].width = mHdr.outPostviewBuf.width = pvWidth;
    mHdr.ciBufOut.ciPostviewBuf[0].stride = mHdr.outPostviewBuf.stride = pvWidth;
    mHdr.ciBufOut.ciPostviewBuf[0].height = mHdr.outPostviewBuf.height = pvHeight;
    AtomCP::setIaFrameFormat(&mHdr.ciBufOut.ciPostviewBuf[0], format);
    mHdr.outPostviewBuf.format = format;
    mHdr.ciBufOut.ciPostviewBuf[0].size = mHdr.outPostviewBuf.size = pvSize;

    LOG1("HDR: Initialized output CI postview buff @%p: (data=%p, size=%d, width=%d, height=%d, format=%d)",
            &mHdr.ciBufOut.ciPostviewBuf[0],
            mHdr.ciBufOut.ciPostviewBuf[0].data,
            mHdr.ciBufOut.ciPostviewBuf[0].size,
            mHdr.ciBufOut.ciPostviewBuf[0].width,
            mHdr.ciBufOut.ciPostviewBuf[0].height,
            mHdr.ciBufOut.ciPostviewBuf[0].format);

    return status;
}

status_t ControlThread::hdrProcess(AtomBuffer * snapshotBuffer, AtomBuffer* postviewBuffer)
{
    LOG1("@%s", __FUNCTION__);

    // Initialize the HDR CI input buffers (main/postview) for this capture
    if (snapshotBuffer->shared) {
        mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].data = (void *) *((char **)snapshotBuffer->buff->data);
    } else {
        mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].data = snapshotBuffer->buff->data;
    }

    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].width = snapshotBuffer->width;
    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].stride = snapshotBuffer->width;
    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].height = snapshotBuffer->height;
    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].size = snapshotBuffer->size;
    AtomCP::setIaFrameFormat(&mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum], snapshotBuffer->format);

    LOG1("HDR: Initialized input CI main     buff %d @%p: (addr=%p, length=%d, width=%d, height=%d, format=%d)",
            mBurstCaptureNum,
            &mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum],
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].data,
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].size,
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].width,
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].height,
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].format);

    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].data = postviewBuffer->buff->data;  /* postview buffers are never shared (i.e. coming from the PictureThread) */
    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].width = postviewBuffer->width;
    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].height = postviewBuffer->height;
    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].size = postviewBuffer->size;
    AtomCP::setIaFrameFormat(&mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum], postviewBuffer->format);

    LOG1("HDR: Initialized input CI postview buff %d @%p: (addr=%p, length=%d, width=%d, height=%d, format=%d)",
            mBurstCaptureNum,
            &mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum],
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].data,
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].size,
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].width,
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].height,
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].format);

    return mCP->computeCDF(mHdr.ciBufIn, mBurstCaptureNum);
}

void ControlThread::hdrRelease()
{
    // Deallocate memory
    if (mHdr.outMainBuf.buff != NULL) {
        mHdr.outMainBuf.buff->release(mHdr.outMainBuf.buff);
    }
    if (mHdr.outPostviewBuf.buff != NULL) {
        mHdr.outPostviewBuf.buff->release(mHdr.outPostviewBuf.buff);
    }
    if (mHdr.ciBufIn.ciMainBuf != NULL) {
        delete[] mHdr.ciBufIn.ciMainBuf;
    }
    if (mHdr.ciBufIn.ciPostviewBuf != NULL) {
        delete[] mHdr.ciBufIn.ciPostviewBuf;
    }
    if (mHdr.ciBufIn.hist != NULL) {
        delete[] mHdr.ciBufIn.hist;
    }
    if (mHdr.ciBufOut.ciMainBuf != NULL) {
        delete[] mHdr.ciBufOut.ciMainBuf;
    }
    if (mHdr.ciBufOut.ciPostviewBuf != NULL) {
        delete[] mHdr.ciBufOut.ciPostviewBuf;
    }
}

status_t ControlThread::hdrCompose()
{
    LOG1("%s",__FUNCTION__);
    status_t status = NO_ERROR;

    // initialize the meta data with last picture of
    // the HDR sequence
    PictureThread::MetaData hdrPicMetaData;
    fillPicMetaData(hdrPicMetaData, false);

    /*
     * Stop ISP before composing HDR since standalone acceleration requires ISP to be stopped.
     * The below call won't release the capture buffers since they are needed by HDR compose
     * method. The capture buffers will be released in stopCapture method.
     */
    status = mISP->stop();
    if (status != NO_ERROR) {
        hdrPicMetaData.free();
        LOGE("Error stopping ISP!");
        return status;
    }

    status = mCP->initializeHDR(mHdr.ciBufOut.ciMainBuf->width,
                                mHdr.ciBufOut.ciMainBuf->height);
    if (status != NO_ERROR) {
        hdrPicMetaData.free();
        LOGE("HDR buffer allocation failed");
        return UNKNOWN_ERROR;
    }

    bool doEncode = false;
    status = mCP->composeHDR(mHdr.ciBufIn, mHdr.ciBufOut, mHdr.vividness, mHdr.sharpening);
    if (status == NO_ERROR) {
        mHdr.outMainBuf.width = mHdr.ciBufOut.ciMainBuf->width;
        mHdr.outMainBuf.height = mHdr.ciBufOut.ciMainBuf->height;
        mHdr.outMainBuf.size = mHdr.ciBufOut.ciMainBuf->size;
        if (hdrPicMetaData.aeConfig) {
            hdrPicMetaData.aeConfig->evBias = 0.0;
        }
        status = mPictureThread->encode(hdrPicMetaData, &mHdr.outMainBuf, &mHdr.outPostviewBuf);
        if (status == NO_ERROR) {
            doEncode = true;
        }
    } else {
        LOGE("HDR Composition failed !");
    }

    if (doEncode == false)
        hdrPicMetaData.free();

    mCP->uninitializeHDR();

    return status;
}

/*
 * Helper methods used during the takePicture sequence
 * If possible it retrieves the  buffers allocated by the HW JPEG encoder
 * and passes them to the ISP to be used
 * If the operation fails we default to internally (by AtomISP) allocated buffers
 * Use buffers sharing only if the pixel format is NV12
 * @param[in] format V4L2 color space format of the frame
 * @param[in] width width in pixels
 * @param[in] height height in lines
 */
void ControlThread::setExternalSnapshotBuffers(int format, int width, int height)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (format == V4L2_PIX_FMT_NV12) {
        // Try to use buffer sharing
        char* snapshotBufferPtr;
        int numberOfSnapshots;
        status = mPictureThread->getSharedBuffers(width, height, &snapshotBufferPtr, &numberOfSnapshots);
        if (status == NO_ERROR) {
            status = mISP->setSnapshotBuffers((void*)snapshotBufferPtr, numberOfSnapshots);
            if (status == NO_ERROR) {
                LOG1("Using shared buffers for snapshot");
            } else {
                LOGW("Cannot set shared buffers in atomisp, using internal buffers!");
            }
        } else {
            LOGW("Cannot get shared buffers from libjpeg, using internal buffers!");
        }
    } else {
        LOG1("Using internal buffers for snapshot");
    }
}

/**
 * From Android API:
 * Starts the face detection. This should be called after preview is started.
 * The camera will notify Camera.FaceDetectionListener
 *  of the detected faces in the preview frame. The detected faces may be the same as
 *  the previous ones.
 *
 *  Applications should call stopFaceDetection() to stop the face detection.
 *
 *  This method is supported if getMaxNumDetectedFaces() returns a number larger than 0.
 *  If the face detection has started, apps should not call this again.
 *  When the face detection is running, setWhiteBalance(String), setFocusAreas(List),
 *  and setMeteringAreas(List) have no effect.
 *  The camera uses the detected faces to do auto-white balance, auto exposure, and autofocus.
 *
 *  If the apps call autoFocus(AutoFocusCallback), the camera will stop sending face callbacks.
 *
 *  The last face callback indicates the areas used to do autofocus.
 *  After focus completes, face detection will resume sending face callbacks.
 *
 *  If the apps call cancelAutoFocus(), the face callbacks will also resume.
 *
 *  After calling takePicture(Camera.ShutterCallback, Camera.PictureCallback, Camera.PictureCallback)
 *  or stopPreview(), and then resuming preview with startPreview(),
 *  the apps should call this method again to resume face detection.
 *
 */
status_t ControlThread::startFaceDetection()
{
    LOG2("@%s", __FUNCTION__);
    if (mState == STATE_STOPPED || mFaceDetectionActive) {
        LOGE("starting FD in stop state");
        return INVALID_OPERATION;
    }
    if (mPostProcThread != 0) {
        mPostProcThread->startFaceDetection();
        mFaceDetectionActive = true;
        enableMsgType(CAMERA_MSG_PREVIEW_METADATA);
        return NO_ERROR;
    } else{
        return INVALID_OPERATION;
    }
}

status_t ControlThread::stopFaceDetection(bool wait)
{
    LOG2("@%s", __FUNCTION__);
    if(!mFaceDetectionActive) {
        return NO_ERROR;
    }

    mFaceDetectionActive = false;
    disableMsgType(CAMERA_MSG_PREVIEW_METADATA);
    if (mPostProcThread != 0) {
        mPostProcThread->stopFaceDetection(wait);
        return NO_ERROR;
    } else {
        return INVALID_OPERATION;
    }
}

status_t ControlThread::startSmartShutter(SmartShutterMode mode)
{
    LOG1("@%s", __FUNCTION__);
    if (mState == STATE_STOPPED)
        return INVALID_OPERATION;

    int level = 0;

    if (mode == SMILE_MODE && !mPostProcThread->isSmileRunning()) {
        level = mParameters.getInt(IntelCameraParameters::KEY_SMILE_SHUTTER_THRESHOLD);
    } else if (mode == BLINK_MODE && !mPostProcThread->isBlinkRunning()) {
        level = mParameters.getInt(IntelCameraParameters::KEY_BLINK_SHUTTER_THRESHOLD);
    } else {
        return INVALID_OPERATION;
    }

    mPostProcThread->startSmartShutter(mode, level);
    LOG1("%s: mode: %d Active Mode: (smile %d (%d) , blink %d (%d), smart %d)",
         __FUNCTION__, mode,
         mPostProcThread->isSmileRunning(), mPostProcThread->getSmileThreshold(),
         mPostProcThread->isBlinkRunning(), mPostProcThread->getBlinkThreshold(),
         mPostProcThread->isSmartRunning());

    return NO_ERROR;
}

status_t ControlThread::stopSmartShutter(SmartShutterMode mode)
{
    LOG1("@%s", __FUNCTION__);

    mPostProcThread->stopSmartShutter(mode);
    LOG1("%s: mode: %d Active Mode: (smile %d (%d) , blink %d (%d), smart %d)",
         __FUNCTION__, mode,
         mPostProcThread->isSmileRunning(), mPostProcThread->getSmileThreshold(),
         mPostProcThread->isBlinkRunning(), mPostProcThread->getBlinkThreshold(),
         mPostProcThread->isSmartRunning());

    return NO_ERROR;
}

status_t ControlThread::startFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    if (mPostProcThread->isFaceRecognitionRunning()) {
        LOGE("@%s: face recognition already started", __FUNCTION__);
        return INVALID_OPERATION;
    }
    mPostProcThread->startFaceRecognition();
    return NO_ERROR;
}

status_t ControlThread::stopFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    if (mPostProcThread->isFaceRecognitionRunning()) {
        mPostProcThread->stopFaceRecognition();
    }
    return NO_ERROR;
}

status_t ControlThread::enableFocusMoveMsg(bool enable)
{
    LOG1("@%s", __FUNCTION__);
    if (enable) {
        enableMsgType(CAMERA_MSG_FOCUS_MOVE);
    } else {
        disableMsgType(CAMERA_MSG_FOCUS_MOVE);
    }

    return NO_ERROR;
}

status_t ControlThread::enableIntelParameters()
{
    // intel parameters support more effects
    // so use supported effects list stored in mIntelParameters.
    if (mIntelParameters.get(CameraParameters::KEY_SUPPORTED_EFFECTS))
        mParameters.remove(CameraParameters::KEY_SUPPORTED_EFFECTS);

    String8 params(mParameters.flatten());
    String8 intel_params(mIntelParameters.flatten());
    String8 delimiter(";");
    params += delimiter;
    params += intel_params;
    mParameters.unflatten(params);
    updateParameterCache();

    mIntelParamsAllowed = true;
    return NO_ERROR;
}

status_t ControlThread::cancelCaptureOnTrigger()
{
    LOG1("@%s", __FUNCTION__);
    if( !mPostProcThread->isSmartRunning())
        return NO_ERROR;
    if(mPostProcThread != 0)
        mPostProcThread->stopCaptureOnTrigger();
    return NO_ERROR;
}

status_t ControlThread::startPanorama()
{
    LOG2("@%s", __FUNCTION__);
    if (mPanoramaThread->getState() != PANORAMA_STOPPED) {
        return INVALID_OPERATION;
    }
    if (mPanoramaThread != 0) {
        mPanoramaThread->startPanorama();

        // in continuous capture mode, check if postview size matches live preview size.
        // if not, restart preview so that pv size gets set to lpv size
        if (mState == STATE_CONTINUOUS_CAPTURE) {
            int lpwWidth, lpwHeight, pvWidth, pvHeight, pvFormat;
            IntelCameraParameters::getPanoramaLivePreviewSize(lpwWidth, lpwHeight, mParameters);
            mISP->getPostviewFrameFormat(pvWidth, pvHeight, pvFormat);
            if (lpwWidth != pvWidth || lpwHeight != pvHeight)
                restartPreview(false);
        }

        return NO_ERROR;
    } else {
        return INVALID_OPERATION;
    }
}

status_t ControlThread::stopPanorama()
{
    LOG2("@%s", __FUNCTION__);
    if (mPanoramaThread->getState() == PANORAMA_STOPPED)
        return NO_ERROR;
    if (mPanoramaThread != 0) {
        mPanoramaThread->stopPanorama();
        return NO_ERROR;
    } else{
        return INVALID_OPERATION;
    }
}

status_t ControlThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;

        case MESSAGE_ID_START_PREVIEW:
            status = handleMessageStartPreview();
            break;

        case MESSAGE_ID_STOP_PREVIEW:
            status = handleMessageStopPreview();
            break;

        case MESSAGE_ID_START_RECORDING:
            status = handleMessageStartRecording();
            break;

        case MESSAGE_ID_STOP_RECORDING:
            status = handleMessageStopRecording();
            break;

        case MESSAGE_ID_RELEASE_PREVIEW_FRAME:
            status = handleMessageReleasePreviewFrame(
                &msg.data.releasePreviewFrame);
            break;

        case MESSAGE_ID_PANORAMA_PICTURE:
            status = handleMessagePanoramaPicture();
            break;

        case MESSAGE_ID_TAKE_PICTURE:
            status = handleMessageTakePicture();
            break;

        case MESSAGE_ID_SMART_SHUTTER_PICTURE:
                status = handleMessageTakeSmartShutterPicture();
            break;

        case MESSAGE_ID_CANCEL_PICTURE:
            status = handleMessageCancelPicture();
            break;

        case MESSAGE_ID_AUTO_FOCUS:
            status = handleMessageAutoFocus();
            break;

        case MESSAGE_ID_CANCEL_AUTO_FOCUS:
            status = handleMessageCancelAutoFocus();
            break;

        case MESSAGE_ID_RELEASE_RECORDING_FRAME:
            status = handleMessageReleaseRecordingFrame(&msg.data.releaseRecordingFrame);
            break;

        case MESSAGE_ID_PREVIEW_DONE:
            status = handleMessagePreviewDone(&msg.data.previewDone);
            break;

        case MESSAGE_ID_PICTURE_DONE:
            status = handleMessagePictureDone(&msg.data.pictureDone);
            break;

        case MESSAGE_ID_AUTO_FOCUS_DONE:
            status = handleMessageAutoFocusDone();
            break;

        case MESSAGE_ID_SET_PARAMETERS:
            status = handleMessageSetParameters(&msg.data.setParameters);
            break;

        case MESSAGE_ID_GET_PARAMETERS:
            status = handleMessageGetParameters(&msg.data.getParameters);
            break;

        case MESSAGE_ID_COMMAND:
            status = handleMessageCommand(&msg.data.command);
            break;

        case MESSAGE_ID_SET_PREVIEW_WINDOW:
            status = handleMessageSetPreviewWindow(&msg.data.previewWin);
            break;

        case MESSAGE_ID_STORE_METADATA_IN_BUFFER:
            status = handleMessageStoreMetaDataInBuffers(&msg.data.storeMetaDataInBuffers);
            break;

        case MESSAGE_ID_SCENE_DETECTED:
            status = handleMessageSceneDetected(&msg.data.sceneDetected);
            break;

        case MESSAGE_ID_PANORAMA_CAPTURE_TRIGGER:
            status = handleMessagePanoramaCaptureTrigger();
            break;

        case MESSAGE_ID_POST_PROC_CAPTURE_TRIGGER:
            status = handleMessageTakePicture();
            // in Smart Shutter with HDR, we need to reset the flag in case no save original
            // to have a clean flag for new capture sequence.
            if (!mHdr.enabled || !mHdr.saveOrig)
                mPostProcThread->resetSmartCaptureTrigger();
            break;

        case MESSAGE_ID_PANORAMA_FINALIZE:
             status = handleMessagePanoramaFinalize(&msg.data.panoramaFinalized);
             break;

        default:
            LOGE("Invalid message");
            status = BAD_VALUE;
            break;
    };

    if (status != NO_ERROR)
        LOGE("Error handling message: %d", (int) msg.id);
    return status;
}

AtomBuffer* ControlThread::findRecordingBuffer(void *findMe)
{
    // This is a small list, so incremental search is not an issue right now
    if (mCoupledBuffers) {
        if(mStoreMetaDataInBuffers) {
            for (int i = 0; i < mNumBuffers; i++) {
                if (mCoupledBuffers[i].recordingBuff.metadata_buff &&
                     mCoupledBuffers[i].recordingBuff.metadata_buff->data == findMe)
                    return &mCoupledBuffers[i].recordingBuff;
            }
        } else {
            for (int i = 0; i < mNumBuffers; i++) {
                if (mCoupledBuffers[i].recordingBuff.buff &&
                    mCoupledBuffers[i].recordingBuff.buff->data == findMe)
                    return &mCoupledBuffers[i].recordingBuff;
           }
        }
    }
    return NULL;
}

status_t ControlThread::dequeuePreview()
{
    LOG2("@%s", __FUNCTION__);
    AtomBuffer buff;
    status_t status = NO_ERROR;
    atomisp_frame_status frameStatus;

    status = mISP->getPreviewFrame(&buff, &frameStatus);

    if (status == NO_ERROR) {
        if (mPreviewThread->getPreviewState() != PreviewThread::STATE_ENABLED) {
            if (frameStatus != ATOMISP_FRAME_STATUS_CORRUPTED
                && mAAA->is3ASupported())
                m3AThread->newFrame(buff.capture_timestamp);
            mISP->putPreviewFrame(&buff);
            return status;
        }

        bool videoMode =
            (mState == STATE_PREVIEW_VIDEO || mState == STATE_RECORDING);

        bool skipFrame = checkSkipFrame(buff.frameCounter);

        if (videoMode) {
            mCoupledBuffers[buff.id].previewBuff = buff;
            mCoupledBuffers[buff.id].previewBuffReturned = skipFrame;
            if (skipFrame) {
                LOG2("Dropping preview video frame, frame num=%d", buff.frameCounter);
                queueCoupledBuffers(buff.id);
                return status;
            }
        }

        if (frameStatus != ATOMISP_FRAME_STATUS_CORRUPTED) {
            if (mVideoSnapshotrequested && videoMode) {
                mVideoSnapshotrequested--;
                encodeVideoSnapshot(buff.id);
            }
            if (mAAA->is3ASupported()) {
                status = m3AThread->newFrame(buff.capture_timestamp);
                if (status != NO_ERROR)
                    LOGW("Error notifying new frame to 3A thread!");
            }

            if (skipFrame) {
                LOG2("Dropping preview frame, frame num=%d", buff.frameCounter);
                mISP->putPreviewFrame(&buff);
                return status;
            }

            PerformanceTraces::FaceLock::getCurFrameNum(buff.frameCounter);

            status = mPreviewThread->preview(&buff);
            if (status != NO_ERROR) {
                LOGE("Error sending buffer to preview thread");
                mISP->putPreviewFrame(&buff);
            }
        }
        else {
            LOGW("Preview frame %d corrupted, ignoring", buff.id);
            if (videoMode) {
                mCoupledBuffers[buff.id].previewBuffReturned = true;
                queueCoupledBuffers(buff.id);
            } else {
                // If not in video mode (mCoupledBuffers not used), we can put
                // the frame back immediately to ISP.
                mISP->putPreviewFrame(&buff);
            }
        }
    } else {
        LOGE("Error getting preview frame from ISP");
    }

    return status;
}

status_t ControlThread::dequeueRecording()
{
    LOG2("@%s", __FUNCTION__);
    AtomBuffer buff;
    nsecs_t timestamp;
    status_t status = NO_ERROR;
    atomisp_frame_status frameStatus;

    status = mISP->getRecordingFrame(&buff, &timestamp, &frameStatus);
    if (status == NO_ERROR) {
        mCoupledBuffers[buff.id].recordingBuff = buff;
        if (frameStatus != ATOMISP_FRAME_STATUS_CORRUPTED) {
            mCoupledBuffers[buff.id].recordingBuffReturned = false;
            // See if recording has started.
            // If it has, process the buffer, unless frame is to be dropped.
            // If recording hasn't started or frame is dropped, return the buffer to the driver
            if (mState == STATE_RECORDING && !checkSkipFrame(buff.frameCounter)) {
                mVideoThread->video(&buff, timestamp);
            } else {
                mCoupledBuffers[buff.id].recordingBuffReturned = true;
            }
        } else {
            LOGW("Recording frame %d corrupted, ignoring", buff.id);
            mCoupledBuffers[buff.id].recordingBuffReturned = true;
        }
    } else {
        LOGE("Error: getting recording from isp\n");
    }

    return status;
}


/**
 * This function implements the frame skip algorithm.
 * - If user requests 15fps, drop every even frame
 * - If user requests 10fps, drop two frames every three frames
 * @returns true: skip,  false: not skip
 */
// TODO: The above only applies to 30fps. Generalize this to support other sensor FPS as well.
bool ControlThread::checkSkipFrame(int frameNum)
{
    if (mSetFPS == 15 && (frameNum % 2 == 0)) {
        LOG2("Preview FPS: %d. Skipping frame num: %d", mSetFPS, frameNum);
        return true;
    }

    if (mSetFPS == 10 && (frameNum % 3 != 0)) {
        LOG2("Preview FPS: %d. Skipping frame num: %d", mSetFPS, frameNum);
        return true;
    }

    return false;
}

bool ControlThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning) {

        switch (mState) {

        case STATE_STOPPED:
            LOG2("In STATE_STOPPED");
            // in these states all we do is wait for messages
            status = waitForAndExecuteMessage();
            break;

        case STATE_CAPTURE:
            LOG2("In STATE_CAPTURE...");
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                // make sure ISP has data before we ask for some
                if (mISP->dataAvailable() &&
                    (mBurstLength > 1 && mBurstCaptureNum < mBurstLength)) {
                    status = captureBurstPic();
                } else {
                    status = waitForAndExecuteMessage();
                }
            }
            break;

        case STATE_PREVIEW_STILL:
            LOG2("In STATE_PREVIEW_STILL...");
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                if (mISP->dataAvailable())
                    status = dequeuePreview();
                else
                    status = waitForAndExecuteMessage();
            }
            break;

        case STATE_PREVIEW_VIDEO:
        case STATE_RECORDING:
            LOG2("In %s...", mState == STATE_PREVIEW_VIDEO ? "STATE_PREVIEW_VIDEO" : "STATE_RECORDING");
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                // make sure ISP has data before we ask for some
                if (mISP->dataAvailable()) {
                    status = dequeueRecording();
                    if (status == NO_ERROR)
                        status = dequeuePreview();
                } else {
                    status = waitForAndExecuteMessage();
                }
            }
            break;

        case STATE_CONTINUOUS_CAPTURE:
            LOG2("In STATE_CONTINUOUS_CAPTURE...");
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                // make sure ISP has data before we ask for some
                if (burstMoreCapturesNeeded())
                    status = captureFixedBurstPic();
                else if (mISP->dataAvailable())
                    status = dequeuePreview();
                else
                    status = waitForAndExecuteMessage();
            }
            break;

        default:
            break;
        };
    }

    return false;
}

status_t ControlThread::requestExitAndWait()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_EXIT;

    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

} // namespace android
