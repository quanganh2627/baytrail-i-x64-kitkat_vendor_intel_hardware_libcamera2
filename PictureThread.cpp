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
#define LOG_TAG "Camera_PictureThread"

#include "PerformanceTraces.h"
#include "PictureThread.h"
#include "LogHelper.h"
#include "Callbacks.h"
#include "CallbacksThread.h"
#include <utils/Timers.h>

namespace android {

static const unsigned char JPEG_MARKER_SOI[2] = {0xFF, 0xD8}; // JPEG StartOfImage marker
static const unsigned char JPEG_MARKER_EOI[2] = {0xFF, 0xD9}; // JPEG EndOfImage marker
static const int SIZE_OF_APP0_MARKER = 18;      /* Size of the JFIF App0 marker
                                                 * This is introduced by SW encoder after
                                                 * SOI. And sometimes needs to be removed
                                                 */

PictureThread::PictureThread() :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PictureThread", MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mCallbacks(Callbacks::getInstance())
    ,mCallbacksThread(CallbacksThread::getInstance())
    ,mPictureQuality(80)
    ,mThumbnailQuality(50)
    ,mInputBuffers(0)
{
    LOG1("@%s", __FUNCTION__);
    mOutBuf.buff = NULL;
    mExifBuf.buff = NULL;
    mInputBufferArray = NULL;
    mInputBuffDataArray = NULL;
    mHwCompressor = new JpegHwEncoder();
}

PictureThread::~PictureThread()
{
    LOG1("@%s", __FUNCTION__);

    if (mOutBuf.buff != NULL) {
        mOutBuf.buff->release(mOutBuf.buff);
    }
    if (mExifBuf.buff != NULL) {
        mExifBuf.buff->release(mExifBuf.buff);
    }

    freeInputBuffers();

    if(mHwCompressor)
        delete mHwCompressor;
}

/*
 * encodeToJpeg: encodes the given buffer and creates the final JPEG file
 * It allocates the memory for the final JPEG that contains EXIF(with thumbnail)
 * plus main picture
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
    int totalSize = 0;
    int exifSize = 0;
    int mainSize = 0;
    bool failback = false;

    size_t bufferSize = (mainBuf->width * mainBuf->height * 2);
    if (mOutBuf.buff != NULL && bufferSize != mOutBuf.buff->size) {
        mOutBuf.buff->release(mOutBuf.buff);
        mOutBuf.buff = NULL;
    }

    if (mOutBuf.buff == NULL || mOutBuf.buff->data == NULL || mOutBuf.buff->size <= 0) {
        mCallbacks->allocateMemory(&mOutBuf, bufferSize);
    }
    if (mExifBuf.buff == NULL || mExifBuf.buff->data == NULL || mExifBuf.buff->size <= 0) {
        mCallbacks->allocateMemory(&mExifBuf, MAX_EXIF_SIZE + sizeof(JPEG_MARKER_SOI) + sizeof(JPEG_MARKER_EOI));
    }
    if (mOutBuf.buff == NULL || mOutBuf.buff->data == NULL) {
        LOGE("Could not allocate memory for temp buffer!");
        return NO_MEMORY;
    }
    LOG1("Out buffer: @%p (%d bytes)", mOutBuf.buff->data, mOutBuf.buff->size);
    LOG1("Exif buffer: @%p (%d bytes)", mExifBuf.buff->data, mExifBuf.buff->size);

    // Start encoding main picture using HW encoder
    inBuf.clear();
    if (mainBuf->shared) {
       inBuf.buf = (unsigned char *) *((char **)mainBuf->buff->data);
    } else {
       inBuf.buf = (unsigned char *) mainBuf->buff->data;
    }
    inBuf.width = mainBuf->width;
    inBuf.height = mainBuf->height;
    inBuf.format = mainBuf->format;
    inBuf.size = frameSize(mainBuf->format, mainBuf->width, mainBuf->height);
    outBuf.clear();
    outBuf.width = mainBuf->width;
    outBuf.height = mainBuf->height;
    outBuf.quality = mPictureQuality;
    endTime = systemTime();

    if(mHwCompressor && mHwCompressor->isInitialized() &&
       mHwCompressor->encodeAsync(inBuf, outBuf) == 0) {
        LOG1("Picture JPEG (time to start encode: %ums)", (unsigned)((systemTime() - endTime) / 1000000));
    } else {
        LOGW("JPEG HW encoding failed, falling back to SW");
        failback = true;
    }

    // Convert and encode the thumbnail, if present and EXIF maker is initialized
    if (mExifMaker.isInitialized())
    {
        // Copy the SOI marker
        unsigned char* currentPtr = (unsigned char*)mExifBuf.buff->data;
        memcpy(currentPtr, JPEG_MARKER_SOI, sizeof(JPEG_MARKER_SOI));
        totalSize += sizeof(JPEG_MARKER_SOI);
        currentPtr += sizeof(JPEG_MARKER_SOI);

        exifSize = encodeExifAndThumbnail(thumbBuf, currentPtr);
        if (exifSize == 0) {
            // This is not critical, we can continue with main picture image
            LOGW("Could not encode thumbnail stream!");
            exifSize = mExifMaker.makeExif(&currentPtr);
        }

        currentPtr += exifSize;
        totalSize += exifSize;
        // Copy the EOI marker
        memcpy(currentPtr, (void*)JPEG_MARKER_EOI, sizeof(JPEG_MARKER_EOI));
        totalSize += sizeof(JPEG_MARKER_EOI);
        currentPtr += sizeof(JPEG_MARKER_EOI);
        exifSize = totalSize;
    }

    if (failback) {  // Encode main picture with SW encoder
        inBuf.clear();
        if (mainBuf->shared) {
           inBuf.buf = (unsigned char *) *((char **)mainBuf->buff->data);
       } else {
           inBuf.buf = (unsigned char *) mainBuf->buff->data;
       }
       int realWidth = (mainBuf->stride > mainBuf->width)? mainBuf->stride:
                                                           mainBuf->width;
       inBuf.width = realWidth;
       inBuf.height = mainBuf->height;
       inBuf.format = mainBuf->format;
       inBuf.size = frameSize(mainBuf->format, mainBuf->width, mainBuf->height);
       outBuf.clear();
       outBuf.buf = (unsigned char*)mOutBuf.buff->data;
       outBuf.width = realWidth;
       outBuf.height = mainBuf->height;
       outBuf.quality = mPictureQuality;
       outBuf.size = mOutBuf.buff->size;
       endTime = systemTime();
       int mainSize = mCompressor.encode(inBuf, outBuf);
       LOG1("Picture JPEG size: %d (time to encode: %ums)", mainSize, (unsigned)((systemTime() - endTime) / 1000000));
       if (mainSize > 0) {
          totalSize += mainSize;
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
          // Copy EXIF (it will also have the SOI and EOI markers)
          memcpy(destBuf->buff->data, mExifBuf.buff->data, exifSize);
          // Copy the final JPEG stream into the final destination buffer
          // avoid the copying the SOI and APP0
          char *copyTo = (char*)destBuf->buff->data + exifSize;
          char *copyFrom = (char*)mOutBuf.buff->data + sizeof(JPEG_MARKER_SOI) + SIZE_OF_APP0_MARKER;
          memcpy(copyTo, copyFrom, mainSize);

          destBuf->id = mainBuf->id;
       }

    } else {
        endTime = systemTime();
        mHwCompressor->waitToComplete(&mainSize);
        if (mainSize > 0) {
            totalSize += mainSize;
        }
        LOG1("Picture JPEG size: %d (waited for encode to finish: %ums)", mainSize, (unsigned)((systemTime() - endTime) / 1000000));
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
            destBuf->id = mainBuf->id;
        }
        outBuf.clear();
        outBuf.buf = (unsigned char*)destBuf->buff->data + exifSize;
        outBuf.width = mainBuf->width;
        outBuf.height = mainBuf->height;
        outBuf.quality = mPictureQuality;
        outBuf.size = mainSize;
        if(mHwCompressor->getOutput(outBuf) < 0) {
            LOGE("Could not encode picture stream!");
            status = UNKNOWN_ERROR;
        }
    }

    LOG1("Total JPEG size: %d (time to encode: %ums)", totalSize, (unsigned)((systemTime() - startTime) / 1000000));
    return status;
}

status_t PictureThread::encode(MetaData &metaData, AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ENCODE;
    msg.data.encode.metaData = metaData;
    msg.data.encode.snaphotBuf = *snaphotBuf;
    if (postviewBuf) {
        msg.data.encode.postviewBuf = *postviewBuf;
    } else {
        // thumbnail is optional
        LOG1("@%s, encoding without Thumbnail", __FUNCTION__);
        msg.data.encode.postviewBuf.buff = NULL;
        msg.data.encode.postviewBuf.gfxData = NULL;
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

    params->set(CameraParameters::KEY_ROTATION, "0");
    params->setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    params->set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
            CameraParameters::PIXEL_FORMAT_JPEG);
    params->set(CameraParameters::KEY_JPEG_QUALITY, "80");
    params->set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "50");
}

void PictureThread::initialize(const CameraParameters &params)
{
    mExifMaker.initialize(params);
    int q = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
    if (q != 0)
        mPictureQuality = q;
    q = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if (q != 0)
        mThumbnailQuality = q;
}


status_t PictureThread::getSharedBuffers(int width, int height, char** sharedBuffersPtr, int *sharedBuffersNum)
{
    LOG1("@%s mInputBuffers %d", __FUNCTION__, mInputBuffers);
    status_t status = NO_ERROR;
    Message msg;

    if(sharedBuffersPtr == NULL || sharedBuffersNum == NULL) {
        LOGE("invalid parameters passed to %s", __FUNCTION__);
        return BAD_VALUE;
    }


    msg.id = MESSAGE_ID_FETCH_BUFS;
    msg.data.alloc.width = width;
    msg.data.alloc.height = height;

    status = mMessageQueue.send(&msg,MESSAGE_ID_FETCH_BUFS);

    if(  status == NO_ERROR &&
         mInputBufferArray[0].width ==  width &&
         mInputBufferArray[0].height ==  height ) {

        *sharedBuffersPtr = (char *)mInputBuffDataArray;
        *sharedBuffersNum = mInputBuffers;
    } else {
        status = BAD_VALUE;
        LOGE("Picture thread did not had any buffers, or it had with wrong dimensions." \
              " This should not happen!!");
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

status_t PictureThread::flushBuffers()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FLUSH;

    // we own the dynamically allocated MetaData, so free
    // data of pending message before flushing them
    Vector<Message> pending;
    mMessageQueue.remove(MESSAGE_ID_ENCODE, &pending);
    Vector<Message>::iterator it;
    for(it = pending.begin(); it != pending.end(); ++it) {
      it->data.encode.metaData.free();
    }

    return mMessageQueue.send(&msg, MESSAGE_ID_FLUSH);
}

status_t PictureThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

/**
 * Frees resources tired to metaData object.
 */
void PictureThread::MetaData::free()
{
    if (ia3AMkNote)
        AtomAAA::getInstance()->put3aMakerNote(ia3AMkNote);

    if (aeConfig)
        delete aeConfig;
}

/**
 * Passes the picture metadata to EXIFMaker.
 */
void PictureThread::setupExifWithMetaData(const PictureThread::MetaData &metaData)
{
    mExifMaker.pictureTaken();
    if (metaData.atomispMkNote)
        mExifMaker.setDriverData(*metaData.atomispMkNote);
    if (metaData.ia3AMkNote)
        mExifMaker.setMakerNote(*metaData.ia3AMkNote);
    if (metaData.aeConfig)
        mExifMaker.setSensorAeConfig(*metaData.aeConfig);
    if (metaData.flashFired)
        mExifMaker.enableFlash();
}

status_t PictureThread::handleMessageEncode(MessageEncode *msg)
{
    LOG1("@%s: snapshot ID = %d", __FUNCTION__, msg->snaphotBuf.id);
    status_t status = NO_ERROR;
    AtomBuffer jpegBuf;

    if (msg->snaphotBuf.width == 0 ||
        msg->snaphotBuf.height == 0 ||
        msg->snaphotBuf.format == 0) {
        LOGE("Picture information not set yet!");
        return UNKNOWN_ERROR;
    }

    jpegBuf.buff = NULL;

    PERFORMANCE_TRACES_SHOT2SHOT_STEP("encoding frame", msg->snaphotBuf.frameCounter);

    // prepare EXIF data
    setupExifWithMetaData(msg->metaData);

    // Encode the image
    AtomBuffer *postviewBuf;
    if(msg->postviewBuf.buff == NULL && msg->postviewBuf.gfxData == NULL)
        postviewBuf = NULL;
    else
        postviewBuf = &msg->postviewBuf;

    status = encodeToJpeg(&msg->snaphotBuf, postviewBuf, &jpegBuf);
    if (status != NO_ERROR) {
        LOGE("Error generating JPEG image!");
        if (jpegBuf.buff != NULL && jpegBuf.buff->data != NULL) {
            LOG1("Releasing jpegBuf @%p", jpegBuf.buff->data);
            jpegBuf.buff->release(jpegBuf.buff);
        }
        jpegBuf.buff = NULL;
    }

    // ownership was transferred to us from ControlThread, so we need
    // to free resources here after encoding
    msg->metaData.free();

    PERFORMANCE_TRACES_SHOT2SHOT_STEP("frame encoded", msg->snaphotBuf.frameCounter);

    mCallbacksThread->compressedFrameDone(&jpegBuf, &msg->snaphotBuf, &msg->postviewBuf);
    return status;
}

status_t PictureThread::handleMessageAllocBufs(MessageAllocBufs *msg)
{
    LOG1("@%s: width = %d, height = %d, numBufs = %d",
            __FUNCTION__,
            msg->width,
            msg->height,
            msg->numBufs);
    status_t status = NO_ERROR;

    /* check if re-allocation is needed */
    if( (mInputBufferArray != NULL) &&
        (mInputBuffers == msg->numBufs) &&
        (mInputBufferArray[0].width == msg->width) &&
        (mInputBufferArray[0].height == msg->height)) {
        LOG1("Trying to allocate same number of buffers with same resolution... skipping");
        return NO_ERROR;
    }

    /* Free old buffers if already allocated */
    size_t bufferSize = (msg->width * msg->height * 2);
    if (mOutBuf.buff != NULL && bufferSize != mOutBuf.buff->size) {
        mOutBuf.buff->release(mOutBuf.buff);
        mOutBuf.buff = NULL;
    }

    /* Allocate Output buffer : JPEG and EXIF */
    if (mOutBuf.buff == NULL || mOutBuf.buff->data == NULL || mOutBuf.buff->size <= 0) {
        mCallbacks->allocateMemory(&mOutBuf, bufferSize);
    }
    if (mExifBuf.buff == NULL || mExifBuf.buff->data == NULL || mExifBuf.buff->size <= 0) {
        mCallbacks->allocateMemory(&mExifBuf, MAX_EXIF_SIZE + sizeof(JPEG_MARKER_SOI) + sizeof(JPEG_MARKER_EOI));
    }
    if ((mOutBuf.buff == NULL || mOutBuf.buff->data == NULL) ||
        (mExifBuf.buff == NULL || mExifBuf.buff->data == NULL) ){
        LOGE("Could not allocate memory for output buffers!");
        return NO_MEMORY;
    }

    /* re-allocates array of input buffers into mInputBufferArray */
    freeInputBuffers();
    status = allocateInputBuffers(msg->width, msg->height, msg->numBufs);
    if(status != NO_ERROR)
        return status;

    /* Now let the encoder know about the new buffers for the surfaces*/
    if(mHwCompressor) {
        status = mHwCompressor->setInputBuffers(mInputBufferArray, mInputBuffers);
        if(status)
            LOGW("HW Encoder cannot use pre-allocate buffers");
    }

    return NO_ERROR;
}

status_t PictureThread::handleMessageFetchBuffers(MessageAllocBufs *msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if(mInputBuffers == 0) {
        LOGW("trying to get shared buffers before being allocated");
        msg->numBufs = 1;
        status = handleMessageAllocBufs(msg);
    }
    mMessageQueue.reply(MESSAGE_ID_FETCH_BUFS, status);
    return status;
}

status_t PictureThread::allocateInputBuffers(int width, int height, int numBufs)
{
    LOG1("@%s size (%dx%d) num %d", __FUNCTION__, width, height, numBufs);
    size_t bufferSize = frameSize(V4L2_PIX_FMT_NV12, width, height);

    if(numBufs == 0)
        return NO_ERROR;

    mInputBufferArray = new AtomBuffer[numBufs];
    mInputBuffDataArray = new char*[numBufs];
    if((mInputBufferArray == NULL) || mInputBuffDataArray == NULL)
        goto bailout;

    mInputBuffers = numBufs;

    for (int i = 0; i < mInputBuffers; i++) {
        mCallbacks->allocateMemory(&mInputBufferArray[i].buff, bufferSize);
        if(mInputBufferArray[i].buff == NULL || mInputBufferArray[i].buff->data == NULL) {
            mInputBuffers = i;
            goto bailout;
        }
        mInputBufferArray[i].width = width;
        mInputBufferArray[i].height = height;
        mInputBufferArray[i].format = V4L2_PIX_FMT_NV12;
        mInputBufferArray[i].size = bufferSize;
        mInputBuffDataArray[i] = (char *) mInputBufferArray[i].buff->data;
        LOG2("Snapshot buffer[%d] allocated, ptr = %p",i,mInputBufferArray[i].buff->data);
    }
    return NO_ERROR;

bailout:
    LOGE("Error allocating input buffers");
    freeInputBuffers();
    return NO_MEMORY;
}

void PictureThread::freeInputBuffers()
{
    LOG1("@%s", __FUNCTION__);

    if(mInputBufferArray != NULL) {
       for (int i = 0; i < mInputBuffers; i++)
           mInputBufferArray[i].buff->release(mInputBufferArray[i].buff);
       delete [] mInputBufferArray;
       mInputBufferArray = NULL;
       mInputBuffers = 0;
    }

    if(mInputBuffDataArray != NULL) {
       delete [] mInputBuffDataArray;
       mInputBuffDataArray = NULL;
    }
}

/**
 * Encode Thumbnail picture into mOutBuf
 * and encode the Exif data into buffer exifDst
 * returns encoded size if successful or
 * zero if encoding failed
 */
int PictureThread::encodeExifAndThumbnail(AtomBuffer *thumbBuf, unsigned char* exifDst)
{
    LOG1("@%s", __FUNCTION__);
    int size = 0;
    int exifSize = 0;
    nsecs_t endTime;
    JpegCompressor::InputBuffer inBuf;
    JpegCompressor::OutputBuffer outBuf;

    if (thumbBuf == NULL || exifDst == NULL)
        goto exit;

    if (thumbBuf->type == ATOM_BUFFER_PREVIEW_GFX)
    {
        if (thumbBuf->gfxData == NULL) {
           LOGW("Emtpy preview buffer was sent as thumbnail");
           goto exit;
        } else {
           inBuf.buf = (unsigned char*)thumbBuf->gfxData;
        }
    } else
    {
        if (thumbBuf->buff != NULL &&
            thumbBuf->buff->data == NULL) {
            LOGW("Emtpy buffer was sent for thumbnail");
            goto exit;
        } else {
            inBuf.buf = (unsigned char*)thumbBuf->buff->data;
        }
    }

    if (thumbBuf->width > 0 &&
        thumbBuf->height > 0) {

        // setup the JpegCompressor input and output buffers
        inBuf.width = thumbBuf->width;
        inBuf.height = thumbBuf->height;
        inBuf.format = thumbBuf->format;
        inBuf.size = frameSize(thumbBuf->format, thumbBuf->width, thumbBuf->height);

        outBuf.buf = (unsigned char*)mOutBuf.buff->data;
        outBuf.width = thumbBuf->width;
        outBuf.height = thumbBuf->height;
        outBuf.quality = mThumbnailQuality;
        outBuf.size = mOutBuf.buff->size;

        do {
            endTime = systemTime();
            size = mCompressor.encode(inBuf, outBuf);
            LOG1("Thumbnail JPEG size: %d (time to encode: %ums)", size, (unsigned)((systemTime() - endTime) / 1000000));
            if (size > 0) {
                mExifMaker.setThumbnail(outBuf.buf, size);
            } else {
                // This is not critical, we can continue with main picture image
                LOGE("Could not encode thumbnail stream!");
            }

            exifSize = mExifMaker.makeExif(&exifDst);
            outBuf.quality = outBuf.quality - 5;

        } while (exifSize > 0 && size > 0 && outBuf.quality > 0  && !mExifMaker.isThumbnailSet());
    }
exit:
    return exifSize;
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

        case MESSAGE_ID_FETCH_BUFS:
            status = handleMessageFetchBuffers(&msg.data.alloc);
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
