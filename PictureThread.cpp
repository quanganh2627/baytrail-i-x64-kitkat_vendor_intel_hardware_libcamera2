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
#define LOG_TAG "Atom_PictureThread"

#include "PictureThread.h"
#include "LogHelper.h"
#include "Callbacks.h"
#include "CallbacksThread.h"
#include <utils/Timers.h>

namespace android {

static const unsigned char JPEG_MARKER_SOI[2] = {0xFF, 0xD8}; // JPEG StartOfImage marker
static const unsigned char JPEG_MARKER_EOI[2] = {0xFF, 0xD9}; // JPEG EndOfImage marker

PictureThread::PictureThread(ICallbackPicture *pictureDone) :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PictureThread", MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mPictureDoneCallback(pictureDone)
    ,mCallbacks(Callbacks::getInstance())
    ,mCallbacksThread(CallbacksThread::getInstance())
    ,mPictureWidth(0)
    ,mPictureHeight(0)
    ,mPictureFormat(0)
    ,mThumbWidth(0)
    ,mThumbHeight(0)
    ,mThumbFormat(0)
    ,mPictureQuality(80)
    ,mThumbnailQuality(50)
    ,mUsingSharedBuffers(false)
{
    LOG1("@%s", __FUNCTION__);
    mOutBuf.buff = NULL;
    mExifBuf.buff = NULL;
}

PictureThread::~PictureThread()
{
    LOG1("@%s", __FUNCTION__);
    if (mUsingSharedBuffers) {
        // Keep the shared buffers until we die
        compressor.stopSharedBuffersEncode();
    }
    if (mOutBuf.buff != NULL) {
        mOutBuf.buff->release(mOutBuf.buff);
    }
    if (mExifBuf.buff != NULL) {
        mExifBuf.buff->release(mExifBuf.buff);
    }
}

/*
 * encodeToJpeg: encodes the given buffer and creates the final JPEG file
 * Input:  mainBuf  - buffer containing the main picture image
 *         thumbBuf - buffer containing the thumbnail image (optional, can be NULL)
 * Output: destBuf  - buffer containing the final JPEG image including EXIF header
 *         Note that, if present, thumbBuf will be included in EXIF header
 */
status_t PictureThread::encodeToJpeg(AtomBuffer *mainBuf, AtomBuffer *thumbBuf, AtomBuffer *destBuf)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    JpegCompressor::InputBuffer inBuf;
    JpegCompressor::OutputBuffer outBuf;
    nsecs_t startTime = systemTime();
    nsecs_t endTime;

    if (mOutBuf.buff == NULL || mOutBuf.buff->data == NULL || mOutBuf.buff->size <= 0) {
        int bufferSize = (mPictureWidth * mPictureHeight * 2);
        mCallbacks->allocateMemory(&mOutBuf, bufferSize);
    }
    if (mExifBuf.buff == NULL || mExifBuf.buff->data == NULL || mExifBuf.buff->size <= 0) {
        mCallbacks->allocateMemory(&mExifBuf, MAX_EXIF_SIZE);
    }
    if (mOutBuf.buff == NULL || mOutBuf.buff->data == NULL) {
        LOGE("Could not allocate memory for temp buffer!");
        return NO_MEMORY;
    }
    LOG1("Out buffer: @%p (%d bytes)", mOutBuf.buff->data, mOutBuf.buff->size);
    LOG1("Exif buffer: @%p (%d bytes)", mExifBuf.buff->data, mExifBuf.buff->size);
    // Convert and encode the thumbnail, if present and EXIF maker is initialized
    if (exifMaker.isInitialized() &&
        thumbBuf != NULL &&
        thumbBuf->buff != NULL &&
        thumbBuf->buff->data != NULL &&
        thumbBuf->buff->size > 0) {
        // setup the JpegCompressor input and output buffers
        inBuf.clear();
        inBuf.buf = (unsigned char*)thumbBuf->buff->data;
        inBuf.width = mThumbWidth;
        inBuf.height = mThumbHeight;
        inBuf.format = mThumbFormat;
        inBuf.size = frameSize(mThumbFormat, mThumbWidth, mThumbHeight);
        outBuf.clear();
        outBuf.buf = (unsigned char*)mOutBuf.buff->data;
        outBuf.width = mThumbWidth;
        outBuf.height = mThumbHeight;
        outBuf.quality = mThumbnailQuality;
        outBuf.size = mOutBuf.buff->size;
        endTime = systemTime();
        int size = compressor.encode(inBuf, outBuf);
        LOG1("Thumbnail JPEG size: %d (time to encode: %ums)", size, (unsigned)((systemTime() - endTime) / 1000000));
        if (size > 0) {
            exifMaker.setThumbnail(outBuf.buf, size);
        } else {
            // This is not critical, we can continue with main picture image
            LOGE("Could not encode thumbnail stream!");
        }
    }
    int totalSize = 0;
    int exifSize = 0;
    if (exifMaker.isInitialized()) {
        // Copy the SOI marker
        unsigned char* currentPtr = (unsigned char*)mExifBuf.buff->data;
        memcpy(currentPtr, JPEG_MARKER_SOI, sizeof(JPEG_MARKER_SOI));
        totalSize += sizeof(JPEG_MARKER_SOI);
        currentPtr += sizeof(JPEG_MARKER_SOI);
        exifSize = exifMaker.makeExif(&currentPtr);
        currentPtr += exifSize;
        totalSize += exifSize;
        // Copy the EOI marker
        memcpy(currentPtr, (void*)JPEG_MARKER_EOI, sizeof(JPEG_MARKER_EOI));
        totalSize += sizeof(JPEG_MARKER_EOI);
        currentPtr += sizeof(JPEG_MARKER_EOI);
        exifSize = totalSize;
    }

