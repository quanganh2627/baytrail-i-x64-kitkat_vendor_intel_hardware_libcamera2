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
#include <hal_public.h>
#include <gui/Surface.h>
#include "PerformanceTraces.h"
#include <ui/GraphicBuffer.h>
#include "media/openmax/OMX_IVCommon.h"     // for HWC overlay supported YUV color format
//TODO: use a HAL YUV define once HWC starts using those
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
    ,mLastFrameTs(0)
    ,mFramesDone(0)
    ,mCallbacksThread(CallbacksThread::getInstance())
    ,mMessageHandler(NULL)
{
    LOG1("@%s", __FUNCTION__);

    mMessageHandler = new GfxPreviewHandler(this);

    assert(mMessageHandler != NULL);
}

PreviewThread::~PreviewThread()
{
    LOG1("@%s", __FUNCTION__);
    mMessageHandler->mDebugFPS.clear();
    delete mMessageHandler;
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

    delete mMessageHandler;
    mMessageHandler = NULL;

    if(set)
        mMessageHandler = new OverlayPreviewHandler(this, rotation);
    else
        mMessageHandler = new GfxPreviewHandler(this);

    if (mMessageHandler == NULL)
        status = NO_MEMORY;

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
    params->setPreviewFormat(cameraParametersFormat(mMessageHandler->mPreviewFormat));

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
    mSetFPS = fps;
    return NO_ERROR;
}

/**
 * This function implements the frame skip algorithm.
 * - If user requests 15fps, drop every even frame
 * - If user requests 10fps, drop two frames every three frames
 * @returns true: skip,  false: not skip
 */
// TODO: The above only applies to 30fps. Generalize this to support other sensor FPS as well.
bool PreviewThread::checkSkipFrame(int frameNum)
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

    status = mMessageHandler->fetchPreviewBuffers(pvBufs,count);

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
    return (mMessageHandler->mPreviewWindow != NULL);
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
            status = mMessageHandler->handlePreview(&msg.data.preview);
            frameDone(msg.data.preview.buff);
            break;

        case MESSAGE_ID_POSTVIEW:
            status = mMessageHandler->handlePostview(&msg.data.preview);
            mCallbacksThread->postviewRendered();
            break;

        case MESSAGE_ID_SET_PREVIEW_WINDOW:
            status = mMessageHandler->handleSetPreviewWindow(&msg.data.setPreviewWindow);
            break;

        case MESSAGE_ID_WINDOW_QUERY:
            status = handleMessageIsWindowConfigured();
            break;

        case MESSAGE_ID_SET_PREVIEW_CONFIG:
            status = mMessageHandler->handleSetPreviewConfig(&msg.data.setPreviewConfig);
            break;

        case MESSAGE_ID_FLUSH:
            status = handleMessageFlush();
            break;

        case MESSAGE_ID_FETCH_PREVIEW_BUFS:
            status = mMessageHandler->handleFetchPreviewBuffers();
            break;

        case MESSAGE_ID_RETURN_PREVIEW_BUFS:
            status = mMessageHandler->handleReturnPreviewBuffers();
            break;

        case MESSAGE_ID_SET_CALLBACK:
            status = handleMessageSetCallback(&msg.data.setCallback);
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
    mMessageHandler->mDebugFPS->run();

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    // stop gathering frame rate stats
    mMessageHandler->mDebugFPS->requestExitAndWait();

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

/******************************************************************************
 *  Common Preview Message Handler methods
 *****************************************************************************/
PreviewThread::PreviewMessageHandler::PreviewMessageHandler(PreviewThread* aThread) :
            mPreviewWindow(NULL)
           ,mPvThread(aThread)
           ,mPreviewBuf(AtomBufferFactory::createAtomBuffer(ATOM_BUFFER_PREVIEW))
           ,mCallbacks(Callbacks::getInstance())
           ,mCallbacksThread(CallbacksThread::getInstance())
           ,mDebugFPS(new DebugFrameRate())
           ,mPreviewWidth(640)
           ,mPreviewHeight(480)
           ,mPreviewStride(640)
           ,mPreviewFormat(V4L2_PIX_FMT_NV21)
{
}

