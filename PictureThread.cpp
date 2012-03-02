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
#include <utils/Timers.h>

namespace android {

static const unsigned char JPEG_MARKER_SOI[2] = {0xFF, 0xD8}; // JPEG StartOfImage marker
static const unsigned char JPEG_MARKER_EOI[2] = {0xFF, 0xD9}; // JPEG EndOfImage marker

PictureThread::PictureThread(ICallbackPicture *pictureDone) :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PictureThread")
    ,mThreadRunning(false)
    ,mPictureDoneCallback(pictureDone)
    ,mCallbacks(Callbacks::getInstance())
    ,mPictureWidth(0)
    ,mPictureHeight(0)
    ,mPictureFormat(0)
    ,mThumbWidth(0)
    ,mThumbHeight(0)
    ,mThumbFormat(0)
    ,mPictureQuality(80)
    ,mThumbnailQuality(50)
{
    LOG1("@%s", __FUNCTION__);
}

PictureThread::~PictureThread()
{
    LOG1("@%s", __FUNCTION__);
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

    AtomBuffer tempBuf;
    size_t bufferSize = (mPictureWidth * mPictureHeight * 2) + MAX_EXIF_SIZE;
    mCallbacks->allocateMemory(&tempBuf, bufferSize);
    if (tempBuf.buff == NULL || tempBuf.buff->data == NULL) {
        LOGE("Could not allocate memory for temp buffer!");
        return NO_MEMORY;
    }
    LOG1("Temp buffer: @%p (%d bytes)", tempBuf.buff->data, tempBuf.buff->size);
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
        outBuf.buf = (unsigned char*)tempBuf.buff->data + MAX_EXIF_SIZE;
        outBuf.width = mThumbWidth;
        outBuf.height = mThumbHeight;
        outBuf.quality = mThumbnailQuality;
        outBuf.size = bufferSize - MAX_EXIF_SIZE;
        int size = compressor.encode(inBuf, outBuf);
        LOG1("Thumbnail JPEG size: %d", size);
        if (size > 0) {
            exifMaker.setThumbnail(outBuf.buf, size);
        } else {
            // This is not critical, we can continue with main picture image
            LOGE("Could not encode thumbnail stream!");
        }
    }

    int totalSize = 0;
    unsigned char* currentPtr = (unsigned char*)tempBuf.buff->data;
    unsigned char* exifEnd = NULL;
    if (exifMaker.isInitialized()) {
        // We can include makeExif in copyTime because what it actually does is a bunch of memcpy's
        // Copy the SOI marker
        memcpy(currentPtr, JPEG_MARKER_SOI, sizeof(JPEG_MARKER_SOI));
        currentPtr += sizeof(JPEG_MARKER_SOI);
        totalSize += sizeof(JPEG_MARKER_SOI);
        size_t exifSize = exifMaker.makeExif(&currentPtr);
        currentPtr += exifSize;
        totalSize += exifSize;
        exifEnd = currentPtr;
    }

    // Convert and encode the main picture image
    // setup the JpegCompressor input and output buffers
    inBuf.clear();
    if (mainBuf->shared) {
        inBuf.buf = (unsigned char *) *((char **)mainBuf->buff->data);;
    } else {
        inBuf.buf = (unsigned char *) mainBuf->buff->data;
    }
    inBuf.width = mPictureWidth;
    inBuf.height = mPictureHeight;
    inBuf.format = mPictureFormat;
    inBuf.size = frameSize(mPictureFormat, mPictureWidth, mPictureHeight);
    outBuf.clear();
    outBuf.buf = currentPtr;
    outBuf.width = mPictureWidth;
    outBuf.height = mPictureHeight;
    outBuf.quality = mPictureQuality;
    outBuf.size = bufferSize - totalSize;
    int size = compressor.encode(inBuf, outBuf);
    LOG1("Picture JPEG size: %d", size);
    if (size > 0) {
        totalSize += size;
    } else {
        LOGE("Could not encode picture stream!");
        status = UNKNOWN_ERROR;
    }

    if (status == NO_ERROR) {
        if (exifEnd != NULL) {
            // Copy the EOI marker
            memcpy(exifEnd, (void*)JPEG_MARKER_EOI, sizeof(JPEG_MARKER_EOI));
        }
        mCallbacks->allocateMemory(destBuf, totalSize);
        if (destBuf->buff == NULL) {
            LOGE("No memory for final JPEG file!");
            status = NO_MEMORY;
        }
    }
    if (status == NO_ERROR) {
        // Copy the final JPEG stream into the final destination buffer, which MUST have the same size as the encoded JPEG
        memcpy(destBuf->buff->data, tempBuf.buff->data, totalSize);
    }

    tempBuf.buff->release(tempBuf.buff);
    LOG1("Time spent encoding JPEG file: %ums", (unsigned)((systemTime() - startTime) / 1000000));
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

void PictureThread::initialize(const CameraParameters &params, bool flashUsed)
{
    exifMaker.initialize(params);
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
}

status_t PictureThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return status;
}

status_t PictureThread::handleMessageEncode(MessageEncode *msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    size_t exifSize = 0;
    size_t totalSize = 0;
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
        mCallbacks->compressedFrameDone(&jpegBuf);
    } else {
        LOGE("Error generating JPEG image!");
    }

    if (jpegBuf.buff != NULL && jpegBuf.buff->data != NULL) {
        LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
        jpegBuf.buff->release(jpegBuf.buff);
    }
    // When the encoding is done, send back the buffers to camera
    mPictureDoneCallback->pictureDone(&msg->snaphotBuf, &msg->postviewBuf);

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
