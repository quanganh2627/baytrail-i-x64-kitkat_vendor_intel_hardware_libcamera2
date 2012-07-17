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
#include "PreviewThread.h"
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
#include "cameralibs/intel_camera_extensions.h"

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
 * MAX_JPEG_BUFFERS: the maximum numbers of queued JPEG buffers
 */
#define MAX_JPEG_BUFFERS 4

/*
 * ASPECT_TOLERANCE: the tolerance between aspect ratios to consider them the same
 */
#define ASPECT_TOLERANCE 0.001

/*
 * DEFAULT_HDR_BRACKETING: the number of bracketed captures to be made in order to compose
 * a HDR image.
 */
#define DEFAULT_HDR_BRACKETING 3

ControlThread::ControlThread() :
    Thread(true) // callbacks may call into java
    ,mISP(NULL)
    ,mAAA(NULL)
    ,mPreviewThread(NULL)
    ,mPictureThread(NULL)
    ,mVideoThread(NULL)
    ,m3AThread(NULL)
    ,mProxyToOlaService(NULL)
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
    ,mBurstSkipFrames(0)
    ,mBurstLength(0)
    ,mBurstCaptureNum(0)
    ,mPublicAeMode(CAM_AE_MODE_AUTO)
    ,mPublicAfMode(CAM_AF_MODE_AUTO)
    ,mParamCache(NULL)
    ,mLastRecordingBuffIndex(0)
    ,mStoreMetaDataInBuffers(false)
{
    // DO NOT PUT ANY ALLOCATION CODE IN THIS METHOD!!!
    // Put all init code in the init() method.
    // This is a workaround for an issue with Thread reference counting.

    // Set the log level from property
    char gLogLevelProp[PROPERTY_VALUE_MAX];
    if (property_get("camera.hal.debug", gLogLevelProp, NULL)) {
        gLogLevel = atoi(gLogLevelProp);
        // Check that the property value is a valid integer
        if (gLogLevel > MAX_LOG_LEVEL || gLogLevel < MIN_LOG_LEVEL) {
            LOGE("Invalid camera.hal.debug property integer value: %s",gLogLevelProp);
            gLogLevel = 0;
        }
    }

    LOG1("@%s", __FUNCTION__);
}

ControlThread::~ControlThread()
{
    // DO NOT PUT ANY CODE IN THIS METHOD!!!
    // Put all deinit code in the deinit() method.
    // This is a workaround for an issue with Thread reference counting.
    LOG1("@%s", __FUNCTION__);
}

status_t ControlThread::init(int cameraId)
{
    LOG1("@%s: cameraId = %d", __FUNCTION__, cameraId);

    status_t status = UNKNOWN_ERROR;

    mISP = new AtomISP(cameraId);
    if (mISP == NULL) {
        LOGE("error creating ISP");
        goto bail;
    }

    mNumBuffers = mISP->getNumBuffers();

    mAAA = AtomAAA::getInstance();
    if (mAAA == NULL) {
        LOGE("error creating AAA");
        goto bail;
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
    m3AThread = new AAAThread(this);
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

    mPostProcThread = new PostProcThread(this);
    if (mPostProcThread == NULL) {
        LOGE("error creating PostProcThread");
        goto bail;
    }

    mProxyToOlaService = new HalProxyOla(this);
    if(mProxyToOlaService == NULL) {
        LOGE("error creating Proxy for OLA Buffer Service");
        goto bail;
    }
    mPreviewThread->setServiceProxy(mProxyToOlaService.get());

    // get default params from AtomISP and JPEG encoder
    mISP->getDefaultParameters(&mParameters, &mIntelParameters);
    mPictureThread->getDefaultParameters(&mParameters);
    mPreviewThread->getDefaultParameters(&mParameters);
    mPostProcThread->getDefaultParameters(&mParameters);
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

    // Disable bracketing by default
    mBracketing.mode = BRACKET_NONE;

    // Disable HDR by default
    mHdr.enabled = false;

    // Set property to inform system what camera is in use
    char facing[PROPERTY_VALUE_MAX];
    snprintf(facing, PROPERTY_VALUE_MAX, "%d", cameraId);
    property_set("media.camera.facing", facing);

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

    if (mPostProcThread != NULL) {
        mPostProcThread->requestExitAndWait();
        mPostProcThread.clear();
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

    if (mProxyToOlaService != NULL) {
        mProxyToOlaService.clear();
    }

    if (mParamCache != NULL)
        free(mParamCache);

    if (mISP != NULL) {
        delete mISP;
    }
    if (mAAA != NULL) {
        delete mAAA;
    }
    if (mCallbacks != NULL) {
        delete mCallbacks;
    }
}

status_t ControlThread::setPreviewWindow(struct preview_stream_ops *window)
{
    LOG1("@%s: window = %p", __FUNCTION__, window);

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
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_START_PREVIEW;
    return mMessageQueue.send(&msg, MESSAGE_ID_START_PREVIEW);
}

status_t ControlThread::stopPreview()
{
    LOG1("@%s", __FUNCTION__);
    // send message and block until thread processes message
    if (mState == STATE_STOPPED) {
        return NO_ERROR;
    }

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
    bool enabled = (mState == STATE_PREVIEW_STILL ||
            mState == STATE_PREVIEW_VIDEO ||
            mState == STATE_RECORDING);
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
    Message msg;
    msg.id = MESSAGE_ID_SET_PARAMETERS;
    msg.data.setParameters.params = const_cast<char*>(params); // We swear we won't modify params :)
    return mMessageQueue.send(&msg, MESSAGE_ID_SET_PARAMETERS);
}

char* ControlThread::getParameters()
{
    LOG1("@%s", __FUNCTION__);

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
    LOG1("@%s: params = %p", __FUNCTION__, params);
    if (params)
        free(params);
}

bool ControlThread::isParameterSet(const char* param)
{
    const char* strParam = mParameters.get(param);
    int len = strlen(CameraParameters::TRUE);
    if (strParam != NULL && strncmp(strParam, CameraParameters::TRUE, len) == 0) {
        return true;
    }
    return false;
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

    if (oldVal != newVal)
        return newVal;

    return String8::empty();
}

status_t ControlThread::takePicture()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_TAKE_PICTURE;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::cancelPicture()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_PICTURE;
    return mMessageQueue.send(&msg);
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
            snapshotBuf->buff->data,
            postviewBuf->buff->data,
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

status_t ControlThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return NO_ERROR;
}

status_t ControlThread::startPreviewCore(bool videoMode)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int width;
    int height;
    int format;
    State state;
    AtomMode mode;

    if (mState != STATE_STOPPED) {
        LOGE("Must be in STATE_STOPPED to start preview");
        return INVALID_OPERATION;
    }

    if (videoMode) {
        LOG1("Starting preview in video mode");
        state = STATE_PREVIEW_VIDEO;
        mode = MODE_VIDEO;
    } else {
        LOG1("Starting preview in still mode");
        state = STATE_PREVIEW_STILL;
        mode = MODE_PREVIEW;
    }

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
        if(isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION))
            mISP->setDVS(true);
        else
            mISP->setDVS(false);
    }

    mNumBuffers = mISP->getNumBuffers();

    LOG1("Using preview format: %s", v4l2Fmt2Str(format));
    mParameters.getPreviewSize(&width, &height);
    mISP->setPreviewFrameFormat(width, height);
    mPreviewThread->setPreviewConfig(width, height, format, mNumBuffers);

    mCoupledBuffers = new CoupledBuffer[mNumBuffers];
    memset(mCoupledBuffers, 0, mNumBuffers * sizeof(CoupledBuffer));

    AtomBuffer *pvBufs;
    int count;
    status = mPreviewThread->fetchPreviewBuffers(&pvBufs, &count);
    if ((status == NO_ERROR) && (count == mNumBuffers)){
        mISP->setGraphicPreviewBuffers(pvBufs, mNumBuffers);
    }

    // start the data flow
    status = mISP->start(mode);
    if (status == NO_ERROR) {
        memset(mCoupledBuffers, 0, sizeof(mCoupledBuffers));
        mState = state;
        if (mAAA->is3ASupported()) {
            // Enable auto-focus by default
            mAAA->setAfEnabled(true);
            m3AThread->enable3A();
            if (videoMode) {
                m3AThread->enableDVS(true);
            }
        }
    } else {
        LOGE("Error starting ISP!");
        mPreviewThread->returnPreviewBuffers();
    }

    return status;
}

status_t ControlThread::stopPreviewCore()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mState == STATE_PREVIEW_VIDEO && mAAA->is3ASupported()) {
        m3AThread->enableDVS(false);
    }

    // Before stopping the ISP, flush any buffers in picture
    // and video threads. This is needed as AtomISP::stop() may
    // deallocate buffers and the picture/video threads might
    // otherwise hold invalid references.
    status = mPreviewThread->flushBuffers();
    if (mState == STATE_PREVIEW_VIDEO ||
        mState == STATE_RECORDING) {
        status = mVideoThread->flushBuffers();
    }

    status = mISP->stop();
    if (status == NO_ERROR) {
        mState = STATE_STOPPED;
    } else {
        LOGE("Error stopping ISP in preview mode!");
    }

    status = mPreviewThread->returnPreviewBuffers();

    delete [] mCoupledBuffers;
    // set to null because frames can be returned to hal in stop state
    // need to check for null in relevant locations
    mCoupledBuffers = NULL;
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

    status = mPictureThread->flushBuffers();
    if (status != NO_ERROR) {
        LOGE("Error flushing PictureThread!");
        return status;
    }

    status = mISP->stop();
    if (status != NO_ERROR) {
        LOGE("Error stopping ISP!");
        return status;
    }
    status = mISP->releaseCaptureBuffers();

    mState = STATE_STOPPED;
    mBurstCaptureNum = 0;

    // Reset AE and AF in case HDR/bracketing was used (these features
    // manually configure AE and AF during takePicture)
    if (mBracketing.mode == BRACKET_EXPOSURE)
        mAAA->setAeMode(mPublicAeMode);
    if (mBracketing.mode == BRACKET_FOCUS)
        mAAA->setAfMode(mPublicAfMode);

    if (mHdr.enabled) {
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
        // restore HDR save original settings as requested by the application
        mHdr.saveOrig = mHdr.appSaveOrig;
        mHdr.saveOrigRequest = mHdr.appSaveOrigRequest;
    }
    return status;
}

status_t ControlThread::restartPreview(bool videoMode)
{
    LOG1("@%s: mode = %s", __FUNCTION__, videoMode?"VIDEO":"STILL");
    bool faceActive = mFaceDetectionActive;
    stopFaceDetection(true);
    status_t status = stopPreviewCore();
    if (status == NO_ERROR)
        status = startPreviewCore(videoMode);
    if (faceActive)
        startFaceDetection();
    return status;
}

status_t ControlThread::handleMessageStartPreview()
{
    LOG1("@%s", __FUNCTION__);
    status_t status;
    if (mState == STATE_CAPTURE) {
        status = stopCapture();
        if (status != NO_ERROR) {
            LOGE("Could not stop capture before start preview!");
            return status;
        }
    }
    if (mState == STATE_STOPPED) {
        // API says apps should call startFaceDetection when resuming preview
        // stop FD here to avoid accidental FD.
        stopFaceDetection();
        bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;
        status = startPreviewCore(videoMode);
    } else {
        LOGE("Error starting preview. Invalid state!");
        status = INVALID_OPERATION;
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_START_PREVIEW, status);
    return status;
}