PreviewThread::PreviewMessageHandler::~PreviewMessageHandler()
{
    LOG1("@%s",__FUNCTION__);
    freeLocalPreviewBuf();
}

void PreviewThread::PreviewMessageHandler::freeLocalPreviewBuf(void)
{
    if (mPreviewBuf.buff) {
        LOG1("releasing existing preview buffer\n");
        mPreviewBuf.buff->release(mPreviewBuf.buff);
        mPreviewBuf.buff = 0;
    }
}

void PreviewThread::PreviewMessageHandler::allocateLocalPreviewBuf(void)
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

/******************************************************************************
 *  GFx Preview message Handler
 *****************************************************************************/

PreviewThread::GfxPreviewHandler::GfxPreviewHandler(PreviewThread* aThread) :
         PreviewMessageHandler(aThread)
        ,mBuffersInWindow(0), mNumOfPreviewBuffers(0), mFetchDone(false)
{
    LOG1("@%s",__FUNCTION__);
    mPreviewBuffers.setCapacity(MAX_NUMBER_PREVIEW_GFX_BUFFERS);
    mPreviewInClient.setCapacity(MAX_NUMBER_PREVIEW_GFX_BUFFERS);

}

PreviewThread::GfxPreviewHandler::~GfxPreviewHandler()
{
    LOG1("@%s",__FUNCTION__);
    freeGfxPreviewBuffers();
}

status_t
PreviewThread::GfxPreviewHandler::fetchPreviewBuffers(AtomBuffer **pvBufs, int *count)
{
    *pvBufs = mPreviewBuffers.editArray();
    *count = mPreviewBuffers.size();
    return NO_ERROR;
}

/**
 * stream-time dequeueing of buffers from preview_window_ops
 */
AtomBuffer* PreviewThread::GfxPreviewHandler::dequeueFromWindow()
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
                    const Rect bounds(mPreviewWidth, mPreviewHeight);
                    tmpBuf.buff = NULL;     // We do not allocate a normal camera_memory_t
                    tmpBuf.id = mPreviewBuffers.size();
                    tmpBuf.type = ATOM_BUFFER_PREVIEW_GFX;
                    tmpBuf.mNativeBufPtr = buf;
                    tmpBuf.stride = stride;
                    tmpBuf.width = mPreviewWidth;
                    tmpBuf.height = mPreviewHeight;
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
 *  handler for each preview frame when we are using Gfx plane
 *  and Gfx buffers to render preview
 *
 */
status_t
PreviewThread::GfxPreviewHandler::handlePreview(MessagePreview *msg)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    bool passedToGfx = false;
    LOG2("Buff: id = %d, data = %p",
            msg->buff.id,
            msg->buff.gfxData);

    mPvThread->inputBufferCallback();

    PreviewState state = mPvThread->getPreviewState();
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
                    LOG2("copying frame %p -> %p : size %d", buf->gfxData, msg->buff.buff->data, msg->buff.size);
                    memcpy(buf->gfxData, msg->buff.buff->data, msg->buff.size);
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
    mDebugFPS->update(); // update fps counter

    if (!passedToGfx) {
        // passing the input buffer as output
        if (!mPvThread->outputBufferCallback(&(msg->buff)))
            msg->buff.owner->returnBuffer(&msg->buff);
    } else {
        // input buffer was passed to Gfx queue, now try
        // dequeueing to replace output callback buffer
        AtomBuffer *outputBuffer = dequeueFromWindow();
        if (outputBuffer) {
            // restore the owner from input
            outputBuffer->owner = msg->buff.owner;
            if (!mPvThread->outputBufferCallback(outputBuffer))
                msg->buff.owner->returnBuffer(outputBuffer);
        }
    }

    return status;
}

