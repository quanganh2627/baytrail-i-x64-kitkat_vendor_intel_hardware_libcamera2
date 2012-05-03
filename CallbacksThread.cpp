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
    ,mJpegRequested(0)
    ,mPostviewRequested(0)
    ,mRawRequested(0)
{
    LOG1("@%s", __FUNCTION__);
}

CallbacksThread::~CallbacksThread()
{
    LOG1("@%s", __FUNCTION__);
    mInstance = NULL;
}

status_t CallbacksThread::shutterSound()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_CALLBACK_SHUTTER;
    return mMessageQueue.send(&msg);
}

status_t CallbacksThread::compressedFrameDone(AtomBuffer* jpegBuf, AtomBuffer* snapshotBuf, AtomBuffer* postviewBuf)
{
    LOG1("@%s: ID = %d", __FUNCTION__, jpegBuf->id);
    Message msg;
    msg.id = MESSAGE_ID_JPEG_DATA_READY;
    msg.data.compressedFrame.jpegBuff.buff = NULL;
    msg.data.compressedFrame.snapshotBuff.buff = NULL;
    msg.data.compressedFrame.postviewBuff.buff = NULL;
    if (jpegBuf != NULL) {
        msg.data.compressedFrame.jpegBuff = *jpegBuf;
    }
    if (snapshotBuf != NULL) {
        msg.data.compressedFrame.snapshotBuff = *snapshotBuf;
    }
    if (postviewBuf != NULL) {
        msg.data.compressedFrame.postviewBuff = *postviewBuf;
    }
    return mMessageQueue.send(&msg);
}


status_t CallbacksThread::requestTakePicture(bool postviewCallback, bool rawCallback)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_JPEG_DATA_REQUEST;
    msg.data.dataRequest.postviewCallback = postviewCallback;
    msg.data.dataRequest.rawCallback = rawCallback;
    return mMessageQueue.send(&msg);
}

status_t CallbacksThread::flushPictures()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FLUSH;

    Vector<Message> vect;
    mMessageQueue.remove(MESSAGE_ID_JPEG_DATA_READY, &vect);

    // deallocate all the buffers we are flushing
    for (size_t i = 0; i < vect.size(); i++) {
        LOG1("Caught a mem leak. Woohoo!");
        AtomBuffer buff = vect[i].data.compressedFrame.jpegBuff;
        buff.buff->release(buff.buff);
    }
    vect.clear();

    mMessageQueue.remove(MESSAGE_ID_JPEG_DATA_REQUEST, NULL); // there is no data for this message

    return mMessageQueue.send(&msg, MESSAGE_ID_FLUSH);
}

void CallbacksThread::facesDetected(camera_frame_metadata_t &face_metadata)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FACES;
    msg.data.faces.meta_data= face_metadata;
    mMessageQueue.send(&msg);
}

status_t CallbacksThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

status_t CallbacksThread::handleMessageCallbackShutter()
{
    LOG1("@%s", __FUNCTION__);
    mCallbacks->shutterSound();
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageJpegDataReady(MessageFrame *msg)
{
    LOG1("@%s: JPEG buffers queued: %d, mJpegRequested = %u, mPostviewRequested = %u, mRawRequested = %u",
            __FUNCTION__,
            mBuffers.size(),
            mJpegRequested,
            mPostviewRequested,
            mRawRequested);
    AtomBuffer jpegBuf = msg->jpegBuff;
    AtomBuffer snapshotBuf = msg->snapshotBuff;
    AtomBuffer postviewBuf= msg->postviewBuff;

    if (jpegBuf.buff == NULL && snapshotBuf.buff != NULL && postviewBuf.buff != NULL) {
        LOGW("@%s: returning raw frames used in failed encoding", __FUNCTION__);
        mPictureDoneCallback->pictureDone(&snapshotBuf, &postviewBuf);
        return NO_ERROR;
    }

    if (mJpegRequested > 0) {
        if (mPostviewRequested > 0) {
            mCallbacks->postviewFrameDone(&postviewBuf);
            mPostviewRequested--;
        }
        if (mRawRequested > 0) {
            mCallbacks->rawFrameDone(&snapshotBuf);
            mRawRequested--;
        }
        mCallbacks->compressedFrameDone(&jpegBuf);
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
        mJpegRequested--;
        if (snapshotBuf.buff != NULL && postviewBuf.buff != NULL) {
            // Return the raw buffers back to ISP
            mPictureDoneCallback->pictureDone(&snapshotBuf, &postviewBuf);
        }
    } else {
        // Insert the buffer on the top
        mBuffers.push(*msg);
    }

    return NO_ERROR;
}

status_t CallbacksThread::handleMessageJpegDataRequest(MessageDataRequest *msg)
{
    LOG1("@%s: JPEG buffers queued: %d, mJpegRequested = %u, mPostviewRequested = %u, mRawRequested = %u",
            __FUNCTION__,
            mBuffers.size(),
            mJpegRequested,
            mPostviewRequested,
            mRawRequested);
    if (!mBuffers.isEmpty()) {
        AtomBuffer jpegBuf = mBuffers[0].jpegBuff;
        AtomBuffer snapshotBuf = mBuffers[0].snapshotBuff;
        AtomBuffer postviewBuf = mBuffers[0].postviewBuff;
        if (msg->postviewCallback) {
            mCallbacks->postviewFrameDone(&postviewBuf);
        }
        if (msg->rawCallback) {
            mCallbacks->rawFrameDone(&snapshotBuf);
        }
        mCallbacks->compressedFrameDone(&jpegBuf);
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
        if (snapshotBuf.buff != NULL && postviewBuf.buff != NULL) {
            // Return the raw buffers back to ISP
            mPictureDoneCallback->pictureDone(&snapshotBuf, &postviewBuf);
        }
        mBuffers.removeAt(0);
    } else {
        mJpegRequested++;
        if (msg->postviewCallback) {
            mPostviewRequested++;
        }
        if (msg->rawCallback) {
            mRawRequested++;
        }
    }

    return NO_ERROR;
}

status_t CallbacksThread::handleMessageFlush()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mJpegRequested = 0;
    mPostviewRequested = 0;
    mRawRequested = 0;
    for (size_t i = 0; i < mBuffers.size(); i++) {
        AtomBuffer jpegBuf = mBuffers[i].jpegBuff;
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
    }
    mBuffers.clear();
    mMessageQueue.reply(MESSAGE_ID_FLUSH, status);
    return status;
}

status_t CallbacksThread::handleMessageFaces(MessageFaces *msg)
{
    mCallbacks->facesDetected(msg->meta_data);
    return NO_ERROR;
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
            status = handleMessageJpegDataRequest(&msg.data.dataRequest);
            break;

        case MESSAGE_ID_FLUSH:
            status = handleMessageFlush();
            break;

        case MESSAGE_ID_FACES:
            status = handleMessageFaces(&msg.data.faces);
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
