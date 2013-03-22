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
#define LOG_TAG "Camera_PreviewThread"

#include "PreviewThread.h"
#include "LogHelper.h"
#include "DebugFrameRate.h"
#include "Callbacks.h"
#include "CallbacksThread.h"
#include "ColorConverter.h"
#ifndef GRAPHIC_IS_GEN
#include <hal_public.h>
#endif
#include <gui/Surface.h>
#include "PerformanceTraces.h"
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>
#include "AtomCommon.h"
#include "nv12rotation.h"

namespace android {

PreviewThread::PreviewThread() :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PreviewThread", (int) MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mState(STATE_STOPPED)
    ,mSetFPS(30)
    ,mSensorFPS(30.0f)
    ,mLastFrameTs(0)
    ,mFramesDone(0)
    ,mCallbacksThread(CallbacksThread::getInstance())
    ,mPreviewWindow(NULL)
    ,mPreviewBuf(AtomBufferFactory::createAtomBuffer(ATOM_BUFFER_PREVIEW))
    ,mCallbacks(Callbacks::getInstance())
    ,mBuffersInWindow(0)
    ,mNumOfPreviewBuffers(0)
    ,mFetchDone(false)
    ,mDebugFPS(new DebugFrameRate())
    ,mPreviewWidth(640)
    ,mPreviewHeight(480)
    ,mPreviewStride(640)
    ,mPreviewFormat(V4L2_PIX_FMT_NV21)
    ,mOverlayEnabled(false)
    ,mRotation(0)
{
    LOG1("@%s", __FUNCTION__);
    mPreviewBuffers.setCapacity(MAX_NUMBER_PREVIEW_GFX_BUFFERS);
    mPreviewInClient.setCapacity(MAX_NUMBER_PREVIEW_GFX_BUFFERS);
}

PreviewThread::~PreviewThread()
{
    LOG1("@%s", __FUNCTION__);
    mDebugFPS.clear();
    freeGfxPreviewBuffers();
}

status_t PreviewThread::setCallback(ICallbackPreview *cb, ICallbackPreview::CallbackType t)
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_CALLBACK;
    msg.data.setCallback.icallback = cb;
    msg.data.setCallback.type = t;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::handleMessageSetCallback(MessageSetCallback *msg)
{
    CallbackVector *cbVector =
        (msg->type == ICallbackPreview::INPUT
      || msg->type == ICallbackPreview::INPUT_ONCE)?
         &mInputBufferCb : &mOutputBufferCb;

    CallbackVector::iterator it = cbVector->begin();
    for (;it != cbVector->end(); ++it) {
        if (it->value == msg->icallback)
            return ALREADY_EXISTS;
        if (msg->type == ICallbackPreview::OUTPUT_WITH_DATA &&
            it->key == ICallbackPreview::OUTPUT_WITH_DATA) {
            return ALREADY_EXISTS;
        }
    }

    cbVector->push(callback_pair_t(msg->type, msg->icallback));
    return NO_ERROR;
}

void PreviewThread::inputBufferCallback()
{
    if (mInputBufferCb.empty())
        return;
    Vector<CallbackVector::iterator> toDrop;
    CallbackVector::iterator it = mInputBufferCb.begin();
    for (;it != mInputBufferCb.end(); ++it) {
        it->value->previewBufferCallback(NULL, it->key);
        if (it->key == ICallbackPreview::INPUT_ONCE)
            toDrop.push(it);
    }
    while (!toDrop.empty()) {
        mInputBufferCb.erase(toDrop.top());
        toDrop.pop();
    }
}

bool PreviewThread::outputBufferCallback(AtomBuffer *buff)
{
    bool ownership_passed = false;
    if (mOutputBufferCb.empty())
        return ownership_passed;
    Vector<CallbackVector::iterator> toDrop;
    CallbackVector::iterator it = mOutputBufferCb.begin();
    for (;it != mOutputBufferCb.end(); ++it) {
        if (it->key == ICallbackPreview::OUTPUT_WITH_DATA) {
            ownership_passed = true;
            it->value->previewBufferCallback(buff, it->key);
        } else {
            it->value->previewBufferCallback(buff, it->key);
        }
        if (it->key == ICallbackPreview::OUTPUT_ONCE)
            toDrop.push(it);
    }
    while (!toDrop.empty()) {
        mInputBufferCb.erase(toDrop.top());
        toDrop.pop();
    }
    return ownership_passed;
}

status_t PreviewThread::enableOverlay(bool set, int rotation)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if ((mState != STATE_STOPPED) && (mState != STATE_NO_WINDOW)) {
        LOGE("Cannot set overlay once Preview is configured");
        return INVALID_OPERATION;
    }
    mOverlayEnabled = set;
    mRotation = rotation;

    return status;
}

void PreviewThread::getDefaultParameters(CameraParameters *params)
{
    LOG2("@%s", __FUNCTION__);
    if (!params) {
        LOGE("params is null!");
        return;
    }

    /**
     * PREVIEW
     */
    params->setPreviewFormat(cameraParametersFormat(mPreviewFormat));

    char previewFormats[100] = {0};
    if (snprintf(previewFormats, sizeof(previewFormats), "%s,%s",
                 CameraParameters::PIXEL_FORMAT_YUV420SP,
                 CameraParameters::PIXEL_FORMAT_YUV420P) < 0) {
        LOGE("Could not generate %s string: %s", CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, strerror(errno));
        return;
    }
    else {
        LOG1("preview format %s\n", previewFormats);
    }
    params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, previewFormats);

}

