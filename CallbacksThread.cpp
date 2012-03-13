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
#define LOG_TAG "Atom_CallbacksThread"

#include "CallbacksThread.h"
#include "LogHelper.h"
#include "Callbacks.h"

namespace android {

CallbacksThread* CallbacksThread::mInstance = NULL;

CallbacksThread::CallbacksThread() :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("CallbacksThread", MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mCallbacks(Callbacks::getInstance())
    ,mJpegRequested(false)
{
    LOG1("@%s", __FUNCTION__);
}

CallbacksThread::~CallbacksThread()
{
    LOG1("@%s", __FUNCTION__);
    mInstance = NULL;
    clearJpegBuffers();
}

void CallbacksThread::clearJpegBuffers()
{
    LOG1("@%s", __FUNCTION__);
    mJpegRequested = false;
    for (size_t i = 0; i < mJpegBuffers.size(); i++) {
        AtomBuffer jpegBuf = mJpegBuffers[i];
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
    }
    mJpegBuffers.clear();
}

status_t CallbacksThread::shutterSound()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_CALLBACK_SHUTTER;
    return mMessageQueue.send(&msg);
}

status_t CallbacksThread::compressedFrameDone(AtomBuffer* jpegBuf)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_JPEG_DATA_READY;
    msg.data.compressedFrame.buff = *jpegBuf;
    return mMessageQueue.send(&msg);
}

status_t CallbacksThread::requestTakePicture()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_JPEG_DATA_REQUEST;
    return mMessageQueue.send(&msg);
}

status_t CallbacksThread::flushPictures()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FLUSH;
    mMessageQueue.clearAll();
    return mMessageQueue.send(&msg, MESSAGE_ID_FLUSH);
}

status_t CallbacksThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    clearJpegBuffers();
    return status;
}

status_t CallbacksThread::handleMessageCallbackShutter()
{
    LOG1("@%s", __FUNCTION__);
    mCallbacks->shutterSound();
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageJpegDataReady(MessageCompressedFrame *msg)
{
    LOG1("@%s: JPEG buffers queued: %d, mJpegRequested = %d", __FUNCTION__, mJpegBuffers.size(), mJpegRequested);
    AtomBuffer jpegBuf = msg->buff;
    if (mJpegRequested) {
        mCallbacks->compressedFrameDone(&jpegBuf);
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
        mJpegRequested = false;
    } else {
        // Insert the buffer on the top
        mJpegBuffers.push(jpegBuf);
    }
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageJpegDataRequest()
{
    LOG1("@%s: JPEG buffers queued: %d, mJpegRequested = %d", __FUNCTION__, mJpegBuffers.size(), mJpegRequested);
    if (!mJpegBuffers.isEmpty()) {
        AtomBuffer jpegBuf = mJpegBuffers[0];
        mCallbacks->compressedFrameDone(&jpegBuf);
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
        mJpegBuffers.removeAt(0);
    } else {
        mJpegRequested = true;
    }
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageFlush()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    clearJpegBuffers();
    mMessageQueue.reply(MESSAGE_ID_FLUSH, status);
    return status;
}

status_t CallbacksThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;

        case MESSAGE_ID_CALLBACK_SHUTTER:
            status = handleMessageCallbackShutter();
            break;

        case MESSAGE_ID_JPEG_DATA_READY:
            status = handleMessageJpegDataReady(&msg.data.compressedFrame);
            break;

        case MESSAGE_ID_JPEG_DATA_REQUEST:
            status = handleMessageJpegDataRequest();
            break;

        case MESSAGE_ID_FLUSH:
            status = handleMessageFlush();
            break;

        default:
            status = BAD_VALUE;
            break;
    };
    return status;
}

bool CallbacksThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    return false;
}

status_t CallbacksThread::requestExitAndWait()
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