    // Convert and encode the main picture image
    // setup the JpegCompressor input and output buffers
    inBuf.clear();
    if (mainBuf->shared) {
        inBuf.buf = (unsigned char *) *((char **)mainBuf->buff->data);
    } else {
        inBuf.buf = (unsigned char *) mainBuf->buff->data;
    }
    inBuf.width = mPictureWidth;
    inBuf.height = mPictureHeight;
    inBuf.format = mPictureFormat;
    inBuf.size = frameSize(mPictureFormat, mPictureWidth, mPictureHeight);
    outBuf.clear();
    outBuf.buf = (unsigned char*)mOutBuf.buff->data;
    outBuf.width = mPictureWidth;
    outBuf.height = mPictureHeight;
    outBuf.quality = mPictureQuality;
    outBuf.size = mOutBuf.buff->size;
    endTime = systemTime();
    int mainSize = compressor.encode(inBuf, outBuf);
    LOG1("Picture JPEG size: %d (time to encode: %ums)", mainSize, (unsigned)((systemTime() - endTime) / 1000000));
    if (mainSize > 0) {
        // We will skip SOI marker from final file
        totalSize += (mainSize - sizeof(JPEG_MARKER_SOI));
    } else {
        LOGE("Could not encode picture stream!");
        status = UNKNOWN_ERROR;
    }

    if (status == NO_ERROR) {
        mCallbacks->allocateMemory(destBuf, totalSize);
        if (destBuf->buff == NULL) {
            LOGE("No memory for final JPEG file!");
            status = NO_MEMORY;
        }
    }
    if (status == NO_ERROR) {
        // Copy EXIF (it will also have the SOI and EOI markers
        memcpy(destBuf->buff->data, mExifBuf.buff->data, exifSize);
        // Copy the final JPEG stream into the final destination buffer, but exclude the SOI marker
        char *copyTo = (char*)destBuf->buff->data + exifSize;
        char *copyFrom = (char*)mOutBuf.buff->data + sizeof(JPEG_MARKER_SOI);
        memcpy(copyTo, copyFrom, mainSize - sizeof(JPEG_MARKER_SOI));
    }
    LOG1("Total JPEG size: %d (time to encode: %ums)", totalSize, (unsigned)((systemTime() - startTime) / 1000000));
    return status;
}


status_t PictureThread::encode(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ENCODE;
    msg.data.encode.snaphotBuf = *snaphotBuf;
    if (postviewBuf) {
        msg.data.encode.postviewBuf = *postviewBuf;
    } else {
        // thumbnail is optional
        msg.data.encode.postviewBuf.buff = NULL;
    }
    return mMessageQueue.send(&msg);
}

void PictureThread::getDefaultParameters(CameraParameters *params)
{
    LOG1("@%s", __FUNCTION__);
    if (!params) {
        LOGE("null params");
        return;
    }

    params->setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    params->set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
            CameraParameters::PIXEL_FORMAT_JPEG);
    params->set(CameraParameters::KEY_JPEG_QUALITY, "80");
    params->set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "50");
}

void PictureThread::initialize(const CameraParameters &params, const atomisp_makernote_info &makerNote, bool flashUsed)
{
    exifMaker.initialize(params, makerNote);
    if (flashUsed)
        exifMaker.enableFlash();
    params.getPictureSize(&mPictureWidth, &mPictureHeight);
    mThumbWidth = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    mThumbHeight = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    int q = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
    if (q != 0)
        mPictureQuality = q;
    q = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if (q != 0)
        mThumbnailQuality = q;
    mUsingSharedBuffers = false;
}

