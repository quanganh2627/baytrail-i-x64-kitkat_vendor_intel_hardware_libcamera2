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

namespace android {

CameraHardware::CameraHardware()
                  : mParameters(),
                    mHeap(0),
                    mCurrentRecordingFrame(0),
                    mRecordingHeap(0),
		    mRawHeap(0),
		    mCamera(0),
		    cur_snr(0),
		    mPreviewRunning(false),
		    mRecordRunning(false),
                    mPreviewFrameSize(0),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    mMsgEnabled(0),
                    mCurrentFrame(0),
                    mLastTS(0),
                    mLastFPS(0)
{
    mCamera = new IntelCamera();
    cur_snr = mCamera->getSensorInfos();
    mCamera->printSensorInfos();
    initDefaultParameters();
}

void CameraHardware::initHeapLocked(int size)
{
	int recorder_size;
    if (size != mPreviewFrameSize) {
        mHeap = new MemoryHeapBase(size);
        mBuffer = new MemoryBase(mHeap, 0, size);
        LOGD("%s Re Alloc Preview frame size=%d",__func__, size);
		
		const char *preview_fmt;
		preview_fmt = mParameters.getPreviewFormat();
		
		if (strcmp(preview_fmt, "yuv420sp") == 0) {
		  recorder_size = size;
		}  else if (strcmp(preview_fmt, "yuv422i-yuyv") == 0){
		  recorder_size = size;
		} else if (strcmp(preview_fmt, "rgb565") == 0){
		  recorder_size = (size * 3)/4;
		} else {
		  LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
		}	


        mRecordingHeap = new MemoryHeapBase(recorder_size * kRecordingBufferCount);
        for (int i = 0; i < kRecordingBufferCount; i++) {
            mRecordingBuffers[i] =
                new MemoryBase(mRecordingHeap, i * recorder_size, recorder_size);
            mRecordingBuffersState[i] = RecordingBuffersStateReleased;
        }
		
        LOGD("%s Re Alloc Recording frame size=%d",__func__, recorder_size);

        mPreviewFrameSize = size;
    }

}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

    p.setPreviewSize(640, 480);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("rgb565"); /* yuv420sp */
    p.setPictureSize(1600, 1200);
    p.setPictureFormat("jpeg");

    /*
      frameworks/base/core/java/android/hardware/Camera.java
    */

    p.set("jpeg-quality","100");
    p.set("whitebalance", "auto");
    p.set("effect", "none");
    p.set("rotation","90");
    p.set("flash-mode","off");
    p.set("jpeg-quality-values","1,20,30,40,50,60,70,80,90,99,100");
    p.set("effect-values","none,mono,negative,sepia,aqua,pastel,whiteboard");
    p.set("flash-mode-values","off,auto,on");
    p.set("rotation-values","0,90,180");
    p.set("focus-mode","auto");

    if (cur_snr != NULL) {
      if (cur_snr->type == SENSOR_TYPE_2M) {
	// 2M
	p.set("picture-size-values","320x240,640x480,800x600,1280x1024,1600x1200");
	p.set("whitebalance-values","auto");
      } else {
	// 5M
	p.set("focus-mode-values","auto,infinity,macro");
	p.set("picture-size-values","640x480,1280x720,1280x960,1920x1080,2592x1944");
	p.set("whitebalance-values","auto,cloudy-daylight,daylight,fluorescent,incandescent,shade,twilight,warm-fluorescent");
      }
    }

    mParameters = p;

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }
}

CameraHardware::~CameraHardware()
{
    delete mCamera;
    singleton.clear();
}

