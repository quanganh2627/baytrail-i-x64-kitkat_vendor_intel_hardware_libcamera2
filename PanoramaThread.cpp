/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2012 Intel Corporation
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
#define LOG_TAG "Camera_Panorama"
//#define LOG_NDEBUG 0

#include <math.h>
#include <assert.h>

#include "PanoramaThread.h"
#include "IntelParameters.h"
#include "FeatureData.h"
#include "LogHelper.h"
#include "AtomAAA.h"
#include "AtomCommon.h"
#include "AtomISP.h"
#include "PlatformData.h"

namespace android {

PanoramaThread::PanoramaThread(ICallbackPanorama *panoramaCallback) :
    Thread(false)
    ,mPanoramaCallback(panoramaCallback)
    ,mContext(NULL)
    ,mMessageQueue("Panorama", (int) MESSAGE_ID_MAX)
    ,mPanoramaTotalCount(0)
    ,mThreadRunning(false)
    ,mPanoramaWaitingForImage(false)
    ,mCallbacksThread(CallbacksThread::getInstance())
    ,mCallbacks(Callbacks::getInstance()) // for memory allocation
    ,mPostviewBuf(AtomBufferFactory::createAtomBuffer(ATOM_BUFFER_POSTVIEW))
    ,mState(PANORAMA_STOPPED)
    ,mPreviewWidth(0)
    ,mPreviewHeight(0)
{
    LOG1("@%s", __FUNCTION__);
    mCurrentMetadata.direction = 0;
    mCurrentMetadata.motion_blur = false;
    mCurrentMetadata.horizontal_displacement = 0;
    mCurrentMetadata.vertical_displacement = 0;
    mPanoramaMaxSnapshotCount = PlatformData::getMaxPanoramaSnapshotCount();
}

PanoramaThread::~PanoramaThread()
{
    LOG1("@%s", __FUNCTION__);
}

void PanoramaThread::getDefaultParameters(CameraParameters *intel_params)
{
    LOG1("@%s", __FUNCTION__);
    if (!intel_params) {
        LOGE("params is null!");
        assert(false);
    }
    // Set if Panorama is available or not.
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_PANORAMA, FeatureData::panoramaSupported());
    intel_params->set(IntelCameraParameters::KEY_PANORAMA_MAX_SNAPSHOT_COUNT, mPanoramaMaxSnapshotCount);
}

void PanoramaThread::startPanorama(void)
{
    LOG1("@%s", __FUNCTION__);
    if (mState == PANORAMA_STOPPED) {
        Message msg;
        msg.id = MESSAGE_ID_START_PANORAMA;
        mMessageQueue.send(&msg);
        mState = PANORAMA_STARTED;
    }
}

status_t PanoramaThread::handleMessageStartPanorama(void)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
#ifdef ENABLE_INTEL_EXTRAS
    mContext = ia_panorama_init(NULL);
    if (mContext == NULL) {
        LOGE("fatal - error initializing panorama");
        assert(false);
        return UNKNOWN_ERROR;
    }
    // allocate memory for the live preview callback. Max thumbnail in NV12 + metadata.
    mCallbacks->allocateMemory(&mPostviewBuf, frameSize(V4L2_PIX_FMT_NV12, LARGEST_THUMBNAIL_WIDTH, LARGEST_THUMBNAIL_HEIGHT) +
                               sizeof(camera_panorama_metadata));
    if (mPostviewBuf.buff == NULL) {
        LOGE("fatal - out of memory for live preview callback");
        assert(false);
        return NO_MEMORY;
    }
#endif
    return status;
}

void PanoramaThread::stopPanorama(bool synchronous)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STOP_PANORAMA;
    msg.data.stop.synchronous = synchronous;
    mMessageQueue.send(&msg, synchronous ? MESSAGE_ID_STOP_PANORAMA : (MessageId) -1);
}

status_t PanoramaThread::handleMessageStopPanorama(MessageStopPanorama stop)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (mContext) {
        if (mPanoramaTotalCount > 0)
            cancelStitch();
#ifdef ENABLE_INTEL_EXTRAS
        ia_panorama_uninit(mContext);
#endif
        mContext = NULL;
    }
    if (mPostviewBuf.buff != NULL) {
        mPostviewBuf.buff->release(mPostviewBuf.buff);
        mPostviewBuf.buff = NULL;
    }
    mState = PANORAMA_STOPPED;
    if (stop.synchronous)
        mMessageQueue.reply(MESSAGE_ID_STOP_PANORAMA, status);
    return status;
}

