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

#define LOG_TAG "CameraHardware"
#include <utils/Log.h>

#include "CameraHardware.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "SkBitmap.h"
#include "SkImageEncoder.h"
#include "SkStream.h"

namespace android {
#define TRUE 1
#define FALSE 0
#define CAMERA_MSG_TOUCH_TO_FOCUS 0x200

bool share_buffer_caps_set=false;

CameraHardware::CameraHardware(int cameraId)
    : mParameters(),
      mCameraId(cameraId),
      mPreviewFrame(0),
      mPostPreviewFrame(0),
      mRecordingFrame(0),
      mPostRecordingFrame(0),
      mCamera(0),
      mAAA(0),
      semAAA(),
      mWinFocus(),
      mIsTouchFocus(FALSE),
      mAaaThreadStarted(0),
      mRecordingRunning(false),
      mPreviewFrameSize(0),
      mNotifyCb(0),
      mDataCb(0),
      mDataCbTimestamp(0),
      mCallbackCookie(0),
      mMsgEnabled(0),
      mPreviewLastTS(0),
      mPreviewLastFPS(0),
      mRecordingLastTS(0),
      mRecordingLastFPS(0)
{
    int fd;
    int semret;
    mCamera = new IntelCamera();

    LOGD("!!!line:%d, in CameraHardware, mAAA:%d", __LINE__, (unsigned int)mAAA);
    mAAA = new AAAProcess(ENUM_SENSOR_TYPE_RAW);
    if(!mAAA) {
        LOGE("!!!line:%d, CameraHardware fail, mAAA is null", __LINE__);
        return;
    }
    semret = sem_init(&semAAA, 0, 0);
    if(semret) {
        LOGE("!!!line:%d, CameraHardware fail, semAAA is null, semret:%d", __LINE__, semret);
        return;
    }


    initDefaultParameters();
    /* Init the 3A library only once */
    mAAA->Init();
    mCameraState = CAM_DEFAULT;
    LOGD("libcamera version: 2011-03-01 1.0.1");
}

void CameraHardware::initHeapLocked(int size)
{
    LOGD("xiaolin@%s(), size %d, preview size%d", __func__, size, mPreviewFrameSize);
    int recordersize;
    if (size != mPreviewFrameSize) {
        const char *preview_fmt;
        preview_fmt = mParameters.getPreviewFormat();

        if (strcmp(preview_fmt, "yuv420sp") == 0) {
            recordersize = size;
        }  else if (strcmp(preview_fmt, "yuv422i-yuyv") == 0) {
            recordersize = size;
        } else if (strcmp(preview_fmt, "rgb565") == 0) {
            recordersize = (size * 3)/4;
        } else {
            LOGD("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
            recordersize = size;
        }

        mPreviewBuffer.heap = new MemoryHeapBase(size * kBufferCount);
        mRecordingBuffer.heap = new MemoryHeapBase(recordersize * kBufferCount);

        for (int i=0; i < kBufferCount; i++) {
            mPreviewBuffer.flags[i] = 0;
            mRecordingBuffer.flags[i] = 0;

            mPreviewBuffer.base[i] =
                new MemoryBase(mPreviewBuffer.heap, i * size, size);
            clrBF(&mPreviewBuffer.flags[i], BF_ENABLED|BF_LOCKED);
            mPreviewBuffer.start[i] = (uint8_t *)mPreviewBuffer.heap->base() + (i * size);

            mRecordingBuffer.base[i] =
                new MemoryBase(mRecordingBuffer.heap, i *recordersize, recordersize);
            clrBF(&mRecordingBuffer.flags[i], BF_ENABLED|BF_LOCKED);
            mRecordingBuffer.start[i] = (uint8_t *)mRecordingBuffer.heap->base() + (i * recordersize);

        }
        LOGD("%s Re Alloc previewframe size=%d, recordersiz=%d",__func__, size, recordersize);
        mPreviewFrameSize = size;
    }

}

void CameraHardware::initPreviewBuffer() {

}

void CameraHardware::deInitPreviewBuffer() {

}

void CameraHardware::initRecordingBuffer() {

}

void CameraHardware::deInitRecordingBuffer() {

}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
    LOGD("!!!line:%d, enter initDefaultParameters\n", __LINE__);
    p.setPreviewSize(640, 480);
    p.setPreviewFrameRate(30);
    p.setPreviewFormat("yuv420sp");
#else
    LOGD("!!!line:%d, enter initDefaultParameters\n", __LINE__);
    p.setPreviewSize(320, 240);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("rgb565");
#endif
    //mAAA->ModeSpecInit();

    p.setPictureFormat("jpeg");
    p.set("preview-format-values", "yuv420sp,rgb565");
    p.set("preview-size-values", "640x480");
    p.set("picture-format-values", "jpeg");
    p.set("jpeg-quality","100");

    p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, "auto,portrait,sports,landscape,night,fireworks");
    p.set(CameraParameters::KEY_SCENE_MODE, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, "auto,incandescent,fluorescent,daylight,cloudy-daylight");
    p.set(CameraParameters::KEY_WHITE_BALANCE, "auto");

    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto,infinity,macro,continuous-video");
    p.set(CameraParameters::KEY_FOCUS_MODE, "auto");

    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
    p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "2");
    p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-2");
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "1");

    p.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, "auto,50hz,60hz,off");
    p.set(CameraParameters::KEY_ANTIBANDING, "auto");

    p.set("effect", "none");
    p.set("flash-mode","off");
    p.set("jpeg-quality-values","1,20,30,40,50,60,70,80,90,99,100");
    p.set("effect-values","none,mono,negative,sepia,aqua,pastel,whiteboard");
    p.set("flash-mode-values","off,auto,on");
    p.set("rotation-values","0,90,180");
    p.set("video-frame-format","yuv420sp");
    p.set("zoom-supported", "true");
    p.set("max-zoom", "64");
    p.set("zoom-ratios", "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,"
          "22,23,24,24,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,"
          "45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64");
    p.set("zoom", 0);

    if (mCameraId == CAMERA_FACING_BACK) {
        // For main back camera
        p.set("rotation","90");
        p.set("picture-size-values","320x240,640x480,1024x768,1280x720,1920x1080,2048x1536,2560x1920,3264x2448,3840x2400,4096x3072,4352x3264");
    } else {
        // For front camera
        p.set("rotation","0");
        p.set("picture-size-values","320x240,640x480,1280x720,1920x1080");
    }

    mParameters = p;

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }
}

