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
#include "ColorConverter.h"
#include <gui/Surface.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>
#include "AtomCommon.h"

namespace android {

PreviewThread::PreviewThread(ICallbackPreview *previewDone) :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PreviewThread", (int) MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mDebugFPS(new DebugFrameRate())
    ,mPreviewDoneCallback(previewDone)
    ,mCallbacks(Callbacks::getInstance())
    ,mPreviewWindow(NULL)
    ,mPreviewWidth(640)
    ,mPreviewHeight(480)
    ,mPreviewFormat(V4L2_PIX_FMT_NV21)
    ,mBuffersInWindow(0)
{
    LOG1("@%s", __FUNCTION__);
    mPreviewBuffers.setCapacity(MAX_NUMBER_PREVIEW_GFX_BUFFERS);
    mPreviewInClient.setCapacity(MAX_NUMBER_PREVIEW_GFX_BUFFERS);
    mPreviewBuf.buff = 0;
}

PreviewThread::~PreviewThread()
{
    LOG1("@%s", __FUNCTION__);
    mDebugFPS.clear();
    freeLocalPreviewBuf();
    freeGfxPreviewBuffers();
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

status_t PreviewThread::setPreviewWindow(struct preview_stream_ops *window)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_PREVIEW_WINDOW;
    msg.data.setPreviewWindow.window = window;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::setPreviewConfig(int preview_width, int preview_height,
                                         int preview_format, int buffer_count)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_PREVIEW_CONFIG;
    msg.data.setPreviewConfig.width = preview_width;
    msg.data.setPreviewConfig.height = preview_height;
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

    *pvBufs = mPreviewBuffers.editArray();
    *count = mPreviewBuffers.size();

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

    return mMessageQueue.send(&msg);
}
status_t PreviewThread::preview(AtomBuffer *buff)
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_PREVIEW;
    msg.data.preview.buff = *buff;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::flushBuffers()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FLUSH;
    mMessageQueue.remove(MESSAGE_ID_PREVIEW);
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
 * Calls previewDone callback if data is available
 *
 * Handles both internally and externally allocated preview
 * frames.
 */
status_t PreviewThread::callPreviewDone(MessagePreview *msg)
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
 *  handler for each preview frame
 *
 * */
status_t PreviewThread::handleMessagePreview(MessagePreview *msg)
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
            NV12ToYV12(mPreviewWidth, mPreviewHeight, src, mPreviewBuf.buff->data);
            break;

        case V4L2_PIX_FMT_NV21:
            NV12ToNV21(mPreviewWidth, mPreviewHeight, src, mPreviewBuf.buff->data);
            break;

        default:
            memcpy(mPreviewBuf.buff->data,  src,  msg->buff.size);
            break;
        }
        mCallbacks->previewFrameDone(&mPreviewBuf);
    }

    mDebugFPS->update(); // update fps counter

    status = callPreviewDone(msg);

    return status;
}

status_t PreviewThread::handleMessageSetPreviewWindow(MessageSetPreviewWindow *msg)
{
    LOG1("@%s: window = %p", __FUNCTION__, msg->window);
    status_t status = NO_ERROR;

    if (mPreviewWindow != NULL) {
        freeGfxPreviewBuffers();
    }
    mPreviewWindow = msg->window;

    if (mPreviewWindow != NULL) {
        LOG1("Setting new preview window %p", mPreviewWindow);
        int previewWidthPadded =
            paddingWidth(V4L2_PIX_FMT_NV12, mPreviewWidth, mPreviewHeight);

        // write-often: main use-case, stream image data to window
        // read-rarely: 2nd use-case, memcpy to application data callback
        mPreviewWindow->set_usage(mPreviewWindow,
                                  (GRALLOC_USAGE_SW_READ_RARELY |
                                   GRALLOC_USAGE_SW_WRITE_OFTEN));
        mPreviewWindow->set_buffers_geometry(
                mPreviewWindow,
                previewWidthPadded,
                mPreviewHeight,
                HAL_PIXEL_FORMAT_YV12);
    }

    return NO_ERROR;
}

status_t PreviewThread::handleMessageSetPreviewConfig(MessageSetPreviewConfig *msg)
{
    LOG1("@%s: width = %d, height = %d, format = %x", __FUNCTION__,
         msg->width, msg->height, msg->format);
    status_t status = NO_ERROR;

    if ((msg->width != 0 && msg->height != 0) &&
            (mPreviewWidth != msg->width || mPreviewHeight != msg->height)) {
        LOG1("Setting new preview size: %dx%d", msg->width, msg->height);
        if (mPreviewWindow != NULL) {
            int previewWidthPadded = paddingWidth(V4L2_PIX_FMT_NV12, msg->width, msg->height);
            // if preview size changed, update the preview window
            mPreviewWindow->set_buffers_geometry(mPreviewWindow,
                                                 previewWidthPadded,
                                                 msg->height,
                                                 HAL_PIXEL_FORMAT_YV12);

        }
        mPreviewWidth = msg->width;
        mPreviewHeight = msg->height;

        allocateLocalPreviewBuf();
    }

    status = allocateGfxPreviewBuffers(msg->bufferCount);

    if ((msg->format != 0) && (mPreviewFormat != msg->format)) {
        switch(msg->format) {
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_NV21:
            mPreviewFormat = msg->format;
            LOG1("Setting new preview format: %s", v4l2Fmt2Str(mPreviewFormat));
            break;

        default:
            LOGE("Invalid preview format: %x:%s", msg->format, v4l2Fmt2Str(msg->format));
            status = -1;
        }
    }

    return status;
}

status_t PreviewThread::handleMessageFetchPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mMessageQueue.reply(MESSAGE_ID_FETCH_PREVIEW_BUFS, status);
    return status;
}

status_t PreviewThread::handleMessageReturnPreviewBuffers()
{
    LOG1("@%s", __FUNCTION__);

    return freeGfxPreviewBuffers();
}

status_t PreviewThread::handleMessageFlush()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_FLUSH, status);
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
            status = handleMessagePreview(&msg.data.preview);
            break;

        case MESSAGE_ID_SET_PREVIEW_WINDOW:
            status = handleMessageSetPreviewWindow(&msg.data.setPreviewWindow);
            break;

        case MESSAGE_ID_SET_PREVIEW_CONFIG:
            status = handleMessageSetPreviewConfig(&msg.data.setPreviewConfig);
            break;

        case MESSAGE_ID_FLUSH:
            status = handleMessageFlush();
            break;

        case MESSAGE_ID_FETCH_PREVIEW_BUFS:
            status = handleMessageFetchPreviewBuffers();
            break;
        case MESSAGE_ID_RETURN_PREVIEW_BUFS:
           status = handleMessageReturnPreviewBuffers();
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
    LOG1("allocating the preview buffer\n");
    freeLocalPreviewBuf();
    mCallbacks->allocateMemory(&mPreviewBuf, mPreviewWidth*mPreviewHeight*3/2);
    if(!mPreviewBuf.buff) {
        LOGE("getting memory failed\n");
    }
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

} // namespace android