void PanoramaThread::startPanoramaCapture()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_START_PANORAMA_CAPTURE;
    mMessageQueue.send(&msg);
}

status_t PanoramaThread::handleMessageStartPanoramaCapture()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (mState == PANORAMA_STARTED) {
        reInit();
        mState = PANORAMA_DETECTING_OVERLAP;
    }
    else
        status = INVALID_OPERATION;

    return status;
}
void PanoramaThread::stopPanoramaCapture()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STOP_PANORAMA_CAPTURE;
    mMessageQueue.send(&msg);
}

status_t PanoramaThread::handleMessageStopPanoramaCapture()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (mState == PANORAMA_DETECTING_OVERLAP || mState == PANORAMA_WAITING_FOR_SNAPSHOT)
        mState = PANORAMA_STARTED;
    else
        status = INVALID_OPERATION;

    return status;
}

status_t PanoramaThread::reInit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
#ifdef ENABLE_INTEL_EXTRAS
    ia_panorama_reinit(mContext);
#endif
    return status;
}

bool PanoramaThread::isBlurred(int width, int dx, int dy) const
{
    LOG1("@%s", __FUNCTION__);
    AtomAAA* aaa = AtomAAA::getInstance();
    SensorAeConfig config;
    aaa->getExposureInfo(config);

    float speed = sqrtf(dx * dx + dy * dy);
    float percentage = speed / width; // assuming square pixels
    float blurvalue = percentage * config.expTime;

    return blurvalue > PANORAMA_MAX_BLURVALUE;
}

bool PanoramaThread::detectOverlap(ia_frame *frame)
{
    LOG2("@%s", __FUNCTION__);
    bool overlap = false;
#ifdef ENABLE_INTEL_EXTRAS
    if (mPanoramaTotalCount < mPanoramaMaxSnapshotCount) {
        frame->format = ia_frame_format_nv12;
        ia_err err = ia_panorama_detect_overlap(mContext, frame);
        LOG2("@%s: direction: %d, H-displacement: %d, V-displacement: %d", __FUNCTION__,
            mContext->direction, mContext->horizontal_displacement, mContext->vertical_displacement);

        if (err != ia_err_none) {
            LOGE("ia_panorama_detect_overlap failed, error = %d", err);
            return false;
        }

        int x = 0.65f * frame->width; // target horizontal displacement
        int y = 0.65f * frame->height; // target vertical displacement
        int dx = 0.15f * frame->width; // delta from target h_displacement to a maximum allowed h_displacement
        int dy = 0.15f * frame->height; // delta from target v_displacement to a maximum allowed v_displacement
        int displacementX = abs(mContext->horizontal_displacement);
        int displacementY = abs(mContext->vertical_displacement);

        mCurrentMetadata.direction = mContext->direction;

        // calculate motion blur (based on movement compared to previous displacement)
        int dxPrev = mContext->horizontal_displacement - mCurrentMetadata.horizontal_displacement;
        int dyPrev = mContext->vertical_displacement - mCurrentMetadata.vertical_displacement;
        mCurrentMetadata.motion_blur = isBlurred(frame->width, dxPrev, dyPrev);
        // store values, do displacement callback
        mCurrentMetadata.horizontal_displacement = mContext->horizontal_displacement;
        mCurrentMetadata.vertical_displacement = mContext->vertical_displacement;
        mCurrentMetadata.finalization_started = false;
        mCallbacksThread->panoramaDisplUpdate(mCurrentMetadata);

        // capture triggering, after first capture, if not blurred, and with proper displacement (based on decided direction)
        if (mPanoramaTotalCount > 0 && !mCurrentMetadata.motion_blur) {
            if (displacementX > x && displacementX < x + dx &&
                (mContext->direction == 1 || mContext->direction == 2)) {
                return true;
            }
            if(displacementY > y && displacementY < y + dy &&
                (mContext->direction == 3 || mContext->direction == 4)) {
                return true;
            }
        }
    }
#endif
    return overlap;
}