CameraHardware::~CameraHardware()
{
    delete mCamera;
    sem_close(&semAAA);
    delete mAAA;
    singleton.clear();
}

sp<IMemoryHeap> CameraHardware::getPreviewHeap() const
{
    return mPreviewBuffer.heap;
}

sp<IMemoryHeap> CameraHardware::getRawHeap() const
{
    return mRawHeap;
}

void CameraHardware::setCallbacks(notify_callback notify_cb,
                                  data_callback data_cb,
                                  data_callback_timestamp data_cb_timestamp,
                                  void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void CameraHardware::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void CameraHardware::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool CameraHardware::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}

// ---------------------------------------------------------------------------
int CameraHardware::previewThread()
{
    int semret;
    static int restarted = 0;
    if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
        // Get a preview frame
        int previewFrame = mPreviewFrame;
        if( !isBFSet(mPreviewBuffer.flags[previewFrame], BF_ENABLED)
                && !isBFSet(mPreviewBuffer.flags[previewFrame], BF_LOCKED)) {

            setBF(&mPreviewBuffer.flags[previewFrame],BF_LOCKED);

            if (mCamera->captureGrabFrame() == 0) {
                LOGE("%s: DQ error, restart the preview\n", __func__);
                if (restarted)
                    return -1;
                /* Stop 3A thread */
                mAAA->SetAfEnabled(FALSE);
                mAAA->SetAeEnabled(FALSE);
                mAAA->SetAwbEnabled(FALSE);
                semret = sem_post(&semAAA);

                /* Stop the camera device */
                mCamera->captureStop();
                mCamera->captureUnmapFrame();
                mCamera->captureFinalize();

                /* Start the camera device */
                int w, h, preview_size;
                int fd = 0;

                mParameters.getPreviewSize(&w, &h);

                if (mCamera->captureOpen(&fd) < 0)
                    return -1;

                mAAA->IspSetFd(fd);

                //    int fd = mCamera->get_device_fd();
                mAAA->SwitchMode(CI_ISP_MODE_PREVIEW);

                mCamera->captureInit(w, h, mPreviewPixelFormat, 3, mCameraId);
                mCamera->captureMapFrame();
                mCamera->captureStart();
                mCamera->set_zoom_val(mCamera->get_zoom_val());

                mAAA->SetAfEnabled(TRUE);
                mAAA->SetAeEnabled(TRUE);
                mAAA->SetAwbEnabled(TRUE);

                /* Apply the previous frame results FIXME: Does this take
                 * effect*/
                mAAA->AwbApplyResults();
                mAAA->AeApplyResults();
                /* Create the 3A thread again */
                if(!mAaaThreadStarted) {
                    if (createThread(beginAaaThread, this) == false) {
                        LOGE("!!!line:%d, in startPreview, create aaa thread fail", __LINE__);
                        return -1;
                    }
                    mAaaThreadStarted = 1;
                }
                restarted = 1;
                return NO_ERROR;
            }

            restarted = 0;
            const char *preview_fmt;
            preview_fmt = mParameters.getPreviewFormat();

            if (!strcmp(preview_fmt, "yuv420sp") ||
                    !strcmp(preview_fmt, "yuv422i-yuyv") ||
                    !strcmp(preview_fmt, "rgb565")) {
#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
                /* only copy current frame id */
                unsigned int frame_id = mCamera->captureGetFrameID();
                memcpy(mPreviewBuffer.start[previewFrame],
                       &frame_id,
                       sizeof(unsigned int));
#else
                mCamera->captureGetFrame(mPreviewBuffer.start[previewFrame]);
#endif
            } else {
                LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
                return -1;
            }

            semret = sem_post(&semAAA);
            if(semret) {
                LOGE("!!!line:%d, in previewThread, sem_post fail, semret:%d", __LINE__, semret);
            }
            if(mAAA->GetAfStillFrames() && mAAA->GetAfStillEnabled())
            {
                mAAA->SetAfStillFrames(mAAA->GetAfStillFrames() + 1);
            }
            //LOGD("xiaolin@%s()222@", __func__);
            clrBF(&mPreviewBuffer.flags[previewFrame],BF_LOCKED);
            setBF(&mPreviewBuffer.flags[previewFrame],BF_ENABLED);

            //mCamera->captureRecycleFrame();
            mPreviewFrame = (previewFrame + 1) % kBufferCount;
        }

        // Notify the client of a new preview frame.
        int postPreviewFrame = mPostPreviewFrame;
        if (isBFSet(mPreviewBuffer.flags[postPreviewFrame], BF_ENABLED)
                && !isBFSet(mPreviewBuffer.flags[postPreviewFrame], BF_LOCKED)) {
            setBF(&mPreviewBuffer.flags[postPreviewFrame],BF_LOCKED);
            nsecs_t current_ts = systemTime(SYSTEM_TIME_MONOTONIC);
            nsecs_t interval_ts = current_ts - mPreviewLastTS;
            float current_fps, average_fps;
            mPreviewLastTS = current_ts;
            current_fps = (float)1000000000 / (float)interval_ts;
            average_fps = (current_fps + mPreviewLastFPS) / 2;
            mPreviewLastFPS = current_fps;

            LOGV("Preview FPS : %2.1f\n", average_fps);
            LOGV("transfer a preview frame to client (index:%d/%d)",
                 postPreviewFrame, kBufferCount);

            if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
                mDataCb(CAMERA_MSG_PREVIEW_FRAME,
                        mPreviewBuffer.base[postPreviewFrame], mCallbackCookie);
            }
            clrBF(&mPreviewBuffer.flags[postPreviewFrame],BF_LOCKED|BF_ENABLED);
            mPostPreviewFrame = (postPreviewFrame + 1) % kBufferCount;
        }
    }

    // TODO: Have to change the recordingThread() function to others thread ways
    recordingThread();

    mCamera->captureRecycleFrame();

    return NO_ERROR;
}

