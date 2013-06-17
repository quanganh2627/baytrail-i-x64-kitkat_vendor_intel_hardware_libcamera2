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
#include "MemoryUtils.h"
#include "PerformanceTraces.h"

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
    ,mULLRequested(0)
    ,mWaitRendering(false)
{
    LOG1("@%s", __FUNCTION__);
    mFaceMetadata.faces = new camera_face_t[MAX_FACES_DETECTABLE];
    memset(mFaceMetadata.faces, 0, MAX_FACES_DETECTABLE * sizeof(camera_face_t));
    mFaceMetadata.number_of_faces = 0;
    mPostponedJpegReady.id = (MessageId) -1;
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

void CallbacksThread::panoramaSnapshot(const AtomBuffer &livePreview)
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
    MemoryUtils::freeAtomBuffer(msg->snapshot);
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

/**
 * Sends an "ULL triggered"-callback to the application
 * \param id ID of the post-processed ULL snapshot that will be provided to the
 * application after the post-processing is done.
 */
status_t CallbacksThread::ullTriggered(int id)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ULL_TRIGGERED;
    msg.data.ull.id = id;

    return mMessageQueue.send(&msg);
}

/**
 * Requests a ULL capture to be sent to client
 * the next JPEG image done received by the CallbackThread will be returned to
 * the client via  a custom callback rather than the normal JPEG data callabck
 * \param id [in] Running number identifying the ULL capture. It matches the
 * number provided to the application when ULL starts
 */
status_t CallbacksThread::requestULLPicture(int id)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ULL_JPEG_DATA_REQUEST;
    msg.data.ull.id = id;

    return mMessageQueue.send(&msg);
}

status_t  CallbacksThread::previewFrameDone(AtomBuffer *aPreviewFrame)
{
    if(aPreviewFrame == NULL) return BAD_VALUE;

    LOG2("@%s: ID = %d", __FUNCTION__, aPreviewFrame->id);
    Message msg;
    msg.id = MESSAGE_ID_PREVIEW_DONE;
    msg.data.preview.frame = *aPreviewFrame;

    return mMessageQueue.send(&msg);

}

status_t CallbacksThread::postviewRendered()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_POSTVIEW_RENDERED;
    return mMessageQueue.send(&msg);
}

status_t CallbacksThread::handleMessagePostviewRendered()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (mWaitRendering) {
        mWaitRendering = false;
        // check if handling of jpeg data ready was postponed for this
        if (mPostponedJpegReady.id == MESSAGE_ID_JPEG_DATA_READY) {
           status = handleMessageJpegDataReady(&mPostponedJpegReady.data.compressedFrame);
           mPostponedJpegReady.id = (MessageId) -1;
        }
    }
    return status;
}


/**
 * Allocate memory for callbacks needed in takePicture()
 *
 * \param postviewCallback allocate for postview callback
 * \param rawCallback      allocate for raw callback
 * \param waitRendering    synchronize compressed frame callback with
 *                         postviewRendered()
 */
status_t CallbacksThread::requestTakePicture(bool postviewCallback, bool rawCallback, bool waitRendering)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_JPEG_DATA_REQUEST;
    msg.data.dataRequest.postviewCallback = postviewCallback;
    msg.data.dataRequest.rawCallback = rawCallback;
    msg.data.dataRequest.waitRendering = waitRendering;
    return mMessageQueue.send(&msg);
}

status_t CallbacksThread::flushPictures()
{
    LOG1("@%s", __FUNCTION__);
    // we own the dynamically allocated jpegbuffer. Free that buffer for all
    // the pending messages before flushing them
    Vector<Message> pending;
    mMessageQueue.remove(MESSAGE_ID_JPEG_DATA_READY, &pending);
    Vector<Message>::iterator it;
    for (it = pending.begin(); it != pending.end(); ++it) {
       camera_memory_t* b = it->data.compressedFrame.jpegBuff.buff;
       b->release(b);
       b = NULL;
    }

    if (mWaitRendering) {
        mWaitRendering = false;
        // check if handling of jpeg data was postponed
        if (mPostponedJpegReady.id == MESSAGE_ID_JPEG_DATA_READY) {
            camera_memory_t* b = mPostponedJpegReady.data.compressedFrame.jpegBuff.buff;
            b->release(b);
            b = NULL;
            mPostponedJpegReady.id = (MessageId) -1;
        }
    }

    /* Remove also any requests that may be queued  */
    mMessageQueue.remove(MESSAGE_ID_JPEG_DATA_REQUEST, NULL);

    Message msg;
    msg.id = MESSAGE_ID_FLUSH;
    return mMessageQueue.send(&msg);
}