sp<IMemoryHeap> CameraHardware::getPreviewHeap() const
{
    return mHeap;
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
    // Notify the client of a new frame.
    if (mPreviewRunning) {
        if ( mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
	  if(mCamera->isImageProcessEnabled()) {
	      mCamera->imageProcessAF();
	      mCamera->imageProcessAE();
	      mCamera->imageProcessAWB();
	    }
	    mCamera->imageProcessBP();
	    mCamera->imageProcessBL();

	    mCamera->captureGrabFrame();
		const char *preview_fmt;
		preview_fmt = mParameters.getPreviewFormat();
		
		if (strcmp(preview_fmt, "yuv420sp") == 0) {
			mCurrentFrame = mCamera->captureGetFrame(mHeap->getBase(), 0);
		}  else if (strcmp(preview_fmt, "yuv422i-yuyv") == 0){
			mCurrentFrame = mCamera->captureGetFrame(mHeap->getBase(), 0);
		} else if (strcmp(preview_fmt, "rgb565") == 0){
			mCurrentFrame = mCamera->captureGetFrame(mHeap->getBase(), 1);
		} else {
		  LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
		  return -1;
		}		
	    //mCurrentFrame = mCamera->captureGetFrame(mHeap->getBase(), 0);

            if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
                if (mRecordingBuffersState[mCurrentRecordingFrame] ==
                    RecordingBuffersStateReleased) {
                    sp<MemoryHeapBase> heap = mRecordingHeap;
                    sp<MemoryBase> buffer =
                        mRecordingBuffers[mCurrentRecordingFrame];
                    void *base = heap->base();
                    ssize_t offset =
                        mCurrentRecordingFrame * mPreviewFrameSize;
                    uint8_t *recordingframe = ((uint8_t *)base) + offset;
                    nsecs_t current_ts = systemTime(SYSTEM_TIME_MONOTONIC);
					#if 0
                    memcpy(recordingframe, mHeap->getBase(),
                           mPreviewFrameSize);
                    #else
					mCamera->captureGetFrame(recordingframe, 0);
					#endif

                    mRecordingBuffersState[mCurrentRecordingFrame] =
                        RecordingBuffersStateLocked;
                    mCurrentRecordingFrame =
                        (mCurrentRecordingFrame + 1) % kRecordingBufferCount;

//#if !LOG_NDEBUG
                    nsecs_t interval_ts = current_ts - mLastTS;
                    float current_fps, average_fps;

                    mLastTS = current_ts;
                    current_fps = (float)1000000000 / (float)interval_ts;
                    average_fps = (current_fps + mLastFPS) / 2;
                    mLastFPS = current_fps;

                    LOGD("Recording FPS : %2.1f\n", average_fps);
//#endif
                    LOGV("give a recorded frame to client (index:%d/%d)\n"
                         mCurrentRecordingFrame, kRecordingBufferCount);

                    mDataCbTimestamp(current_ts, CAMERA_MSG_VIDEO_FRAME,
                                     buffer, mCallbackCookie);
                }
            }
	    LOGV("%s : mCurrentFrame = %u", __func__, mCurrentFrame);

	    mDataCb(CAMERA_MSG_PREVIEW_FRAME, mBuffer, mCallbackCookie);
	    mCamera->captureRecycleFrame();
#if 0
	    usleep(mDelay);
#endif
	}
    }
    return NO_ERROR;
}

status_t CameraHardware::startPreview()
{
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        return INVALID_OPERATION;
    }

    int w, h, preview_size;
    mParameters.getPreviewSize(&w, &h);
    //mCamera->capture_init(w, h, INTEL_PIX_FMT_YUYV, 3);
    mCamera->captureInit(w, h, INTEL_PIX_FMT_NV12, 3);
    mCamera->captureStart();
    //preview_size = 
    mCamera->captureMapFrame();
	
	const char *preview_fmt;
	preview_fmt = mParameters.getPreviewFormat();
	
    if (strcmp(preview_fmt, "yuv420sp") == 0) {
	  preview_size = (w * h * 3)/2;
    }  else if (strcmp(preview_fmt, "yuv422i-yuyv") == 0){
	  preview_size = w * h * 2;
    } else if (strcmp(preview_fmt, "rgb565") == 0){
	  preview_size = w * h * 2;
    } else {
      LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
      return -1;
    }	
    initHeapLocked(preview_size);

    mCamera->setAE("on");
    mCamera->setAWB(mParameters.get("whitebalance"));
    mCamera->setAF(mParameters.get("focus-mode"));
    mCamera->setColorEffect(mParameters.get("effect"));

    mPreviewThread = new PreviewThread(this);
    mPreviewRunning = true;

    return NO_ERROR;
}

void CameraHardware::stopPreview()
{
    sp<PreviewThread> previewThread;
    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
      previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();

    if (mPreviewRunning) {
      mCamera->captureUnmapFrame();
      mCamera->captureFinalize();
    }

    mPreviewRunning = false;
}