status_t PreviewThread::setFramerate(int fps)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_FRAMERATE;
    msg.data.framerate.fps = fps;
    mMessageQueue.send(&msg);
    return NO_ERROR;
}

status_t PreviewThread::handleSetFramerate(MessageSetFramerate *msg)
{
    LOG1("@%s", __FUNCTION__);
    mSetFPS = msg->fps;
    return OK;
}

status_t PreviewThread::setSensorFramerate(float fps)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_SENSOR_FRAMERATE;
    msg.data.sensorFramerate.fps = fps;
    mMessageQueue.send(&msg);
    return NO_ERROR;
}

status_t PreviewThread::handleSetSensorFramerate(MessageSetSensorFramerate *msg)
{
    LOG1("@%s", __FUNCTION__);
    mSensorFPS = msg->fps;
    return OK;
}

/**
 * This function implements the frame skip algorithm.
 * - If user requests half of sensor fps, drop every even frame
 * - If user requests third of sensor fps, drop two frames every three frames
 * @returns true: skip,  false: not skip
 */
bool PreviewThread::checkSkipFrame(int frameNum)
{
    if (fabs(mSensorFPS / mSetFPS - 2) < 0.1f && (frameNum % 2 == 0)) {
        LOG2("Preview FPS: %d. Skipping frame num: %d", mSetFPS, frameNum);
        return true;
    }

    if (fabs(mSensorFPS / mSetFPS - 3) < 0.1f && (frameNum % 3 != 0)) {
        LOG2("Preview FPS: %d. Skipping frame num: %d", mSetFPS, frameNum);
        return true;
    }

    // TODO skipping support for 25fps sensor framerate

    return false;
}


status_t PreviewThread::setPreviewWindow(struct preview_stream_ops *window)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_PREVIEW_WINDOW;
    msg.data.setPreviewWindow.window = window;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::setPreviewConfig(int preview_width, int preview_height, int preview_stride,
                                         int preview_format, int buffer_count)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_PREVIEW_CONFIG;
    msg.data.setPreviewConfig.width = preview_width;
    msg.data.setPreviewConfig.height = preview_height;
    msg.data.setPreviewConfig.stride = preview_stride;
    msg.data.setPreviewConfig.format = preview_format;
    msg.data.setPreviewConfig.bufferCount = buffer_count;
    setState(STATE_CONFIGURED);
    return mMessageQueue.send(&msg);
}

/**
 * Retrieve the GFx Preview buffers
 *
 * This is done sending a synchronous message to make sure
 * that the previewThread has processed all previous messages
 */
status_t PreviewThread::fetchPreviewBuffers(AtomBuffer **pvBufs, int *count)
{
    LOG1("@%s", __FUNCTION__);

    Message msg;
    msg.id = MESSAGE_ID_FETCH_PREVIEW_BUFS;

    status_t status;
    status = mMessageQueue.send(&msg, MESSAGE_ID_FETCH_PREVIEW_BUFS);

    *count = mPreviewBuffers.size();
    if(*count != 0) {
        *pvBufs = mPreviewBuffers.editArray();
    } else {
        *pvBufs = NULL;
    }

    LOG1("@%s: got [%d] buffers @ %p", __FUNCTION__, *count, *pvBufs);
    return status;
}

/**
 * Returns the GFx Preview buffers to the window
 * There is no need for parameters since the PreviewThread
 * keeps track of the buffers already
 *
 */
status_t PreviewThread::returnPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);

    Message msg;
    msg.id = MESSAGE_ID_RETURN_PREVIEW_BUFS;
    return mMessageQueue.send(&msg, MESSAGE_ID_RETURN_PREVIEW_BUFS);
}

status_t PreviewThread::preview(AtomBuffer *buff)
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_PREVIEW;
    msg.data.preview.buff = *buff;
    return mMessageQueue.send(&msg);
}

/**
 * override for IAtomIspObserver::atomIspNotify()
 *
 * PreviewThread gets attached to receive preview stream here.
 *
 * We decide wether to pass buffers further or not
 *
 * Skip frame request for target video fps is also checked here,
 * since we want to output the same fps to display and video.
 * ControlThread is currently observing the same event, so we
 * pass the skip information within FrameBufferMessage::status.
 */
bool PreviewThread::atomIspNotify(IAtomIspObserver::Message *msg, const ObserverState state)
{
    LOG2("@%s", __FUNCTION__);
    if (!msg) {
        LOG1("Received observer state change");
        // We are currently not receiving MESSAGE_ID_END_OF_STREAM when stream
        // stops. Observer gets paused when device is about to be stopped and
        // after pausing, we no longer receive new frames for the same session.
        // Reset frame counter based on any observer state change
        mFramesDone = 0;
        return false;
    }

    AtomBuffer *buff = &msg->data.frameBuffer.buff;
    if (msg->id == MESSAGE_ID_FRAME) {
        if (checkSkipFrame(buff->frameCounter)) {
            buff->status = FRAME_STATUS_SKIPPED;
            buff->owner->returnBuffer(buff);
        } else if(buff->status == FRAME_STATUS_CORRUPTED) {
            buff->owner->returnBuffer(buff);
        } else {
            PerformanceTraces::FaceLock::getCurFrameNum(buff->frameCounter);
            preview(buff);
        }
    } else {
        LOG1("Received unexpected notify message id %d!", msg->id);
    }

    return false;
}