status_t
PreviewThread::GfxPreviewHandler::handleSetPreviewWindow(MessageSetPreviewWindow *msg)
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

    if (mPreviewWindow != NULL) {
        LOG1("Setting new preview window %p", mPreviewWindow);
        int previewWidthPadded = mPreviewWidth;

        // write-never: main use-case, stream image data to window by ISP only
        // read-rarely: 2nd use-case, memcpy to application data callback
        mPreviewWindow->set_usage(mPreviewWindow,
                                  (GRALLOC_USAGE_SW_READ_RARELY |
                                   GRALLOC_USAGE_SW_WRITE_NEVER |
                                   GRALLOC_USAGE_HW_COMPOSER));
        mPreviewWindow->set_buffers_geometry(
                mPreviewWindow,
                previewWidthPadded,
                mPreviewHeight,
                HAL_PIXEL_FORMAT_NV12);
    }

    return NO_ERROR;
}

status_t
PreviewThread::GfxPreviewHandler::handleSetPreviewConfig(MessageSetPreviewConfig *msg)
{
    LOG1("@%s: width = %d, height = %d, format = %x", __FUNCTION__,
         msg->width, msg->height, msg->format);
    status_t status = NO_ERROR;

    if ((msg->width != 0 && msg->height != 0) &&
        (mPreviewWidth != msg->width || mPreviewHeight != msg->height)) {
        LOG1("Setting new preview size: %dx%d, stride:%d", msg->width, msg->height, msg->stride);
        if (mPreviewWindow != NULL) {

            // if preview size changed, update the preview window
            mPreviewWindow->set_buffers_geometry(mPreviewWindow,
                                                 msg->width,
                                                 msg->height,
                                                 HAL_PIXEL_FORMAT_NV12);

            int stride = getGfxBufferStride();
            if(stride != msg->stride) {
                LOG1("the stride %d in GFX is different from stride %d in ISP:", stride, msg->stride)
                mPreviewWindow->set_buffers_geometry(mPreviewWindow,
                                                     msg->stride,
                                                     msg->height,
                                                     HAL_PIXEL_FORMAT_NV12);
            }
        }
        mPreviewWidth = msg->width;
        mPreviewHeight = msg->height;
        mPreviewStride = msg->stride;
    }

    mPreviewFormat = msg->format;

    allocateLocalPreviewBuf();

    status = allocateGfxPreviewBuffers(msg->bufferCount);

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
status_t
PreviewThread::GfxPreviewHandler::handleFetchPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

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
    mPvThread->mMessageQueue.reply(MESSAGE_ID_FETCH_PREVIEW_BUFS, status);
    return status;
freeDeQueued:
    freeGfxPreviewBuffers();
    mPvThread->mMessageQueue.reply(MESSAGE_ID_FETCH_PREVIEW_BUFS, status);
    return status;
}