status_t PanoramaThread::stitch(AtomBuffer *img, AtomBuffer *pv)
{
    LOG1("@%s", __FUNCTION__);
    if (mState != PANORAMA_WAITING_FOR_SNAPSHOT) {
        LOGE("Panorama stitch called in wrong state (%d)", mState);
        return INVALID_OPERATION;
    }

    Message msg;
    msg.id = MESSAGE_ID_STITCH;
    msg.data.stitch.img = *img;
    msg.data.stitch.pv = *pv;
    return mMessageQueue.send(&msg, MESSAGE_ID_STITCH);
}

status_t PanoramaThread::cancelStitch()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
#ifdef ENABLE_INTEL_EXTRAS
    ia_panorama_cancel_stitching(mContext);
#endif
    return status;
}

void PanoramaThread::finalize(void)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FINALIZE;
    mMessageQueue.send(&msg);
}

#define is_aligned(POINTER, BYTE_COUNT) \
    (((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT) == 0)


status_t PanoramaThread::handleMessageFinalize()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
#ifdef ENABLE_INTEL_EXTRAS
    if (mState == PANORAMA_DETECTING_OVERLAP || mState == PANORAMA_WAITING_FOR_SNAPSHOT)
        handleMessageStopPanoramaCapture(); // drops state to PANORAMA_STARTED

    ia_frame *pFrame = ia_panorama_finalize(mContext);
    if (!pFrame) {
        LOGE("ia_panorama_finalize failed");
        return UNKNOWN_ERROR;
    }

    mPanoramaTotalCount = 0;
    mCurrentMetadata.direction = 0;
    mCurrentMetadata.motion_blur = false;
    mCurrentMetadata.horizontal_displacement = 0;
    mCurrentMetadata.vertical_displacement = 0;

    AtomBuffer img = AtomBufferFactory::createAtomBuffer(ATOM_BUFFER_PANORAMA);

    img.width = pFrame->width;
    img.height = pFrame->height;
    img.stride = pFrame->stride;
    img.format = V4L2_PIX_FMT_NV12;
    img.size = frameSize(V4L2_PIX_FMT_NV12, img.stride, img.height); // because pFrame->size from panorama is currently incorrectly zero
    // allocate some dummy memory (for struct in .buff basically)
    mCallbacks->allocateMemory(&img, 0);
    // store data pointer and ownership for releasing purposes (see ::returnBuffer)
    img.gfxData = img.buff->data;
    img.owner = this;
    // .. and put panorama engine memory into the data pointer for the encoding
    img.buff->data = pFrame->data;
    // return panorama image via callback to PostProcThread, which passes it onwards
    mPanoramaCallback->panoramaFinalized(&img);
#endif
    return status;
}

/**
 * returnBuffer is used for returning the finalized buffer after jpeg has been delivered
 */
void PanoramaThread::returnBuffer(AtomBuffer *atomBuffer) {
    LOG1("@%s", __FUNCTION__);
    // restore original pointer, which was stored into gfxData, and then release
    atomBuffer->buff->data = atomBuffer->gfxData;
    atomBuffer->buff->release(atomBuffer->buff);
    // panorama engine releases its memory either at reinit (handleMessageStartPanoramaCapture)
    // or uninit (handleMessageStopPanorama)
}

void PanoramaThread::sendFrame(AtomBuffer &buf)
{
    LOG2("@%s", __FUNCTION__);
    ia_frame frame;
    if (buf.type == ATOM_BUFFER_PREVIEW) {
        frame.data = (unsigned char*) buf.buff->data;
    } else {
        frame.data = (unsigned char*) buf.gfxData;
    }
    frame.width = buf.width;
    frame.stride = buf.stride;
    frame.height = buf.height;
    frame.size = buf.size;

    Message msg;
    msg.id = MESSAGE_ID_FRAME;
    msg.data.frame.frame = frame;
    mMessageQueue.send(&msg, MESSAGE_ID_FRAME);
}

status_t PanoramaThread::handleFrame(MessageFrame frame)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mPreviewWidth = frame.frame.width;
    mPreviewHeight = frame.frame.height;
    if (mState == PANORAMA_DETECTING_OVERLAP) {
        if (mPanoramaTotalCount == 0 || detectOverlap(&frame.frame)) {
            mState = PANORAMA_WAITING_FOR_SNAPSHOT;
            mPanoramaCallback->panoramaCaptureTrigger();
        }
    }
    mMessageQueue.reply(MESSAGE_ID_FRAME, status);
    return status;
}

PanoramaState PanoramaThread::getState(void)
{
    return mState;
}