status_t CameraHardware::startPreview()
{
    LOGD("%s: Begin\n", __func__);
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        LOGE("!!!line:%d, startPreview, Already running", __LINE__);
        return INVALID_OPERATION;
    }

    int w, h, preview_size;
    int fd = 0;

    mParameters.getPreviewSize(&w, &h);

    if (mCamera->captureOpen(&fd) < 0)
        return -1;

    mAAA->IspSetFd(fd);

    //    int fd = mCamera->get_device_fd();
    mAAA->SwitchMode(CI_ISP_MODE_PREVIEW);

    mCamera->captureInit(w, h, mPreviewPixelFormat, 3, mCameraId);

    mAAA->ModeSpecInit();

    mAAA->SetAfEnabled(TRUE);
    mAAA->SetAeEnabled(TRUE);
    mAAA->SetAwbEnabled(TRUE);


    mCamera->captureMapFrame();
    mCamera->captureStart();
    mCamera->set_zoom_val(mCamera->get_zoom_val());

    const char *preview_fmt;
    preview_fmt = mParameters.getPreviewFormat();

    if (strcmp(preview_fmt, "yuv420sp") == 0) {
        preview_size = (w * h * 3)/2;
    }  else if (strcmp(preview_fmt, "yuv422i-yuyv") == 0) {
        preview_size = w * h * 2;
    } else if (strcmp(preview_fmt, "rgb565") == 0) {
        //FIXME
        preview_size = w * h * 2;
    } else {
        LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
        return -1;
    }

    mCameraState = CAM_PREVIEW;
    LOGD("!!!line:%d, before SwitchMode!", __LINE__);
    initHeapLocked(preview_size);

    mPreviewThread = new PreviewThread(this);

    if(!mAaaThreadStarted) {
        if (createThread(beginAaaThread, this) == false) {
            LOGE("!!!line:%d, in startPreview, create aaa thread fail", __LINE__);
            return -1;
        }
        mAaaThreadStarted = 1;
    }

    return NO_ERROR;
}