status_t PreviewThread::postview(AtomBuffer *buff)
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_POSTVIEW;
    msg.data.preview.buff = *buff;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::flushBuffers()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FLUSH;
    mMessageQueue.remove(MESSAGE_ID_PREVIEW);
    mMessageQueue.remove(MESSAGE_ID_POSTVIEW);
    return mMessageQueue.send(&msg, MESSAGE_ID_FLUSH);
}

status_t PreviewThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

/**
 * Public preview state checker
 *
 * State transitions do not synchronize with stream.
 * State distinctly serves the client to deside what
 * to do with preview frame buffers.
 */
PreviewThread::PreviewState PreviewThread::getPreviewState() const
{
    Mutex::Autolock lock(&mStateMutex);
    return mState;
}

/**
 * Public state setter for allowed transitions
 *
 * Note: state != STATE_STOPPED &&
 *       state != STATE_ENABLED_HIDDEN &&
 *       state != STATE_ENABLED_HIDDEN_PASSTHROUGH
 *       means that public API shows preview enabled()
 *       (+ queued startPreview handled by ControlThread)
 *
 * Note: only internally handled transition is initially
 *       STATE_CONFIGURED - which requires the client to
 *       call setPreviewConfig()
 *
 * Allowed transitions:
 * _STOPPED -> _NO_WINDOW:
 *   - Preview is started without window handle
 * _NO_WINDOW -> _STOPPED:
 * _ENABLED -> _STOPPED:
 * _ENABLED_HIDDEN -> _STOPPED:
 *  - Preview is stopped with one of the supported transition
 * _CONFIGURED -> _ENABLED:
 *  - preview gets enabled normally through supported transition
 * _ENABLED_HIDDEN -> _ENABLED:
 *  - preview gets restored visible (currently no-op internally)
 *  _ENABLED -> _HIDDEN:
 *  - public API preview state is shown disabled, we retain the
 *    preview stream active, but do not send buffers to display
 *  _ENABLED -> _HIDDEN_PASSTHROUGH:
 *  - public API preview state is shown disabled, we keep passing
 *    buffers to display
 */
status_t PreviewThread::setPreviewState(PreviewState state)
{
    LOG1("@%s: state request %d", __FUNCTION__, state);
    status_t status = INVALID_OPERATION;
    Mutex::Autolock lock(&mStateMutex);

    switch (state) {
        case STATE_NO_WINDOW:
           if (mState == STATE_STOPPED)
                status = NO_ERROR;
            break;
        case STATE_STOPPED:
            if (mState == STATE_NO_WINDOW
             || mState == STATE_ENABLED
             || mState == STATE_ENABLED_HIDDEN
             || mState == STATE_ENABLED_HIDDEN_PASSTHROUGH)
                status = NO_ERROR;
            break;
        case STATE_ENABLED:
            if (mState == STATE_CONFIGURED
             || mState == STATE_ENABLED_HIDDEN
             || mState == STATE_ENABLED_HIDDEN_PASSTHROUGH)
                status = NO_ERROR;
            break;
        case STATE_ENABLED_HIDDEN:
        case STATE_ENABLED_HIDDEN_PASSTHROUGH:
            if (mState == STATE_ENABLED)
                status = NO_ERROR;
            break;
        case STATE_CONFIGURED:
        default:
            break;
    }

    if (status != NO_ERROR) {
        LOG1("Invalid preview state transition request %d => %d", mState, state);
    } else {
        mState = state;
    }

    return status;
}

/**
 * Protected state setter for internal transitions
 */
status_t PreviewThread::setState(PreviewState state)
{
    LOG1("@%s: state %d => %d", __FUNCTION__, mState, state);
    Mutex::Autolock lock(&mStateMutex);
    mState = state;
    return NO_ERROR;
}

/**
 * Synchronous query to check if a valid native window
 * has been received.
 *
 * First we send a synchronous message (handler does nothing)
 * when it is processed we are sure that all previous commands have
 * been processed so we can check the mPreviewWindow variable.
 **/
bool PreviewThread::isWindowConfigured()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_WINDOW_QUERY;
    mMessageQueue.send(&msg, MESSAGE_ID_WINDOW_QUERY);
    return (mPreviewWindow != NULL);
}

status_t PreviewThread::handleMessageIsWindowConfigured()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_WINDOW_QUERY, status);
    return status;
}

/**
 * helper function to update per-frame locally tracked timestamps and counters
 */
void PreviewThread::frameDone(AtomBuffer &buff)
{
    LOG2("@%s", __FUNCTION__);
    mLastFrameTs = nsecs_t(buff.capture_timestamp.tv_sec) * 1000000LL
                 + nsecs_t(buff.capture_timestamp.tv_usec);
    mFramesDone++;
}