status_t
PreviewThread::GfxPreviewHandler::handleReturnPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    status = freeGfxPreviewBuffers();
    mPvThread->mMessageQueue.reply(MESSAGE_ID_RETURN_PREVIEW_BUFS, status);
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
status_t
PreviewThread::GfxPreviewHandler::allocateGfxPreviewBuffers(int numberOfBuffers) {
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
status_t
PreviewThread::GfxPreviewHandler::freeGfxPreviewBuffers() {
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
int
PreviewThread::GfxPreviewHandler::getGfxBufferStride(void)
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
status_t PreviewThread::GfxPreviewHandler::handlePostview(MessagePreview *msg)
{
    int err, stride;
    buffer_handle_t *buf;
    void *src;
    void *dst;

    LOG1("@%s: width = %d, height = %d (texture)", __FUNCTION__,
         msg->buff.width, msg->buff.height);

    if (!mPreviewWindow) {
        LOGW("Unable to provide 'preview-keep-alive' frame, no window!");
        return NO_ERROR;
    }

    if (msg->buff.type != ATOM_BUFFER_POSTVIEW) {
        // support implemented for using AtomISP postview type only
        LOGD("Unable to provide 'preview-keep-alive' frame, input buffer type unexpected");
        return UNKNOWN_ERROR;
    }

    if (mPvThread->getPreviewState() != STATE_STOPPED) {
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

    src = msg->buff.buff->data;
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
    const Rect bounds(msg->buff.width, msg->buff.height);

    // queue one from the window
    err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf, &stride);
    if (err != 0) {
        LOGW("Error dequeuing preview buffer for 'preview-keep-alive'");
        return UNKNOWN_ERROR;
    }

    err = mapper.lock(*buf,
        GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_SW_READ_NEVER,
        bounds, &dst);
    if (err != 0) {
        mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
        return UNKNOWN_ERROR;
    }

    memcpy (dst, src, msg->buff.size);

    mapper.unlock(*buf);

    err = mPreviewWindow->enqueue_buffer(mPreviewWindow, buf);
    if (err != 0)
        LOGE("Surface::queueBuffer returned error %d", err);

    LOG1("@%s: done", __FUNCTION__);

    return NO_ERROR;
}

/******************************************************************************
 *   Overlay Preview Message Handler
 ******************************************************************************/
/**
 * Overlay Preview Handler is in charge of handling th preview thread messages
 * when we want to use the HW overlay to render the preview.
 * The main differences with Gfx preview handler are:
 *  - it sets GRALLOC flags to signal that the buffer is to be render via HWC.
 *  - uses a different color format. It is esentially NV12 but with some exotic
 *    stride requirement
 *
 * In some cases it requires a rotation that it is currently done in SW inside
 * the preview thread.
 * In cases where no rotation is needed a spacial memcopy is used to comply  with
 * the stride requirements of the color format
 */
PreviewThread::OverlayPreviewHandler::OverlayPreviewHandler(PreviewThread* aThread,
                                                            int OverlayRotation) :
        PreviewMessageHandler(aThread),
        mRotation(OverlayRotation)
{

}

status_t
PreviewThread::OverlayPreviewHandler::fetchPreviewBuffers(AtomBuffer **pvBufs, int *count)
{
    *pvBufs = NULL;
    *count = 0;
    return INVALID_OPERATION;
}

status_t
PreviewThread::OverlayPreviewHandler::handleFetchPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mPvThread->mMessageQueue.reply(MESSAGE_ID_FETCH_PREVIEW_BUFS, status);
    return status;
}

status_t
PreviewThread::OverlayPreviewHandler::handleReturnPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mPvThread->mMessageQueue.reply(MESSAGE_ID_RETURN_PREVIEW_BUFS, status);
    return status;
}

/**
 * Handle each preview frame when it is render via HW overlay
 */
status_t
PreviewThread::OverlayPreviewHandler::handlePreview(MessagePreview *msg)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    LOG2("Buff: id = %d, data = %p",
           msg->buff.id,
           msg->buff.buff->data);

    mPvThread->inputBufferCallback();

    if (mPreviewWindow != 0) {
       buffer_handle_t *buf;
       int err;
       int stride;
       int usage;
       int w = mPreviewWidth;
       int h = mPreviewHeight;

       if ((err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf, &stride)) != 0) {
           LOGE("Surface::dequeueBuffer returned error %d", err);
       } else {

           if (mPreviewWindow->lock_buffer(mPreviewWindow, buf) != NO_ERROR) {
               LOGE("Failed to lock preview buffer!");
               mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
               status = NO_MEMORY;
               goto exit;
           }

           GraphicBufferMapper &mapper = GraphicBufferMapper::get();
           if (mRotation == 90 || mRotation == 270) {
               w = mPreviewHeight;
               h = mPreviewWidth;
           }

           const Rect bounds(w, h);
           long long dst;      // this should be void* but this is a temporary workaround to bug BZ:34172
           usage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                   GRALLOC_USAGE_SW_READ_NEVER  |
                   GRALLOC_USAGE_HW_COMPOSER;

           if (mapper.lock(*buf, usage, bounds, (void**)&dst) != NO_ERROR) {
               LOGE("Failed to lock GraphicBufferMapper!");
               mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
               status = NO_MEMORY;
               goto exit;
           }

           /* copies and rotates if necessary */
           copyPreviewBuffer((const char *)msg->buff.buff->data,      // source image
                             (char*) dst);                            // target image

           mapper.unlock(*buf);
           if ((err = mPreviewWindow->enqueue_buffer(mPreviewWindow, buf)) != 0) {
               LOGE("Surface::queueBuffer returned error %d", err);
           }
       }
       buf = NULL;
    }

    if(!mPreviewBuf.buff) {
       allocateLocalPreviewBuf();
    }
    if(mPreviewBuf.buff) {
       switch(mPreviewFormat) {

       case V4L2_PIX_FMT_YUV420:
           align16ConvertNV12ToYV12(mPreviewWidth, mPreviewHeight, msg->buff.stride,
                                    msg->buff.buff->data, mPreviewBuf.buff->data);
           break;

       case V4L2_PIX_FMT_NV21:
           trimConvertNV12ToNV21(mPreviewWidth, mPreviewHeight, msg->buff.stride,
                                 msg->buff.buff->data, mPreviewBuf.buff->data);
           break;
       case V4L2_PIX_FMT_RGB565:
           trimConvertNV12ToRGB565(mPreviewWidth, mPreviewHeight, msg->buff.stride,
                                   msg->buff.buff->data, mPreviewBuf.buff->data);
           break;
       default:
           LOGE("invalid format: %d", mPreviewFormat);
           status = -1;
           break;
       }
       if (status == NO_ERROR)
           mCallbacks->previewFrameDone(&mPreviewBuf);
    }
    mDebugFPS->update(); // update fps counter