void CameraHardware::stopPreview()
{
    sp<PreviewThread> previewThread;

    // Stop 3A thread first
    int semret;
    mAAA->SetAfEnabled(FALSE);
    mAAA->SetAeEnabled(FALSE);
    mAAA->SetAwbEnabled(FALSE);
    semret = sem_post(&semAAA);
    if(semret) {
        LOGE("!!!line:%d, in stopPreview, sem_post fail, semret:%d", __LINE__, semret);
    }

    {   // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);

    if (mPreviewThread != 0) {
        mPreviewThread.clear();
        mCamera->captureStop();
        mCamera->captureUnmapFrame();
        mCamera->captureFinalize();
    }

    mCameraState = CAM_DEFAULT;
}

bool CameraHardware::previewEnabled()
{
    return (mPreviewThread != 0);
}

int CameraHardware::recordingThread()
{

    if (!share_buffer_caps_set) {
        unsigned int *frame_id;
        unsigned int frame_num;
        frame_num = mCamera->get_frame_num();
        frame_id = new unsigned int[frame_num];
        mCamera->get_frame_id(frame_id, frame_num);
        mParameters.set_frame_id(frame_id, frame_num);
        delete [] frame_id;
        share_buffer_caps_set=true;
    }

    if (mRecordingRunning && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
        // Get a recording frame
        int recordingFrame = mRecordingFrame;
        int previewFrame = (mPreviewFrame + 3) % kBufferCount;
        if (!isBFSet(mRecordingBuffer.flags[recordingFrame], BF_ENABLED)
                && !isBFSet(mRecordingBuffer.flags[recordingFrame], BF_LOCKED)) {
#if 0 /* in order to avoid conversion twice with nv21 */
            mCamera->captureGetRecordingFrame(mRecordingBuffer.start[recordingFrame]);
#else
            setBF(&mPreviewBuffer.flags[previewFrame], BF_LOCKED);
            setBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
#if 0
            memcpy(mRecordingBuffer.start[recordingFrame],
                   mPreviewBuffer.start[previewFrame], mPreviewFrameSize);
#endif
            if (mParameters.get_buffer_sharing())
                mCamera->captureGetRecordingFrame(mRecordingBuffer.start[recordingFrame], 1);
            else
                mCamera->captureGetRecordingFrame(mRecordingBuffer.start[recordingFrame], 0);
            clrBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
            clrBF(&mPreviewBuffer.flags[previewFrame], BF_LOCKED);
#endif
            setBF(&mRecordingBuffer.flags[recordingFrame],BF_ENABLED);
            mRecordingFrame = (recordingFrame + 1) % kBufferCount;
        }

        // Notify the client of a new recording frame.
        int postRecordingFrame = mPostRecordingFrame;
        if ( !isBFSet(mRecordingBuffer.flags[postRecordingFrame], BF_LOCKED)
                && isBFSet(mRecordingBuffer.flags[postRecordingFrame], BF_ENABLED)) {
            nsecs_t current_ts = systemTime(SYSTEM_TIME_MONOTONIC);
            nsecs_t interval_ts = current_ts - mRecordingLastTS;
            float current_fps, average_fps;
            mRecordingLastTS = current_ts;
            current_fps = (float)1000000000 / (float)interval_ts;
            average_fps = (current_fps + mRecordingLastFPS) / 2;
            mRecordingLastFPS = current_fps;

            LOGV("Recording FPS : %2.1f\n", average_fps);
            LOGV("transfer a recording frame to client (index:%d/%d) %u",
                 postRecordingFrame, kBufferCount, (unsigned int)current_ts);

            clrBF(&mRecordingBuffer.flags[postRecordingFrame],BF_ENABLED);
            setBF(&mRecordingBuffer.flags[postRecordingFrame],BF_LOCKED);

            mDataCbTimestamp(current_ts, CAMERA_MSG_VIDEO_FRAME,
                             mRecordingBuffer.base[postRecordingFrame], mCallbackCookie);

            mPostRecordingFrame = (postRecordingFrame + 1) % kBufferCount;
        }
    }
    return NO_ERROR;
}

status_t CameraHardware::startRecording()
{
    for (int i=0; i < kBufferCount; i++) {
        clrBF(&mPreviewBuffer.flags[i], BF_ENABLED|BF_LOCKED);
        clrBF(&mRecordingBuffer.flags[i], BF_ENABLED|BF_LOCKED);
    }

    mRecordingRunning = true;
    mCameraState = CAM_VID_RECORD;
    // TODO: mAAA->SwitchMode(CI_ISP_MODE_VIDEO);
    mAAA->SwitchMode(CI_ISP_MODE_PREVIEW);

    return NO_ERROR;
}

void CameraHardware::stopRecording()
{
    mRecordingRunning = false;
    mCameraState = CAM_PREVIEW;
    mAAA->SwitchMode(CI_ISP_MODE_PREVIEW);
}

bool CameraHardware::recordingEnabled()
{
    return mRecordingRunning;
}

void CameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
    ssize_t offset = mem->offset();
    size_t size = mem->size();
    int releasedFrame = offset / size;

#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
    unsigned int *buff = (unsigned int *)mem->pointer();

    LOGV(" releaseRecordingFrame : buff = %x ", buff[0]);
    if(mRecordingRunning) {
        LOGV(" Calls to captureRecycleFrame ");

        mCamera->captureRecycleFrameWithFrameId(*buff);

        LOGV(" Called captureRecycleFrame ");
    }
#endif

    clrBF(&mRecordingBuffer.flags[releasedFrame], BF_LOCKED);

    LOGV("a recording frame transfered to client has been released (index:%d/%d)",
         releasedFrame, kBufferCount);
}

// ---------------------------------------------------------------------------

int CameraHardware::beginAutoFocusThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->autoFocusThread();
}

int CameraHardware::autoFocusThread()
{
    bool rc = false;
    int i;
    int afret;
    cam_Window win;

    LOGD("!!!line:%d, enter autoFocusThread", __LINE__);

    if(mIsTouchFocus)
    {
        if(AAA_SUCCESS != mAAA->AfGetWindow(&win))
        {
            LOGE("!!!line:%d, AfGetWindow fail", __LINE__);
        }
        LOGD("!!!line:%d, AfGetWindow(): x_left:%d, x_right:%d, y_top:%d, y_bottom:%d, weight:%d",
             __LINE__, win.x_left, win.x_right, win.y_top, win.y_bottom, win.weight);

        LOGD("!!!line:%d, mWinFocus: x_left:%d, x_right:%d, y_top:%d, y_bottom:%d, weight:%d",
             __LINE__, mWinFocus.x_left, mWinFocus.x_right,
             mWinFocus.y_top, mWinFocus.y_bottom, mWinFocus.weight);

        win.x_left = mWinFocus.x_left;
        win.x_right = mWinFocus.x_right;
        win.y_top = mWinFocus.y_top;
        win.y_bottom = mWinFocus.y_bottom;
        if(AAA_SUCCESS != mAAA->AfSetWindow(&win))
        {
            LOGE("!!!line:%d, AfSetWindow fail", __LINE__);
        }
    }

    mAAA->SetAfStillFrames(1);
    while(TRUE) {
        afret = mAAA->AfStillIsComplete();
        if(( afret == AAA_SUCCESS) || mAAA->GetAfStillIsOverFrames()) {
            LOGD("!!!line:%d, in autoFocusThread, afret:%d (0 means success), do af frames:%d", __LINE__, afret, mAAA->GetAfStillFrames());
            mAAA->AfStillStop ();
            rc = (afret == AAA_SUCCESS) ? true : false;
            break;
        }
    }
    mAAA->SetAfStillFrames(0);
    mAAA->SetAfStillEnabled(FALSE);

    if(mIsTouchFocus)
    {
        mIsTouchFocus = FALSE;
        mNotifyCb(CAMERA_MSG_TOUCH_TO_FOCUS, rc, 0, mCallbackCookie);
    }
    else
        mNotifyCb(CAMERA_MSG_FOCUS, rc, 0, mCallbackCookie);

    return NO_ERROR;
}