status_t ControlThread::handleMessageStopPreview()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    // In STATE_CAPTURE, preview is already stopped, nothing to do
    if (mState != STATE_CAPTURE) {
        stopFaceDetection(true);
        if (mState != STATE_STOPPED) {
            status = stopPreviewCore();
        } else {
            LOGE("Error stopping preview. Invalid state!");
            status = INVALID_OPERATION;
        }
    }
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
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mPreviewThread != NULL) {
        status = mPreviewThread->setPreviewWindow(msg->window);
    }

    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;

    // Only restart preview if preview is active
    if (previewEnabled() && (msg->window != NULL)) {
       restartPreview(videoMode);
    }

    return status;
}

status_t ControlThread::handleMessageStartRecording()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    AeMode aeMode = CAM_AE_MODE_NOT_SET;

    if (mState == STATE_PREVIEW_VIDEO) {
        mState = STATE_RECORDING;
    } else if (mState == STATE_PREVIEW_STILL) {
        /* We are in PREVIEW_STILL mode; in order to start recording
         * we first need to stop AtomISP and restart it with MODE_VIDEO
         */
        status = restartPreview(true);
        if (status != NO_ERROR) {
            LOGE("Error restarting preview in video mode");
        }
        mState = STATE_RECORDING;
    } else {
        LOGE("Error starting recording. Invalid state!");
        status = INVALID_OPERATION;
    }

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

    /*
     *  Also, release snapshot shared buffers allocated before video recording.
     *  It seems that there is a bug in the HW JPEG encoder: if we use preallocated buffers
     *  after a video recording, those preallocated buffers for JPEG encoding are causing
     *  a hang in HW JPEG encoder. Therefore, a workaround is to deallocate the shared JPEG
     *  buffers after a video recording session.
     *  The shared JPEG buffers will be reallocated with the next setParameters or takePicture
     *  call.
     */
    mPictureThread->releaseSharedBuffers();

    // release buffers owned by encoder since it is not going to return them
    if (mCoupledBuffers) {
        for (int i = 0; i < mNumBuffers; i++) {
            if (!mCoupledBuffers[i].recordingBuffReturned) {
               mCoupledBuffers[i].recordingBuffReturned = true;
               queueCoupledBuffers(i);
            }
        }
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_STOP_RECORDING, status);
    return status;
}

bool ControlThread::runPreFlashSequence()
{
    size_t framesTillFlashComplete = 0;
    AtomBuffer buff;
    bool ret = false;
    status_t status = NO_ERROR;
    atomisp_frame_status frameStatus = ATOMISP_FRAME_STATUS_OK;

    // Stage 1
    status = mISP->getPreviewFrame(&buff);
    if (status == NO_ERROR) {
        mISP->putPreviewFrame(&buff);
        mAAA->applyPreFlashProcess(CAM_FLASH_STAGE_NONE);
    } else {
        return ret;
    }

    // Stage 2
    status = mISP->getPreviewFrame(&buff);
    if (status == NO_ERROR) {
        mISP->putPreviewFrame(&buff);
        mAAA->applyPreFlashProcess(CAM_FLASH_STAGE_PRE);
    } else {
        return ret;
    }

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

    if (ret) {
        mAAA->applyPreFlashProcess(CAM_FLASH_STAGE_MAIN);
    } else {
        mAAA->apply3AProcess(true, buff.capture_timestamp);
    }

    return ret;
}