status_t PreviewThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;

        case MESSAGE_ID_PREVIEW:
            status = handlePreview(&msg.data.preview);
            frameDone(msg.data.preview.buff);
            break;

        case MESSAGE_ID_POSTVIEW:
            status = handlePostview(&msg.data.preview);
            mCallbacksThread->postviewRendered();
            break;

        case MESSAGE_ID_SET_PREVIEW_WINDOW:
            status = handleSetPreviewWindow(&msg.data.setPreviewWindow);
            break;

        case MESSAGE_ID_WINDOW_QUERY:
            status = handleMessageIsWindowConfigured();
            break;

        case MESSAGE_ID_SET_PREVIEW_CONFIG:
            status = handleSetPreviewConfig(&msg.data.setPreviewConfig);
            break;

        case MESSAGE_ID_FLUSH:
            status = handleMessageFlush();
            break;

        case MESSAGE_ID_FETCH_PREVIEW_BUFS:
            status = handleFetchPreviewBuffers();
            break;

        case MESSAGE_ID_RETURN_PREVIEW_BUFS:
            status = handleReturnPreviewBuffers();
            break;

        case MESSAGE_ID_SET_CALLBACK:
            status = handleMessageSetCallback(&msg.data.setCallback);
            break;

        case MESSAGE_ID_SET_FRAMERATE:
            status = handleSetFramerate(&msg.data.framerate);
            break;

        case MESSAGE_ID_SET_SENSOR_FRAMERATE:
            status = handleSetSensorFramerate(&msg.data.sensorFramerate);
            break;

        default:
            LOGE("Invalid message");
            status = BAD_VALUE;
            break;
    };
    return status;
}

bool PreviewThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    // start gathering frame rate stats
    mDebugFPS->run();

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    // stop gathering frame rate stats
    mDebugFPS->requestExitAndWait();

    return false;
}

status_t PreviewThread::requestExitAndWait()
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

status_t PreviewThread::handleMessageFlush()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_FLUSH, status);
    return status;
}

void PreviewThread::freeLocalPreviewBuf(void)
{
    if (mPreviewBuf.buff) {
        LOG1("releasing existing preview buffer\n");
        mPreviewBuf.buff->release(mPreviewBuf.buff);
        mPreviewBuf.buff = 0;
    }
}

void PreviewThread::allocateLocalPreviewBuf(void)
{
    size_t size(0);
    int stride(0);
    size_t ySize(0);
    int cStride(0);
    size_t cSize(0);

    LOG1("allocating the preview buffer\n");
    freeLocalPreviewBuf();

    switch(mPreviewFormat) {
    case V4L2_PIX_FMT_YUV420:
        stride = ALIGN16(mPreviewWidth);
        ySize = stride * mPreviewHeight;
        cStride = ALIGN16(stride/2);
        cSize = cStride * mPreviewHeight/2;
        size = ySize + cSize * 2;
        break;

    case V4L2_PIX_FMT_NV21:
        size = mPreviewWidth*mPreviewHeight*3/2;
        break;

    case V4L2_PIX_FMT_RGB565:
        size = mPreviewWidth*mPreviewHeight*2;
        break;

    default:
        LOGE("invalid preview format: %d", mPreviewFormat);
        break;
    }

    mCallbacks->allocateMemory(&mPreviewBuf, size);
    if(!mPreviewBuf.buff) {
        LOGE("getting memory failed\n");
    }
}


/**
 * stream-time dequeueing of buffers from preview_window_ops
 */
AtomBuffer* PreviewThread::dequeueFromWindow()
{
    AtomBuffer *ret = NULL;

    // mMinUndequeued is a constraint set by native window and
    // it controls when we can dequeue a frame and call previewDone.
    // Typically at least two frames must be kept in native window
    // when streaming.
    if(mBuffersInWindow > mMinUndequeued) {
        int err, stride;
        buffer_handle_t *buf;

        err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf, &stride);
        if (err != 0) {
            LOGW("Error dequeuing preview buffer");
        } else {
            size_t i = 0;
            for(; i < mPreviewBuffers.size(); i++) {
                if (buf == mPreviewBuffers[i].mNativeBufPtr) {
                    mBuffersInWindow--;
                    mPreviewInClient.push(i);
                    ret = &(mPreviewBuffers.editItemAt(i));
                    break;
                }
            }
            if (ret == NULL) {
                if (mFetchDone) {
                    LOGW("unknown gfx buffer dequeued, i %d, ptr %p",
                         i, mPreviewBuffers[i].mNativeBufPtr);
                    mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
                } else {
                    // stream-time fetching until target buffer count
                    void *dst;
                    AtomBuffer tmpBuf;
                    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
                    // Note: selected lock mode relies that if buffers were not
                    // prefetched, we end up in full frame memcpy path
                    int lockMode = GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_OFTEN;
                    int w,h;
                    getEffectiveDimensions(&w,&h);
                    const Rect bounds(w, h);
                    tmpBuf.buff = NULL;     // We do not allocate a normal camera_memory_t
                    tmpBuf.id = mPreviewBuffers.size();
                    tmpBuf.type = ATOM_BUFFER_PREVIEW_GFX;
                    tmpBuf.mNativeBufPtr = buf;
                    tmpBuf.stride = stride;
                    tmpBuf.width = w;
                    tmpBuf.height = h;
                    tmpBuf.size = frameSize(V4L2_PIX_FMT_NV12, tmpBuf.stride, tmpBuf.height);
                    tmpBuf.format = V4L2_PIX_FMT_NV12;
                    if(mapper.lock(*buf, lockMode, bounds, &dst) != NO_ERROR) {
                        LOGE("Failed to lock GraphicBufferMapper!");
                        mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
                    } else {
                        tmpBuf.gfxData = dst;
                        mPreviewBuffers.push(tmpBuf);
                        mPreviewInClient.push(tmpBuf.id);
                        mBuffersInWindow--;
                        ret = &(mPreviewBuffers.editItemAt(tmpBuf.id));
                        if (mPreviewBuffers.size() == mNumOfPreviewBuffers)
                            mFetchDone = true;
                    }
                }
            }
        }
    }
    else {
        LOG2("@%s: %d buffers in window, not enough, need %d",
             __FUNCTION__, mBuffersInWindow, mMinUndequeued);
    }

    return ret;
}