status_t CameraHardware::autoFocus()
{
    LOGD("!!!line:%d, enter autoFocus", __LINE__);

    if (mCameraState == CAM_PIC_FOCUS)
        return NO_ERROR;
    Mutex::Autolock lock(mLock);
    mCameraState = CAM_PIC_FOCUS;
    mAAA->SetAfEnabled(FALSE);
    mAAA->SetAfStillEnabled(TRUE);
    mAAA->SetAfStillFrames(0);
    mAAA->AfStillStart();
    if (createThread(beginAutoFocusThread, this) == false) {
        LOGE("!!!line:%d, create thread beginAutoFocusThread fail", __LINE__);
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t CameraHardware::cancelAutoFocus()
{
    LOGD("!!!line:%d, enter cancelAutoFocus", __LINE__);

    mAAA->SetAfStillFrames(0);
    mAAA->AfStillStop();
    mAAA->SetAfEnabled(TRUE);
    mAAA->SetAfStillEnabled(FALSE);
    mCameraState = CAM_PREVIEW;
    mAAA->SwitchMode(CI_ISP_MODE_PREVIEW);
    return NO_ERROR;
}

status_t CameraHardware::touchToFocus(int blockNumber)
{
    LOGD("!!!line:%d, enter touchToFocus", __LINE__);
    mIsTouchFocus = TRUE;
    return autoFocus();
}

status_t CameraHardware::cancelTouchToFocus()
{
    LOGD("!!!line:%d, enter cancelTouchToFocus", __LINE__);
    return cancelAutoFocus();
}


/*static*/
int CameraHardware::beginPictureThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->pictureThread();
}

#define MAX_FRAME_WAIT 3
#define FLASH_FRAME_WAIT 4
int CameraHardware::pictureThread()
{
    int frame_cnt = 0;
    int frame_wait = MAX_FRAME_WAIT;
    ci_adv_dis_vector vector;
    int frame_number = 1;
    bool flash_necessary = false;

    if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        //FIXME: use a canned YUV image!
        // In the meantime just make another fake camera picture.
        //          mDataCb(CAMERA_MSG_RAW_IMAGE, mBuffer, mCallbackCookie);
    }

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        int w, h;
        mParameters.getPictureSize(&w, &h);
		LOGD("%s: picture size is %dx%d\n", __func__, w, h);
        int fd = -1;
        mCamera->captureOpen(&fd);
        mAAA->IspSetFd(fd);
        mAAA->SwitchMode(CI_ISP_MODE_CAPTURE);
        mCamera->captureInit(w, h, mPicturePixelFormat, 1, mCameraId);
        mCamera->captureMapFrame();
        mCamera->captureStart();
        mCamera->set_zoom_val(mCamera->get_zoom_val());
        /* apply the 3A results from the preview */
        mAAA->SetAfEnabled(TRUE);
        mAAA->SetAeEnabled(TRUE);
        mAAA->SetAwbEnabled(TRUE);
        mAAA->AwbApplyResults();
        LOGD("%s: apply 3A results from preview\n", __func__);
        mAAA->AeApplyResults();

        int jpegSize;
        int sensorsize;
        while (frame_wait--) {
            mCamera->captureGrabFrame();
            mCamera->captureRecycleFrame();
        }

        mAAA->IspSetFd(mCamera->get_device_fd());
        //Fixme: mAAA->AeIsFlashNecessary (&flash_necessary);
        if (flash_necessary == true && mCameraId == CAMERA_FACING_BACK) {
            // pre-flash process
            // flash off
            mAAA->AeCalcForFlash();
            mCamera->captureFlashOff();
            mCamera->captureGrabFrame();
            usleep(200 * 1000);
            mAAA->GetStatistics();
            mCamera->captureRecycleFrame();

            // pre-flash
            mAAA->AeCalcWithoutFlash();
            mCamera->captureFlashOnCertainDuration(0, 0, 8, 0);
            mAAA->AwbCalcFlash();
            mCamera->captureGrabFrame();
            usleep(200 * 1000);
            mAAA->GetStatistics();
            mCamera->captureRecycleFrame();

            // main flash
            mAAA->AeCalcWithFlash();
            mAAA->AwbCalcFlash();
            mCamera->captureFlashOnCertainDuration(0, 0, 8, 15);
            mAAA->AwbApplyResults();
        }

        sensorsize = mCamera->captureGrabFrame();
        jpegSize=(sensorsize * 3)/10;

        LOGD(" - JPEG size saved = %dB, %dK",jpegSize, jpegSize/1000);

        sp<MemoryHeapBase> heapSensor = new MemoryHeapBase(sensorsize);
        sp<MemoryBase> bufferSensor = new MemoryBase(heapSensor, 0, sensorsize);
        sp<MemoryHeapBase> heapJpeg = new MemoryHeapBase(jpegSize);
        sp<MemoryBase> bufferJpeg = new MemoryBase(heapJpeg, 0, jpegSize);

        mCamera->captureGetFrame(heapSensor->getBase());

        mCamera->captureRecycleFrame();
        mCamera->captureStop();
        mAAA->SwitchMode(CI_ISP_MODE_PREVIEW);
        mCamera->captureUnmapFrame();
        mCamera->captureFinalize();

        SkImageEncoder::Type fm;
        fm = SkImageEncoder::kJPEG_Type;


        bool success = false;
        SkMemoryWStream *stream =new SkMemoryWStream(heapJpeg->getBase(),jpegSize);

        SkBitmap *bitmap =new SkBitmap();
        bitmap->setConfig(SkBitmap::kRGB_565_Config, w,h);

        bitmap->setPixels(heapSensor->getBase(),NULL);
        SkImageEncoder* encoder = SkImageEncoder::Create(fm);
        if (NULL != encoder) {
            success = encoder->encodeStream(stream, *bitmap, 75);
            //success = encoder->encodeFile("/data/tmp.jpg", *bitmap, 75);
            //LOGD(" sucess is %d",success);
            delete encoder;
        }
        //mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
        mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, bufferJpeg, mCallbackCookie);

        bufferJpeg.clear();
        heapJpeg.clear();
        bufferSensor.clear();
        heapSensor.clear();
    }
    return NO_ERROR;
}

