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

PreviewThread::PreviewThread(ICallbackPreview *previewDone) :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PreviewThread", (int) MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mPreviewDoneCallback(previewDone)
    ,mMessageHandler(NULL)
{
    LOG1("@%s", __FUNCTION__);


    mMessageHandler = new GfxPreviewHandler(this, previewDone);

    assert(mMessageHandler != NULL);
}

PreviewThread::~PreviewThread()
{
    LOG1("@%s", __FUNCTION__);
    mMessageHandler->mDebugFPS.clear();
    delete mMessageHandler;
}

status_t PreviewThread::enableOverlay(bool set)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    delete mMessageHandler;
    mMessageHandler = NULL;

    if(set)
        mMessageHandler = new OverlayPreviewHandler(this, mPreviewDoneCallback);
    else
        mMessageHandler = new GfxPreviewHandler(this, mPreviewDoneCallback);

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
            break;

        case MESSAGE_ID_POSTVIEW:
            status = mMessageHandler->handlePostview(&msg.data.preview);
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
PreviewThread::PreviewMessageHandler::PreviewMessageHandler(PreviewThread* aThread, ICallbackPreview *previewDone) :
            mPreviewWindow(NULL)
           ,mPvThread(aThread)
           ,mPreviewBuf(AtomBufferFactory::createAtomBuffer(ATOM_BUFFER_PREVIEW))
           ,mCallbacks(Callbacks::getInstance())
           ,mCallbacksThread(CallbacksThread::getInstance())
           ,mPreviewDoneCallback(previewDone)
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

PreviewThread::GfxPreviewHandler::GfxPreviewHandler(PreviewThread* aThread, ICallbackPreview *previewDone) :
         PreviewMessageHandler(aThread, previewDone)
        ,mBuffersInWindow(0)
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
 * Calls previewDone callback if data is available
 *
 * Handles both internally and externally allocated preview
 * frames.
 */