/**
 *  This method gets executed for each preview frames that the Thread
 *  receives.
 *  The message is sent by the observer thread that polls the preview
 *  stream
 *
 */
status_t PreviewThread::handlePreview(MessagePreview *msg)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    bool passedToGfx = false;
    LOG2("Buff: id = %d, data = %p",
            msg->buff.id,
            msg->buff.gfxData);

    PreviewState state = getPreviewState();
    if (state != STATE_ENABLED && state != STATE_ENABLED_HIDDEN_PASSTHROUGH)
        goto skip_displaying;

    if (mPreviewWindow != 0) {
        int err;
        buffer_handle_t *bufToEnqueue = NULL;

        if (msg->buff.type != ATOM_BUFFER_PREVIEW_GFX) {
            // client not passing our buffers, not in 0-copy path
            // do basic checks that configuration matches for a frame copy
            // Note: ignoring format, as we seem to use fixed NV12
            // while PreviewThread is configured according to public
            // parameter for callback conversions
            if (msg->buff.width != mPreviewWidth ||
                msg->buff.height != mPreviewHeight ||
                msg->buff.stride != mPreviewStride) {
                LOG1("%s: not passing buffer to window, conflicting format", __FUNCTION__);
                LOG1(", input : %dx%d(%d:%x:%s)",
                     msg->buff.width, msg->buff.height, msg->buff.stride, msg->buff.format,
                    v4l2Fmt2Str(msg->buff.format));
                LOG1(", preview : %dx%d(%d:%x:%s)",
                     mPreviewWidth, mPreviewHeight,
                     mPreviewStride, mPreviewFormat, v4l2Fmt2Str(mPreviewFormat));
            } else {
                AtomBuffer *buf = dequeueFromWindow();
                if (buf) {
                    LOG2("copying frame %p -> %p : size %d", msg->buff.buff->data, buf->gfxData, msg->buff.size);
                    LOG2("src frame  %dx%d stride %d ", msg->buff.width, msg->buff.height,msg->buff.stride);
                    LOG2("dst frame  %dx%d stride %d ", buf->width, buf->height, buf->stride);

                    copyPreviewBuffer(&msg->buff, buf);

                    bufToEnqueue = buf->mNativeBufPtr;
                } else {
                    LOGE("failed to dequeue from window");
                }
            }
        } else {
            // proceed in 0-copy path
            bufToEnqueue = msg->buff.mNativeBufPtr;
            passedToGfx = true;
        }

        if (bufToEnqueue != NULL) {
            if ((err = mPreviewWindow->enqueue_buffer(mPreviewWindow,
                            bufToEnqueue)) != 0) {
                LOGE("Surface::queueBuffer returned error %d", err);
                passedToGfx = false;
            } else {
                for (size_t i = 0; i< mPreviewInClient.size(); i++) {
                    buffer_handle_t *bufHandle =
                        mPreviewBuffers[mPreviewInClient[i]].mNativeBufPtr;
                    if (bufToEnqueue == bufHandle) {
                        mPreviewInClient.removeAt(i);
                        break;
                    }
                }
                mBuffersInWindow++;
                // preview frame shown, update perf traces
                PERFORMANCE_TRACES_PREVIEW_SHOWN(msg->buff.frameCounter);
            }
        }
    }

    if(!mPreviewBuf.buff) {
        allocateLocalPreviewBuf();
    }

    if(mCallbacks->msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME) && mPreviewBuf.buff) {
        void *src;
        if(msg->buff.type == ATOM_BUFFER_PREVIEW)
            src = msg->buff.buff->data;
        else
            src = msg->buff.gfxData;

        switch(mPreviewFormat) {

        case V4L2_PIX_FMT_YUV420:
            align16ConvertNV12ToYV12(mPreviewWidth, mPreviewHeight, msg->buff.stride, src, mPreviewBuf.buff->data);
            break;

        case V4L2_PIX_FMT_NV21:
            trimConvertNV12ToNV21(mPreviewWidth, mPreviewHeight, msg->buff.stride, src, mPreviewBuf.buff->data);
            break;

        case V4L2_PIX_FMT_RGB565:
            trimConvertNV12ToRGB565(mPreviewWidth, mPreviewHeight, msg->buff.stride, src, mPreviewBuf.buff->data);
            break;

        default:
            LOGE("invalid format: %d", mPreviewFormat);
            status = -1;
            break;
        }
        if (status == NO_ERROR)
            mCallbacksThread->previewFrameDone(&mPreviewBuf);
    }