bool CameraHardware::previewEnabled() {
    return mPreviewRunning;
}

status_t CameraHardware::startRecording()
{
    mRecordRunning = true;
    return NO_ERROR;
}

void CameraHardware::stopRecording()
{
    mRecordRunning = false;
}

bool CameraHardware::recordingEnabled()
{
    return mRecordRunning;
}

void CameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
    ssize_t offset = mem->offset();
    size_t size = mem->size();
    int index = offset / size;

    mRecordingBuffersState[index] = RecordingBuffersStateReleased;

    LOGV("recording buffer [index:%d/%d] has been released",
         index, kRecordingBufferCount);
}

// ---------------------------------------------------------------------------

int CameraHardware::beginAutoFocusThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->autoFocusThread();
}

int CameraHardware::autoFocusThread()
{
    if (mMsgEnabled & CAMERA_MSG_FOCUS) {
	mCamera->setAF(mParameters.get("focus-mode"));
	mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
    }
    return NO_ERROR;
}

status_t CameraHardware::autoFocus()
{
    Mutex::Autolock lock(mLock);
    if (createThread(beginAutoFocusThread, this) == false) {
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t CameraHardware::cancelAutoFocus()
{
    return NO_ERROR;
}

/*static*/ int CameraHardware::beginPictureThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->pictureThread();
}

int CameraHardware::pictureThread()
{
    if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        //FIXME: use a canned YUV image!
        // In the meantime just make another fake camera picture.
      //          mDataCb(CAMERA_MSG_RAW_IMAGE, mBuffer, mCallbackCookie);
    }
    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
      sp<MemoryHeapBase> heap = new MemoryHeapBase(mJpegFrameSize);
      sp<MemoryBase> buffer = new MemoryBase(heap, 0, mJpegFrameSize);

      mCamera->imageProcessAE();
      mCamera->imageProcessAWB();
      mCamera->imageProcessBP();
      mCamera->imageProcessBL();

      mCamera->captureGrabFrame();

      mCurrentFrame = mCamera->captureGetFrame(heap->getBase(), 0);
      mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
      mCamera->captureUnmapFrame();
      //      mCamera->capture_recycle_frame();
      mCamera->captureFinalize();
    }
    return NO_ERROR;
}

status_t CameraHardware::takePicture()
{
    disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    stopPreview();

    int w, h;
    mParameters.getPictureSize(&w, &h);

    mCamera->captureInit(w, h, INTEL_PIX_FMT_JPEG, 1);
    mCamera->captureStart();
    mJpegFrameSize = mCamera->captureMapFrame();

    mCamera->setAE("on");
    mCamera->setAWB(mParameters.get("whitebalance"));
    mCamera->setColorEffect(mParameters.get("effect"));
    mCamera->setJPEGRatio(mParameters.get("jpeg-quality"));

    if (createThread(beginPictureThread, this) == false)
        return -1;

    return NO_ERROR;
}

