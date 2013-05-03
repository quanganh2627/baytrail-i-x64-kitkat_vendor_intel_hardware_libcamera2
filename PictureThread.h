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

#ifndef ANDROID_LIBCAMERA_PICTURE_THREAD_H
#define ANDROID_LIBCAMERA_PICTURE_THREAD_H

#include <utils/threads.h>
#include <camera.h>
#include <camera/CameraParameters.h>
#include "MessageQueue.h"
#include "AtomCommon.h"
#include "EXIFMaker.h"
#include "JpegHwEncoder.h"
#include "JpegCompressor.h"

namespace android {

class Callbacks;
class CallbacksThread;

class ISnapshotBufferUser {
public:
    ISnapshotBufferUser() {}
    virtual ~ISnapshotBufferUser() {}
    virtual status_t snapshotsAllocated(AtomBuffer *bufs, int numBufs) = 0;
};

class PictureThread : public Thread {

// constructor destructor
public:
    PictureThread(I3AControls *aaaControls);
    virtual ~PictureThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public types
    class MetaData {
    public:
      bool flashFired;                       /*!< whether flash was fired */
      SensorAeConfig *aeConfig;              /*!< defined in AtomAAA.h */
      atomisp_makernote_info *atomispMkNote; /*!< kernel provided metadata, defined linux/atomisp.h */
      ia_3a_mknote *ia3AMkNote;              /*!< defined in ia_3a_types.h */
      bool saveMirrored;                     /*!< whether to do mirroring */
      int cameraOrientation;                 /*!< camera sensor orientation */
      int currentOrientation;                /*!< Current orientation of the device */

      void free(I3AControls* aaaControls);
    };

// public methods
public:

    status_t encode(MetaData &metaData, AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf = NULL);

    void getDefaultParameters(CameraParameters *params);
    void initialize(const CameraParameters &params);
    status_t allocSharedBuffers(int width, int height, int sharedBuffersNum,
                                int format, ISnapshotBufferUser *user);

    status_t wait(); // wait to finish queued messages (sync)
    status_t flushBuffers();

    // Exif
    void setExifMaker(const String8& data);
    void setExifModel(const String8& data);
    void setExifSoftware(const String8& data);

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_ENCODE,
        MESSAGE_ID_ALLOC_BUFS,
        MESSAGE_ID_WAIT,
        MESSAGE_ID_FLUSH,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessageAllocBufs {
        int width;          /*!> width of the requested buffers */
        int height;         /*!> height of the requested buffers */
        int numBufs;        /*!> amount of buffers to allocate */
        int format;         /*!> V4L2 pixel format */
        ISnapshotBufferUser *user;      /*!> pointer to the user of those buffers */
    };

    struct MessageEncode {
        AtomBuffer snaphotBuf;
        AtomBuffer postviewBuf;
        MetaData metaData;
    };

    // union of all message data
    union MessageData {

        // MESSAGE_ID_ENCODE
        MessageEncode encode;
        MessageAllocBufs alloc;
    };

    // message id and message data
    struct Message {
        MessageId id;
        MessageData data;
    };

// private methods
private:

    // thread message execution functions
    status_t handleMessageExit();
    status_t handleMessageEncode(MessageEncode *encode);
    status_t handleMessageAllocBufs(MessageAllocBufs *alloc);
    status_t handleMessageWait();
    status_t handleMessageFlush();

    // main message function
    status_t waitForAndExecuteMessage();

    void setupExifWithMetaData(const MetaData &metaData);
    status_t encodeToJpeg(AtomBuffer *mainBuf, AtomBuffer *thumbBuf, AtomBuffer *destBuf);
    status_t allocateInputBuffers(int format, int width, int height, int numBufs);
    void     freeInputBuffers();
    int      encodeExifAndThumbnail(AtomBuffer *thumbnail, unsigned char* exifDst);
    status_t startHwEncoding(AtomBuffer *mainBuf);
    status_t completeHwEncode(AtomBuffer *mainBuf, AtomBuffer *destBuf);
    void     encodeExif(AtomBuffer *thumBuf);
    status_t doSwEncode(AtomBuffer *mainBuf, AtomBuffer* destBuf);
    status_t scaleMainPic(AtomBuffer *mainBuf);

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message, MessageId> mMessageQueue;
    bool        mThreadRunning;
    Callbacks       *mCallbacks;
    CallbacksThread *mCallbacksThread;
    JpegCompressor   mCompressor;
    JpegHwEncoder   *mHwCompressor;
    EXIFMaker       *mExifMaker;
    AtomBuffer      mExifBuf;
    AtomBuffer      mOutBuf;
    AtomBuffer      mThumbBuf;
    AtomBuffer      mScaledPic; /*!< Temporary local buffer where we scale the main
                                     picture (snapshot) in case is of a different
                                     resolution than the image requested by the client */

    /*
     * The resolution below is set up during initialize in case the receiving buffer
     * is of a different resolution so we know we have to scale
     */
    int mPictWidth;     /*!< Width of the main snapshot to encode */
    int mPictHeight;    /*!< Height of the main snapshot to encode */
    int mPictureQuality;
    int mThumbnailQuality;

    /* Input buffers */
    AtomBuffer *mInputBufferArray;
    char       **mInputBuffDataArray;   /*!< Convenience variable. TODO remove and use mInputBufferArray */
    int        mInputBuffers;

    // Exif data
    String8 mExifMakerName;
    String8 mExifModelName;
    String8 mExifSoftwareName;

    // 3A controls
    I3AControls* m3AControls;
}; // class PictureThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_PICTURE_THREAD_H