skip_displaying:
    inputBufferCallback();

    mDebugFPS->update(); // update fps counter

    if (!passedToGfx) {
        // passing the input buffer as output
        if (!outputBufferCallback(&(msg->buff)))
            msg->buff.owner->returnBuffer(&msg->buff);
    } else {
        // input buffer was passed to Gfx queue, now try
        // dequeueing to replace output callback buffer
        AtomBuffer *outputBuffer = dequeueFromWindow();
        if (outputBuffer) {
            // restore the owner from input
            outputBuffer->owner = msg->buff.owner;
            if (!outputBufferCallback(outputBuffer))
                msg->buff.owner->returnBuffer(outputBuffer);
        }
    }

    return status;
}

status_t PreviewThread::handleSetPreviewWindow(MessageSetPreviewWindow *msg)
{
    LOG1("@%s: window = %p", __FUNCTION__, msg->window);

    if (mPreviewWindow == msg->window) {
        LOG1("Received the same window handle, nothing needs to be done.");
        return NO_ERROR;
    }

    if (mPreviewWindow != NULL) {
        freeGfxPreviewBuffers();
    }

    mPreviewWindow = msg->window;
    int w = mPreviewWidth;
    int h = mPreviewHeight;
    int usage;

    getEffectiveDimensions(&w,&h);

    if (mPreviewWindow != NULL) {

        if (mOverlayEnabled) {
            // write-often: overlay copy into the buffer
            // read-never: we do not use this buffer for callbacks. We never read from it
            usage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                    GRALLOC_USAGE_SW_READ_NEVER  |
                    GRALLOC_USAGE_HW_COMPOSER;
        } else {
            // write-never: main use-case, stream image data to window by ISP only
            // read-rarely: 2nd use-case, memcpy to application data callback
            usage = GRALLOC_USAGE_SW_READ_RARELY |
                    GRALLOC_USAGE_SW_WRITE_NEVER |
                    GRALLOC_USAGE_HW_COMPOSER;
        }

        LOG1("Setting new preview window %p (%dx%d)", mPreviewWindow,w,h);
        mPreviewWindow->set_usage(mPreviewWindow, usage);
#ifndef GRAPHIC_IS_GEN
        mPreviewWindow->set_buffers_geometry(mPreviewWindow, w, h, HAL_PIXEL_FORMAT_NV12);
#else
        mPreviewWindow->set_buffers_geometry(mPreviewWindow, w, h, HAL_PIXEL_FORMAT_YV12);
#endif
    }

    return NO_ERROR;
}

status_t PreviewThread::handleSetPreviewConfig(MessageSetPreviewConfig *msg)
{
    LOG1("@%s: width = %d, height = %d, format = %x", __FUNCTION__,
         msg->width, msg->height, msg->format);
    status_t status = NO_ERROR;
    int w = msg->width;
    int h = msg->height;
    int bufferCount = msg->bufferCount;

    if ((w != 0 && h != 0) &&
        (mPreviewWidth != w || mPreviewHeight !=h)) {
        LOG1("Setting new preview size: %dx%d, stride:%d", w, h, msg->stride);
        if (mPreviewWindow != NULL) {

            /**
             *  if preview size changed, update the preview window
             *  but account for the rotation when setting the geometry
             */
            if (mRotation == 90 || mRotation == 270) {
                // we swap width and height
                w = msg->height;
                h = msg->width;
            }
            if(mOverlayEnabled)
                bufferCount = GFX_OVERLAY_BUFFERS_DURING_OVERLAY_USE;
#ifndef GRAPHIC_IS_GEN
            mPreviewWindow->set_buffers_geometry(mPreviewWindow, w, h, HAL_PIXEL_FORMAT_NV12);
#else
            mPreviewWindow->set_buffers_geometry(mPreviewWindow, w, h, HAL_PIXEL_FORMAT_YV12);
#endif
        }

        /**
         * we keep in our internal fields the resolution provided by CtrlThread
         * in order to get the effective resolution taking into account the
         * rotation use \sa getEffectiveDimensions
         */
        mPreviewWidth = msg->width;
        mPreviewHeight = msg->height;
        mPreviewStride = msg->stride;
    }

    mPreviewFormat = msg->format;

    allocateLocalPreviewBuf();

    status = allocateGfxPreviewBuffers(bufferCount);

    return status;
}

/**
 * handle fetchPreviewBuffers()
 *
 * By fetching all our external buffers at once, we provide an
 * array of loose pointers to buffers acquired from NativeWindow
 * ops. Pre-fetching is typical operation when ISP is fed with
 * graphic-buffers to attain 0-copy preview loop.
 *
 * If buffers are not fetched in the beginning of streaming,
 * buffers allocated by AtomISP are expected.
 */
