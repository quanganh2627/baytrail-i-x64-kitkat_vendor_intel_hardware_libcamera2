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
#define LOG_TAG "Atom_PreviewThread"

#include "PreviewThread.h"
#include "LogHelper.h"
#include "DebugFrameRate.h"
#include "Callbacks.h"
#include "ColorConverter.h"
#include <ui/android_native_buffer.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>

namespace android {

PreviewThread::PreviewThread(ICallbackPreview *previewDone) :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PreviewThread")
    ,mThreadRunning(false)
    ,mDebugFPS(new DebugFrameRate())
    ,mPreviewDoneCallback(previewDone)
    ,mCallbacks(NULL)
    ,mPreviewWindow(NULL)
    ,mPreviewWidth(640)
    ,mPreviewHeight(480)
{
    LOG_FUNCTION
}

PreviewThread::~PreviewThread()
{
    LOG_FUNCTION
    mDebugFPS.clear();
}

void PreviewThread::setCallbacks(Callbacks *callbacks)
{
    LOG_FUNCTION
    mCallbacks = callbacks;
}

status_t PreviewThread::setPreviewWindow(struct preview_stream_ops *window)
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_SET_PREVIEW;
    msg.data.setPreview.window = window;
    msg.data.setPreview.width = 0;
    msg.data.setPreview.height = 0;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::setPreviewSize(int preview_width, int preview_height)
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_SET_PREVIEW;
    msg.data.setPreview.window = NULL;
    msg.data.setPreview.width = preview_width;
    msg.data.setPreview.height = preview_height;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::preview(AtomBuffer *buff)
{
    LOG_FUNCTION2
    Message msg;
    msg.id = MESSAGE_ID_PREVIEW;
    msg.data.preview.buff = buff;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::handleMessageExit()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done
    mPreviewWindow = NULL;

    return status;
}

status_t PreviewThread::handleMessagePreview(MessagePreview *msg)
{
    LOG_FUNCTION2
    status_t status = NO_ERROR;

    LogDetail2("Buff: id = %d, data = %p",
            msg->buff->id,
            msg->buff->buff->data);

    if (mPreviewWindow != 0) {
        buffer_handle_t *buf;
        int err;
        int stride;
        if ((err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf, &stride)) != 0) {
            LogError("Surface::dequeueBuffer returned error %d", err);
        } else {
            if (mPreviewWindow->lock_buffer(mPreviewWindow, buf) != NO_ERROR) {
                LogError("Failed to lock preview buffer!");
                mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
                return status;
            }
            GraphicBufferMapper &mapper = GraphicBufferMapper::get();
            const Rect bounds(mPreviewWidth, mPreviewHeight);
            void *dst;

            if (mapper.lock(*buf, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst) != NO_ERROR) {
                LogError("Failed to lock GraphicBufferMapper!");
                mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
                return status;
            }
            NV12ToRGB565(mPreviewWidth,
                    mPreviewHeight,
                    msg->buff->buff->data,
                    dst);
            if ((err = mPreviewWindow->enqueue_buffer(mPreviewWindow, buf)) != 0) {
                LogError("Surface::queueBuffer returned error %d", err);
            }
            mapper.unlock(*buf);
        }
        buf = NULL;
    } else {
        mCallbacks->previewFrameDone(msg->buff);
    }

    mDebugFPS->update(); // update fps counter
    mPreviewDoneCallback->previewDone(msg->buff);

    return status;
}

status_t PreviewThread::handleMessageSetPreview(MessageSetPreview *msg)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    LogDetail2("Preview: window = %p, width = %d, height = %d",
                msg->window,
                msg->width,
                msg->height);

    if (msg->width != 0 && msg->width != 0) {
        mPreviewWidth = msg->width;
        mPreviewHeight = msg->height;
        LogDetail("Setting new preview size: %dx%d", mPreviewWidth, mPreviewHeight);
    }
    if (msg->window != NULL) {
        mPreviewWindow = msg->window;
        LogDetail("Setting new preview window %p", mPreviewWindow);
        mPreviewWindow->set_usage(mPreviewWindow, GRALLOC_USAGE_SW_WRITE_OFTEN);
        mPreviewWindow->set_buffers_geometry(
                mPreviewWindow,
                mPreviewWidth,
                mPreviewHeight,
                HAL_PIXEL_FORMAT_RGB_565);
    }

    return NO_ERROR;
}

status_t PreviewThread::waitForAndExecuteMessage()
{
    LOG_FUNCTION2
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

        case MESSAGE_ID_SET_PREVIEW:
            status = handleMessageSetPreview(&msg.data.setPreview);
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
    LOG_FUNCTION2
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
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_EXIT;

    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

} // namespace android