void PictureThread::setNumberOfShots(int num)
{
    LOG1("@%s: num = %u", __FUNCTION__, num);
}

status_t PictureThread::getSharedBuffers(int width, int height, void** sharedBuffersPtr, int sharedBuffersNum)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    size_t bufferSize = (width * height * 2);
    bool allocBuf = true;
    if (mOutBuf.buff != NULL) {
        if (bufferSize != mOutBuf.buff->size) {
            mOutBuf.buff->release(mOutBuf.buff);
        } else {
            allocBuf = false;
        }
    }
    if (allocBuf) {
        mCallbacks->allocateMemory(&mOutBuf, bufferSize);
        if (mOutBuf.buff == NULL || mOutBuf.buff->data == NULL) {
            LOGE("Could not allocate memory for output buffer!");
            return NO_MEMORY;
        }
    }
    status = compressor.startSharedBuffersEncode(mOutBuf.buff->data, mOutBuf.buff->size);
    if (status == NO_ERROR) {
        status = compressor.getSharedBuffers(width, height, sharedBuffersPtr, sharedBuffersNum);
        if (status == NO_ERROR) {
            mUsingSharedBuffers = true;
        }
    }
    return status;
}

status_t PictureThread::allocSharedBuffers(int width, int height, int sharedBuffersNum)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ALLOC_BUFS;
    msg.data.alloc.width = width;
    msg.data.alloc.height = height;
    msg.data.alloc.numBufs = sharedBuffersNum;
    return mMessageQueue.send(&msg);
}

status_t PictureThread::wait()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_WAIT;
    return mMessageQueue.send(&msg, MESSAGE_ID_WAIT);
}

status_t PictureThread::flushMessages()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FLUSH;
    mMessageQueue.clearAll();
    return mMessageQueue.send(&msg, MESSAGE_ID_FLUSH);
}

status_t PictureThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

status_t PictureThread::handleMessageEncode(MessageEncode *msg)
{
    LOG1("@%s: snapshot ID = %d", __FUNCTION__, msg->snaphotBuf.id);
    status_t status = NO_ERROR;
    int exifSize = 0;
    int totalSize = 0;
    AtomBuffer jpegBuf;

    if (mPictureWidth == 0 ||
        mPictureHeight == 0 ||
        mPictureFormat == 0) {
        LOGE("Picture information not set yet!");
        return UNKNOWN_ERROR;
    }

    // Encode the image
    AtomBuffer *postviewBuf = msg->postviewBuf.buff == NULL ? NULL : &msg->postviewBuf;
    if ((status = encodeToJpeg(&msg->snaphotBuf, postviewBuf, &jpegBuf)) == NO_ERROR) {
        mCallbacksThread->compressedFrameDone(&jpegBuf);
    } else {
        LOGE("Error generating JPEG image!");
        if (jpegBuf.buff != NULL && jpegBuf.buff->data != NULL) {
            LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
            jpegBuf.buff->release(jpegBuf.buff);
        }
    }

    // When the encoding is done, send back the buffers to camera
    mPictureDoneCallback->pictureDone(&msg->snaphotBuf, &msg->postviewBuf);

    return status;
}

status_t PictureThread::handleMessageAllocBufs(MessageAllocBufs *msg)
{
    LOG1("@%s: width = %d, height = %d, numBufs = %d",
            __FUNCTION__,
            msg->width,
            msg->height,
            msg->numBufs);
    // Send NULL as buffer pointer: don't care about the buffers now, just allocate them
    return getSharedBuffers(msg->width, msg->height, NULL, msg->numBufs);
}

status_t PictureThread::handleMessageWait()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_WAIT, status);
    return status;
}

status_t PictureThread::handleMessageFlush()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    // Now, flush the queued JPEG buffers from CallbacksThread
    status = mCallbacksThread->flushPictures();
    mMessageQueue.reply(MESSAGE_ID_FLUSH, status);
    return status;
}

status_t PictureThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;

        case MESSAGE_ID_ENCODE:
            status = handleMessageEncode(&msg.data.encode);
            break;

        case MESSAGE_ID_ALLOC_BUFS:
            status = handleMessageAllocBufs(&msg.data.alloc);
            break;

        case MESSAGE_ID_WAIT:
            status = handleMessageWait();
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

bool PictureThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    return false;
}

status_t PictureThread::requestExitAndWait()
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