status_t CameraHardware::takePicture()
{
    LOGD("!!!line:%d, enter takePicture", __LINE__);

    if(mCameraState == CAM_PIC_SNAP)
        return NO_ERROR;

    disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    stopPreview();
    mCameraState = CAM_PIC_SNAP;

    mAAA->SetStillStabilizationEnabled(FALSE);
    mAAA->SetRedEyeRemovalEnabled(FALSE);

    if (createThread(beginPictureThread, this) == false)
        return -1;

    return NO_ERROR;
}

status_t CameraHardware::cancelPicture()
{
    LOGD("!!!line:%d, in takePicture", __LINE__);
    return NO_ERROR;
}

/*static*/
int CameraHardware::beginAaaThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->aaaThread();
}

int CameraHardware::aaaThread()
{
    LOGD("!!!line:%d, enter aaaThread", __LINE__);

    while(1) {
        sem_wait(&semAAA);

        if(!previewEnabled()) {
            LOGD("!!!line:%d, in aaaThread, preview not enabled", __LINE__);
            break;
        }

        if(!mAAA->GetAeEnabled()
                && !mAAA->GetAfEnabled()
                && !mAAA->GetAwbEnabled()) {
            LOGD("!!!line:%d, in aaaThread, aaa function not start", __LINE__);
            break;
        }

        mAAA->GetStatistics();

        mAAA->AeProcess();
        mAAA->AfProcess();
        mAAA->AwbProcess();

        mAAA->AwbApplyResults();
        mAAA->AeApplyResults();
    }

    mAaaThreadStarted = 0;
    LOGD("!!!line:%d, leave aaaThread", __LINE__);
    return NO_ERROR;
}

status_t CameraHardware::dump(int fd, const Vector<String16>& args) const
{
    LOGD("%s",__func__);
    return NO_ERROR;
}

