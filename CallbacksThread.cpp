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
#define LOG_TAG "Camera_CallbacksThread"

#include "CallbacksThread.h"
#include "LogHelper.h"
#include "Callbacks.h"
#include "FaceDetector.h"

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
    mFaceMetadata.faces = new camera_face_t[MAX_FACES_DETECTABLE];
    memset(mFaceMetadata.faces, 0, MAX_FACES_DETECTABLE * sizeof(camera_face_t));
    mFaceMetadata.number_of_faces = 0;
}

CallbacksThread::~CallbacksThread()
{
    LOG1("@%s", __FUNCTION__);
    delete [] mFaceMetadata.faces;
    mFaceMetadata.faces = NULL;
    mInstance = NULL;
}

status_t CallbacksThread::shutterSound()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_CALLBACK_SHUTTER;
    return mMessageQueue.send(&msg);
}

void CallbacksThread::panoramaDisplUpdate(camera_panorama_metadata_t &metadata)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_PANORAMA_DISPL_UPDATE;
    msg.data.panoramaDisplUpdate.metadata = metadata;
    mMessageQueue.send(&msg);
}

status_t CallbacksThread::handleMessagePanoramaDisplUpdate(MessagePanoramaDisplUpdate *msg)
{
    LOG1("@%s", __FUNCTION__);
    mCallbacks->panoramaDisplUpdate(msg->metadata);
    return OK;
}

void CallbacksThread::panoramaSnapshot(AtomBuffer &livePreview)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_PANORAMA_SNAPSHOT;
    msg.data.panoramaSnapshot.snapshot = livePreview;
    mMessageQueue.send(&msg);
}

status_t CallbacksThread::handleMessagePanoramaSnapshot(MessagePanoramaSnapshot *msg)
{
    LOG1("@%s", __FUNCTION__);
    mCallbacks->panoramaSnapshot(msg->snapshot);
    return OK;
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
    return mMessageQueue.send(&msg, MESSAGE_ID_FLUSH);
}

void CallbacksThread::autofocusDone(bool status)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.data.autoFocusDone.status = status;
    msg.id = MESSAGE_ID_AUTO_FOCUS_DONE;
    mMessageQueue.send(&msg);
}

status_t CallbacksThread::handleMessageAutoFocusDone(MessageAutoFocusDone *msg)
{
    LOG1("@%s", __FUNCTION__);
    mCallbacks->autofocusDone(msg->status);
    return NO_ERROR;
}

void CallbacksThread::facesDetected(camera_frame_metadata_t &face_metadata)
{
    LOG1("@%s", __FUNCTION__);
    int num_faces;
    if (face_metadata.number_of_faces > MAX_FACES_DETECTABLE) {
        LOGW("@%s: %d faces detected, limiting to %d", __FUNCTION__,
            face_metadata.number_of_faces, MAX_FACES_DETECTABLE);
        num_faces = MAX_FACES_DETECTABLE;
    } else {
        num_faces = face_metadata.number_of_faces;
    }
    mFaceMetadata.number_of_faces = num_faces;
    memcpy(mFaceMetadata.faces, face_metadata.faces, mFaceMetadata.number_of_faces * sizeof(camera_face_t));

    Message msg;
    msg.id = MESSAGE_ID_FACES;
    msg.data.faces.meta_data = mFaceMetadata;
    mMessageQueue.send(&msg);
}

status_t CallbacksThread::sceneDetected(int sceneMode, bool sceneHdr)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SCENE_DETECTED;
    msg.data.sceneDetected.sceneMode = sceneMode;
    msg.data.sceneDetected.sceneHdr = sceneHdr;
    return mMessageQueue.send(&msg);
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
        if ((snapshotBuf.buff != NULL && postviewBuf.buff != NULL) ||
            snapshotBuf.type == ATOM_BUFFER_PANORAMA) {
            // Return the raw buffers back to ControlThread
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
    LOG1("@%s", __FUNCTION__);
    mCallbacks->facesDetected(msg->meta_data);
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageSceneDetected(MessageSceneDetected *msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mCallbacks->sceneDetected(msg->sceneMode, msg->sceneHdr);
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
            status = handleMessageJpegDataRequest(&msg.data.dataRequest);
            break;

        case MESSAGE_ID_AUTO_FOCUS_DONE:
            status = handleMessageAutoFocusDone(&msg.data.autoFocusDone);
            break;

        case MESSAGE_ID_FLUSH:
            status = handleMessageFlush();
            break;

        case MESSAGE_ID_FACES:
            status = handleMessageFaces(&msg.data.faces);
            break;

        case MESSAGE_ID_SCENE_DETECTED:
            status = handleMessageSceneDetected(&msg.data.sceneDetected);
            break;

        case MESSAGE_ID_PANORAMA_DISPL_UPDATE:
            status = handleMessagePanoramaDisplUpdate(&msg.data.panoramaDisplUpdate);
            break;

        case MESSAGE_ID_PANORAMA_SNAPSHOT:
            status = handleMessagePanoramaSnapshot(&msg.data.panoramaSnapshot);
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