status_t PreviewThread::handleFetchPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (mOverlayEnabled)
        goto freeDeQueued;

    if (mPreviewBuffers.isEmpty()) {
        GraphicBufferMapper &mapper = GraphicBufferMapper::get();
        AtomBuffer tmpBuf;
        int err, stride;
        buffer_handle_t *buf;
        void *dst;
        int lockMode = GRALLOC_USAGE_SW_READ_OFTEN |
                       GRALLOC_USAGE_SW_WRITE_NEVER |
                       GRALLOC_USAGE_HW_COMPOSER;
        const Rect bounds(mPreviewWidth, mPreviewHeight);
        for (size_t i = 0; i < mNumOfPreviewBuffers; i++) {
            err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf, &stride);
            if(err != 0) {
                LOGE("Surface::dequeueBuffer returned error %d", err);
                status = UNKNOWN_ERROR;
                goto freeDeQueued;
            }
            tmpBuf.buff = NULL;     // We do not allocate a normal camera_memory_t
            tmpBuf.id = i;
            tmpBuf.type = ATOM_BUFFER_PREVIEW_GFX;
            tmpBuf.mNativeBufPtr = buf;
            tmpBuf.stride = stride;
            tmpBuf.width = mPreviewWidth;
            tmpBuf.height = mPreviewHeight;
            tmpBuf.size = frameSize(V4L2_PIX_FMT_NV12, tmpBuf.stride, tmpBuf.height);
            tmpBuf.format = V4L2_PIX_FMT_NV12;

            status = mapper.lock(*buf, lockMode, bounds, &dst);
            if(status != NO_ERROR) {
                LOGE("Failed to lock GraphicBufferMapper!");
                goto freeDeQueued;
            }

            tmpBuf.gfxData = dst;
            mPreviewBuffers.push(tmpBuf);
            mPreviewInClient.push(i);
            LOG1("%s: got Gfx Buffer: native_ptr %p, size:(%dx%d), stride: %d ", __FUNCTION__,
                 buf, mPreviewWidth, mPreviewHeight, tmpBuf.stride);
        } // for

        mBuffersInWindow = 0;
        mFetchDone = true;
    }
    mMessageQueue.reply(MESSAGE_ID_FETCH_PREVIEW_BUFS, status);
    return status;
freeDeQueued:
    freeGfxPreviewBuffers();
    mMessageQueue.reply(MESSAGE_ID_FETCH_PREVIEW_BUFS, status);
    return status;
}

status_t PreviewThread::handleReturnPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    status = freeGfxPreviewBuffers();
    mMessageQueue.reply(MESSAGE_ID_RETURN_PREVIEW_BUFS, status);
    return status;
}


/**
 * Allocates preview buffers from native window.
 *
 * @param numberOfBuffers:[IN]: Number of requested buffers to allocate
 *
 * @return NO_MEMORY: If it could not allocate or dequeue the required buffers
 * @return INVALID_OPERATION: if it couldn't allocate the buffers because lack of preview window
 */
status_t PreviewThread::allocateGfxPreviewBuffers(int numberOfBuffers) {
    LOG1("@%s: num buf: %d", __FUNCTION__, numberOfBuffers);
    status_t status = NO_ERROR;
    if(!mPreviewBuffers.isEmpty()) {
        LOGW("Preview buffers already allocated size=[%d] -- this should not happen",mPreviewBuffers.size());
        freeGfxPreviewBuffers();
    }

    if (mPreviewWindow != 0) {

        if(numberOfBuffers > MAX_NUMBER_PREVIEW_GFX_BUFFERS)
            return NO_MEMORY;

        int res = mPreviewWindow->set_buffer_count(mPreviewWindow, numberOfBuffers);
        if (res != 0) {
            LOGW("Surface::set_buffer_count returned %d", res);
            return NO_MEMORY;
        }

        mPreviewWindow->get_min_undequeued_buffer_count(mPreviewWindow, &mMinUndequeued);
        LOG1("Surface::get_min_undequeued_buffer_count buffers %d", mMinUndequeued);
        if (mMinUndequeued < 0 || mMinUndequeued > numberOfBuffers - 1) {
            LOGE("unexpected min undeueued requirement %d", mMinUndequeued);
            return INVALID_OPERATION;
        }

        mBuffersInWindow = numberOfBuffers;
        mNumOfPreviewBuffers = numberOfBuffers;
        mFetchDone = false;
    } else {
        status = INVALID_OPERATION;
    }

    return status;
}

/**
 * Frees the  preview buffers taken from native window.
 * Goes through the list of GFx preview buffers and unlocks them all
 * using the Graphic Buffer Mapper
 * it does cancel only the ones currently not used by the window
 *
 *
 * @return NO_ERROR
 *
 */
status_t PreviewThread::freeGfxPreviewBuffers() {
    LOG1("@%s: preview buffer: %d, in user: %d", __FUNCTION__, mPreviewBuffers.size(),
                                                               mPreviewInClient.size());
    size_t i;
    int res;
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();

    if ((mPreviewWindow != NULL) && (!mPreviewBuffers.isEmpty())) {

        for( i = 0; i < mPreviewBuffers.size(); i++) {
            res = mapper.unlock(*(mPreviewBuffers[i].mNativeBufPtr));
            if (res != 0) {
                LOGW("%s: unlocking gfx buffer %d failed!", __FUNCTION__, i);
            }

        }

        for( i = 0; i < mPreviewInClient.size(); i++) {
            buffer_handle_t *bufHandle =
                mPreviewBuffers[mPreviewInClient[i]].mNativeBufPtr;
            LOG1("%s: canceling gfx buffer[%d]: %p (value = %p)", __FUNCTION__, i, bufHandle, *bufHandle);
            res = mPreviewWindow->cancel_buffer(mPreviewWindow, bufHandle);
            if (res != 0)
                LOGW("%s: canceling gfx buffer %d failed!", __FUNCTION__, i);
        }
        LOG1("%s: clearing vectors !",__FUNCTION__);
        mPreviewBuffers.clear();
        mPreviewInClient.clear();
        mBuffersInWindow = 0;
    }

    return NO_ERROR;
}