status_t ControlThread::skipFrames(size_t numFrames, size_t doBracket)
{
    LOG1("@%s: numFrames=%d, doBracket=%d", __FUNCTION__, numFrames, doBracket);
    status_t status = NO_ERROR;
    AtomBuffer snapshotBuffer, postviewBuffer;

    for (size_t i = 0; i < numFrames; i++) {
        if (i < doBracket) {
            status = applyBracketing();
            if (status != NO_ERROR) {
                LOGE("Error applying bracketing in skip frame %d!", i);
                return status;
            }
        }
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

/*
 *  For Exposure Bracketing, the applied exposure value will be available in
 *  current frame + 2. Therefore, in order to do a correct exposure bracketing
 *  we need to skip 2 frames. But, when burst-skip-frames parameter is set
 *  (>0) we have some special cases, described below.
 *
 *  We apply bracketing only for the first skipped frames, so the
 *  desired result will be available in the real needed frame.
 *  Below is the explanation:
 *  (S stands for skipped frame)
 *  (F stands for forced skipped frame, in order to get the desired exposure
 *   in the next real frame)
 *
 *  For burst-skip-frames=1
 *  Applied exposure value   EV0     EV1     EV2     EV3     EV4     EV5
 *  Frame number             FS0  S1   2  S3   4  S5   6  S7   8  S9  10 S11
 *  Output exposure value            EV0 EV0 EV1 EV1 EV2 EV2 EV3 EV3 EV4 EV4
 *  Explanation: in the beginning, we need to force one frame skipping, so
 *  that the applied exposure will be available in frame 2. Continuing the
 *  burst, we don't need to force skip frames, because we will apply the
 *  bracketing exposure in burst sequence (see the timeline above).
 *
 *  For burst-skip-frames=3
 *  Applied exposure value   EV0             EV1             EV2
 *  Frame number              S0  S1  S2   3  S4  S5  S6   7  S8  S9 S10  11
 *  Output exposure value            EV0 EV0 EV0 EV0 EV1 EV1 EV1 EV1 EV2 EV2
 *  Explanation: for burst-skip-frames >= 2, it's enough to apply the exposure
 *  bracketing in the first skipped frame in order to get the applied exposure
 *  in the next real frame (see the timeline above).
 */

status_t ControlThread::initBracketing()
{
    LOG1("@%s: mode = %d", __FUNCTION__, mBracketing.mode);
    status_t status = NO_ERROR;
    ia_3a_af_lens_range lensRange;
    int currentFocusPos;

    switch (mBracketing.mode) {
    case BRACKET_EXPOSURE:
        if (mBurstLength > 1) {
            mAAA->setAeMode(CAM_AE_MODE_MANUAL);
            mBracketing.currentValue = EV_MIN;
            mBracketing.minValue = EV_MIN;
            mBracketing.maxValue = EV_MAX;
            mBracketing.step = (mBracketing.maxValue - mBracketing.minValue) / (mBurstLength - 1);
            LOG1("Initialized Exposure Bracketing to: (min: %.2f, max:%.2f, step:%.2f)",
                    mBracketing.minValue,
                    mBracketing.maxValue,
                    mBracketing.step);
        } else {
            LOG1("Can't do bracketing with only one capture, disable bracketing!");
            mBracketing.mode = BRACKET_NONE;
        }
        break;
    case BRACKET_FOCUS:
        if (mBurstLength > 1) {
            status = mAAA->getAfLensPosRange(&lensRange);
            if (status == NO_ERROR) {
                status = mAAA->getCurrentFocusPosition(&currentFocusPos);
            }
            if (status == NO_ERROR) {
                status = mAAA->setAeMode(CAM_AE_MODE_MANUAL);
            }
            if (status == NO_ERROR) {
                mAAA->setAfMode(CAM_AF_MODE_MANUAL);
            }
            mBracketing.currentValue = lensRange.macro;
            mBracketing.minValue = lensRange.macro;
            mBracketing.maxValue = lensRange.infinity;
            mBracketing.step = (lensRange.infinity - lensRange.macro) / (mBurstLength - 1);
            // Initialize the current focus position and increment
            if (status == NO_ERROR) {
                /*
                 * For focus we need to bring the focus position
                 * to the initial position in the bracketing sequence.
                 */
                status = mAAA->getCurrentFocusPosition(&currentFocusPos);
                if (status == NO_ERROR) {
                    status = mAAA->setManualFocusIncrement(mBracketing.minValue - currentFocusPos);
                }
                if (status == NO_ERROR) {
                    status = mAAA->updateManualFocus();
                }
            }
            if (status == NO_ERROR) {
                LOG1("Initialized Focus Bracketing to: (min: %.2f, max:%.2f, step:%.2f)",
                        mBracketing.minValue,
                        mBracketing.maxValue,
                        mBracketing.step);
            }
        } else {
            LOG1("Can't do bracketing with only one capture, disable bracketing!");
            mBracketing.mode = BRACKET_NONE;
        }
        break;
    case BRACKET_NONE:
        // Do nothing here
        break;
    }

    if (mBracketing.mode == BRACKET_EXPOSURE && mBurstSkipFrames < 2) {
        /*
         *  If we are in Exposure Bracketing, and mBurstSkipFrames < 2, we need to
         *  skip some initial frames and apply bracketing (explanation above):
         *  2 frames for mBurstSkipFrames == 0
         *  1 frame  for mBurstSkipFrames == 1
         */
        skipFrames(2 - mBurstSkipFrames, 2 - mBurstSkipFrames);
    } else if (mBracketing.mode == BRACKET_FOCUS && mBurstSkipFrames < 1) {
        /*
         *  If we are in Focus Bracketing, and mBurstSkipFrames < 1, we need to
         *  skip one initial frame w/o apply bracketing so that the focus will be
         *  positioned in the initial position.
         */
        skipFrames(1, 0);
    }

    return status;
}

status_t ControlThread::applyBracketing()
{
    LOG1("@%s: mode = %d", __FUNCTION__, mBracketing.mode);
    status_t status = NO_ERROR;
    int currentFocusPos;
    SensorParams sensorParams;

    switch (mBracketing.mode) {
    case BRACKET_EXPOSURE:
        if (mBracketing.currentValue <= mBracketing.maxValue) {
            LOG1("Applying Exposure Bracketing: %.2f", mBracketing.currentValue);
            status = mAAA->applyEv(mBracketing.currentValue);
            mAAA->getExposureInfo(sensorParams);
            sensorParams.evBias = mBracketing.currentValue;

            LOG1("Adding sensorParams to list (size=%d+1)", mBracketingParams.size());
            mBracketingParams.push_front(sensorParams);
            if (status == NO_ERROR) {
                LOG1("Exposure Bracketing: incrementing exposure value with: %.2f", mBracketing.step);
                mBracketing.currentValue += mBracketing.step;
            }
        }
        break;
    case BRACKET_FOCUS:
        if (mBracketing.currentValue + mBracketing.step <= mBracketing.maxValue) {
            status = mAAA->setManualFocusIncrement(mBracketing.step);
        }
        if (status == NO_ERROR) {
            mBracketing.currentValue += mBracketing.step;
            status = mAAA->updateManualFocus();
            mAAA->getCurrentFocusPosition(&currentFocusPos);
            LOG1("Applying Focus Bracketing: %d", currentFocusPos);
        }
        break;
    case BRACKET_NONE:
        // Do nothing here
        break;
    }

    return status;
}

status_t ControlThread::handleMessageTakePicture(bool clientRequest)
{
    LOG1("@%s: clientRequest = %d", __FUNCTION__, clientRequest);
    status_t status = NO_ERROR;
    AtomBuffer snapshotBuffer, postviewBuffer;
    State origState = mState;
    int width, height, format, size;
    int pvWidth, pvHeight, pvFormat, pvSize;
    FlashMode flashMode = mAAA->getAeFlashMode();
    bool flashOn = (flashMode == CAM_AE_FLASH_MODE_TORCH ||
        flashMode == CAM_AE_FLASH_MODE_ON);
    atomisp_makernote_info makerNote;
    SensorParams sensorParams;

    // TODO: temporary, video snapshot needs to be supported, but
    //       implementation is not ready yet
    if (origState == STATE_RECORDING) {
        LOGE("Video snapshot not supported!");
        return INVALID_OPERATION;
    }

    if (clientRequest || (mHdr.enabled && mHdr.saveOrigRequest)) {
        bool requestPostviewCallback = true;
        bool requestRawCallback = true;
        if (origState == STATE_CAPTURE && mBurstLength <= 1) {
            // If burst-length is NOT specified, but more pictures are requested, call the shutter sound now
            mCallbacksThread->shutterSound();
        }
        // TODO: Fix the TestCamera application bug and remove this workaround
        // WORKAROUND BEGIN: Due to a TesCamera application bug send the POSTVIEW and RAW callbacks only for single shots
        if (origState == STATE_CAPTURE || mBurstLength > 1) {
            requestPostviewCallback = false;
            requestRawCallback = false;
        }
        // WORKAROUND END
        // Notify CallbacksThread that a picture was requested, so grab one from queue
        mCallbacksThread->requestTakePicture(requestPostviewCallback, requestRawCallback);
        if (mHdr.enabled && mHdr.saveOrigRequest) {
            // After we requested a picture from CallbackThread, disable saveOrigRequest (we need just one picture for original)
            mHdr.saveOrigRequest = false;
        }

        /*
         *  If the CallbacksThread has already JPEG buffers in queue, make sure we use them, before
         *  continuing to dequeue frames from ISP and encode them
         */
        if (origState == STATE_CAPTURE) {
            if (mCallbacksThread->getQueuedBuffersNum() > MAX_JPEG_BUFFERS) {
                return NO_ERROR;
            }
            // Check if ISP has free buffers we can use
            if (!mISP->dataAvailable()) {
                // If ISP has no data, do nothing and return
                return NO_ERROR;
            }
        }

        // If burst length was specified stop capturing when reached the requested burst captures
        if (mBurstLength > 1 && mBurstCaptureNum >= mBurstLength) {
            return NO_ERROR;
        }
    }

    if (origState != STATE_PREVIEW_STILL && origState != STATE_PREVIEW_VIDEO &&
        origState != STATE_RECORDING && origState != STATE_CAPTURE) {
        LOGE("we only support snapshot in preview, recording, and capture modes");
        return INVALID_OPERATION;
    }
    if (origState != STATE_CAPTURE) {
        stopFaceDetection();
    }

    if (origState == STATE_PREVIEW_STILL || origState == STATE_PREVIEW_VIDEO) {
        if (mBurstLength <= 1) {
            // If flash mode is not ON or TORCH, check for other modes: AUTO, DAY_SYNC, SLOW_SYNC
            if (!flashOn && mAAA->is3ASupported()) {
                if (DetermineFlash(flashMode)) {
                    flashOn = mAAA->getAeFlashNecessary();
                    if (flashOn) {
                        if (mAAA->getAeMode() != CAM_AE_MODE_MANUAL) {
                            flashOn = runPreFlashSequence();
                        }
                    }
                }
            }
        }
        status = stopPreviewCore();
        if (status != NO_ERROR) {
            LOGE("Error stopping preview!");
            return status;
        }
        mState = STATE_CAPTURE;
        mBurstCaptureNum = 0;
    }

    // Get the current params
    mParameters.getPictureSize(&width, &height);
    pvWidth = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    pvHeight= mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    format = mISP->getSnapshotPixelFormat();
    size = frameSize(format, width, height);
    pvSize = frameSize(format, pvWidth, pvHeight);

    if (origState == STATE_RECORDING) {
        // override picture size to video size if recording
        int vidWidth, vidHeight;
        mISP->getVideoSize(&vidWidth, &vidHeight);
        if (width != vidWidth || height != vidHeight) {
            LOGW("Warning overriding snapshot size=%d,%d to %d,%d",
                    width, height, vidWidth, vidHeight);
            width = vidWidth;
            height = vidHeight;
        }
    }

    status = mISP->getMakerNote(&makerNote);
    if (status != NO_ERROR) {
        LOGW("Could not get maker note information!");
    }

    // Configure PictureThread
    if (origState == STATE_PREVIEW_STILL || origState == STATE_PREVIEW_VIDEO) {
        mPictureThread->initialize(mParameters, makerNote, flashOn);

    } else if (origState == STATE_RECORDING) { // STATE_RECORDING

        // Picture thread uses snapshot-size to configure itself. However,
        // if in recording mode we need to override snapshot with video-size.
        CameraParameters copyParams = mParameters;
        copyParams.setPictureSize(width, height); // make sure picture size is same as video size
        mPictureThread->initialize(copyParams, makerNote, flashOn);
    }

    if (origState == STATE_PREVIEW_STILL || origState == STATE_PREVIEW_VIDEO) {
        // Configure and start the ISP
        mISP->setSnapshotFrameFormat(width, height, format);
        mISP->setPostviewFrameFormat(pvWidth, pvHeight, format);
        if (mHdr.enabled) {
            mHdr.outMainBuf.buff = NULL;
            mHdr.outPostviewBuf.buff = NULL;
            mISP->setSnapshotNum(mHdr.bracketNum);
        } else {
            mISP->setSnapshotNum(NUM_BURST_BUFFERS);
        }

        /*
         * Use buffers sharing only if the pixel format is NV12 and HDR is not enabled.
         * HDR shared buffers cannot be accessed from multiresolution fw, so we need user-space
         * buffers when doing HDR composition.
         */
        if (format == V4L2_PIX_FMT_NV12 && !mHdr.enabled) {
            // Try to use buffer sharing
            void* snapshotBufferPtr;
            status = mPictureThread->getSharedBuffers(width, height, &snapshotBufferPtr, NUM_BURST_BUFFERS);
            if (status == NO_ERROR) {
                status = mISP->setSnapshotBuffers(snapshotBufferPtr, NUM_BURST_BUFFERS);
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

        if ((status = mISP->start(MODE_CAPTURE)) != NO_ERROR) {
            LOGE("Error starting the ISP driver in CAPTURE mode!");
            return status;
        }

        // HDR init
        if (mHdr.enabled) {
            // Initialize the HDR output buffers
            // Main output buffer
            mCallbacks->allocateMemory(&mHdr.outMainBuf, size);
            if (mHdr.outMainBuf.buff == NULL) {
                LOGE("HDR: Error allocating memory for HDR main buffer!");
                status = NO_MEMORY;
                return status;
            }
            mHdr.outMainBuf.shared = false;
            LOG1("HDR: using %p as HDR main output buffer", mHdr.outMainBuf.buff->data);
            // Postview output buffer
            mCallbacks->allocateMemory(&mHdr.outPostviewBuf, pvSize);
            if (mHdr.outPostviewBuf.buff == NULL) {
                LOGE("HDR: Error allocating memory for HDR postview buffer!");
                status = NO_MEMORY;
                return status;
            }
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
                status = NO_MEMORY;
                return status;
            }

            mHdr.ciBufOut.ciMainBuf->addr = mHdr.outMainBuf.buff->data;
            mHdr.ciBufOut.ciMainBuf[0].width = mHdr.outMainBuf.width = width;
            mHdr.ciBufOut.ciMainBuf[0].height = mHdr.outMainBuf.height = height;
            mHdr.ciBufOut.ciMainBuf[0].format = mHdr.outMainBuf.format = format;
            mHdr.ciBufOut.ciMainBuf[0].length = mHdr.outMainBuf.size = size;

            LOG1("HDR: Initialized output CI main     buff @%p: (addr=%p, length=%d, width=%d, height=%d, format=%d)",
                    &mHdr.ciBufOut.ciMainBuf[0],
                    mHdr.ciBufOut.ciMainBuf[0].addr,
                    mHdr.ciBufOut.ciMainBuf[0].length,
                    mHdr.ciBufOut.ciMainBuf[0].width,
                    mHdr.ciBufOut.ciMainBuf[0].height,
                    mHdr.ciBufOut.ciMainBuf[0].format);

            mHdr.ciBufOut.ciPostviewBuf[0].addr = mHdr.outPostviewBuf.buff->data;
            mHdr.ciBufOut.ciPostviewBuf[0].width = mHdr.outPostviewBuf.width = pvWidth;
            mHdr.ciBufOut.ciPostviewBuf[0].height = mHdr.outPostviewBuf.height = pvHeight;
            mHdr.ciBufOut.ciPostviewBuf[0].format = mHdr.outPostviewBuf.format = format;
            mHdr.ciBufOut.ciPostviewBuf[0].length = mHdr.outPostviewBuf.size = pvSize;

            LOG1("HDR: Initialized output CI postview buff @%p: (addr=%p, length=%d, width=%d, height=%d, format=%d)",
                    &mHdr.ciBufOut.ciPostviewBuf[0],
                    mHdr.ciBufOut.ciPostviewBuf[0].addr,
                    mHdr.ciBufOut.ciPostviewBuf[0].length,
                    mHdr.ciBufOut.ciPostviewBuf[0].width,
                    mHdr.ciBufOut.ciPostviewBuf[0].height,
                    mHdr.ciBufOut.ciPostviewBuf[0].format);
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

        if (mBurstLength > 1 && mBracketing.mode != BRACKET_NONE) {
            initBracketing();
        }
    }

    if (mState == STATE_CAPTURE) {
        // Turn on flash. If flash mode is torch, then torch is already on
        if (flashOn && flashMode != CAM_AE_FLASH_MODE_TORCH) {
            LOG1("Requesting flash");
            if (mISP->setFlash(1) != NO_ERROR) {
                LOGE("Failed to enable the Flash!");
            }
        } else if (DetermineFlash(flashMode)) {
            mISP->setFlashIndicator(TORCH_INTENSITY);
        }

        if (mBurstLength > 1 && mBurstSkipFrames > 0) {
            LOG1("Skipping %d burst frames", mBurstSkipFrames);
            int doBracketNum = 0;
            if (mBracketing.mode == BRACKET_EXPOSURE && mBurstSkipFrames >= 2) {
                // In Exposure Bracket, if mBurstSkipFrames >= 2 apply bracketing every first skipped frame
                // This is because, exposure needs 2 frames for the exposure value to take effect
                doBracketNum = 1;
            } else if (mBracketing.mode == BRACKET_FOCUS && mBurstSkipFrames >= 1) {
                // In Focus Bracket, if mBurstSkipFrames >= 1 apply bracketing every first skipped frame
                // This is because focus needs only 1 frame for the focus position to take effect
                doBracketNum = 1;
            }
            if ((status = skipFrames(mBurstSkipFrames, doBracketNum)) != NO_ERROR) {
                LOGE("Error skipping burst frames!");
                return status;
            }
        }

        // If mBurstSkipFrames < 2, apply exposure bracketing every real frame
        // If mBurstSkipFrames < 1, apply focus bracketing every real frame
        if ((mBurstSkipFrames < 2 && mBracketing.mode == BRACKET_EXPOSURE) ||
            (mBurstSkipFrames < 1 && mBracketing.mode == BRACKET_FOCUS)) {
            applyBracketing();
        }

        // Get the snapshot
        if ((status = mISP->getSnapshot(&snapshotBuffer, &postviewBuffer)) != NO_ERROR) {
            LOGE("Error in grabbing snapshot!");
            return status;
        }

        if (mHdr.enabled) {
            // Initialize the HDR CI input buffers (main/postview) for this capture
            if (snapshotBuffer.shared) {
                mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].addr = (void *) *((char **)snapshotBuffer.buff->data);
                LOGW("HDR: Warning: shared buffer detected in HDR composing. The composition might fail!");
            } else {
                mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].addr = snapshotBuffer.buff->data;
            }
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].addr = snapshotBuffer.buff->data;
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].width = width;
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].height = height;
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].format = format;
            mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].length = size;

            LOG1("HDR: Initialized input CI main     buff %d @%p: (addr=%p, length=%d, width=%d, height=%d, format=%d)",
                    mBurstCaptureNum,
                    &mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum],
                    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].addr,
                    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].length,
                    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].width,
                    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].height,
                    mHdr.ciBufIn.ciMainBuf[mBurstCaptureNum].format);

            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].addr = postviewBuffer.buff->data;
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].width = pvWidth;
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].height = pvHeight;
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].format = format;
            mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].length = pvSize;

            LOG1("HDR: Initialized input CI postview buff %d @%p: (addr=%p, length=%d, width=%d, height=%d, format=%d)",
                    mBurstCaptureNum,
                    &mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum],
                    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].addr,
                    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].length,
                    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].width,
                    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].height,
                    mHdr.ciBufIn.ciPostviewBuf[mBurstCaptureNum].format);

            status = mAAA->computeCDF(mHdr.ciBufIn, mBurstCaptureNum);
            if (status != NO_ERROR) {
                LOGE("HDR: Error in compute CDF for capture %d in HDR sequence!", mBurstCaptureNum);
                return status;
            }
        }

        mBurstCaptureNum++;

        mAAA->getExposureInfo(sensorParams);
        if (mAAA->getEv(&sensorParams.evBias) != NO_ERROR) {
            sensorParams.evBias = EV_UPPER_BOUND;
        }

        if ((!mHdr.enabled || (mHdr.enabled && mBurstCaptureNum == 1)) &&
            (origState != STATE_CAPTURE || mBurstLength > 1)) {
            // Send request to play the Shutter Sound: in single shots or when burst-length is specified
            mCallbacksThread->shutterSound();
        }

        // TODO: here we should display the picture using PreviewThread

        // Turn off flash
        if (!flashOn && DetermineFlash(flashMode)) {
            mISP->setFlashIndicator(0);
        }
    }

    // Do jpeg encoding
    if (mState == STATE_CAPTURE) {
        if (!mBracketingParams.empty()) {
            LOG1("Popping sensorParams from list (size=%d-1)", mBracketingParams.size());
            sensorParams = *(--mBracketingParams.end());
            mBracketingParams.erase(--mBracketingParams.end());
        }
        if (!mHdr.enabled || (mHdr.enabled && mHdr.saveOrig)) {
            bool doEncode = false;
            if (mHdr.enabled) {
                // In HDR mode, if saveOrig flag is set, save only the EV0 snapshot
                if (mHdr.saveOrig && sensorParams.evBias == 0) {
                    LOG1("Sending EV0 original picture to JPEG encoder (id=%d)", snapshotBuffer.id);
                    doEncode = true;
                    // Disable the saveOrig flag once we encode the EV0 original snapshot
                    mHdr.saveOrig = false;
                }
            } else {
                doEncode = true;
            }
            if (doEncode) {
                postviewBuffer.width = pvWidth;
                postviewBuffer.height = pvHeight;
                status = mPictureThread->encode(&sensorParams, &snapshotBuffer, &postviewBuffer);
            }
        }
        if (mHdr.enabled && mBurstCaptureNum == mHdr.bracketNum) {
            // This was the last capture in HDR sequence, compose the final HDR image
            LOG1("HDR: last capture, composing HDR image...");

            /*
             * Stop ISP before composing HDR since standalone acceleration requires ISP to be stopped.
             * The below call won't release the capture buffers since they are needed by HDR compose
             * method. The capture buffers will be released in stopCapture method.
             */
            status = mISP->stop();
            if (status != NO_ERROR) {
                LOGE("Error stopping ISP!");
                return status;
            }
            status = mAAA->composeHDR(mHdr.ciBufIn, mHdr.ciBufOut, mHdr.vividness, mHdr.sharpening);
            if (status == NO_ERROR) {
                mHdr.outMainBuf.width = mHdr.ciBufOut.ciMainBuf->width;
                mHdr.outMainBuf.height = mHdr.ciBufOut.ciMainBuf->height;
                mHdr.outMainBuf.format = mHdr.ciBufOut.ciMainBuf->format;
                mHdr.outMainBuf.size = mHdr.ciBufOut.ciMainBuf->length;
                status = mPictureThread->encode(&sensorParams, &mHdr.outMainBuf, &mHdr.outPostviewBuf);
            }
        }
    } else {
        // If we are in video mode we simply use the recording buffer for picture encoding
        // No need to stop, reconfigure, and restart the ISP
        mCoupledBuffers[mLastRecordingBuffIndex].videoSnapshotBuff = true;
        status = mPictureThread->encode(NULL, &mCoupledBuffers[mLastRecordingBuffIndex].recordingBuff);
    }

    return status;
}