status_t CameraHardware::cancelPicture()
{
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

    CameraParameters p = params;
#if 0
    if (strcmp(p.getPreviewFormat(), "yuv420sp") != 0) {
        LOGE("Only yuv420sp preview is supported");
        return -1;
    }
#endif

    if (strcmp(p.getPictureFormat(), "jpeg") != 0) {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    int preview_width,preview_height;
    p.getPreviewSize(&preview_width,&preview_height);
    if( (preview_width != 640) && (preview_height != 480) ) {
      preview_width = 640;
      preview_height = 480;
      LOGE("Only %dx%d for preview are supported",preview_width,preview_height);
    }
    p.setPreviewSize(preview_width,preview_height);

    const char *new_value, *set_value;
    new_value = p.getPreviewFormat();
    set_value = mParameters.getPreviewFormat();

	int preview_size;

    if (strcmp(new_value, "yuv420sp") == 0) {
	  preview_size = (preview_width * preview_height * 3)/2;
    }  else if (strcmp(new_value, "yuv422i-yuyv") == 0){
	  preview_size = preview_width * preview_height * 2;
    } else if (strcmp(new_value, "rgb565") == 0){
     /* use the NV12 for camera output */
	  preview_size = preview_width * preview_height * 2;
    } else {
      LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
      return -1;
    }

    int fps = p.getPreviewFrameRate();
    p.setPreviewFrameRate(fps);
    LOGD("PREVIEW SIZE: %dx%d, PICTURE FPS: %d", preview_width, preview_height, fps);

    int picture_width, picture_height;
    p.getPictureSize(&picture_width, &picture_height);

    if(mCamera != NULL) {
        LOGD("verify a jpeg picture size %dx%d",picture_width,picture_height);
	if ( !mCamera->isResolutionSupported(picture_width, picture_height) ) {
	  LOGE("this jpeg resolution w=%d * h=%d is not supported",picture_width,picture_height);
	  mCamera->getMaxResolution(&picture_width, &picture_height);
	  LOGD("set into max jpeg resolution w=%d * h=%d",picture_width,picture_height);
	}
    }

    p.setPictureSize(picture_width, picture_height);
    LOGD("PICTURE SIZE: w=%d h=%d", picture_width, picture_height);


    if ( (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) || (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) ) {
      const char *new_value, *set_value;

      int jpeg_quality = p.getInt("jpeg-quality");
      new_value = p.get("jpeg-quality");
      set_value = mParameters.get("jpeg-quality");
      LOGD(" - jpeg-quality = new \"%s\" (%d) / current \"%s\"",new_value, jpeg_quality, set_value);
      if (strcmp(set_value, new_value) != 0) {
        p.set("jpeg-quality", new_value);
	LOGD("     ++ Changed jpeg-quality to %s",p.get("jpeg-quality"));
	mCamera->setJPEGRatio(p.get("jpeg-quality"));
      }

      int effect = p.getInt("effect");
      new_value = p.get("effect");
      set_value = mParameters.get("effect");
      LOGD(" - effect = new \"%s\" (%d) / current \"%s\"",new_value, effect, set_value);
      if (strcmp(set_value, new_value) != 0) {
        p.set("effect", new_value);
	LOGD("     ++ Changed effect to %s",p.get("effect"));
	mCamera->setColorEffect(p.get("effect"));
      }

      int whitebalance = p.getInt("whitebalance");
      new_value = p.get("whitebalance");
      set_value = mParameters.get("whitebalance");
      LOGD(" - whitebalance = new \"%s\" (%d) / current \"%s\"", new_value, whitebalance, set_value);
      if (strcmp(set_value, new_value) != 0) {
        p.set("whitebalance", new_value);
	LOGD("     ++ Changed whitebalance to %s",p.get("whitebalance"));
	mCamera->setAWB(p.get("whitebalance"));
      }

      int focus_mode = p.getInt("focus-mode");
      new_value = p.get("focus-mode");
      set_value = mParameters.get("focus-mode");
      LOGD(" - focus-mode = new \"%s\" (%d) / current \"%s\"", new_value, focus_mode, set_value);
      if (strcmp(set_value, new_value) != 0) {
        p.set("focus-mode", new_value);
	LOGD("     ++ Changed focus-mode to %s",p.get("focus-mode"));
	mCamera->setAF(p.get("focus-mode"));
      }

      int rotation = p.getInt("rotation");
      new_value = p.get("rotation");
      set_value = mParameters.get("rotation");
      LOGD(" - rotation = new \"%s\" (%d) / current \"%s\"", new_value, rotation, set_value);
      if (strcmp(set_value, new_value) != 0) {
        p.set("rotation", new_value);
	LOGD("     ++ Changed rotation to %s",p.get("rotation"));
      }

      int flash_mode = p.getInt("flash-mode");
      new_value = p.get("flash-mode");
      set_value = mParameters.get("flash-mode");
      LOGD(" - flash-mode = new \"%s\" (%d) / current \"%s\"", new_value, flash_mode, set_value);
      if (strcmp(set_value, new_value) != 0) {
        p.set("flash-mode", new_value);
	LOGD("     ++ Changed flash-mode to %s",p.get("flash-mode"));
      }
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

sp<CameraHardwareInterface> CameraHardware::createInstance()
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardware());
    singleton = hardware;
    return hardware;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    return CameraHardware::createInstance();
}

}; // namespace android
