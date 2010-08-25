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

bool share_buffer_caps_set=false;

CameraHardware::CameraHardware()
                  : mParameters(),
		    mPreviewFrame(0),
		    mPostPreviewFrame(0),
		    mRecordingFrame(0),
		    mPostRecordingFrame(0),
		    mCamera(0),
		    mCurrentSensor(0),
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
    mCamera = new IntelCamera();
    mCurrentSensor = mCamera->getSensorInfos();
    mCamera->printSensorInfos();
    initDefaultParameters();
  LOGE("libcamera version: 2010-07-05 0.3.3");
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
		}  else if (strcmp(preview_fmt, "yuv422i-yuyv") == 0){
		  recordersize = size;
		} else if (strcmp(preview_fmt, "rgb565") == 0){
		  recordersize = (size * 3)/4;
		} else {
		  LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
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

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
    p.setPreviewSize(640, 480);
    p.setPreviewFrameRate(30);
    p.setPreviewFormat("yuv420sp");
#else
    p.setPreviewSize(320, 240);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("rgb565");
#endif
    p.setPictureSize(1600, 1200);
    p.setPictureFormat("jpeg");

    /*
      frameworks/base/core/java/android/hardware/Camera.java
    */

    p.set("preview-format-values", "yuv420sp,rgb565");
    p.set("preview-size-values", "640x480");
//    p.set("preview-size-values", "320x240");
    p.set("picture-format-values", "jpeg");
    p.set("focus-mode-values", "fixed");

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

    if (mCurrentSensor != NULL) {
      if (mCurrentSensor->type == SENSOR_TYPE_2M) {
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
    if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
        // Get a preview frame
        int previewFrame = mPreviewFrame;
	if( !isBFSet(mPreviewBuffer.flags[previewFrame], BF_ENABLED)
	    && !isBFSet(mPreviewBuffer.flags[previewFrame], BF_LOCKED)) {

	    setBF(&mPreviewBuffer.flags[previewFrame],BF_LOCKED);
#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
	    unsigned int ret_frame_size = mCamera->captureGrabFrame(); 
            if(ret_frame_size == (unsigned int)-1) {
                 clrBF(&mPreviewBuffer.flags[previewFrame],BF_LOCKED);
                 usleep(10000);     
                 return NO_ERROR;
            }
#else
	    mCamera->captureGrabFrame(); 
#endif
	    if(mCamera->isImageProcessEnabled()) {
	        mCamera->imageProcessAF();
		mCamera->imageProcessAE();
		mCamera->imageProcessAWB();
	    }
	    mCamera->imageProcessBP();
	    mCamera->imageProcessBL();
		
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

	    mDataCb(CAMERA_MSG_PREVIEW_FRAME,
		    mPreviewBuffer.base[postPreviewFrame], mCallbackCookie);
	    clrBF(&mPreviewBuffer.flags[postPreviewFrame],BF_LOCKED|BF_ENABLED);
	    mPostPreviewFrame = (postPreviewFrame + 1) % kBufferCount;
	}
    }

    // TODO: Have to change the recordingThread() function to others thread ways
    recordingThread();

#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
    if(!mRecordingRunning) {
#endif
         mCamera->captureRecycleFrame();
#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
    }
#endif


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
    mCamera->captureInit(w, h, mPreviewPixelFormat, 3);
    mCamera->captureStart();

    mCamera->setAE("on");
    mCamera->setAWB(mParameters.get("whitebalance"));
    mCamera->setAF(mParameters.get("focus-mode"));
    mCamera->setColorEffect(mParameters.get("effect"));
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

    mPreviewThread = new PreviewThread(this);

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

    if (mPreviewThread != 0) {
      mPreviewThread.clear();
      mCamera->captureUnmapFrame();
      mCamera->captureFinalize();
    }
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

    return NO_ERROR;
}

void CameraHardware::stopRecording()
{
    mRecordingRunning = false;
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

#define MAX_FRAME_WAIT 20
int CameraHardware::pictureThread()
{
    int frame_cnt = 0;

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

	mCamera->captureInit(w, h, mPicturePixelFormat, 1);
	mCamera->captureStart();

	mCamera->setAE("on");
	mCamera->setAWB(mParameters.get("whitebalance"));
	mCamera->setColorEffect(mParameters.get("effect"));
	mCamera->setJPEGRatio(mParameters.get("jpeg-quality"));

	int jpegSize;
	while (1) {
		jpegSize = mCamera->captureGrabFrame();
		if (mCamera->getSensorInfos()->type == SENSOR_TYPE_2M)
			break;
		mCamera->imageProcessAE();
		mCamera->imageProcessAWB();
		frame_cnt++;
		if ((mCamera->isImageProcessFinishedAE() && mCamera->isImageProcessFinishedAWB()) ||
		    frame_cnt >= MAX_FRAME_WAIT)
			break;
		mCamera->captureRecycleFrame();
	}
	LOGD(" - JPEG size saved = %dB, %dK",jpegSize, jpegSize/1000);

	mCamera->imageProcessBP();
	mCamera->imageProcessBL();

	mCamera->captureMapFrame();
	sp<MemoryHeapBase> heap = new MemoryHeapBase(jpegSize);
	sp<MemoryBase> buffer = new MemoryBase(heap, 0, jpegSize);
	mCamera->captureGetFrame(heap->getBase());
	mCamera->captureUnmapFrame();

	mCamera->captureRecycleFrame();
	mCamera->captureFinalize();

	mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
    }
    return NO_ERROR;
}

status_t CameraHardware::takePicture()
{
    disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    stopPreview();

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
      mPreviewPixelFormat = INTEL_PIX_FMT_NV12;
	  preview_size = (preview_width * preview_height * 3)/2;
    }  else if (strcmp(new_value, "yuv422i-yuyv") == 0){
      mPreviewPixelFormat = INTEL_PIX_FMT_YUYV;
	  preview_size = preview_width * preview_height * 2;
    } else if (strcmp(new_value, "rgb565") == 0){
      mPreviewPixelFormat = INTEL_PIX_FMT_RGB565;
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
    if (strcmp(new_value, "jpeg") == 0) {
        mPicturePixelFormat = INTEL_PIX_FMT_JPEG;
    } else {
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