status_t CameraHardware::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);
    // XXX verify params

    params.dump();  // print parameters for debug

    CameraParameters p = params;

    int preview_size;
    int preview_width,preview_height;
    p.getPreviewSize(&preview_width,&preview_height);
    /*
        if( (preview_width != 640) && (preview_height != 480) ) {
          preview_width = 640;
          preview_height = 480;
          LOGE("Only %dx%d for preview are supported",preview_width,preview_height);
        }
    */
    p.setPreviewSize(preview_width,preview_height);

    int new_fps = p.getPreviewFrameRate();
    int set_fps = mParameters.getPreviewFrameRate();
    LOGD(" - FPS = new \"%d\" / current \"%d\"",new_fps, set_fps);
    if (new_fps != set_fps) {
        p.setPreviewFrameRate(new_fps);
        LOGD("     ++ Changed FPS to %d",p.getPreviewFrameRate());
    }
    LOGD("PREVIEW SIZE: %dx%d, FPS: %d", preview_width, preview_height, new_fps);

    const char *new_value, *set_value;

    new_value = p.getPreviewFormat();
    set_value = mParameters.getPreviewFormat();

    if (strcmp(new_value, "yuv420sp") == 0) {
        mPreviewPixelFormat = V4L2_PIX_FMT_NV12;
        preview_size = (preview_width * preview_height * 3)/2;
    }  else if (strcmp(new_value, "yuv422i-yuyv") == 0) {
        mPreviewPixelFormat = V4L2_PIX_FMT_YUYV;
        preview_size = preview_width * preview_height * 2;
    } else if (strcmp(new_value, "rgb565") == 0) {
        mPreviewPixelFormat = V4L2_PIX_FMT_RGB565;
        preview_size = preview_width * preview_height * 2;
    } else {
        LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
        return -1;
    }

    LOGD(" - Preview pixel format = new \"%s\"  / current \"%s\"",new_value, set_value);
    if (strcmp(set_value, new_value)) {
        p.setPreviewFormat(new_value);
        LOGD("     ++ Changed Preview Pixel Format to %s",p.getPreviewFormat());
    }

    new_value = p.getPictureFormat();
    LOGD("%s",new_value);
    set_value = mParameters.getPictureFormat();
    if (strcmp(new_value, "jpeg") == 0)
        mPicturePixelFormat = V4L2_PIX_FMT_RGB565;
    else {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    LOGD(" - Picture pixel format = new \"%s\"  / current \"%s\"",new_value, set_value);
    if (strcmp(set_value, new_value)) {
        p.setPictureFormat(new_value);
        LOGD("     ++ Changed Preview Pixel Format to %s",p.getPictureFormat());
    }

    int picture_width, picture_height;
    p.getPictureSize(&picture_width, &picture_height);

    if(mCamera != NULL)
        LOGD("verify a jpeg picture size %dx%d",picture_width,picture_height);

    p.setPictureSize(picture_width, picture_height);
    //mAAA->ModeSpecInit();
    LOGD("PICTURE SIZE: w=%d h=%d", picture_width, picture_height);


    if ( (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) || (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) ) {
        int jpeg_quality = p.getInt("jpeg-quality");
        new_value = p.get("jpeg-quality");
        set_value = mParameters.get("jpeg-quality");
        LOGD(" - jpeg-quality = new \"%s\" (%d) / current \"%s\"",new_value, jpeg_quality, set_value);
        if (strcmp(set_value, new_value) != 0) {
            p.set("jpeg-quality", new_value);
            LOGD("     ++ Changed jpeg-quality to %s",p.get("jpeg-quality"));
            //mCamera->setJPEGRatio(p.get("jpeg-quality"));
        }

        int effect = p.getInt("effect");
        new_value = p.get("effect");
        set_value = mParameters.get("effect");
        LOGD(" - effect = new \"%s\" (%d) / current \"%s\"",new_value, effect, set_value);
        if (strcmp(set_value, new_value) != 0) {
            p.set("effect", new_value);
            LOGD("     ++ Changed effect to %s",p.get("effect"));
            //mCamera->setColorEffect(p.get("effect"));
        }

        /* white balance */
        const char * pwb = CameraParameters::KEY_WHITE_BALANCE;
        int whitebalance = p.getInt(pwb);
        new_value = p.get(pwb);
        set_value = mParameters.get(pwb);
        LOGD(" - whitebalance = new \"%s\" (%d) / current \"%s\"", new_value, whitebalance, set_value);

        if (strcmp(set_value, new_value) != 0) {
            int wb_mode;
            p.set(pwb, new_value);

            if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_AUTO))
                wb_mode = CAM_AWB_MODE_AUTO;
            else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_INCANDESCENT))
                wb_mode = CAM_AWB_MODE_WARM_INCANDESCENT;
            else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_FLUORESCENT))
                wb_mode = CAM_AWB_MODE_FLUORESCENT;
            else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_WARM_FLUORESCENT))
                wb_mode = CAM_AWB_MODE_WARM_FLUORESCENT;
            else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_DAYLIGHT))
                wb_mode = CAM_AWB_MODE_DAYLIGHT;
            else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT))
                wb_mode = CAM_AWB_MODE_CLOUDY;
            else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_TWILIGHT))
                wb_mode = CAM_AWB_MODE_SUNSET;
            else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_SHADE))
                wb_mode = CAM_AWB_MODE_SHADOW;
            else
                wb_mode = CAM_AWB_MODE_AUTO;
            mAAA->AwbSetMode(wb_mode);

            LOGD("     ++ Changed whitebalance to %s, wb_mode:%d\n",p.get(pwb), wb_mode);
        }

        const char * pexp = CameraParameters::KEY_EXPOSURE_COMPENSATION;
        int exposure = p.getInt(pexp);
        new_value = p.get(pexp);
        set_value = mParameters.get(pexp);
        LOGD(" -  = new \"%s\" (%d) / current \"%s\"",new_value, effect, set_value);
        if (strcmp(set_value, new_value) != 0) {
            p.set(pexp, new_value);
            mAAA->AeSetEv(atoi(new_value));
            int ev = 0;
            mAAA->AeGetEv(&ev);
            LOGD("exposure, line:%d,  get ev value:%d\n", __LINE__, ev);
            LOGD("     ++ Changed exposure effect to %s",p.get(pexp));
        }

        int zoom = p.getInt("zoom");
        //process zoom
        mCamera->set_zoom_val(zoom);

        const char * pfocusmode = CameraParameters::KEY_FOCUS_MODE;
        int focus_mode = p.getInt(pfocusmode);
        new_value = p.get(pfocusmode);
        set_value = mParameters.get(pfocusmode);
        LOGD(" - focus-mode = new \"%s\" (%d) / current \"%s\"", new_value, focus_mode, set_value);
        if (strcmp(set_value, new_value) != 0) {
            int afmode;
            p.set(pfocusmode, new_value);

            if(!strcmp(new_value, CameraParameters::FOCUS_MODE_AUTO))
                afmode = CAM_FOCUS_MODE_AUTO;
            else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_INFINITY))
                afmode = CAM_FOCUS_MODE_NORM;
            else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_MACRO))
                afmode = CAM_FOCUS_MODE_MACRO;
            else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO))
                afmode = CAM_FOCUS_MODE_AUTO;
            else
                afmode = CAM_FOCUS_MODE_AUTO;
            mAAA->AfSetMode(afmode);

            LOGD("     ++ Changed focus-mode to %s, afmode:%d",p.get(pfocusmode), afmode);
        }

        const char * pantibanding = CameraParameters::KEY_ANTIBANDING;
        int antibanding = p.getInt(pantibanding);
        new_value = p.get(pantibanding);
        set_value = mParameters.get(pantibanding);
        LOGD(" - antibanding = new \"%s\" (%d) / current \"%s\"", new_value, antibanding, set_value);
        if (strcmp(set_value, new_value) != 0) {
            int bandingval;
            p.set(pantibanding, new_value);

            if(!strcmp(new_value, CameraParameters::ANTIBANDING_AUTO))
                bandingval = CAM_AEFLICKER_MODE_AUTO;
            else if(!strcmp(new_value, CameraParameters::ANTIBANDING_50HZ))
                bandingval = CAM_AEFLICKER_MODE_50HZ;
            else if(!strcmp(new_value, CameraParameters::ANTIBANDING_60HZ))
                bandingval = CAM_AEFLICKER_MODE_60HZ;
            else if(!strcmp(new_value, CameraParameters::ANTIBANDING_OFF))
                bandingval = CAM_AEFLICKER_MODE_OFF;
            else
                bandingval = CAM_AEFLICKER_MODE_AUTO;
            mAAA->AfSetMode(bandingval);

            LOGD("     ++ Changed focus-mode to %s, antibanding val:%d",p.get(pantibanding), bandingval);
        }

        int rotation = p.getInt("rotation");
        new_value = p.get("rotation");
        set_value = mParameters.get("rotation");
        LOGD(" - rotation = new \"%s\" (%d) / current \"%s\"", new_value, rotation, set_value);
        if (strcmp(set_value, new_value) != 0) {
            p.set("rotation", new_value);
            LOGD("     ++ Changed rotation to %s",p.get("rotation"));
        }

        int scene_mode = p.getInt("scene-mode");
        new_value = p.get("scene-mode");
        set_value = mParameters.get("scene-mode");
        LOGD(" - scene-mode = new \"%s\" (%d) / current \"%s\"", new_value, scene_mode, set_value);
        if (strcmp(set_value, new_value) != 0) {
            p.set("scene-mode", new_value);
            LOGD("     ++ Changed scene-mode to %s",p.get("scene-mode"));
            if (!strcmp (new_value, "auto")) {
                scene_mode = CAM_SCENE_MODE_AUTO;
            }
            else if (!strcmp (new_value, "portrait")) {
                scene_mode = CAM_SCENE_MODE_PORTRAIT;
            }
            else if (!strcmp (new_value, "sports")) {
                scene_mode = CAM_SCENE_MODE_SPORTS;
            }
            else if (!strcmp (new_value, "landscape")) {
                scene_mode = CAM_SCENE_MODE_LANDSCAPE;
            }
            else if (!strcmp (new_value, "night")) {
                scene_mode = CAM_SCENE_MODE_NIGHT;
            }
            else if (!strcmp (new_value, "fireworks")) {
                scene_mode = CAM_SCENE_MODE_FIREWORKS;
            }
            else {
                scene_mode = CAM_SCENE_MODE_AUTO;
                LOGD("     ++ Not supported scene-mode");
            }
            mAAA->AeSetSceneMode (scene_mode);
        }
        int flash_mode = p.getInt("flash-mode");
        new_value = p.get("flash-mode");
        set_value = mParameters.get("flash-mode");
        LOGD(" - flash-mode = new \"%s\" (%d) / current \"%s\"", new_value, flash_mode, set_value);
        if (strcmp(set_value, new_value) != 0) {
            p.set("flash-mode", new_value);
            LOGD("     ++ Changed flash-mode to %s",p.get("flash-mode"));

            if (!strcmp (new_value, "auto")) {
                flash_mode = CAM_FLASH_MODE_AUTO;
            }
            else if (!strcmp (new_value, "off")) {
                flash_mode = CAM_FLASH_MODE_OFF;
            }
            else if (!strcmp (new_value, "on")) {
                flash_mode = CAM_FLASH_MODE_ON;
            }
            else {
                flash_mode = CAM_FLASH_MODE_AUTO;
                LOGD("     ++ Not supported flash-mode");
            }
            mAAA->AeSetFlashMode (flash_mode);
        }

        mWinFocus.x_left = p.getInt("touchfocus-x-left");
        mWinFocus.x_right = p.getInt("touchfocus-x-right");
        mWinFocus.y_top = p.getInt("touchfocus-x-top");
        mWinFocus.y_bottom = p.getInt("touchfocus-x-bottom");
    }

    mParameters = p;

    //int preview_size = mCamera->getFrameSize(preview_width,preview_height);
    initHeapLocked(preview_size);

    return NO_ERROR;
}

CameraParameters CameraHardware::getParameters() const
{
    Mutex::Autolock lock(mLock);
    return mParameters;
}

status_t CameraHardware::sendCommand(int32_t command, int32_t arg1,
                                     int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHardware::release()
{
}

wp<CameraHardwareInterface> CameraHardware::singleton;

sp<CameraHardwareInterface> CameraHardware::createInstance(int cameraId)
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardware(cameraId));
    singleton = hardware;
    return hardware;
}

}; // namespace android
