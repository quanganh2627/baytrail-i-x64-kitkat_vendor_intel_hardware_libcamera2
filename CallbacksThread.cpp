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
    ,mPostviewRequested(false)
    ,mRawRequested(false)
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

status_t CallbacksThread::compressedFrameDone(AtomBuffer* jpegBuf)
{
    LOG1("@%s: ID = %d", __FUNCTION__, jpegBuf->id);
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

status_t CallbacksThread::postCaptureFrames(AtomBuffer* postviewBuf, AtomBuffer* snapshotBuf)
{
    LOG1("@%s: ID post %d : ID raw %d", __FUNCTION__, postviewBuf->id, snapshotBuf->id);
    Message msg;
    msg.id = MESSAGE_ID_POSTCAPTURE_READY;
    msg.data.postCaptureFrame.postView = *postviewBuf;
    msg.data.postCaptureFrame.snapshot = *snapshotBuf;
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
        AtomBuffer buff = vect[i].data.compressedFrame.buff;
        buff.buff->release(buff.buff);
    }
    vect.clear();

    mMessageQueue.remove(MESSAGE_ID_POSTCAPTURE_READY, &vect);

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
    LOG1("@%s: JPEG buffers queued: %d, mJpegRequested = %d", __FUNCTION__, mJpegBuffers.size(), mJpegRequested);
    AtomBuffer jpegBuf = msg->buff;
    AtomBuffer rawBuf = mRawBuffers[0];
    AtomBuffer postviewBuf= mPostviewBuffers[0];

    if (jpegBuf.buff == NULL) {
        LOGW("@%s: returning raw frames used in failed encoding", __FUNCTION__);
        goto justReturn;
    }

    if (mJpegRequested) {
        mCallbacks->compressedFrameDone(&jpegBuf);
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
        mJpegRequested = false;
    } else {
        // Insert the buffer on the top
        mJpegBuffers.push(jpegBuf);
    }

    if (mPostviewRequested) {
        mCallbacks->postviewFrameDone(&postviewBuf);
        mPostviewRequested = false;
    }

    if(jpegBuf.id != postviewBuf.id)
        LOGW("@%s: received jpeg buf id does not match the raw frames id... find the bug", __FUNCTION__);

justReturn:
    // When the encoding is done, send back the buffers to Control thread
    mPictureDoneCallback->pictureDone(&rawBuf, &postviewBuf);
    mRawBuffers.removeAt(0);
    mPostviewBuffers.removeAt(0);
    return NO_ERROR;
}

status_t CallbacksThread::handleMessagePostCaptureDataReady(MessagePostCaptureFrame *msg)
{
    LOG1("@%s: ID: %d",__FUNCTION__, msg->postView.id);
    AtomBuffer pvBuf = msg->postView;
    AtomBuffer snapshotBuf = msg->snapshot;

/*    if (mPostviewRequested) {
        mCallbacks->postviewFrameDone(&pvBuf);
        mPostviewRequested = false;
    }*/
    if (mRawRequested) {
        mCallbacks->rawFrameDone(&snapshotBuf);
        mRawRequested = false;
    }

    // Insert the buffers on the top
    mPostviewBuffers.push(pvBuf);
    mRawBuffers.push(snapshotBuf);

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

    mPostviewRequested = true;
    mRawRequested = true;
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageFlush()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mJpegRequested = false;
    for (size_t i = 0; i < mJpegBuffers.size(); i++) {
        AtomBuffer jpegBuf = mJpegBuffers[i];
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
    }
    mJpegBuffers.clear();
    mPostviewBuffers.clear();
    mRawBuffers.clear();
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
            status = handleMessageJpegDataRequest();
            break;

        case MESSAGE_ID_POSTCAPTURE_READY:
            status = handleMessagePostCaptureDataReady(&msg.data.postCaptureFrame);
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