void CallbacksThread::autofocusDone(bool status)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.data.autoFocusDone.status = status;
    msg.id = MESSAGE_ID_AUTO_FOCUS_DONE;
    mMessageQueue.send(&msg);
}

void CallbacksThread::focusMove(bool start)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FOCUS_MOVE;
    msg.data.focusMove.start = start;
    mMessageQueue.send(&msg);
}

status_t CallbacksThread::handleMessageAutoFocusDone(MessageAutoFocusDone *msg)
{
    LOG1("@%s", __FUNCTION__);
    mCallbacks->autofocusDone(msg->status);
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageFocusMove(CallbacksThread::MessageFocusMove *msg)
{
    LOG1("@%s", __FUNCTION__);
    mCallbacks->focusMove(msg->start);
    return NO_ERROR;
}


status_t CallbacksThread::sendError(int id)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.data.error.id = id;
    msg.id = MESSAGE_ID_ERROR_CALLBACK;

    return mMessageQueue.send(&msg);
}

status_t CallbacksThread::handleMessageSendError(MessageError *msg)
{
    LOGE("@%s: id %d", __FUNCTION__,msg->id);
    mCallbacks->cameraError(msg->id);
    return NO_ERROR;
}

void CallbacksThread::facesDetected(camera_frame_metadata_t &face_metadata)
{
    LOG2("@%s", __FUNCTION__);
    int num_faces;
    if (face_metadata.number_of_faces > MAX_FACES_DETECTABLE) {
        LOGW("@%s: %d faces detected, limiting to %d", __FUNCTION__,
            face_metadata.number_of_faces, MAX_FACES_DETECTABLE);
        num_faces = MAX_FACES_DETECTABLE;
    } else {
        num_faces = face_metadata.number_of_faces;
    }
    if (num_faces > 0)
        PerformanceTraces::FaceLock::stop(num_faces);
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

status_t CallbacksThread::videoFrameDone(AtomBuffer *buff, nsecs_t timestamp)
{
    LOG2("@%s: ID = %d", __FUNCTION__, buff->id);
    Message msg;
    msg.id = MESSAGE_ID_VIDEO_DONE;
    msg.data.video.frame = *buff;
    msg.data.video.timestamp = timestamp;
    return mMessageQueue.send(&msg);
}

/**
 * Process message received from Picture Thread when a the image compression
 * has completed.
 */
status_t CallbacksThread::handleMessageJpegDataReady(MessageFrame *msg)
{
    LOG1("@%s: JPEG buffers queued: %d, mJpegRequested = %u, mPostviewRequested = %u, mRawRequested = %u, mULLRequested = %u",
            __FUNCTION__,
            mBuffers.size(),
            mJpegRequested,
            mPostviewRequested,
            mRawRequested,
            mULLRequested);
    AtomBuffer jpegBuf = msg->jpegBuff;
    AtomBuffer snapshotBuf = msg->snapshotBuff;
    AtomBuffer postviewBuf= msg->postviewBuff;
    AtomBuffer tmpCopy = AtomBufferFactory::createAtomBuffer(ATOM_BUFFER_PREVIEW);
    bool    releaseTmp = false;

    mPictureDoneCallback->encodingDone(&snapshotBuf, &postviewBuf);

    if (jpegBuf.dataPtr == NULL && snapshotBuf.dataPtr != NULL && postviewBuf.dataPtr != NULL) {
        LOGW("@%s: returning raw frames used in failed encoding", __FUNCTION__);
        mPictureDoneCallback->pictureDone(&snapshotBuf, &postviewBuf);
        return NO_ERROR;
    }

    if ((msg->snapshotBuff.type == ATOM_BUFFER_ULL) && (mULLRequested > 0)) {
        return handleMessageUllJpegDataReady(msg);
    }

    if (mJpegRequested > 0) {
        if (mPostviewRequested > 0) {
            if (postviewBuf.type == ATOM_BUFFER_PREVIEW_GFX) {
                convertGfx2Regular(&postviewBuf, &tmpCopy);
                releaseTmp = true;
            } else {
                tmpCopy = postviewBuf;
            }
            mCallbacks->postviewFrameDone(&tmpCopy);
            mPostviewRequested--;
        }
        if (tmpCopy.buff != NULL && releaseTmp) {
            tmpCopy.buff->size = 0;     // we only allocated the camera_memory_t no any actual memory
            tmpCopy.buff->data = NULL;
            MemoryUtils::freeAtomBuffer(tmpCopy);
            releaseTmp = false;
        }

        if (mRawRequested > 0) {
            if (snapshotBuf.type == ATOM_BUFFER_PREVIEW_GFX) {
                convertGfx2Regular(&snapshotBuf, &tmpCopy);
                releaseTmp = true;
            } else if (snapshotBuf.dataPtr != NULL && mCallbacks->msgTypeEnabled(CAMERA_MSG_RAW_IMAGE)) {
                LOG1("snapshotBuf.size:%d", snapshotBuf.size);

                mCallbacks->allocateMemory(&tmpCopy.buff, snapshotBuf.size, false);
                if (tmpCopy.dataPtr != NULL) {
                    memcpy(tmpCopy.dataPtr, snapshotBuf.dataPtr, snapshotBuf.size);
                    releaseTmp = true;
                }
            } else {
                tmpCopy = snapshotBuf;
            }
            mCallbacks->rawFrameDone(&tmpCopy);
            mRawRequested--;
        }
        if (tmpCopy.buff != NULL && releaseTmp) {
            tmpCopy.buff->size = 0;
            tmpCopy.buff->data = NULL;
            MemoryUtils::freeAtomBuffer(tmpCopy);
        }

        mCallbacks->compressedFrameDone(&jpegBuf);
        if (jpegBuf.buff == NULL) {
            LOGW("CallbacksThread received NULL jpegBuf.buff, which should not happen");
        } else {
            LOG1("Releasing jpegBuf @%p", jpegBuf.dataPtr);
            MemoryUtils::freeAtomBuffer(jpegBuf);
        }
        mJpegRequested--;

        if ((snapshotBuf.dataPtr != NULL && postviewBuf.dataPtr != NULL)
            || snapshotBuf.type == ATOM_BUFFER_PANORAMA) {
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

        LOG1("Releasing jpegBuf.buff %p, dataPtr %p", jpegBuf.buff, jpegBuf.dataPtr);
        MemoryUtils::freeAtomBuffer(jpegBuf);

        if (snapshotBuf.dataPtr != NULL && postviewBuf.dataPtr != NULL) {
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
        mWaitRendering = msg->waitRendering;
    }

    return NO_ERROR;
}

status_t CallbacksThread::handleMessageUllTriggered(MessageULLSnapshot *msg)
{
    LOG1("@%s Done id:%d",__FUNCTION__,msg->id);
    int id = msg->id;
    mCallbacks->ullTriggered(id);
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageUllJpegDataRequest(MessageULLSnapshot *msg)
{
    LOG1("@%s Done",__FUNCTION__);
    mULLRequested++;
    mULLid = msg->id;
    return NO_ERROR;
}

status_t CallbacksThread::handleMessageUllJpegDataReady(MessageFrame *msg)
{
    LOG1("@%s",__FUNCTION__);
    AtomBuffer jpegBuf = msg->jpegBuff;
    AtomBuffer snapshotBuf = msg->snapshotBuff;
    AtomBuffer postviewBuf= msg->postviewBuff;

    mULLRequested--;

    if (jpegBuf.dataPtr == NULL && snapshotBuf.dataPtr != NULL && postviewBuf.dataPtr != NULL) {
        LOGW("@%s: returning raw frames used in failed encoding", __FUNCTION__);
        mPictureDoneCallback->pictureDone(&snapshotBuf, &postviewBuf);
        return NO_ERROR;
    } else if (jpegBuf.dataPtr == NULL) {
        // Should not have NULL buffer here in any case, but checking to make Klockwork happy:
        LOGW("NULL jpegBuf.dataPtr received in CallbacksThread. Should not happen.");
        return UNKNOWN_ERROR;
    }

    // Put put the metadata in place to the ULL image buffer. This will be
    // split into separate JPEG buffer and ULL metadata in the service (JNI) layer
    // before passing to application via the Java callback
    camera_ull_metadata_t metadata;
    metadata.id = mULLid;

    AtomBuffer jpegAndMeta = AtomBufferFactory::createAtomBuffer(ATOM_BUFFER_SNAPSHOT);
    mCallbacks->allocateMemory(&jpegAndMeta, jpegBuf.size + sizeof(camera_ull_metadata_t));

    if (jpegAndMeta.buff == NULL) {
        LOGE("Failed to allocate memory for buffer jpegAndMeta");
        return UNKNOWN_ERROR;
    }

    // space for the metadata is reserved in the beginning of the buffer, copy it there
    memcpy(jpegAndMeta.dataPtr, &metadata, sizeof(camera_ull_metadata_t));

    // copy the image data in place, it goes after the metadata in the buffer
    memcpy((char*)jpegAndMeta.dataPtr + sizeof(camera_ull_metadata_t), jpegBuf.dataPtr, jpegBuf.size);

    mCallbacks->ullPictureDone(&jpegAndMeta);

    LOG1("Releasing jpegBuf.buff %p, dataPtr %p", jpegBuf.buff, jpegBuf.dataPtr);
    MemoryUtils::freeAtomBuffer(jpegBuf);

    if (jpegAndMeta.buff == NULL) {
        LOGW("NULL jpegAndMeta buffer, while reaching freeAtomBuffer().");
        return UNKNOWN_ERROR;
    } else {
        LOG1("Releasing jpegAndMeta.buff %p, dataPtr %p", jpegAndMeta.buff, jpegAndMeta.dataPtr);
        MemoryUtils::freeAtomBuffer(jpegAndMeta);
    }

    /**
     *  even if postview is NULL we return the buffer anyway.
     *  at the moment ULL cannot use postview because of the different lifecycle
     *  of the postview and snapshot buffers. Once they are allocated like
     *  snapshots we can check again the postview.
     */
    if (snapshotBuf.dataPtr != NULL) {
        // Return the raw buffers back to ISP
        LOG1("Returning ULL raw image now");
        snapshotBuf.type = ATOM_BUFFER_SNAPSHOT;  // reset the buffer type
        mPictureDoneCallback->pictureDone(&snapshotBuf, &postviewBuf);
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
    mWaitRendering = false;
    mPostponedJpegReady.id = (MessageId) -1;
    for (size_t i = 0; i < mBuffers.size(); i++) {
        AtomBuffer jpegBuf = mBuffers[i].jpegBuff;
        LOG1("Releasing jpegBuf.buff %p, dataPtr %p", jpegBuf.buff, jpegBuf.dataPtr);
        MemoryUtils::freeAtomBuffer(jpegBuf);
    }
    mBuffers.clear();
    return status;
}

status_t CallbacksThread::handleMessageFaces(MessageFaces *msg)
{
    LOG2("@%s", __FUNCTION__);
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

status_t CallbacksThread::handleMessagePreviewDone(MessagePreview *msg)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mCallbacks->previewFrameDone(&(msg->frame));
    return status;
}

status_t CallbacksThread::handleMessageVideoDone(MessageVideo *msg)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mCallbacks->videoFrameDone(&(msg->frame), msg->timestamp);
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

        case MESSAGE_ID_PREVIEW_DONE:
            status = handleMessagePreviewDone(&msg.data.preview);
            break;

        case MESSAGE_ID_VIDEO_DONE:
            status = handleMessageVideoDone(&msg.data.video);
            break;

        case MESSAGE_ID_CALLBACK_SHUTTER:
            status = handleMessageCallbackShutter();
            break;

        case MESSAGE_ID_JPEG_DATA_READY:
            if (mWaitRendering) {
                LOG1("Postponed Jpeg callbacks due rendering");
                mPostponedJpegReady = msg;
                status = NO_ERROR;
            } else {
                status = handleMessageJpegDataReady(&msg.data.compressedFrame);
            }
            break;

        case MESSAGE_ID_POSTVIEW_RENDERED:
            status = handleMessagePostviewRendered();
            break;

        case MESSAGE_ID_JPEG_DATA_REQUEST:
            status = handleMessageJpegDataRequest(&msg.data.dataRequest);
            break;

        case MESSAGE_ID_AUTO_FOCUS_DONE:
            status = handleMessageAutoFocusDone(&msg.data.autoFocusDone);
            break;

        case MESSAGE_ID_FOCUS_MOVE:
            status = handleMessageFocusMove(&msg.data.focusMove);
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

        case MESSAGE_ID_ULL_JPEG_DATA_REQUEST:
            status = handleMessageUllJpegDataRequest(&msg.data.ull);
            break;

        case MESSAGE_ID_ULL_TRIGGERED:
            status = handleMessageUllTriggered(&msg.data.ull);
            break;

        case MESSAGE_ID_ERROR_CALLBACK:
            status = handleMessageSendError(&msg.data.error);
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

/**
 * Converts a Preview Gfx buffer into a regular buffer to be given to the user
 * The caller is responsible of freeing the memory allocated to the
 * regular buffer.
 * This is actually only allocating the struct camera_memory_t.
 * The actual image memory is re-used from the Gfx buffer.
 * Please remember that this memory is own by the native window and not the HAL
 * so we cannot de-allocate it.
 * Here we just present it to the client like any other buffer.
 *
 */
void CallbacksThread::convertGfx2Regular(AtomBuffer* aGfxBuf, AtomBuffer* aRegularBuf)
{
    LOG1("%s", __FUNCTION__);

    mCallbacks->allocateMemory(aRegularBuf, 0);
    aRegularBuf->buff->data = aGfxBuf->dataPtr;
    aRegularBuf->dataPtr = aRegularBuf->buff->data; // Keep the dataPtr in sync
    aRegularBuf->buff->size = aGfxBuf->size;
}
} // namespace android