/**
 * getGfxBufferStride
 * returns the stride of the buffers dequeued by the current window
 *
 * Please NOTE:
 *  It is the caller responsibility to ensure mPreviewWindow is initialized
 */
int PreviewThread::getGfxBufferStride(void)
{
    int stride = 0;
    buffer_handle_t *buf;
    int err;
    err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf, &stride);
    if (!err)
        mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
    else
        LOGE("Surface::dequeueBuffer returned error %d", err);

    return stride;
}

/**
 * Copies snapshot-postview buffer to preview window for preview-keep-alive
 * feature
 *
 * TODO: This is temporary solution to update preview surface while preview
 * is stopped. Buffers coupling (indexes mapping in AtomISP & ControlThread)
 * techniques need to be revisited to properly avoid copy done here and
 * to seamlessly allow using gfx buffers regardless of AtomISP mode.
 * Drawing postview should use generic preview() and this method is to be
 * removed.
 *
 * Note: expects the buffers to be of correct size with configuration
 * left from preview ran before snapshot.
 */
status_t PreviewThread::handlePostview(MessagePreview *msg)
{
    int err;

    LOG1("@%s: width = %d, height = %d ", __FUNCTION__,
         msg->buff.width, msg->buff.height);

    if (!mPreviewWindow) {
        LOGW("Unable to provide 'preview-keep-alive' frame, no window!");
        return NO_ERROR;
    }

    if (msg->buff.type != ATOM_BUFFER_POSTVIEW) {
        // support implemented for using AtomISP postview type only
        LOG1("Unable to provide 'preview-keep-alive' frame, input buffer type unexpected");
        return UNKNOWN_ERROR;
    }

    if (getPreviewState() != STATE_STOPPED) {
        // indicates we didn't stop & return the gfx buffers
        LOGD("Unable to provide 'preview-keep-alive' frame, normal preview active");
        return UNKNOWN_ERROR;
    }

    if (msg->buff.width != mPreviewWidth ||
        msg->buff.height != mPreviewHeight) {
        LOGD("Unable to provide 'preview-keep-alive' frame, postview %dx%d -> preview %dx%d ",
                msg->buff.width, msg->buff.height, mPreviewWidth, mPreviewHeight);
        return UNKNOWN_ERROR;
    }

    AtomBuffer tmpBuf = AtomBufferFactory::createAtomBuffer(ATOM_BUFFER_POSTVIEW);
    getEffectiveDimensions(&tmpBuf.width,&tmpBuf.height);

    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
    const Rect bounds(tmpBuf.width, tmpBuf.height);

    // queue one from the window
    err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &tmpBuf.mNativeBufPtr, &tmpBuf.stride);
    if (err != 0) {
        LOGW("Error dequeuing preview buffer for 'preview-keep-alive'");
        return UNKNOWN_ERROR;
    }

    err = mapper.lock(*tmpBuf.mNativeBufPtr,
        GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_SW_READ_NEVER,
        bounds, &tmpBuf.gfxData);
    if (err != 0) {
        LOGE("Error locking buffer for postview rendering");
        mPreviewWindow->cancel_buffer(mPreviewWindow, tmpBuf.mNativeBufPtr);
        return UNKNOWN_ERROR;
    }

    copyPreviewBuffer(&msg->buff,&tmpBuf);

    mapper.unlock(*tmpBuf.mNativeBufPtr);

    err = mPreviewWindow->enqueue_buffer(mPreviewWindow, tmpBuf.mNativeBufPtr);
    if (err != 0)
        LOGE("Surface::queueBuffer returned error %d", err);

    LOG1("@%s: done", __FUNCTION__);

    return NO_ERROR;
}

/**
 * Copies or rotates the buffer given by the ControlThread.
 *
 * Usually the src is a buffer from the ControlThread and the dst is a Gfx buffer
 * dequeued from the preview window
 *
 * The rotation is passed when the overlay is enabled in cases where the scan
 * order of the display and camera are different
 */
void PreviewThread::copyPreviewBuffer(AtomBuffer* src, AtomBuffer* dst)
{
    switch (mRotation) {
    case 90:
        nv12rotateBy90(src->width,       // width of the source image
                       src->height,      // height of the source image
                       src->stride,      // scanline stride of the source image
                       dst->stride,      // scanline stride of the target image
                       (const char*)src->buff->data,               // source image
                       (char *)dst->gfxData);                 // target image
        break;
    case 270:
        // TODO: Not handled, waiting for Semi
        break;
    case 0:
        memcpy((char *)dst->gfxData, (const char*)src->buff->data,src->size);
        break;
    }

}

/**
 * Returns the effective dimensions of the preview
 * we store only the original request from the client in mPreviewWidth and
 * mPreviewHeight. When we use these values we need to take into account any
 * rotation that we need to do to the buffers in case we are using overlay.
 * \param w [out] pointer to the effective width
 * \param h [out] pointer to the effective height
 */
void PreviewThread::getEffectiveDimensions(int *w, int *h)
{
    if (mRotation == 90 || mRotation == 270) {
        // we swap width and height
        *w = mPreviewHeight;
        *h = mPreviewWidth;
    } else {
        *w = mPreviewWidth;
        *h = mPreviewHeight;
    }
}
} // namespace android