status_t PanoramaThread::handleStitch(MessageStitch stitch)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
#ifdef ENABLE_INTEL_EXTRAS
    ia_frame iaFrame;
    iaFrame.data = stitch.img.buff->data;
    iaFrame.size = stitch.img.size;
    iaFrame.width = stitch.img.width;
    iaFrame.height = stitch.img.height;
    iaFrame.stride = stitch.img.stride;
    iaFrame.format = ia_frame_format_nv12;

    if (iaFrame.stride == 0) {
        LOGW("panorama stitch hack - snapshot frame stride zero, replacing with width %d", iaFrame.width);
        iaFrame.stride = iaFrame.width;
    }
    assert(stitch.pv.size <= frameSize(V4L2_PIX_FMT_NV12, LARGEST_THUMBNAIL_WIDTH, LARGEST_THUMBNAIL_HEIGHT));

    mPanoramaTotalCount++;
    ia_err err = ia_panorama_stitch(mContext, &iaFrame);

    if (err != ia_err_none) {
        LOGE("ia_panorama_stitch failed, error = %d", err);
        status = UNKNOWN_ERROR;

        // TODO fixme we need to fall through, since current panorama lib does not provide valid return values
        status = OK;
    }

    // convert displacement to reflect PV image size
    camera_panorama_metadata metadata = mCurrentMetadata;
    metadata.horizontal_displacement = roundf((float) metadata.horizontal_displacement / mPreviewWidth * stitch.pv.width);
    metadata.vertical_displacement = roundf((float) metadata.vertical_displacement / mPreviewHeight * stitch.pv.height);
    metadata.finalization_started = (mPanoramaTotalCount == mPanoramaMaxSnapshotCount);
    // space for the metadata is reserved in the beginning of the buffer
    memcpy(mPostviewBuf.buff->data, &metadata, sizeof(camera_panorama_metadata));
    // copy PV image
    memcpy((char *)mPostviewBuf.buff->data + sizeof(camera_panorama_metadata), stitch.pv.buff->data, stitch.pv.size);
    // set rest of PV fields
    mPostviewBuf.width = stitch.pv.width;
    mPostviewBuf.height = stitch.pv.height;
    mPostviewBuf.size = stitch.pv.size;
    mPostviewBuf.stride = stitch.pv.stride;

    mCallbacksThread->panoramaSnapshot(mPostviewBuf);

    //panorama engine resets displacement values after stitching, so we reset the current values here, too
    mCurrentMetadata.horizontal_displacement = 0;
    mCurrentMetadata.vertical_displacement = 0;

    mState = PANORAMA_DETECTING_OVERLAP;
    if (mPanoramaTotalCount == mPanoramaMaxSnapshotCount) {
        finalize();
    }

    mMessageQueue.reply(MESSAGE_ID_STITCH, status);
#endif
    return status;
}

bool PanoramaThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    mThreadRunning = true;
    while(mThreadRunning)
        waitForAndExecuteMessage();

    return false;
}

status_t PanoramaThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id)
    {
        case MESSAGE_ID_STITCH:
            status = handleStitch(msg.data.stitch);
            break;
        case MESSAGE_ID_EXIT:
            status = handleExit();
            break;
        case MESSAGE_ID_FRAME:
            status = handleFrame(msg.data.frame);
            break;
        case MESSAGE_ID_FINALIZE:
            status = handleMessageFinalize();
            break;
        case MESSAGE_ID_START_PANORAMA:
            status = handleMessageStartPanorama();
            break;
        case MESSAGE_ID_STOP_PANORAMA:
            status = handleMessageStopPanorama(msg.data.stop);
            break;
        case MESSAGE_ID_START_PANORAMA_CAPTURE:
            status = handleMessageStartPanoramaCapture();
            break;
        case MESSAGE_ID_STOP_PANORAMA_CAPTURE:
            status = handleMessageStopPanoramaCapture();
            break;
        default:
            status = INVALID_OPERATION;
            break;
    }
    if (status != NO_ERROR) {
        LOGE("operation failed, ID = %d, status = %d", msg.id, status);
    }
    return status;
}

status_t PanoramaThread::requestExitAndWait()
{
    LOG2("@%s", __FUNCTION__);
    // first stop synchronously, it cleans up panorama engine etc
    stopPanorama(true);

    Message msg;
    msg.id = MESSAGE_ID_EXIT;
    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

status_t PanoramaThread::handleExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

}; // namespace android

