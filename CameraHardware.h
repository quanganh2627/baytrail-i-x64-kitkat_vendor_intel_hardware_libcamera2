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

#include "IntelCamera.h"
#include <CameraHardwareInterface.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <utils/threads.h>

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
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
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
            mHardware->previewThread();
            // loop until we need to quit
            return true;
        }
    };

    void initHeapLocked(int size);
    void initDefaultParameters();
    void initPreviewBuffer();
    void deInitPreviewBuffer();
    void initRecordingBuffer();
    void deInitRecordingBuffer();

    int previewThread();
    int recordingThread();

    static int beginAutoFocusThread(void *cookie);
    int autoFocusThread();

    static int beginPictureThread(void *cookie);
    int pictureThread();

    mutable Mutex       mLock;

    CameraParameters    mParameters;

    inline void setBF(unsigned int *bufferFlag, unsigned int flag) {
        *bufferFlag |= flag;
    }

    inline void clrBF(unsigned int *bufferFlag, unsigned int flag) {
        *bufferFlag &= ~flag;
    }

    inline bool isBFSet(unsigned int bufferFlag,unsigned int flag) {
        return (bufferFlag & flag);
    }

    static const int    kBufferCount = 4;
    static const int    mAFMaxFrames = 20;

    struct frame_buffer {
        sp<MemoryHeapBase>  heap;
        sp<MemoryBase>      base[kBufferCount];
        uint8_t             *start[kBufferCount];
        unsigned int        flags[kBufferCount];
    } mPreviewBuffer, mRecordingBuffer;

    enum {
        BF_ENABLED = 0x00000001,
        BF_LOCKED
    };

    enum {
        CAM_DEFAULT = 0x01,
        CAM_PREVIEW,
        CAM_PIC_FOCUS,
        CAM_PIC_SNAP,
        CAM_VID_RECORD,
    } mCameraState;

    int 		mCameraId;
    int                 mPreviewFrame;
    int                 mPostPreviewFrame;

    int                 mRecordingFrame;
    int                 mPostRecordingFrame;

    unsigned int        mPreviewPixelFormat;
    unsigned int        mPicturePixelFormat;

    sp<MemoryHeapBase>  mRawHeap;

    IntelCamera        *mCamera;
    //sensor_info_t      *mCurrentSensor;

    bool                mRecordingRunning;
    int                 mPreviewFrameSize;

    // protected by mLock
    sp<PreviewThread>   mPreviewThread;

    notify_callback    mNotifyCb;
    data_callback      mDataCb;
    data_callback_timestamp mDataCbTimestamp;

    void               *mCallbackCookie;

    int32_t             mMsgEnabled;

 // only used from PreviewThread

    // fps
    nsecs_t             mPreviewLastTS;
    float               mPreviewLastFPS;
    nsecs_t             mRecordingLastTS;
    float               mRecordingLastFPS;
};

}; // namespace android

#endif