status_t
PreviewThread::GfxPreviewHandler::callPreviewDone(MessagePreview *msg)
{
    LOG2("@%s", __FUNCTION__);

    if (msg->buff.type == ATOM_BUFFER_PREVIEW) {
        mPreviewDoneCallback->previewDone(&(msg->buff));
        return NO_ERROR;
    }

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
                    mPreviewDoneCallback->previewDone(&(mPreviewBuffers.editItemAt(i)));
                    break;
                }
            }
            if (i == mPreviewBuffers.size()) {
                LOGW("unknown gfx buffer dequeued, i %d, ptr %p",
                     i, mPreviewBuffers[i].mNativeBufPtr);
                mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
            }
        }
    }
    else {
        LOG2("@%s: %d buffers in window, not enough, need %d",
             __FUNCTION__, mBuffersInWindow, mMinUndequeued);
    }

    return NO_ERROR;
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

    LOG2("Buff: id = %d, data = %p",
            msg->buff.id,
            msg->buff.gfxData);

    if ((mPreviewWindow != 0) && (msg->buff.type == ATOM_BUFFER_PREVIEW_GFX)){
        int err;

        if ((err = mPreviewWindow->enqueue_buffer(mPreviewWindow, msg->buff.mNativeBufPtr)) != 0) {
            LOGE("Surface::queueBuffer returned error %d", err);
        }
        else {
            for (size_t i = 0; i< mPreviewInClient.size(); i++) {
                buffer_handle_t *bufHandle =
                    mPreviewBuffers[mPreviewInClient[i]].mNativeBufPtr;
                if (msg->buff.mNativeBufPtr == bufHandle) {
                    mPreviewInClient.removeAt(i);
                    break;
                }
            }
            mBuffersInWindow++;

            // preview frame shown, update perf traces
            PERFORMANCE_TRACES_PREVIEW_SHOWN(msg->buff.frameCounter);
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

    mDebugFPS->update(); // update fps counter
    status = callPreviewDone(msg);

    return status;
}

status_t
PreviewThread::GfxPreviewHandler::handleSetPreviewWindow(MessageSetPreviewWindow *msg)
{
    LOG1("@%s: window = %p", __FUNCTION__, msg->window);
    if (mPreviewWindow != NULL) {
        freeGfxPreviewBuffers();
    }
    mPreviewWindow = msg->window;

    if (mPreviewWindow != NULL) {
        LOG1("Setting new preview window %p", mPreviewWindow);
        int previewWidthPadded = mPreviewWidth;

        // write-often: main use-case, stream image data to window
        // read-rarely: 2nd use-case, memcpy to application data callback
        mPreviewWindow->set_usage(mPreviewWindow,
                                  (GRALLOC_USAGE_SW_READ_RARELY |
                                   GRALLOC_USAGE_SW_WRITE_OFTEN));
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

status_t
PreviewThread::GfxPreviewHandler::handleFetchPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

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
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();

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

        AtomBuffer tmpBuf;
        int err, stride;
        buffer_handle_t *buf;
        void *dst;
        int lockMode = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_NEVER;
        const Rect bounds(mPreviewWidth, mPreviewHeight);

        for (int i = 0; i < numberOfBuffers; i++) {
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
    } else {
        status = INVALID_OPERATION;
    }

    return status;

freeDeQueued:
    for( size_t i = 0; i < mPreviewBuffers.size(); i++) {
        mapper.unlock(*(mPreviewBuffers[i].mNativeBufPtr));
        mPreviewWindow->cancel_buffer(mPreviewWindow, mPreviewBuffers[i].mNativeBufPtr);
    }
    mPreviewBuffers.clear();
    mPreviewInClient.clear();
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

    if (!mPreviewInClient.isEmpty()) {
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

PreviewThread::OverlayPreviewHandler::OverlayPreviewHandler(PreviewThread* aThread, ICallbackPreview *previewDone) :
        PreviewMessageHandler(aThread, previewDone)
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

    if (mPreviewWindow != 0) {
       buffer_handle_t *buf;
       int err;
       int stride;
       int usage;
       int paddedStride = paddingWidthNV12VED(mPreviewHeight,0);
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
           const Rect bounds(mPreviewHeight, mPreviewWidth);
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

           nv12rotateBy90(mPreviewStride,        // width of the source image
                          mPreviewHeight,       // height of the source image
                          mPreviewStride,       // scanline stride of the source image
                          paddedStride,         // scanline stride of the target image
                          (const char *)msg->buff.buff->data,      // source image
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
    mPreviewDoneCallback->previewDone(&msg->buff);

    return status;
}

status_t
PreviewThread::OverlayPreviewHandler::handleSetPreviewWindow(MessageSetPreviewWindow *msg)
{
    LOG1("@%s: window = %p", __FUNCTION__, msg->window);

    mPreviewWindow = msg->window;

    if (mPreviewWindow != NULL) {
        int usage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                    GRALLOC_USAGE_SW_READ_NEVER  |
                    GRALLOC_USAGE_HW_COMPOSER;
        mPreviewWindow->set_usage(mPreviewWindow, usage );
        mPreviewWindow->set_buffer_count(mPreviewWindow, 4);
        mPreviewWindow->set_buffers_geometry(mPreviewWindow,
                                             mPreviewHeight,
                                             mPreviewWidth,
                                             OMX_INTEL_COLOR_FormatYUV420PackedSemiPlanar);
    }

    return NO_ERROR;
}

status_t
PreviewThread::OverlayPreviewHandler::handleSetPreviewConfig(MessageSetPreviewConfig *msg)
{
    LOG1("@%s: width = %d, height = %d, format = %x", __FUNCTION__,
         msg->width, msg->height, msg->format);
    status_t status = NO_ERROR;

    if ((msg->width != 0 && msg->height != 0) &&
            (mPreviewWidth != msg->width || mPreviewHeight != msg->height)) {
        LOG1("Setting new preview size: %dx%d", msg->width, msg->height);
        if (mPreviewWindow != NULL) {

            // if preview size changed, update the preview window
            mPreviewWindow->set_buffers_geometry(mPreviewWindow,
                                                 msg->height,
                                                 msg->width,
                                                 OMX_INTEL_COLOR_FormatYUV420PackedSemiPlanar);
        }
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