exit:
    if (!mPvThread->outputBufferCallback(&msg->buff))
        msg->buff.owner->returnBuffer(&msg->buff);

    return status;
}

void
PreviewThread::OverlayPreviewHandler::copyPreviewBuffer(const char* src, char*dst)
{
    int paddedStride;

    if (mRotation == 90 || mRotation == 270) {
       paddedStride = paddingWidthNV12VED(mPreviewHeight,0);
   } else {
       paddedStride = paddingWidthNV12VED(mPreviewStride,0);
   }

    switch (mRotation) {
    case 90:
        nv12rotateBy90(mPreviewWidth,       // width of the source image
                       mPreviewHeight,       // height of the source image
                       mPreviewStride,       // scanline stride of the source image
                       paddedStride,         // scanline stride of the target image
                       src,                  // source image
                       dst);                 // target image
        break;
    case 270:
        // TODO: Not handled, waiting for Semi
        break;
    case 0:
        strideCopy(mPreviewWidth,       // width of the source image
                    mPreviewHeight,       // height of the source image
                    mPreviewStride,       // scanline stride of the source image
                    paddedStride,         // scanline stride of the target image
                    src,                  // source image
                    dst);
        break;
    }

}

void
PreviewThread::OverlayPreviewHandler::strideCopy(const int   width,
                                                 const int   height,
                                                 const int   rstride,
                                                 const int   wstride,
                                                 const char* sptr,
                                                 char*       dptr)
{
    const char *src = sptr;
    char *dst = dptr;
    // Y
    for (int i = 0; i < height; i++) {
        memcpy(dst,src,rstride);
        dst += wstride;
        src += rstride;
    }
    //UV
    for (int i = 0; i < height/2; i++) {
            memcpy(dst,src,rstride);
            dst += wstride;
            src += rstride;
        }

}

status_t
PreviewThread::OverlayPreviewHandler::handleSetPreviewWindow(MessageSetPreviewWindow *msg)
{
    LOG1("@%s: window = %p", __FUNCTION__, msg->window);

    mPreviewWindow = msg->window;
    int w = mPreviewWidth;
    int h = mPreviewHeight;

    if (mPreviewWindow != NULL) {
        int usage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                    GRALLOC_USAGE_SW_READ_NEVER  |
                    GRALLOC_USAGE_HW_COMPOSER;
        mPreviewWindow->set_usage(mPreviewWindow, usage );
        mPreviewWindow->set_buffer_count(mPreviewWindow, 4);

        if (mRotation == 90 || mRotation == 270) {
            // We swap w and h
            w = mPreviewHeight;
            h = mPreviewWidth;
        }

        mPreviewWindow->set_buffers_geometry(mPreviewWindow, w, h,
                                             OMX_INTEL_COLOR_FormatYUV420PackedSemiPlanar);

    }

    return NO_ERROR;
}