status_t ControlThread::handleMessageCancelPicture()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t ControlThread::handleMessageAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    FlashMode flashMode = mAAA->getAeFlashMode();
    // Implement pre auto-focus functions
    if (flashMode != CAM_AE_FLASH_MODE_TORCH && mAAA->is3ASupported()) {

        if (flashMode == CAM_AE_FLASH_MODE_ON) {
            mFlashAutoFocus = true;
        }

        if (!mFlashAutoFocus && DetermineFlash(flashMode)) {
            // Check the other modes
            LOG1("Flash mode = %d", flashMode);
            if (mAAA->getAeFlashNecessary()) {
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
    if (!mISP->isBufferValid(&msg->buff))
        return DEAD_OBJECT;
    status_t status = NO_ERROR;

    if (mFaceDetectionActive) {
        LOG2("@%s: face detection active", __FUNCTION__);
        int width, height;
        mParameters.getPreviewSize(&width, &height);
        msg->buff.owner = this;
        if (mPostProcThread->sendFrame(&msg->buff, width, height) < 0) {
            msg->buff.owner = 0;
            releasePreviewFrame(&msg->buff);
        }
    } else {
       releasePreviewFrame(&msg->buff);
    }
    return NO_ERROR;
}

status_t ControlThread::handleMessageReleasePreviewFrame(MessageReleasePreviewFrame *msg)
{
    LOG2("handle preview frame release buff id = %d", msg->buff.id);
    status_t status = NO_ERROR;
    if (mState == STATE_PREVIEW_STILL) {
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

    if (mState == STATE_RECORDING) {
        int curBuff = msg->snapshotBuf.id;
        if (mCoupledBuffers && curBuff < mNumBuffers) {
            mCoupledBuffers[curBuff].videoSnapshotBuffReturned = true;
            status = queueCoupledBuffers(curBuff);
            mCoupledBuffers[curBuff].videoSnapshotBuffReturned = false;
            mCoupledBuffers[curBuff].videoSnapshotBuff = false;
        }
    } else if (mState == STATE_CAPTURE) {
        /*
         * If HDR is enabled, don't return the buffers. we need them to compose HDR
         * image. The buffers will be discarded after HDR is done in stopCapture().
         */
        if (!mHdr.enabled) {
            LOG2("@%s: returning post and raw frames id:%d", __FUNCTION__, msg->snapshotBuf.id);
            // Return the picture frames back to ISP
            status = mISP->putSnapshot(&msg->snapshotBuf, &msg->postviewBuf);
            if (status == DEAD_OBJECT) {
                LOG1("Stale snapshot buffer returned to ISP");
            } else if (status != NO_ERROR) {
                LOGE("Error in putting snapshot!");
                return status;
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

status_t ControlThread::validateParameters(const CameraParameters *params)
{
    LOG1("@%s: params = %p", __FUNCTION__, params);
    // PREVIEW
    int previewWidth, previewHeight;
    params->getPreviewSize(&previewWidth, &previewHeight);
    if (previewWidth <= 0 || previewHeight <= 0) {
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
    int videoWidth, videoHeight;
    params->getPreviewSize(&videoWidth, &videoHeight);
    if (videoWidth <= 0 || videoHeight <= 0) {
        LOGE("bad video size");
        return BAD_VALUE;
    }

    // SNAPSHOT
    int pictureWidth, pictureHeight;
    params->getPreviewSize(&pictureWidth, &pictureHeight);
    if (pictureWidth <= 0 || pictureHeight <= 0) {
        LOGE("bad picture size");
        return BAD_VALUE;
    }

    // ZOOM
    int zoom = params->getInt(CameraParameters::KEY_ZOOM);
    int maxZoom = params->getInt(CameraParameters::KEY_MAX_ZOOM);
    if (zoom > maxZoom) {
        LOGE("bad zoom index");
        return BAD_VALUE;
    }

    // FLASH
    const char* flashMode = params->get(CameraParameters::KEY_FLASH_MODE);
    const char* flashModes = params->get(CameraParameters::KEY_SUPPORTED_FLASH_MODES);
    if (flashMode && flashModes && strstr(flashModes, flashMode) == NULL) {
        LOGE("bad flash mode");
        return BAD_VALUE;
    }

    // FOCUS
    const char* focusMode = params->get(CameraParameters::KEY_FOCUS_MODE);
    const char* focusModes = params->get(CameraParameters::KEY_SUPPORTED_FOCUS_MODES);
    if (focusMode && strstr(focusModes, focusMode) == NULL) {
        LOGE("bad focus mode");
        return BAD_VALUE;
    }

    // FOCUS WINDOWS
    int maxWindows = params->getInt(CameraParameters::KEY_MAX_NUM_FOCUS_AREAS);
    if (maxWindows > 0) {
        const char *pFocusWindows = params->get(CameraParameters::KEY_FOCUS_AREAS);
        if (pFocusWindows && (strlen(pFocusWindows) > 0)) {
            LOG1("Scanning AF windows from params: %s", pFocusWindows);
            const char *argTail = pFocusWindows;
            int winCount = 0;
            CameraWindow focusWindow;
            while (argTail && winCount < maxWindows) {
                // String format: "(topleftx,toplefty,bottomrightx,bottomrighty,weight),(...)"
                int i = sscanf(argTail, "(%d,%d,%d,%d,%d)",
                        &focusWindow.x_left,
                        &focusWindow.y_top,
                        &focusWindow.x_right,
                        &focusWindow.y_bottom,
                        &focusWindow.weight);
                argTail = strchr(argTail + 1, '(');
                // Camera app sets invalid window 0,0,0,0,0 - let it slide
                if ( !focusWindow.x_left && !focusWindow.y_top &&
                    !focusWindow.x_right && !focusWindow.y_bottom &&
                    !focusWindow.weight) {
                  continue;
                }
                if (i != 5) {
                    LOGE("bad focus window format");
                    return BAD_VALUE;
                }
                bool verified = verifyCameraWindow(focusWindow);
                if (!verified) {
                    LOGE("bad focus window");
                    return BAD_VALUE;
                }
                if (verified) {
                    winCount++;
                }
            }
            // make sure not too many windows defined (to pass CTS)
            if (argTail) {
                LOGE("bad - too many focus windows or bad format for focus window string");
                return BAD_VALUE;
            }
        }
    }

    // METERING AREAS
    maxWindows = params->getInt(CameraParameters::KEY_MAX_NUM_METERING_AREAS);
    if (maxWindows > 0) {
        const char *pMeteringWindows = params->get(CameraParameters::KEY_METERING_AREAS);
        if (pMeteringWindows && (strlen(pMeteringWindows) > 0)) {
            LOG1("Scanning Metering windows from params: %s", pMeteringWindows);
            const char *argTail = pMeteringWindows;
            int winCount = 0;
            CameraWindow meteringWindow;
            while (argTail && winCount < maxWindows) {
                // String format: "(topleftx,toplefty,bottomrightx,bottomrighty,weight),(...)"
                int i = sscanf(argTail, "(%d,%d,%d,%d,%d)",
                        &meteringWindow.x_left,
                        &meteringWindow.y_top,
                        &meteringWindow.x_right,
                        &meteringWindow.y_bottom,
                        &meteringWindow.weight);
                argTail = strchr(argTail + 1, '(');
                // Camera app sets invalid window 0,0,0,0,0 - let it slide
                if ( !meteringWindow.x_left && !meteringWindow.y_top &&
                    !meteringWindow.x_right && !meteringWindow.y_bottom &&
                    !meteringWindow.weight) {
                  continue;
                }
                if (i != 5) {
                    LOGE("bad metering window format");
                    return BAD_VALUE;
                }
                bool verified = verifyCameraWindow(meteringWindow);
                if (!verified) {
                    LOGE("bad metering window");
                    return BAD_VALUE;
                }
                if (verified) {
                    winCount++;
                }
            }
            // make sure not too many windows defined (to pass CTS)
            if (argTail) {
                LOGE("bad - too many metering windows or bad format for metering window string");
                return BAD_VALUE;
            }
        }
    }

    // MISCELLANEOUS
    // TODO: implement validation for other features not listed above

    // ANTI FLICKER
    const char* flickerMode = params->get(CameraParameters::KEY_ANTIBANDING);
    const char* flickerModes = params->get(CameraParameters::KEY_SUPPORTED_ANTIBANDING);
    if (flickerMode && strstr(flickerModes, flickerMode) == NULL) {
        LOGE("bad anti flicker mode");
        return BAD_VALUE;
    }

    return NO_ERROR;
}

status_t ControlThread::processDynamicParameters(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int oldZoom = oldParams->getInt(CameraParameters::KEY_ZOOM);
    int newZoom = newParams->getInt(CameraParameters::KEY_ZOOM);
    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;

    if (oldZoom != newZoom)
        status = mISP->setZoom(newZoom);

    // Burst mode
    // Get the burst length
    mBurstLength = newParams->getInt(IntelCameraParameters::KEY_BURST_LENGTH);
    mBurstSkipFrames = 0;
    if (mBurstLength <= 0) {
        // Parameter not set, leave it as 0
         mBurstLength = 0;
    } else {
        // Get the burst framerate
        int fps = newParams->getInt(IntelCameraParameters::KEY_BURST_FPS);
        if (fps > MAX_BURST_FRAMERATE) {
            LOGE("Invalid value received for %s: %d", IntelCameraParameters::KEY_BURST_FPS, mBurstSkipFrames);
            return BAD_VALUE;
        }
        if (fps > 0) {
            mBurstSkipFrames = (MAX_BURST_FRAMERATE / fps) - 1;
        }
    }

    // Color effect
    status = processParamEffect(oldParams, newParams);
    // anti flicker
    if (status == NO_ERROR) {
        status = processParamAntiBanding(oldParams, newParams);
    }

    if (mAAA->is3ASupported()) {

        // Changing the scene may change many parameters, including
        // flash, awb. Thus the order of how processParamFoo() are
        // called is important for the parameter changes to take
        // effect, and processParamSceneMode needs to be called first.

        if (status == NO_ERROR) {
            // Scene Mode
            status = processParamSceneMode(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // flash settings
            status = processParamFlash(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            //Focus Mode
            status = processParamFocusMode(oldParams, newParams);
        }

        if (status == NO_ERROR || !mFaceDetectionActive) {
            // white balance
            status = processParamWhiteBalance(oldParams, newParams);
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
            // hdr
            status = processParamHDR(oldParams, newParams);
        }

        if (!mFaceDetectionActive && status == NO_ERROR) {
            // customize metering
            status = processParamSetMeteringAreas(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // exposure compensation
            status = processParamExposureCompensation(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // ae mode
            status = processParamAutoExposureMode(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // ae mode
            status = processParamAutoExposureMeteringMode(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // ISO manual setting (Intel extension)
            status = processParamIso(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // shutter manual setting (Intel extension)
            status = processParamShutter(oldParams, newParams);
        }

        if (status == NO_ERROR) {
            // back lighting correction (Intel extension)
            status = processParamBackLightingCorrectionMode(oldParams, newParams);
        }

    }

    if (status == NO_ERROR) {
        if (mHdr.enabled) {
            /*
             * When doing HDR, we cannot use shared buffers, so we need to release any previously
             * allocated shared buffers before we use the libjpeg to encode user-space buffers.
             */
            status = mPictureThread->releaseSharedBuffers();
        } else {
            int picWidth, picHeight;
            mParameters.getPictureSize(&picWidth, &picHeight);
            status = mPictureThread->allocSharedBuffers(picWidth, picHeight, NUM_BURST_BUFFERS);
            if (status != NO_ERROR) {
                LOGW("Could not pre-allocate picture buffers!");
            }
        }
    }

    return status;
}

status_t ControlThread::processParamAFLock(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // af lock mode
    const char* oldValue = oldParams->get(IntelCameraParameters::KEY_AF_LOCK_MODE);
    const char* newValue = newParams->get(IntelCameraParameters::KEY_AF_LOCK_MODE);
    if (newValue && oldValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        bool af_lock;
        // TODO: once available, use the definitions in Intel
        //       parameter namespace, see UMG BZ26264
        const char* PARAM_LOCK = "lock";
        const char* PARAM_UNLOCK = "unlock";

        if(!strncmp(newValue, PARAM_LOCK, strlen(PARAM_LOCK))) {
            af_lock = true;
        } else if(!strncmp(newValue, PARAM_UNLOCK, strlen(PARAM_UNLOCK))) {
            af_lock = false;
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_AF_LOCK_MODE, newValue);
            return INVALID_OPERATION;
        }
        status = mAAA->setAfLock(af_lock);

        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_AF_LOCK_MODE, newValue);
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
    const char* oldValue = oldParams->get(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK);
    const char* newValue = newParams->get(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK);
    if (newValue && oldValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        bool awb_lock;

        if(!strncmp(newValue, CameraParameters::TRUE, strlen(CameraParameters::TRUE))) {
            awb_lock = true;
        } else if(!strncmp(newValue, CameraParameters::FALSE, strlen(CameraParameters::FALSE))) {
            awb_lock = false;
        } else {
            LOGE("Invalid value received for %s: %s", CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK, newValue);
            return INVALID_OPERATION;
        }
        status = mAAA->setAwbLock(awb_lock);

        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK, newValue);
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
    const char* oldValue = oldParams->get(IntelCameraParameters::KEY_XNR);
    const char* newValue = newParams->get(IntelCameraParameters::KEY_XNR);
    if (newValue && oldValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        if (!strncmp(newValue, CameraParameters::TRUE, MAX_PARAM_VALUE_LENGTH))
            status = mISP->setXNR(true);
        else
            status = mISP->setXNR(false);
    }

    // ANR
    oldValue = oldParams->get(IntelCameraParameters::KEY_ANR);
    newValue = newParams->get(IntelCameraParameters::KEY_ANR);
    if (newValue && oldValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        if (!strncmp(newValue, CameraParameters::TRUE, MAX_PARAM_VALUE_LENGTH))
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
    if (newVal.isEmpty() != true) {

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
    const char* oldValue = oldParams->get(CameraParameters::KEY_AUTO_EXPOSURE_LOCK);
    const char* newValue = newParams->get(CameraParameters::KEY_AUTO_EXPOSURE_LOCK);

    if (newValue && oldValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        bool ae_lock;

        if(!strncmp(newValue, CameraParameters::TRUE, strlen(CameraParameters::TRUE))) {
            ae_lock = true;
        } else  if(!strncmp(newValue, CameraParameters::FALSE, strlen(CameraParameters::FALSE))) {
            ae_lock = false;
        } else {
            LOGE("Invalid value received for %s: %s", CameraParameters::KEY_AUTO_EXPOSURE_LOCK, newValue);
            return INVALID_OPERATION;
        }

        status =  mAAA->setAeLock(ae_lock);
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_AUTO_EXPOSURE_LOCK, newValue);
        }
    }

    return status;
}

status_t ControlThread::processParamFlash(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    const char* oldValue = oldParams->get(CameraParameters::KEY_FLASH_MODE);
    const char* newValue = newParams->get(CameraParameters::KEY_FLASH_MODE);
    if (newValue && oldValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        FlashMode flash = CAM_AE_FLASH_MODE_AUTO;
        if(!strncmp(newValue, CameraParameters::FLASH_MODE_AUTO, strlen(CameraParameters::FLASH_MODE_AUTO)))
            flash = CAM_AE_FLASH_MODE_AUTO;
        else if(!strncmp(newValue, CameraParameters::FLASH_MODE_OFF, strlen(CameraParameters::FLASH_MODE_OFF)))
            flash = CAM_AE_FLASH_MODE_OFF;
        else if(!strncmp(newValue, CameraParameters::FLASH_MODE_ON, strlen(CameraParameters::FLASH_MODE_ON)))
            flash = CAM_AE_FLASH_MODE_ON;
        else if(!strncmp(newValue, CameraParameters::FLASH_MODE_TORCH, strlen(CameraParameters::FLASH_MODE_TORCH)))
            flash = CAM_AE_FLASH_MODE_TORCH;
        else if(!strncmp(newValue, IntelCameraParameters::FLASH_MODE_SLOW_SYNC, strlen(IntelCameraParameters::FLASH_MODE_SLOW_SYNC)))
            flash = CAM_AE_FLASH_MODE_SLOW_SYNC;
        else if(!strncmp(newValue, IntelCameraParameters::FLASH_MODE_DAY_SYNC, strlen(IntelCameraParameters::FLASH_MODE_DAY_SYNC)))
            flash = CAM_AE_FLASH_MODE_DAY_SYNC;

        if (flash == CAM_AE_FLASH_MODE_TORCH && mAAA->getAeFlashMode() != CAM_AE_FLASH_MODE_TORCH) {
            mISP->setTorch(TORCH_INTENSITY);
        }

        if (flash != CAM_AE_FLASH_MODE_TORCH && mAAA->getAeFlashMode() == CAM_AE_FLASH_MODE_TORCH) {
            mISP->setTorch(0);
        }

        status = mAAA->setAeFlashMode(flash);
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_FLASH_MODE, newValue);
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

    if (newVal.isEmpty() != true) {

        v4l2_colorfx effect = V4L2_COLORFX_NONE;
        if (newVal == CameraParameters::EFFECT_MONO)
            effect = V4L2_COLORFX_BW;
        else if (newVal == CameraParameters::EFFECT_NEGATIVE)
            effect = V4L2_COLORFX_NEGATIVE;
        else if (newVal == CameraParameters::EFFECT_SEPIA)
            effect = V4L2_COLORFX_SEPIA;
        else if (newVal == IntelCameraParameters::EFFECT_STILL_SKY_BLUE)
            effect = V4L2_COLORFX_SKY_BLUE;
        else if (newVal == IntelCameraParameters::EFFECT_STILL_GRASS_GREEN)
            effect = V4L2_COLORFX_GRASS_GREEN;
        else if (newVal == IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_MEDIUM)
            effect = V4L2_COLORFX_SKIN_WHITEN;

        // following two values need a explicit cast as the
        // definitions in hardware/intel/linux-2.6/include/linux/atomisp.h
        // have incorrect type (properly defined values are in videodev2.h)
        else if (newVal == IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_LOW)
            effect = (v4l2_colorfx)V4L2_COLORFX_SKIN_WHITEN_LOW;
        else if (newVal == IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_HIGH)
            effect = (v4l2_colorfx)V4L2_COLORFX_SKIN_WHITEN_HIGH;

        status = mISP->setColorEffect(effect);
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
    const char* oldBracket = oldParams->get(IntelCameraParameters::KEY_CAPTURE_BRACKET);
    const char* newBracket = newParams->get(IntelCameraParameters::KEY_CAPTURE_BRACKET);
    if (oldBracket && newBracket && strncmp(newBracket, oldBracket, MAX_PARAM_VALUE_LENGTH) != 0) {
        if(!strncmp(newBracket, "exposure", strlen("exposure"))) {
            mBracketing.mode = BRACKET_EXPOSURE;
        } else if(!strncmp(newBracket, "focus", strlen("focus"))) {
            mBracketing.mode = BRACKET_FOCUS;
        } else if(!strncmp(newBracket, "none", strlen("none"))) {
            mBracketing.mode = BRACKET_NONE;
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_CAPTURE_BRACKET, newBracket);
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_CAPTURE_BRACKET, newBracket);
        }
    }
    return status;
}

status_t ControlThread::processParamHDR(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // Check the HDR parameters
    const char* oldValue = oldParams->get(IntelCameraParameters::KEY_HDR_IMAGING);
    const char* newValue = newParams->get(IntelCameraParameters::KEY_HDR_IMAGING);
    if (oldValue && newValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        if(!strncmp(newValue, "on", strlen("on"))) {
            mHdr.enabled = true;
            mHdr.bracketMode = BRACKET_EXPOSURE;
            mHdr.bracketNum = DEFAULT_HDR_BRACKETING;
        } else if(!strncmp(newValue, "off", strlen("off"))) {
            mHdr.enabled = false;
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_HDR_IMAGING, newValue);
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_HDR_IMAGING, newValue);
        }
    }

    if (mHdr.enabled) {
        // Dependency parameters
        mBurstLength = mHdr.bracketNum;
        mBracketing.mode = mHdr.bracketMode;
    }

    oldValue = oldParams->get(IntelCameraParameters::KEY_HDR_SHARPENING);
    newValue = newParams->get(IntelCameraParameters::KEY_HDR_SHARPENING);
    if (oldValue && newValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        if(!strncmp(newValue, "normal", strlen("normal"))) {
            mHdr.sharpening = HdrImaging::NORMAL_SHARPENING;
        } else if(!strncmp(newValue, "strong", strlen("strong"))) {
            mHdr.sharpening = HdrImaging::STRONG_SHARPENING;
        } else if(!strncmp(newValue, "none", strlen("none"))) {
            mHdr.sharpening = HdrImaging::NO_SHARPENING;
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_HDR_SHARPENING, newValue);
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_HDR_SHARPENING, newValue);
        }
    }

    oldValue = oldParams->get(IntelCameraParameters::KEY_HDR_VIVIDNESS);
    newValue = newParams->get(IntelCameraParameters::KEY_HDR_VIVIDNESS);
    if (oldValue && newValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        if(!strncmp(newValue, "gaussian", strlen("gaussian"))) {
            mHdr.vividness = HdrImaging::GAUSSIAN_VIVIDNESS;
        } else if(!strncmp(newValue, "gamma", strlen("gamma"))) {
            mHdr.vividness = HdrImaging::GAMMA_VIVIDNESS;
        } else if(!strncmp(newValue, "none", strlen("none"))) {
            mHdr.vividness = HdrImaging::NO_VIVIDNESS;
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_HDR_VIVIDNESS, newValue);
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_HDR_VIVIDNESS, newValue);
        }
    }

    oldValue = oldParams->get(IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL);
    newValue = newParams->get(IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL);
    if (oldValue && newValue && strncmp(newValue, oldValue, MAX_PARAM_VALUE_LENGTH) != 0) {
        if(!strncmp(newValue, "on", strlen("on"))) {
            mHdr.appSaveOrig = mHdr.saveOrig = true;
            mHdr.appSaveOrigRequest = mHdr.saveOrigRequest = true;
        } else if(!strncmp(newValue, "off", strlen("off"))) {
            mHdr.appSaveOrig = mHdr.saveOrig = false;
            mHdr.appSaveOrigRequest = mHdr.saveOrigRequest = false;
        } else {
            LOGE("Invalid value received for %s: %s", IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL, newValue);
            status = BAD_VALUE;
        }
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL, newValue);
        }
    }

    return status;
}

status_t ControlThread::processParamSceneMode(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    const char* oldScene = oldParams->get(CameraParameters::KEY_SCENE_MODE);
    const char* newScene = newParams->get(CameraParameters::KEY_SCENE_MODE);
    if (newScene && oldScene && strncmp(newScene, oldScene, MAX_PARAM_VALUE_LENGTH) != 0) {
        SceneMode sceneMode = CAM_AE_SCENE_MODE_AUTO;
        if (!strncmp (newScene, CameraParameters::SCENE_MODE_PORTRAIT, strlen(CameraParameters::SCENE_MODE_PORTRAIT))) {
            sceneMode = CAM_AE_SCENE_MODE_PORTRAIT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            if (PlatformData::supportsBackFlash()) {
                newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_AUTO);
                newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "auto,off,on,torch");
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_AE_METERING_MODES, "auto,center");
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else if (!strncmp (newScene, CameraParameters::SCENE_MODE_SPORTS, strlen(CameraParameters::SCENE_MODE_SPORTS))) {
            sceneMode = CAM_AE_SCENE_MODE_SPORTS;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_INFINITY);
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "off");
                newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else if (!strncmp (newScene, CameraParameters::SCENE_MODE_LANDSCAPE, strlen(CameraParameters::SCENE_MODE_LANDSCAPE))) {
            sceneMode = CAM_AE_SCENE_MODE_LANDSCAPE;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_INFINITY);
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "off");
                newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_OUTDOOR);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else if (!strncmp (newScene, CameraParameters::SCENE_MODE_NIGHT, strlen(CameraParameters::SCENE_MODE_NIGHT))) {
            sceneMode = CAM_AE_SCENE_MODE_NIGHT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "off");
                newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::TRUE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "true");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::TRUE);
        } else if (!strncmp (newScene, CameraParameters::SCENE_MODE_NIGHT_PORTRAIT, strlen(CameraParameters::SCENE_MODE_NIGHT_PORTRAIT))) {
            sceneMode = CAM_AE_SCENE_MODE_NIGHT_PORTRAIT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "on");
                newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_ON);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::TRUE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "true");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::TRUE);
        } else if (!strncmp (newScene, CameraParameters::SCENE_MODE_FIREWORKS, strlen(CameraParameters::SCENE_MODE_FIREWORKS))) {
            sceneMode = CAM_AE_SCENE_MODE_FIREWORKS;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_INFINITY);
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
            if (PlatformData::supportsBackFlash()) {
                newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "off");
                newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else if (!strncmp (newScene, CameraParameters::SCENE_MODE_BARCODE, strlen(CameraParameters::SCENE_MODE_BARCODE))) {
            sceneMode = CAM_AE_SCENE_MODE_TEXT;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_MACRO);
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            if (PlatformData::supportsBackFlash()) {
                newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "auto,off,on,torch");
                newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_AUTO);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        } else {
            if (strncmp (newScene, CameraParameters::SCENE_MODE_AUTO, strlen(CameraParameters::SCENE_MODE_AUTO))) {
                LOG1("Unsupported %s: %s. Using AUTO!", CameraParameters::KEY_SCENE_MODE, newScene);
            }

            sceneMode = CAM_AE_SCENE_MODE_AUTO;
            newParams->set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
            newParams->set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
            newParams->set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, "off,50hz,60hz,auto");
            newParams->set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);
            if (PlatformData::supportsBackFlash()) {
                newParams->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "auto,off,on,torch");
                newParams->set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_AUTO);
            }
            newParams->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
            newParams->set(IntelCameraParameters::KEY_AE_METERING_MODE, IntelCameraParameters::AE_METERING_MODE_AUTO);
            newParams->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, IntelCameraParameters::BACK_LIGHT_COORECTION_OFF);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_XNR, "true,false");
            newParams->set(IntelCameraParameters::KEY_XNR, CameraParameters::FALSE);
            newParams->set(IntelCameraParameters::KEY_SUPPORTED_ANR, "true,false");
            newParams->set(IntelCameraParameters::KEY_ANR, CameraParameters::FALSE);
        }

        mAAA->setAeSceneMode(sceneMode);
        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_SCENE_MODE, newScene);
        }
        // If Intel params are not allowed,
        // we should update Intel params setting to HW, and remove them here.
        if (!mIntelParamsAllowed) {
            processParamBackLightingCorrectionMode(oldParams, newParams);
            processParamXNR_ANR(oldParams, newParams);
            newParams->remove(IntelCameraParameters::KEY_AWB_MAPPING_MODE);
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

bool ControlThread::verifyCameraWindow(const CameraWindow &win)
{
    if (win.x_right <= win.x_left ||
        win.y_bottom <= win.y_top)
        return false;
    if ( (win.y_top < -1000) || (win.y_top > 1000) ) return false;
    if ( (win.y_bottom < -1000) || (win.y_bottom > 1000) ) return false;
    if ( (win.x_right < -1000) || (win.x_right > 1000) ) return false;
    if ( (win.x_left < -1000) || (win.x_left > 1000) ) return false;
    if ( (win.weight < 1) || (win.weight > 1000) ) return false;
    return true;
}

void ControlThread::preSetCameraWindows(CameraWindow* focusWindows, size_t winCount)
{
    LOG1("@%s", __FUNCTION__);
    // Camera KEY_FOCUS_AREAS Coordinates range from -1000 to 1000.
    if (winCount > 0) {
        int width;
        int height;
        const int FOCUS_AREAS_X_OFFSET = 1000;
        const int FOCUS_AREAS_Y_OFFSET = 1000;
        const int FOCUS_AREAS_WIDTH = 2000;
        const int FOCUS_AREAS_HEIGHT = 2000;
        const int WINDOWS_TOTAL_WEIGHT = 16;
        mParameters.getPreviewSize(&width, &height);
        size_t windowsWeight = 0;
        for(size_t i = 0; i < winCount; i++){
            windowsWeight += focusWindows[i].weight;
        }
        if(!windowsWeight)
            windowsWeight = 1;

        size_t weight_sum = 0;
        for(size_t i =0; i < winCount; i++) {
            // skip all zero value
            focusWindows[i].x_left = (focusWindows[i].x_left + FOCUS_AREAS_X_OFFSET) * (width - 1) / FOCUS_AREAS_WIDTH;
            focusWindows[i].x_right = (focusWindows[i].x_right + FOCUS_AREAS_X_OFFSET) * (width - 1) / FOCUS_AREAS_WIDTH;
            focusWindows[i].y_top = (focusWindows[i].y_top + FOCUS_AREAS_Y_OFFSET) * (height - 1) / FOCUS_AREAS_HEIGHT;
            focusWindows[i].y_bottom = (focusWindows[i].y_bottom + FOCUS_AREAS_Y_OFFSET) * (height - 1) / FOCUS_AREAS_HEIGHT;
            focusWindows[i].weight = focusWindows[i].weight * WINDOWS_TOTAL_WEIGHT / windowsWeight;
            weight_sum += focusWindows[i].weight;
            LOG1("Preset camera window %d: (%d,%d,%d,%d,%d)",
                    i,
                    focusWindows[i].x_left,
                    focusWindows[i].y_top,
                    focusWindows[i].x_right,
                    focusWindows[i].y_bottom,
                    focusWindows[i].weight);
        }
        //weight sum value should be WINDOWS_TOTAL_WEIGHT
        focusWindows[winCount-1].weight += WINDOWS_TOTAL_WEIGHT - weight_sum;
    }
}

status_t ControlThread::processParamFocusMode(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    const char* oldFocus = oldParams->get(CameraParameters::KEY_FOCUS_MODE);
    const char* newFocus = newParams->get(CameraParameters::KEY_FOCUS_MODE);
    if (newFocus && oldFocus && strncmp(newFocus, oldFocus, MAX_PARAM_VALUE_LENGTH) != 0) {
        AfMode afMode = CAM_AF_MODE_AUTO;

        if(!strncmp(newFocus, CameraParameters::FOCUS_MODE_AUTO, strlen(CameraParameters::FOCUS_MODE_AUTO))) {
            afMode = CAM_AF_MODE_AUTO;
        } else if(!strncmp(newFocus, CameraParameters::FOCUS_MODE_INFINITY, strlen(CameraParameters::FOCUS_MODE_INFINITY))) {
            afMode = CAM_AF_MODE_INFINITY;
        } else if(!strncmp(newFocus, CameraParameters::FOCUS_MODE_MACRO, strlen(CameraParameters::FOCUS_MODE_MACRO))) {
            afMode = CAM_AF_MODE_MACRO;
        } else if(!strncmp(newFocus, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO, strlen(CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO))) {
            afMode = CAM_AF_MODE_AUTO;
        } else {
            afMode = CAM_AF_MODE_MANUAL;
        }

        status = mAAA->setAfEnabled(true);
        if (status == NO_ERROR) {
            status = mAAA->setAfMode(afMode);
        }
        if (status == NO_ERROR) {
            mPublicAfMode = afMode;
            LOG1("Changed: %s -> %s", CameraParameters::KEY_FOCUS_MODE, newFocus);
        }
    }

    if (!newFocus)
        return status;

    if ((!strncmp(newFocus, CameraParameters::FOCUS_MODE_AUTO, strlen(CameraParameters::FOCUS_MODE_AUTO))) ||
            (!strncmp(newFocus, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO, strlen(CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO))) ||
            (!strncmp(newFocus, CameraParameters::FOCUS_MODE_MACRO, strlen(CameraParameters::FOCUS_MODE_MACRO)))) {

        // By default we will use auto or macro mode
        AfMode newAfMode = CAM_AF_MODE_AUTO;
        if (!strncmp(newFocus, CameraParameters::FOCUS_MODE_MACRO, strlen(CameraParameters::FOCUS_MODE_MACRO)))
            newAfMode = CAM_AF_MODE_MACRO;

        // See if any focus windows are set
        const char *pFocusWindows = newParams->get(CameraParameters::KEY_FOCUS_AREAS);
        const size_t maxWindows = mAAA->getAfMaxNumWindows();
        size_t winCount = 0;
        CameraWindow focusWindows[maxWindows];

        if (!mFaceDetectionActive && pFocusWindows && (maxWindows > 0) && (strlen(pFocusWindows) > 0)) {
            LOG1("Scanning AF windows from params: %s", pFocusWindows);
            const char *argTail = pFocusWindows;
            while (argTail && winCount < maxWindows) {
                // String format: "(topleftx,toplefty,bottomrightx,bottomrighty,weight),(...)"
                int i = sscanf(argTail, "(%d,%d,%d,%d,%d)",
                        &focusWindows[winCount].x_left,
                        &focusWindows[winCount].y_top,
                        &focusWindows[winCount].x_right,
                        &focusWindows[winCount].y_bottom,
                        &focusWindows[winCount].weight);
                if (i != 5)
                    break;
                bool verified = verifyCameraWindow(focusWindows[winCount]);
                LOG1("\tWindow %d (%d,%d,%d,%d,%d) [%s]",
                        winCount,
                        focusWindows[winCount].x_left,
                        focusWindows[winCount].y_top,
                        focusWindows[winCount].x_right,
                        focusWindows[winCount].y_bottom,
                        focusWindows[winCount].weight,
                        (verified)?"GOOD":"IGNORED");
                argTail = strchr(argTail + 1, '(');
                if (verified) {
                    winCount++;
                } else {
                    LOGW("Ignoring invalid focus area: (%d,%d,%d,%d,%d)",
                            focusWindows[winCount].x_left,
                            focusWindows[winCount].y_top,
                            focusWindows[winCount].x_right,
                            focusWindows[winCount].y_bottom,
                            focusWindows[winCount].weight);
                }
            }
            // Looks like focus window(s) were set, so should use touch focus mode
            if (winCount > 0) {
                newAfMode = CAM_AF_MODE_TOUCH;
            }
        }

        // See if we have to change the actual mode (it could be correct already)
        AfMode curAfMode = mAAA->getAfMode();
        if (curAfMode != newAfMode) {
            mPublicAfMode = newAfMode;
            mAAA->setAfMode(newAfMode);
        }

        // If in touch mode, we set the focus windows now
        if (newAfMode == CAM_AF_MODE_TOUCH) {
            preSetCameraWindows(focusWindows, winCount);
            if (mAAA->setAfWindows(focusWindows, winCount) != NO_ERROR) {
                // If focus windows couldn't be set, auto mode is used
                // (AfSetWindowMulti has its own safety checks for coordinates)
                LOGE("Could not set AF windows. Resseting the AF back to %d", curAfMode);
                mAAA->setAfMode(curAfMode);
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
    const size_t maxWindows = mAAA->getAfMaxNumWindows();
    CameraWindow meteringWindows[maxWindows];
    const char *pMeteringWindows = newParams->get(CameraParameters::KEY_METERING_AREAS);
    if (pMeteringWindows && (maxWindows > 0) && (strlen(pMeteringWindows) > 0)) {
        LOG1("Scanning AE metering from params: %s", pMeteringWindows);
        const char *argTail = pMeteringWindows;
        size_t winCount = 0;
        while (argTail && winCount < maxWindows) {
            // String format: "(topleftx,toplefty,bottomrightx,bottomrighty,weight),(...)"
            int len = sscanf(argTail, "(%d,%d,%d,%d,%d)",
                    &meteringWindows[winCount].x_left,
                    &meteringWindows[winCount].y_top,
                    &meteringWindows[winCount].x_right,
                    &meteringWindows[winCount].y_bottom,
                    &meteringWindows[winCount].weight);
            if (len != 5)
                break;
            bool verified = verifyCameraWindow(meteringWindows[winCount]);
            LOG1("\tWindow %d (%d,%d,%d,%d,%d) [%s]",
                    winCount,
                    meteringWindows[winCount].x_left,
                    meteringWindows[winCount].y_top,
                    meteringWindows[winCount].x_right,
                    meteringWindows[winCount].y_bottom,
                    meteringWindows[winCount].weight,
                    (verified)?"GOOD":"IGNORED");
            argTail = strchr(argTail + 1, '(');
            if (verified) {
                winCount++;
            } else {
                LOGW("Ignoring invalid metering area: (%d,%d,%d,%d,%d)",
                        meteringWindows[winCount].x_left,
                        meteringWindows[winCount].y_top,
                        meteringWindows[winCount].x_right,
                        meteringWindows[winCount].y_bottom,
                        meteringWindows[winCount].weight);
            }
        }
        // Looks like metering window(s) were set
        if (winCount > 0) {
            preSetCameraWindows(meteringWindows, winCount);
            if (mAAA->setAfWindows(meteringWindows, winCount) != NO_ERROR) {
                mAAA->setAeMeteringMode(CAM_AE_METERING_MODE_SPOT);
            }
        }
    }
    return status;
}

status_t ControlThread::processParamExposureCompensation(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    const char* oldEv = oldParams->get(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    const char* newEv = newParams->get(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    if (newEv && oldEv && strncmp(newEv, oldEv, MAX_PARAM_VALUE_LENGTH) != 0) {
        int exposure = newParams->getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
        float comp_step = newParams->getFloat(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP);
        status = mAAA->setEv(exposure * comp_step);
        float ev = 0;
        mAAA->getEv(&ev);
        LOGD("exposure compensation to \"%s\" (%d), ev value %f, res %d",
             newEv, exposure, ev, status);
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
    if (newVal.isEmpty() != true) {
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
        mPublicAeMode = ae_mode;
        mAAA->setAeMode(ae_mode);
        LOGD("Changed ae mode to \"%s\" (%d)", newVal.string(), ae_mode);
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
    if (newVal.isEmpty() != true) {
        MeteringMode mode (CAM_AE_METERING_MODE_AUTO);

        if (newVal == "auto") {
            mode = CAM_AE_METERING_MODE_AUTO;
        } else if(newVal == "center") {
            mode = CAM_AE_METERING_MODE_CENTER;
        }

        mAAA->setAeMeteringMode(mode);
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
    if (newVal.isEmpty() != true &&
        mAAA->getAeMode() == CAM_AE_MODE_MANUAL) {
        // note: value format is 'iso-NNN'
        const size_t iso_prefix_len = 4;
        if (newVal.length() > iso_prefix_len) {
            const char* isostr = newVal.string() + iso_prefix_len;
            int iso = atoi(isostr);
            mAAA->setManualIso(iso);
            LOGD("Changed manual iso to \"%s\" (%d)", newVal.string(), iso);
        }
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
    if (newVal.isEmpty() != true &&
        (mAAA->getAeMode() == CAM_AE_MODE_MANUAL ||
         (mAAA->getAeMode() == CAM_AE_MODE_SHUTTER_PRIORITY))) {

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
            mAAA->setManualShutter(shutter);
            LOGD("Changed shutter to \"%s\" (%f)", newVal.string(), shutter);
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
    if (newVal.isEmpty() != true) {
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

status_t ControlThread::processParamWhiteBalance(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    const char* oldWb = oldParams->get(CameraParameters::KEY_WHITE_BALANCE);
    const char* newWb = newParams->get(CameraParameters::KEY_WHITE_BALANCE);
    if (newWb && oldWb && strncmp(newWb, oldWb, MAX_PARAM_VALUE_LENGTH) != 0) {
        AwbMode wbMode = CAM_AWB_MODE_AUTO;
        // TODO: once available, use the definitions in Intel
        //       parameter namespace, see UMG BZ26264
        const char* PARAM_MANUAL = "manual";

        if(!strncmp(newWb, CameraParameters::WHITE_BALANCE_AUTO, strlen(CameraParameters::WHITE_BALANCE_AUTO))) {
            wbMode = CAM_AWB_MODE_AUTO;
        } else if(!strncmp(newWb, CameraParameters::WHITE_BALANCE_INCANDESCENT, strlen(CameraParameters::WHITE_BALANCE_INCANDESCENT))) {
            wbMode = CAM_AWB_MODE_WARM_INCANDESCENT;
        } else if(!strncmp(newWb, CameraParameters::WHITE_BALANCE_FLUORESCENT, strlen(CameraParameters::WHITE_BALANCE_FLUORESCENT))) {
            wbMode = CAM_AWB_MODE_FLUORESCENT;
        } else if(!strncmp(newWb, CameraParameters::WHITE_BALANCE_WARM_FLUORESCENT, strlen(CameraParameters::WHITE_BALANCE_WARM_FLUORESCENT))) {
            wbMode = CAM_AWB_MODE_WARM_FLUORESCENT;
        } else if(!strncmp(newWb, CameraParameters::WHITE_BALANCE_DAYLIGHT, strlen(CameraParameters::WHITE_BALANCE_DAYLIGHT))) {
            wbMode = CAM_AWB_MODE_DAYLIGHT;
        } else if(!strncmp(newWb, CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT, strlen(CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT))) {
            wbMode = CAM_AWB_MODE_CLOUDY;
        } else if(!strncmp(newWb, CameraParameters::WHITE_BALANCE_TWILIGHT, strlen(CameraParameters::WHITE_BALANCE_TWILIGHT))) {
            wbMode = CAM_AWB_MODE_SUNSET;
        } else if(!strncmp(newWb, CameraParameters::WHITE_BALANCE_SHADE, strlen(CameraParameters::WHITE_BALANCE_SHADE))) {
            wbMode = CAM_AWB_MODE_SHADOW;
        } else if(!strncmp(newWb, PARAM_MANUAL, strlen(PARAM_MANUAL))) {
            wbMode = CAM_AWB_MODE_MANUAL_INPUT;
        }

        status = mAAA->setAwbMode(wbMode);

        if (status == NO_ERROR) {
            LOG1("Changed: %s -> %s", CameraParameters::KEY_WHITE_BALANCE, newWb);
        }
    }
    return status;
}

status_t ControlThread::processStaticParameters(const CameraParameters *oldParams,
        CameraParameters *newParams)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    bool previewFormatChanged = false;
    float previewAspectRatio = 0.0f;
    float videoAspectRatio = 0.0f;
    Vector<Size> sizes;
    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;

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
    } else {
        previewAspectRatio = 1.0 * oldWidth / oldHeight;
        LOG1("Preview size/format is unchanged: old=%dx%d %s; ratio=%.3f",
                oldWidth, oldHeight, v4l2Fmt2Str(oldFormat),
                previewAspectRatio);
    }

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
        if (fabsf(videoAspectRatio - previewAspectRatio) > ASPECT_TOLERANCE) {
            LOG1("Our video (%dx%d) aspect ratio does not match preview (%dx%d) aspect ratio!",
                    newWidth, newHeight,
                    previewWidth, previewHeight);
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

    /**
     *  Workaround: The camera firmware doesn't support preview dimensions that
     * are bigger than video dimensions. If a single preview dimension is larger
     * than the video dimension then the FW will downscale the preview resolution
     * to that of the video resolution.
     * Checking if preview is still  bigger than video, this is not supported by the ISP
     */
    if(videoMode) {
        newParams->getPreviewSize(&previewWidth, &previewHeight);
        newParams->getVideoSize(&newWidth, &newHeight);
        if((previewWidth*previewHeight) > (newWidth*newHeight)) {

            newParams->setPreviewSize(newWidth, newHeight);
            LOGW("Warning: Video dimension(s) is smaller than preview dimension(s). "
                 "Overriding preview resolution to video resolution [%d, %d] --> [%d, %d]",
                 previewWidth, previewHeight, newWidth, newHeight);
        }
    }

    // if preview is running and static params have changed, then we need
    // to stop, reconfigure, and restart the isp and all threads.
    // Update the current params before we re-start
    if (previewFormatChanged) {
        mParameters = *newParams;
        switch (mState) {
        case STATE_PREVIEW_VIDEO:
        case STATE_PREVIEW_STILL:
            status = restartPreview(videoMode);
            break;
        case STATE_STOPPED:
            break;
        default:
            LOGE("formats can only be changed while in preview or stop states");
            break;
        };
    }

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
    AtomMode mode = videoMode ? MODE_VIDEO : MODE_PREVIEW;
    mISP->getZoomRatios(mode, &mParameters);
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

status_t ControlThread::handleMessageSetParameters(MessageSetParameters *msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    CameraParameters newParams;
    CameraParameters oldParams = mParameters;
    CameraParamsLogger newParamLogger (msg->params);
    CameraParamsLogger oldParamLogger (mParameters.flatten().string());

    String8 str_params(msg->params);
    newParams.unflatten(str_params);

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

    if (mState == STATE_CAPTURE) {
        int newWidth, newHeight;
        int oldWidth, oldHeight;
        newParams.getPictureSize(&newWidth, &newHeight);
        oldParams.getPictureSize(&oldWidth, &oldHeight);
        // Check the picture size to see if changed. If changed, we need to stop the capture
        if (newWidth != oldWidth || newHeight != oldHeight) {
            LOG1("Picture size has changed! Requesting stopCapture, in order to handle setParameters");
            /*
             * We need to put a message to stop the capture since stopCapture can lead to a callback to
             * CameraService, which can in turn lead to a dead-lock if it's done inside the thread that
             * is processing a call from CameraService (like this one for example).
             */
            Message msg;
            msg.id = MESSAGE_ID_STOP_CAPTURE;
            mMessageQueue.send(&msg);
        }
    }

    status = validateParameters(&newParams);
    if (status != NO_ERROR)
        goto exit;

    mParameters = newParams;

    // Take care of parameters that need to be set while the ISP is stopped
    status = processStaticParameters(&oldParams, &newParams);
    if (status != NO_ERROR)
        goto exit;

    // Take care of parameters that can be set while ISP is running
    status = processDynamicParameters(&oldParams, &newParams);
    if (status != NO_ERROR)
        goto exit;

    mParameters = newParams;
    updateParameterCache();

exit:
    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_SET_PARAMETERS, status);
    return status;
}

status_t ControlThread::handleMessageGetParameters(MessageGetParameters *msg)
{
    status_t status = BAD_VALUE;

    if (msg->params) {
        // let app know if we support zoom in the preview mode indicated
        bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;
        AtomMode mode = videoMode ? MODE_VIDEO : MODE_PREVIEW;
        mISP->getZoomRatios(mode, &mParameters);
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
        //[FIXME]   comment SMART SCENE DETECTION
#if 0
    case CAMERA_CMD_START_SCENE_DETECTION:
        status = startSmartSceneDetection();
        break;
    case CAMERA_CMD_STOP_SCENE_DETECTION:
        status = stopSmartSceneDetection();
        break;
#endif
    case CAMERA_CMD_ENABLE_INTEL_PARAMETERS:
        status = enableIntelParameters();
        mMessageQueue.reply(MESSAGE_ID_COMMAND, status);
        break;
    default:
        break;
    }

    return status;
}

status_t ControlThread::handleMessageStopCapture()
{
    status_t status = NO_ERROR;
    status = stopCapture();
    if (status != NO_ERROR) {
        LOGE("Error stopping ISP from capture mode!");
    }
    return status;
}

status_t ControlThread::handleMessageConfigureFileInject(MessageConfigureFileInject *c)
{
    status_t status = NO_ERROR;
    mISP->configureFileInject(c->fileName, c->width, c->height, c->format, c->bayerOrder);
    mMessageQueue.reply(MESSAGE_ID_CONFIGURE_FILE_INJECT, status);
    return status;
}

status_t ControlThread::handleMessageLoadFirmware(MessageLoadFirmware* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if(mState == STATE_PREVIEW_STILL || mState == STATE_PREVIEW_VIDEO) {
        mISP->loadAccFirmware(msg->fwData, msg->size, msg->fwHandle);
    } else {
        status = INVALID_OPERATION;
        LOGW("@%s: invalid state to load FW", __FUNCTION__);
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_LOAD_FIRMWARE, status);
    return status;
}

status_t ControlThread::handleMessageUnloadFirmware(MessageUnloadFirmware* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if(mState == STATE_PREVIEW_STILL || mState == STATE_PREVIEW_VIDEO) {
        mISP->unloadAccFirmware(msg->fwHandle);
    } else {
        status = INVALID_OPERATION;
        LOGW("@%s: invalid state to load FW", __FUNCTION__);
    }
    return status;

}

status_t ControlThread::handleMessageSetFirmwareArgument(MessageSetFwArg* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if(mState == STATE_PREVIEW_STILL || mState == STATE_PREVIEW_VIDEO) {

       status = mISP->setFirmwareArgument(msg->fwHandle, msg->argIndex,
                                          msg->value, msg->size);
    } else {
        status = INVALID_OPERATION;
        LOGW("@%s: invalid state to load FW", __FUNCTION__);
    }

    mMessageQueue.reply(MESSAGE_ID_SET_FIRMWARE_ARGUMENT, status);
    return status;
}

status_t ControlThread::handleMessageUnsetFirmwareArgument(MessageSetFwArg* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if(mState == STATE_PREVIEW_STILL || mState == STATE_PREVIEW_VIDEO) {
       status = mISP->unsetFirmwareArgument(msg->fwHandle, msg->argIndex);
    } else {
        status = INVALID_OPERATION;
        LOGW("@%s: invalid state to load FW", __FUNCTION__);
    }

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
    mAAA->setSmartSceneDetection(true);
    return NO_ERROR;
}

status_t ControlThread::stopSmartSceneDetection()
{
    LOG2("@%s", __FUNCTION__);
    if (mState == STATE_STOPPED || !mAAA->getSmartSceneDetection()) {
        return INVALID_OPERATION;
    }
    mAAA->setSmartSceneDetection(false);
    return NO_ERROR;
}

status_t ControlThread::handleMessageStoreMetaDataInBuffers(MessageStoreMetaDataInBuffers *msg)
{
    status_t status = NO_ERROR;
    //Prohibit to enable metadata mode if state of HAL isn't equal STATE_STOPPED and STATE_PREVIEW_VIDEO
    if (mState != STATE_STOPPED && mState != STATE_PREVIEW_VIDEO)
        return BAD_VALUE;

    mStoreMetaDataInBuffers = msg->enabled;
    status = mISP->storeMetaDataInBuffers(msg->enabled);
    if(status == NO_ERROR)
        status = mCallbacks->storeMetaDataInBuffers(msg->enabled);

    mMessageQueue.reply(MESSAGE_ID_STORE_METADATA_IN_BUFFER, status);
    return status;
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
    if( !mFaceDetectionActive )
        return NO_ERROR;
    mFaceDetectionActive = false;
    disableMsgType(CAMERA_MSG_PREVIEW_METADATA);
    if (mPostProcThread != 0) {
        mPostProcThread->stopFaceDetection(wait);
        return NO_ERROR;
    } else{
        return INVALID_OPERATION;
    }
}

status_t ControlThread::enableIntelParameters()
{
    // intel parameters support more effects
    // so use supported effects list stored in mIntelParameters.
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
        case MESSAGE_ID_TAKE_PICTURE:
            status = handleMessageTakePicture();
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

        case MESSAGE_ID_STOP_CAPTURE:
            status = handleMessageStopCapture();
            break;

        case MESSAGE_ID_CONFIGURE_FILE_INJECT:
            status = handleMessageConfigureFileInject(&msg.data.configureFileInject);
            break;

        case MESSAGE_ID_SET_PREVIEW_WINDOW:
            status = handleMessageSetPreviewWindow(&msg.data.previewWin);
            break;

        case MESSAGE_ID_LOAD_FIRMWARE:
            status = handleMessageLoadFirmware(&msg.data.loadFW);
            break;

        case MESSAGE_ID_UNLOAD_FIRMWARE:
            status = handleMessageUnloadFirmware(&msg.data.unloadFW);
            break;

        case MESSAGE_ID_SET_FIRMWARE_ARGUMENT:
            status = handleMessageSetFirmwareArgument(&msg.data.setFwArg);
            break;

        case MESSAGE_ID_UNSET_FIRMWARE_ARGUMENT:
            status = handleMessageUnsetFirmwareArgument(&msg.data.setFwArg);
            break;

        case MESSAGE_ID_STORE_METADATA_IN_BUFFER:
            status = handleMessageStoreMetaDataInBuffers(&msg.data.storeMetaDataInBuffers);
            break;

        case MESSAGE_ID_SCENE_DETECTED:
            status = handleMessageSceneDetected(&msg.data.sceneDetected);
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

    status = mISP->getPreviewFrame(&buff);
    if (status == NO_ERROR) {
        if (mState == STATE_PREVIEW_VIDEO || mState == STATE_RECORDING) {
            mCoupledBuffers[buff.id].previewBuff = buff;
            mCoupledBuffers[buff.id].previewBuffReturned = false;
        }
        if (mAAA->is3ASupported()) {
            status = m3AThread->newFrame(buff.capture_timestamp);
            if (status != NO_ERROR)
                LOGW("Error notifying new frame to 3A thread!");
        }
        status = mPreviewThread->preview(&buff);
        if (status != NO_ERROR)
            LOGE("Error sending buffer to preview thread");
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

    status = mISP->getRecordingFrame(&buff, &timestamp);
    if (status == NO_ERROR) {
        mCoupledBuffers[buff.id].recordingBuff = buff;
        mCoupledBuffers[buff.id].recordingBuffReturned = false;
        mLastRecordingBuffIndex = buff.id;
        // See if recording has started.
        // If it has, process the buffer
        // If it hasn't, return the buffer to the driver
        if (mState == STATE_RECORDING) {
            mVideoThread->video(&buff, timestamp);
        } else {
            mCoupledBuffers[buff.id].recordingBuffReturned = true;
        }
    } else {
        LOGE("Error: getting recording from isp\n");
    }

    return status;
}

bool ControlThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning) {

        switch (mState) {

        case STATE_STOPPED:
            LOG2("In STATE_STOPPED...");
            // in the stop state all we do is wait for messages
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
                    (mBurstLength <= 1 || (mBurstLength > 1 && mBurstCaptureNum < mBurstLength))) {
                    status = handleMessageTakePicture(false);
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
                // make sure ISP has data before we ask for some
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
