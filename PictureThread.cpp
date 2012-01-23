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
#include "ColorConverter.h"
#include "LogHelper.h"
#include "Callbacks.h"
#include "SkBitmap.h"
#include "SkImageEncoder.h"
#include "SkStream.h"

namespace android {

PictureThread::PictureThread(ICallbackPicture *pictureDone) :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PictureThread")
    ,mThreadRunning(false)
    ,mPictureDoneCallback(pictureDone)
    ,mCallbacks(NULL)
{
    LOG_FUNCTION
    mPictureInfo.width  = 0;
    mPictureInfo.height = 0;
    mPictureInfo.format = 0;
}

PictureThread::~PictureThread()
{
    LOG_FUNCTION
}

void PictureThread::setCallbacks(Callbacks *callbacks)
{
    LOG_FUNCTION
    mCallbacks = callbacks;
}

status_t PictureThread::encodeToJpeg(AtomBuffer *src, AtomBuffer *dst, int quality)
{
    LOG_FUNCTION
    SkImageEncoder* encoder = NULL;
    SkBitmap bitmap;
    SkDynamicMemoryWStream stream;
    status_t status = NO_ERROR;
    void *rgb = NULL;
    bool rgbAlloc = false;
    int w = mPictureInfo.width;
    int h = mPictureInfo.height;
    int format = mPictureInfo.format;

    LogDetail("w:%d h:%d f:%d", w, h, format);

    // First, be sure that the source has GRB565 format (requested by Skia)
    switch (format) {
        case V4L2_PIX_FMT_NV12:
            LogDetail("Converting frame from NV12 to RGB565");
            rgb = malloc(w * h * 2);
            if (rgb == NULL) {
                LogError("Could not allocate memory for color conversion!");
                status = NO_MEMORY;
                goto exit;
            }
            rgbAlloc = true;
            NV12ToRGB565(w, h, src->buff->data, rgb);
            break;
        case V4L2_PIX_FMT_YUV420:
            LogDetail("Converting frame from YUV420 to RGB565");
            rgb = malloc(w * h * 2);
            if (rgb == NULL) {
                LogError("Could not allocate memory for color conversion!");
                status = NO_MEMORY;
                goto exit;
            }
            rgbAlloc = true;
            YUV420ToRGB565(w, h, src->buff->data, rgb);
            break;
        case V4L2_PIX_FMT_RGB565:
            rgb = src->buff->data;
            break;
        default:
            LogError("Unsupported color format: %d", format);
            status = UNKNOWN_ERROR;
            goto exit;
    }

    LogDetail("Creating encoder...");
    encoder = SkImageEncoder::Create(SkImageEncoder::kJPEG_Type);
    if (encoder != NULL) {
        bitmap.setConfig(SkBitmap::kRGB_565_Config, w, h);
        bitmap.setPixels(rgb, NULL);
        LogDetail("Encoding stream...");
        if (encoder->encodeStream(&stream, bitmap, quality)) {
            int size = stream.getOffset();
            LogDetail("JPEG size: %d", size);
            mCallbacks->allocateMemory(dst, size);
            if (dst->buff != NULL) {
                stream.copyTo(dst->buff->data);
            } else {
                status = NO_MEMORY;
                goto exit;
            }
        } else {
            LogError("Could not encode stream!");
            status = UNKNOWN_ERROR;
            goto exit;
        }
    } else {
        LogError("No memory for encoder");
        status = NO_MEMORY;
        goto exit;
    }

exit:
    if (encoder != NULL) {
        delete encoder;
    }
    if (rgb != NULL && rgbAlloc) {
        free(rgb);
    }
    return status;
}


status_t PictureThread::encode(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf)
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_ENCODE;
    msg.data.encode.snaphotBuf = snaphotBuf;
    msg.data.encode.postviewBuf = postviewBuf;
    return mMessageQueue.send(&msg);
}

status_t PictureThread::handleMessageExit()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return status;
}

status_t PictureThread::handleMessageEncode(MessageEncode *msg)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    AtomBuffer jpegBuf;

    if (mPictureInfo.width == 0 ||
        mPictureInfo.height == 0 ||
        mPictureInfo.format == 0) {
        LogError("Picture information not set yet!");
        return UNKNOWN_ERROR;
    }
    // Encode the image
    // TODO: implement quality passing from ControlThread
    if ((status = encodeToJpeg(msg->snaphotBuf, &jpegBuf, 80)) == NO_ERROR) {
        mCallbacks->compressedFrameDone(&jpegBuf);
        jpegBuf.buff->release(jpegBuf.buff);
    } else {
        LogError("Error encoding JPEG image!");
    }

    // When the encoding is done, send back the buffers to camera
    mPictureDoneCallback->pictureDone(msg->snaphotBuf, msg->postviewBuf);

    return status;
}

status_t PictureThread::waitForAndExecuteMessage()
{
    LOG_FUNCTION2
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
    LOG_FUNCTION2
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    return false;
}

status_t PictureThread::requestExitAndWait()
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