status_t
PreviewThread::OverlayPreviewHandler::handleSetPreviewConfig(MessageSetPreviewConfig *msg)
{
    LOG1("@%s: width = %d (s:%d), height = %d, format = %x", __FUNCTION__,
         msg->width, msg->stride, msg->height, msg->format);
    status_t status = NO_ERROR;
    int w = msg->width;
    int h = msg->height;

    if ((msg->width != 0 && msg->height != 0) &&
            (mPreviewWidth != msg->width || mPreviewHeight != msg->height)) {
        LOG1("Setting new preview size: %dx%d", msg->width, msg->height);
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
            mPreviewWindow->set_buffers_geometry(mPreviewWindow, w, h,
                                                 OMX_INTEL_COLOR_FormatYUV420PackedSemiPlanar);

        }
        // we keep in our internal fields the resolution provided by CtrlThread
        mPreviewWidth = msg->width;
        mPreviewHeight = msg->height;
        mPreviewStride = msg->stride;

        allocateLocalPreviewBuf();
    }

    if ((msg->format != 0) && (mPreviewFormat != msg->format)) {
        switch(msg->format) {
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_RGB565:
            mPreviewFormat = msg->format;
            LOG1("Setting new preview format: %s", v4l2Fmt2Str(mPreviewFormat));
            break;

        default:
            LOGE("Invalid preview format: %x:%s", msg->format, v4l2Fmt2Str(msg->format));
            status = -1;
            break;
        }
    }

    return NO_ERROR;
}

/**
 * Rotates snapshot-postview buffer to preview window overlay buffer
 * for preview-keep-alive feature
 *
 * Note: expects the buffers to be of correct size with configuration
 * left from preview ran before snapshot.
 */
status_t PreviewThread::OverlayPreviewHandler::handlePostview(MessagePreview *msg)
{
    int err, stride;
    buffer_handle_t *buf;
    void *src;
    void *dst;
    LOG1("@%s: width = %d, height = %d (overlay)", __FUNCTION__,
         msg->buff.width, msg->buff.height);

    if (!mPreviewWindow) {
        LOGW("Unable to provide 'preview-keep-alive', no window!");
        return NO_ERROR;
    }

    if (msg->buff.type != ATOM_BUFFER_POSTVIEW) {
        // support implemented for using AtomISP postview type only
        LOGW("Unable to provide 'preview-keep-alive' frame, input buffer type unexpected");
        return UNKNOWN_ERROR;
    }

    if (msg->buff.width != mPreviewWidth ||
        msg->buff.height != mPreviewHeight) {
        LOGW("Unable to provide 'preview-keep-alive' frame, postview %dx%d -> preview %dx%d ",
                msg->buff.width, msg->buff.height, mPreviewWidth, mPreviewHeight);
        return UNKNOWN_ERROR;
    }

    src = msg->buff.buff->data;
    int paddedStride = paddingWidthNV12VED(mPreviewHeight,0);
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
    const Rect bounds(msg->buff.width, msg->buff.height);

    // queue one from the window
    err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf, &stride);
    if (err != 0) {
        LOGW("Error dequeuing preview buffer for 'preview-keep-alive'");
        return UNKNOWN_ERROR;
    }

    err = mapper.lock(*buf,
           GRALLOC_USAGE_SW_WRITE_OFTEN |
           GRALLOC_USAGE_SW_READ_NEVER  |
           GRALLOC_USAGE_HW_COMPOSER, bounds, &dst);
    if (err != 0) {
        mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
        return UNKNOWN_ERROR;
    }

    nv12rotateBy90(mPreviewStride,       // width of the source image
                   mPreviewHeight,       // height of the source image
                   mPreviewStride,       // scanline stride of the source image
                   paddedStride,         // scanline stride of the target image
                   (const char *) src,   // source image
                   (char*) dst);         // target image

    mapper.unlock(*buf);

    err = mPreviewWindow->enqueue_buffer(mPreviewWindow, buf);
    if (err != 0)
        LOGE("Surface::queueBuffer returned error %d", err);

    LOG2("@%s: done", __FUNCTION__);

    return NO_ERROR;
}

} // namespace android
