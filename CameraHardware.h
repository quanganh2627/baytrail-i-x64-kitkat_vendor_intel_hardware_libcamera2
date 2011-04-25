/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_H

#include <semaphore.h>
#include "IntelCamera.h"
#include <CameraHardwareInterface.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <utils/threads.h>
#include <atomisp_config.h>

namespace android {

class CameraHardware : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual void        setCallbacks(notify_callback notify_cb,
                                     data_callback data_cb,
                                     data_callback_timestamp data_cb_timestamp,
                                     void* user);

    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual status_t    startPreview();
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();

    virtual status_t    touchToFocus(int blockNumber);
    virtual status_t    cancelTouchToFocus();

    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
	int flushParameters(const CameraParameters& params);
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t    sendCommand(int32_t command, int32_t arg1,
                                    int32_t arg2);
    virtual void release();

    static sp<CameraHardwareInterface> createInstance(int cameraId);

private:
    CameraHardware(int cameraId);
    virtual             ~CameraHardware();

    static wp<CameraHardwareInterface> singleton;

    class PreviewThread : public Thread {
        CameraHardware* mHardware;
    public:
        PreviewThread(CameraHardware* hw) :
#ifdef SINGLE_PROCESS
            // In single process mode this thread needs to be a java thread,
            // since we won't be calling through the binder.
            Thread(true),
#else
            Thread(false),
#endif
            mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThreadWrapper();
            // loop until we need to quit
            return false;
        }
    };

    class PictureThread : public Thread {
        CameraHardware* mHardware;
    public:
        PictureThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual bool threadLoop() {
            mHardware->pictureThread();
            return false;
        }
    };

    class AutoFocusThread : public Thread {
        CameraHardware* mHardware;
    public:
        AutoFocusThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraAutoFocusThread", PRIORITY_DEFAULT);
        }
        virtual bool threadLoop() {
            mHardware->autoFocusThread();
            return true;
        }
    };
    class AeAfAwbThread : public Thread {
        CameraHardware* mHardware;
    public:
        AeAfAwbThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraAeAfAwbThread", PRIORITY_DEFAULT);
        }
        virtual bool threadLoop() {
            mHardware->aeAfAwbThread();
            return false;
        }
    };

    void setSkipFrame(int frame);
    // Used by auto focus thread to block until it's told to run
    mutable Mutex       mFocusLock;
    mutable Condition   mFocusCondition;
    bool        mExitAutoFocusThread;

    mutable Mutex       mPreviewFrameLock;
    mutable Condition   mPreviewFrameCondition;
    bool        mExitAeAfAwbThread;
    mutable Mutex       mAeAfAwbLock;
    mutable Condition   mAeAfAwbEndCondition;

    // Used by preview thread to block
    mutable Mutex       mPreviewLock;
    mutable Condition   mPreviewCondition;
    mutable Mutex       mDeviceLock;
    bool        mPreviewRunning;
    bool        mExitPreviewThread;

    void initHeapLocked(int size);
    void initDefaultParameters();
    void initPreviewBuffer(int size);
    void deInitPreviewBuffer();
    bool checkRecording(int width, int height);
    void initRecordingBuffer(int size);
    void deInitRecordingBuffer();
    int  encodeToJpeg(int widht, int height, void *buf, int *jsize);
    void processPreviewFrame(void *buffer);
    void processRecordingFrame(void *buffer);
    void print_snapshot_time();

    bool        mRecordRunning;
    mutable Mutex       mRecordLock;
    mutable Condition   mRecordingReleaseCondition;
    int recordingThread();
#if ENABLE_BUFFER_SHARE_MODE
    int getSharedBuffer();
    bool checkSharedBufferModeOff();
    bool requestEnableSharingMode();
    bool requestDisableSharingMode();
#endif
    mutable Mutex       mLock;

    CameraParameters    mParameters;

    static const int    kBufferCount = 4;
    static const int    mAFMaxFrames = 20;

    struct frame_buffer {
        sp<MemoryHeapBase>  heap;
        sp<MemoryBase>      base[kBufferCount];
        uint8_t             *start[kBufferCount];
        unsigned int        flags[kBufferCount];
#if ENABLE_BUFFER_SHARE_MODE
        unsigned char *  pointerArray[kBufferCount];
#endif
    } mPreviewBuffer, mRecordingBuffer;

    sp<MemoryHeapBase>  mFrameIdHeap;
    sp<MemoryBase>      mFrameIdBase;

    int 		mCameraId;
    int                 mPreviewFrame;
    int                 mPostPreviewFrame;

    int                 mRecordingFrame;
    int                 mPostRecordingFrame;

    unsigned int        mPreviewPixelFormat;
    unsigned int        mPicturePixelFormat;
    bool                mVideoPreviewEnabled;

    sp<MemoryHeapBase>  mRawHeap;

    IntelCamera        *mCamera;
    //sensor_info_t      *mCurrentSensor;

    int                 mPreviewFrameSize;
    int                 mRecorderFrameSize;

    // protected by mLock
    sp<PreviewThread>   mPreviewThread;
    int previewThread();
    int previewThreadWrapper();

    sp<AutoFocusThread> mAutoFocusThread;
    int         autoFocusThread();

    sp<AeAfAwbThread> mAeAfAwbThread;
    int         aeAfAwbThread();
    bool        mPreviewAeAfAwbRunning;
    mutable Condition   mPreviewAeAfAwbCondition;

    sp<PictureThread>   mPictureThread;
    int         pictureThread();
    bool        mCaptureInProgress;

    notify_callback    mNotifyCb;
    data_callback      mDataCb;
    data_callback_timestamp mDataCbTimestamp;

    void               *mCallbackCookie;

    int32_t             mMsgEnabled;

    // used to guard threading state
    mutable Mutex       mStateLock;

// only used from PreviewThread

    // fps
    nsecs_t             mPreviewLastTS;
    float               mPreviewLastFPS;
    nsecs_t             mRecordingLastTS;
    float               mRecordingLastFPS;
    mutable Mutex       mSkipFrameLock;
    int                 mSkipFrame;

    //Postpreview
    int                 mPostViewWidth;
    int                 mPostViewHeight;
    int                 mPostViewSize;

    //3A
    AAAProcess          *mAAA;
#if ENABLE_BUFFER_SHARE_MODE
    bool                   isRecordingStarted;
    bool                   isCameraTurnOffBufferSharingMode;
#endif

    inline void setBF(unsigned int *bufferFlag, unsigned int flag) {
        *bufferFlag |= flag;
    }

    inline void clrBF(unsigned int *bufferFlag, unsigned int flag) {
        *bufferFlag &= ~flag;
    }

    inline bool isBFSet(unsigned int bufferFlag,unsigned int flag) {
        return (bufferFlag & flag);
    }

    enum {
        BF_ENABLED = 0x00000001,
        BF_LOCKED
    };
    //Used for picture taken time calculation
#ifdef PERFORMANCE_TUNING
    struct timeval  picture_start, preview_stop, pic_thread_start, snapshot_start, first_frame;
    struct timeval  second_frame, postview, snapshot_stop, jpeg_encoded, preview_start;
#endif
};

}; // namespace android

#endif
