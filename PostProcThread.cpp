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
#define LOG_TAG "Camera_PostProcThread"
//#define LOG_NDEBUG 0

#include <time.h>
#include "LogHelper.h"
#include "Callbacks.h"
#include "CallbacksThread.h"
#include "PostProcThread.h"
#include "IFaceDetectionListener.h"
#include "PlatformData.h"
#include <system/camera.h>
#include "AtomCP.h"
#include "JpegCapture.h"

namespace android {

PostProcThread::PostProcThread(ICallbackPostProc *postProcDone, PanoramaThread *panoramaThread, I3AControls *aaaControls,
                               sp<CallbacksThread> callbacksThread, Callbacks *callbacks, int cameraId) :
    IFaceDetector(callbacksThread.get())
    ,Thread(true) // callbacks may call into java
    ,mFaceDetector(NULL)
    ,mPanoramaThread(panoramaThread)
    ,mMessageQueue("PostProcThread", (int) MESSAGE_ID_MAX)
    ,mLastReportedNumberOfFaces(0)
    ,mCallbacks(callbacks)
    ,mPostProcDoneCallback(postProcDone)
    ,m3AControls(aaaControls)
    ,mThreadRunning(false)
    ,mFaceDetectionRunning(false)
    ,mFaceRecognitionRunning(false)
    ,mZoomRatio(0)
    ,mRotation(0)
    ,mCameraOrientation(0)
    ,mIsBackCamera(false)
    ,mCameraId(cameraId)
    ,mAutoLowLightReporting(false)
    ,mLastLowLightValue(false)
{
    LOG1("@%s", __FUNCTION__);

    //init SmartShutter, must match defaultParams
    mSmartShutter.smartRunning = false;
    mSmartShutter.smileRunning = false;
    mSmartShutter.blinkRunning = false;
    mSmartShutter.captureOnTrigger = false;
    mSmartShutter.captureTriggered = false;
    mSmartShutter.captureForced = false;
    mSmartShutter.smileThreshold = SMILE_THRESHOLD;
    mSmartShutter.blinkThreshold = BLINK_THRESHOLD;
}

PostProcThread::~PostProcThread()
{
    LOG1("@%s", __FUNCTION__);
    if (mFaceDetector != NULL) {
        delete mFaceDetector;
        mFaceDetector = NULL;
    }
}

int PostProcThread::getCameraID()
{
    return mCameraId;
}

/**
 * Calling this is mandatory in order to use face engine functionalities.
 * if *isp is null, face engine will run without acceleration.
 */
status_t PostProcThread::init(void* isp)
{
    mFaceDetector = new FaceDetector();
    if (mFaceDetector == NULL) {
        LOGE("Error creating FaceDetector");
        return UNKNOWN_ERROR;
    }

    mIspHandle = isp;

    return NO_ERROR;
}

void PostProcThread::getDefaultParameters(CameraParameters *params, CameraParameters *intel_params, int cameraId)
{
    LOG1("@%s", __FUNCTION__);
    if (!params) {
        LOGE("params is null!");
        return;
    }
    // Set maximum number of detectable faces
    if (PlatformData::supportsContinuousJpegCapture(cameraId))
        params->set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW, NV12_META_MAX_FACE_COUNT);
    else
        params->set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW, MAX_FACES_DETECTABLE);
    intel_params->set(IntelCameraParameters::KEY_SMILE_SHUTTER_THRESHOLD, STRINGIFY(SMILE_THRESHOLD));
    intel_params->set(IntelCameraParameters::KEY_BLINK_SHUTTER_THRESHOLD, STRINGIFY(BLINK_THRESHOLD));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_SMILE_SHUTTER, PlatformData::supportedSmileShutter(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_BLINK_SHUTTER, PlatformData::supportedBlinkShutter(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_FACE_RECOGNITION, PlatformData::supportedFaceRecognition(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_SCENE_DETECTION, PlatformData::supportedSceneDetection(cameraId));

    mCameraOrientation = PlatformData::cameraOrientation(cameraId);
    // TODO: make sure that CameraId = 0 is main Camera always
    if (cameraId == 0)
        mIsBackCamera = true;
}

void PostProcThread::startFaceDetection()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_START_FACE_DETECTION;
    mMessageQueue.send(&msg);
}

/**
 * override for ICallbackPreview::previewBufferCallback()
 *
 * ControlThread assigns PostProcThread generally to PreviewThreads
 * output data callback.
 *
 * We decide wether to pass buffers to post processing or not.
 */
void PostProcThread::previewBufferCallback(AtomBuffer *buff, ICallbackPreview::CallbackType t)
{
    LOG2("@%s", __FUNCTION__);
    if (t != ICallbackPreview::OUTPUT_WITH_DATA) {
        LOGE("Unexpected preview buffer callback type!");
        return;
    }

    if (mAutoLowLightReporting || mFaceDetectionRunning ||
        mPanoramaThread->getState() == PANORAMA_DETECTING_OVERLAP) {
        if (sendFrame(buff) < 0) {
           buff->owner->returnBuffer(buff);
        }
    } else {
        buff->owner->returnBuffer(buff);
    }
}

status_t PostProcThread::handleMessageStartFaceDetection()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mSmartShutter.smartRunning && mSmartShutter.smileRunning)
        mFaceDetector->setSmileThreshold(mSmartShutter.smileThreshold);
    if (mSmartShutter.smartRunning && mSmartShutter.blinkRunning)
        mFaceDetector->setBlinkThreshold(mSmartShutter.blinkThreshold);

    mRotation = SensorThread::getInstance(this->getCameraID())->registerOrientationListener(this);

    // Reset the face detection state:
    mLastReportedNumberOfFaces = 0;
    // .. also keep the CallbacksThread in sync with the face status:
    mpListener->facesDetected(NULL);

    mFaceDetectionRunning = true;
    return status;
}

void PostProcThread::stopFaceDetection(bool wait)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STOP_FACE_DETECTION;

    if (wait) {
        mMessageQueue.send(&msg, MESSAGE_ID_STOP_FACE_DETECTION); // wait for reply
    } else {
        mMessageQueue.send(&msg);
    }
}

status_t PostProcThread::handleMessageStopFaceDetection()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mFaceDetectionRunning) {
        mFaceDetectionRunning = false;
        status = mFaceDetector->clearFacesDetected();

        SensorThread::getInstance(this->getCameraID())->unRegisterOrientationListener(this);
    }

    mMessageQueue.reply(MESSAGE_ID_STOP_FACE_DETECTION, status);

    return status;
}

/**
 * Flushes the message Q from messages containing new frames
 *
 */
void PostProcThread::flushFrames()
{
    LOG1("@%s", __FUNCTION__);
    mMessageQueue.remove(MESSAGE_ID_FRAME); // flush all buffers

}
// SMART SHUTTER

void PostProcThread::captureOnTrigger()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_CAPTURE_ON_TRIGGER;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageCaptureOnTrigger()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mSmartShutter.captureOnTrigger = true;
    return status;
}

void PostProcThread::stopCaptureOnTrigger()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STOP_CAPTURE_ON_TRIGGER;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageStopCaptureOnTrigger()
{
    status_t status = NO_ERROR;
    LOG1("@%s", __FUNCTION__);
    mSmartShutter.captureOnTrigger = false;
    return status;
}

void PostProcThread::startSmartShutter(SmartShutterMode mode, int level)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_START_SMART_SHUTTER;
    msg.data.smartShutterParam.mode = mode;
    msg.data.smartShutterParam.level = level;
    mMessageQueue.send(&msg, MESSAGE_ID_START_SMART_SHUTTER);
}

status_t PostProcThread::handleMessageStartSmartShutter(MessageSmartShutter params)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (!mFaceDetectionRunning) {
        LOGE("%s: Face Detection must be running", __FUNCTION__);
        mMessageQueue.reply(MESSAGE_ID_START_SMART_SHUTTER, INVALID_OPERATION);
    }
    if (params.mode == SMILE_MODE) {
        mFaceDetector->setSmileThreshold(params.level);
        mSmartShutter.smileRunning = true;
        mSmartShutter.smileThreshold = params.level;
    } else if (params.mode == BLINK_MODE) {
        mFaceDetector->setBlinkThreshold(params.level);
        mSmartShutter.blinkRunning = true;
        mSmartShutter.blinkThreshold = params.level;
    } else {
        mMessageQueue.reply(MESSAGE_ID_START_SMART_SHUTTER, INVALID_OPERATION);
    }
    if (mSmartShutter.smileRunning || mSmartShutter.blinkRunning)
        mSmartShutter.smartRunning = true;

    LOG1("%s: mode: %d Active Mode: (smile %d (%d) , blink %d (%d), smart %d)", __FUNCTION__, params.mode, mSmartShutter.smileRunning, mSmartShutter.smileThreshold, mSmartShutter.blinkRunning, mSmartShutter.blinkThreshold, mSmartShutter.smartRunning);

    mMessageQueue.reply(MESSAGE_ID_START_SMART_SHUTTER, OK);
    return status;
}

void PostProcThread::stopSmartShutter(SmartShutterMode mode)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STOP_SMART_SHUTTER;
    msg.data.smartShutterParam.mode = mode;
    msg.data.smartShutterParam.level = 0;
    mMessageQueue.send(&msg, MESSAGE_ID_STOP_SMART_SHUTTER);
}

status_t PostProcThread::handleMessageStopSmartShutter(MessageSmartShutter params)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (params.mode == SMILE_MODE)
        mSmartShutter.smileRunning = false;
    else if (params.mode == BLINK_MODE)
        mSmartShutter.blinkRunning = false;
    else
        return INVALID_OPERATION;
    if (!mSmartShutter.smileRunning && !mSmartShutter.blinkRunning)
        mSmartShutter.smartRunning = false;

    mMessageQueue.reply(MESSAGE_ID_STOP_SMART_SHUTTER, OK);
    return status;
}

bool PostProcThread::isSmartCaptureTriggered()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_IS_SMART_CAPTURE_TRIGGERED;
    mMessageQueue.send(&msg, MESSAGE_ID_IS_SMART_CAPTURE_TRIGGERED);
    return mSmartShutter.captureTriggered;
}

status_t PostProcThread::handleMessageIsSmartCaptureTriggered()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_IS_SMART_CAPTURE_TRIGGERED, status);
    return status;
}

void PostProcThread::resetSmartCaptureTrigger()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_RESET_SMART_CAPTURE_TRIGGER;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageResetSmartCaptureTrigger()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mSmartShutter.captureTriggered = false;
    return status;
}

void PostProcThread::forceSmartCaptureTrigger()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FORCE_SMART_CAPTURE_TRIGGER;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageForceSmartCaptureTrigger()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mSmartShutter.captureForced = true;
    return status;
}
bool PostProcThread::isSmartRunning()
{
    LOG1("@%s", __FUNCTION__);
    // since start and stop for smartShutter are synchronous and only accessed
    // from ControlThread, we can do a quick path to return the variable in
    // caller context - only safe for ControlThread!
    return mSmartShutter.smartRunning;
}

bool PostProcThread::isSmileRunning()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_IS_SMILE_RUNNING;
    mMessageQueue.send(&msg, MESSAGE_ID_IS_SMILE_RUNNING);
    return mSmartShutter.smileRunning;
}

status_t PostProcThread::handleMessageIsSmileRunning()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_IS_SMILE_RUNNING, status);
    return status;
}

int PostProcThread::getSmileThreshold()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_GET_SMILE_THRESHOLD;
    mMessageQueue.send(&msg, MESSAGE_ID_GET_SMILE_THRESHOLD);
    return mSmartShutter.smileThreshold;
}

status_t PostProcThread::handleMessageGetSmileThreshold()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_GET_SMILE_THRESHOLD, status);
    return status;
}

bool PostProcThread::isBlinkRunning()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_IS_BLINK_RUNNING;
    mMessageQueue.send(&msg, MESSAGE_ID_IS_BLINK_RUNNING);
    return mSmartShutter.blinkRunning;
}

status_t PostProcThread::handleMessageIsBlinkRunning()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_IS_BLINK_RUNNING, status);
    return status;
}

int PostProcThread::getBlinkThreshold()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_GET_BLINK_THRESHOLD;
    mMessageQueue.send(&msg, MESSAGE_ID_GET_BLINK_THRESHOLD);
    return mSmartShutter.blinkThreshold;
}

status_t PostProcThread::handleMessageGetBlinkThreshold()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_GET_BLINK_THRESHOLD, status);
    return status;
}

void PostProcThread::startFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_START_FACE_RECOGNITION;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageStartFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    status = mFaceDetector->startFaceRecognition();
    mFaceRecognitionRunning = true;
    return status;
}

void PostProcThread::stopFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STOP_FACE_RECOGNITION;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageStopFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    status = mFaceDetector->stopFaceRecognition();
    mFaceRecognitionRunning = false;
    return status;
}

bool PostProcThread::isFaceRecognitionRunning()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_IS_FACE_RECOGNITION_RUNNING;
    mMessageQueue.send(&msg, MESSAGE_ID_IS_FACE_RECOGNITION_RUNNING);
    return mFaceRecognitionRunning;
}

status_t PostProcThread::handleMessageIsFaceRecognitionRunning()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_IS_FACE_RECOGNITION_RUNNING, status);
    return status;
}

void PostProcThread::loadIspExtensions(bool videoMode)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_LOAD_ISP_EXTENSIONS;
    msg.data.loadIspExtensions.videoMode = videoMode;
    mMessageQueue.send(&msg, MESSAGE_ID_LOAD_ISP_EXTENSIONS);
}

status_t PostProcThread::handleMessageLoadIspExtensions(const MessageLoadIspExtensions& params)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mIspHandle != NULL &&
            params.videoMode == false)
        mFaceDetector->setAcc(mIspHandle);

    mMessageQueue.reply(MESSAGE_ID_LOAD_ISP_EXTENSIONS, status);
    return status;
}

void PostProcThread::unloadIspExtensions()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_UNLOAD_ISP_EXTENSIONS;
    mMessageQueue.send(&msg, MESSAGE_ID_UNLOAD_ISP_EXTENSIONS);
}

status_t PostProcThread::handleMessageUnloadIspExtensions()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mFaceDetector->setAcc(NULL);
    mMessageQueue.reply(MESSAGE_ID_UNLOAD_ISP_EXTENSIONS, status);
    return status;
}

status_t PostProcThread::handleExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mFaceDetectionRunning) {
        SensorThread::getInstance(this->getCameraID())->unRegisterOrientationListener(this);
    }

    mThreadRunning = false;
    mFaceDetectionRunning = false;
    return status;
}

status_t PostProcThread::setZoom(int zoomRatio)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_ZOOM;
    msg.data.config.value = zoomRatio;
    return mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageSetZoom(MessageConfig &msg)
{
    LOG1("@%s", __FUNCTION__);
    mZoomRatio = msg.value;
    return NO_ERROR;
}

status_t PostProcThread::handleMessageSetRotation(MessageConfig &msg)
{
    LOG1("@%s", __FUNCTION__);
    mRotation = msg.value;
    return NO_ERROR;
}

int PostProcThread::sendFrame(AtomBuffer *img)
{
    if (img != NULL) {
        LOG2("@%s: buf=%p, width=%d height=%d", __FUNCTION__, img, img->width , img->height);
    } else {
        LOG2("@%s: buf=NULL", __FUNCTION__);
    }
    Message msg;
    msg.id = MESSAGE_ID_FRAME;

    // Face detection/recognition and panorama overlap detection may take long time, which
    // slows down the preview because the buffers are not returned until they are processed.
    // Allow post-processing only when the queue is empty. Otherwise the frame will be skipped,
    // and ControlThread returns the buffer back to ISP.
    if (!mMessageQueue.isEmpty()) {
        LOG1("@%s: skipping frame", __FUNCTION__);
        return -1;
    }

    if (img != NULL) {
        msg.data.frame.img = *img;
    } else {
        LOGW("@%s: NULL AtomBuffer frame", __FUNCTION__);
    }

    if (mMessageQueue.send(&msg) == NO_ERROR)
        return 0;
    else
        return -1;
}

void PostProcThread::setAutoLowLightReporting(bool value)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_AUTO_LOW_LIGHT;
    msg.data.config.value = value ? 1 : 0;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageSetAutoLowLight(MessageConfig &msg)
{
    LOG1("@%s", __FUNCTION__);
    mAutoLowLightReporting = (msg.value == 1);
    return OK;
}

bool PostProcThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    mThreadRunning = true;
    while(mThreadRunning)
        waitForAndExecuteMessage();

    return false;
}

status_t PostProcThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id)
    {
        case MESSAGE_ID_FRAME:
            status = handleFrame(msg.data.frame);
            break;
        case MESSAGE_ID_EXIT:
            status = handleExit();
            break;
        case MESSAGE_ID_START_FACE_DETECTION:
            status = handleMessageStartFaceDetection();
            break;
        case MESSAGE_ID_STOP_FACE_DETECTION:
            status = handleMessageStopFaceDetection();
            break;
        case MESSAGE_ID_CAPTURE_ON_TRIGGER:
            status = handleMessageCaptureOnTrigger();
            break;
        case MESSAGE_ID_STOP_CAPTURE_ON_TRIGGER:
            status = handleMessageStopCaptureOnTrigger();
            break;
        case MESSAGE_ID_START_SMART_SHUTTER:
            status = handleMessageStartSmartShutter(msg.data.smartShutterParam);
            break;
        case MESSAGE_ID_STOP_SMART_SHUTTER:
            status = handleMessageStopSmartShutter(msg.data.smartShutterParam);
            break;
        case MESSAGE_ID_IS_SMILE_RUNNING:
            status = handleMessageIsSmileRunning();
            break;
        case MESSAGE_ID_GET_SMILE_THRESHOLD:
            status = handleMessageGetSmileThreshold();
            break;
        case MESSAGE_ID_IS_BLINK_RUNNING:
            status = handleMessageIsBlinkRunning();
            break;
        case MESSAGE_ID_GET_BLINK_THRESHOLD:
            status = handleMessageGetBlinkThreshold();
            break;
        case MESSAGE_ID_IS_SMART_CAPTURE_TRIGGERED:
            status = handleMessageIsSmartCaptureTriggered();
            break;
        case MESSAGE_ID_RESET_SMART_CAPTURE_TRIGGER:
            status = handleMessageResetSmartCaptureTrigger();
            break;
        case MESSAGE_ID_FORCE_SMART_CAPTURE_TRIGGER:
            status = handleMessageForceSmartCaptureTrigger();
            break;
        case MESSAGE_ID_START_FACE_RECOGNITION:
            status = handleMessageStartFaceRecognition();
            break;
        case MESSAGE_ID_STOP_FACE_RECOGNITION:
            status = handleMessageStopFaceRecognition();
            break;
        case MESSAGE_ID_IS_FACE_RECOGNITION_RUNNING:
            status = handleMessageIsFaceRecognitionRunning();
            break;
        case MESSAGE_ID_LOAD_ISP_EXTENSIONS:
            status = handleMessageLoadIspExtensions(msg.data.loadIspExtensions);
            break;
        case MESSAGE_ID_UNLOAD_ISP_EXTENSIONS:
            status = handleMessageUnloadIspExtensions();
            break;
        case MESSAGE_ID_SET_ZOOM:
            status = handleMessageSetZoom(msg.data.config);
            break;
        case MESSAGE_ID_SET_ROTATION:
            status = handleMessageSetRotation(msg.data.config);
            break;
        case MESSAGE_ID_SET_AUTO_LOW_LIGHT:
            status = handleMessageSetAutoLowLight(msg.data.config);
            break;
        default:
            status = INVALID_OPERATION;
            break;
    }
    if (status != NO_ERROR) {
        LOGE("operation failed, ID = %d, status = %d", msg.id, status);
    }
    return status;
}

status_t PostProcThread::requestExitAndWait()
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_EXIT;
    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

status_t PostProcThread::handleFrame(MessageFrame frame)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mFaceDetectionRunning && !PlatformData::supportsContinuousJpegCapture(mCameraId)) {
        LOG2("%s: Face detection executing", __FUNCTION__);
        int num_faces;
        bool smile = false;
        bool blink = true;
        unsigned char *src;
        int rotation;
        src = (unsigned char*) frame.img.dataPtr;
        ia_frame frameData;
        frameData.format = ia_frame_format_nv12;
        frameData.data = src;
        frameData.size = frame.img.size;
        frameData.width = frame.img.width;
        frameData.height = frame.img.height;
        frameData.stride = frame.img.bpl;
        if (AtomCP::setIaFrameFormat(&frameData, frame.img.fourcc) != NO_ERROR) {
            LOGE("@%s: setting ia_frame format failed", __FUNCTION__);
        }

        // correcting acceleration sensor orientation result
        // with camera sensor orientation
        if (mIsBackCamera)
            rotation = (mCameraOrientation + mRotation) % 360;
        else
            rotation = (mCameraOrientation - mRotation + 360) % 360;


        // frame rotation is counter clock wise in libia_face,
        // while it is clock wise for android (valid for CTP)
        if (rotation == 90)
            frameData.rotation = 270;
        else if (rotation == 270)
            frameData.rotation = 90;
        else
            frameData.rotation = rotation;

        num_faces = mFaceDetector->faceDetect(&frameData);

        if (mSmartShutter.smartRunning) {
            if (mSmartShutter.smileRunning)
                smile = mFaceDetector->smileDetect(&frameData);
            if (mSmartShutter.blinkRunning)
                blink = mFaceDetector->blinkDetect(&frameData);
        }

        if (mFaceRecognitionRunning) {
            mFaceDetector->faceRecognize(&frameData);
        }

        camera_face_t faces[num_faces];
        extended_frame_metadata_t extended_face_metadata;

        ia_face_state faceState;
        faceState.faces = new ia_face[num_faces];
        if (faceState.faces == NULL) {
            LOGE("Error allocation memory");
            return NO_MEMORY;
        }

        extended_face_metadata.number_of_faces = mFaceDetector->getFaces(faces, frameData.width, frameData.height);
        extended_face_metadata.faces = faces;
        mFaceDetector->getFaceState(&faceState, frameData.width, frameData.height, mZoomRatio);

        // Find recognized faces from the data (ID is positive), and pick the first one:
        int faceForFocusInd = 0;
        for (int i = 0; i < num_faces; ++i) {
            if (faceState.faces[i].person_id > 0) {
                LOG2("Face index: %d, ID: %d", i, faceState.faces[i].person_id);
                faceForFocusInd = i;
                break;
            }
        }

        //... and put the recognized face as first entry in the array for AF to use
        // No need to swap if face is already in the first pos.
        if (faceForFocusInd > 0) {
            ia_face faceSwapTmp = faceState.faces[0];
            faceState.faces[0] = faceState.faces[faceForFocusInd];
            faceState.faces[faceForFocusInd] = faceSwapTmp;
        }

        // Swap also the face in face metadata going to the application to match the swapped faceState info
        if (extended_face_metadata.number_of_faces > 0 && faceForFocusInd > 0) {
            camera_face_t faceMetaTmp = extended_face_metadata.faces[0];
            extended_face_metadata.faces[0] = extended_face_metadata.faces[faceForFocusInd];
            extended_face_metadata.faces[faceForFocusInd] = faceMetaTmp;
        }

        // pass face info to the callback listener (to be used for 3A)
        if (extended_face_metadata.number_of_faces > 0 || mLastReportedNumberOfFaces != 0) {
            mLastReportedNumberOfFaces = extended_face_metadata.number_of_faces;
            mPostProcDoneCallback->facesDetected(&faceState);
        }

        // TODO passing real auto LLS information from 3A results
        extended_face_metadata.needLLS = false;

        // .. and towards the application
        mpListener->facesDetected(&extended_face_metadata);

        delete[] faceState.faces;
        faceState.faces = NULL;

        // trigger for smart shutter
        if (mSmartShutter.captureOnTrigger) {
            // if
            // smile and blink detection runnning and both detected
            // or
            // only smile detection running and detected
            // or
            // only blink detection running and detected
            if (((smile && mSmartShutter.smileRunning) && (!blink && mSmartShutter.blinkRunning))
                || ((smile && mSmartShutter.smileRunning) && !mSmartShutter.blinkRunning)
                || ((!blink && mSmartShutter.blinkRunning) && !mSmartShutter.smileRunning)
                || (mSmartShutter.captureForced)) {
                mSmartShutter.captureOnTrigger = false;
                mPostProcDoneCallback->postProcCaptureTrigger();
                mSmartShutter.captureTriggered = true;
                mSmartShutter.captureForced = false;
            }
        }
    } else if ((mFaceDetectionRunning || mAutoLowLightReporting) &&
               PlatformData::supportsContinuousJpegCapture(mCameraId)) {
        status = handleExtIspFaceDetection(frame.img.auxBuf);
    }

    // panorama detection, running synchronously
    if (mPanoramaThread->getState() == PANORAMA_DETECTING_OVERLAP) {
        mPanoramaThread->sendFrame(frame.img);
    }

    // return buffer
    if (frame.img.owner != 0) {
        frame.img.owner->returnBuffer(&frame.img);
    }

    return status;
}

status_t PostProcThread::handleExtIspFaceDetection(AtomBuffer *auxBuf)
{
    if (auxBuf == NULL) {
        LOGE("No metadata buffer. FD bailing out.");
        return UNKNOWN_ERROR;
    }

    unsigned char *nv12meta = ((unsigned char*)auxBuf->dataPtr) + NV12_META_START;
    uint16_t numFaces = 0;
    if (mFaceDetectionRunning) {
        uint16_t fdState = getU16fromFrame(nv12meta, NV12_META_FD_ONOFF_ADDR);
        if (fdState != 1) {
            LOGW("Face detection is OFF in metadata, despite mFaceDetectionRunning being true. FD bailing out.");
            return UNKNOWN_ERROR;
        }

        numFaces = getU16fromFrame(nv12meta, NV12_META_FD_COUNT_ADDR);
        if (numFaces > NV12_META_MAX_FACE_COUNT) {
            LOGE("Face count bigger than 16 in metadata, considering metadata corrupted. FD bailing out.");
            return UNKNOWN_ERROR;
        }
    }

    camera_face_t faces[numFaces];
    extended_frame_metadata_t extended_face_metadata;
    extended_face_metadata.faces = faces;
    extended_face_metadata.number_of_faces = numFaces;

    for (int i = 0, addr = 0; i < numFaces; i++) {
        // unsupported fields
        faces[i].id = 0;
        faces[i].left_eye[0]  = faces[i].left_eye[1]  = -2000;
        faces[i].right_eye[0] = faces[i].right_eye[1] = -2000;
        faces[i].mouth[0]     = faces[i].mouth[1]     = -2000;

        // supported fields
        faces[i].rect[0] = (int16_t) getU16fromFrame(nv12meta, NV12_META_FIRST_FACE_ADDR + addr);
        addr += 2;
        faces[i].rect[1] = (int16_t) getU16fromFrame(nv12meta, NV12_META_FIRST_FACE_ADDR + addr);
        addr += 2;
        faces[i].rect[2] = (int16_t) getU16fromFrame(nv12meta, NV12_META_FIRST_FACE_ADDR + addr);
        addr += 2;
        faces[i].rect[3] = (int16_t) getU16fromFrame(nv12meta, NV12_META_FIRST_FACE_ADDR + addr);
        addr += 2;
        faces[i].score   = (int16_t) getU16fromFrame(nv12meta, NV12_META_FIRST_FACE_ADDR + addr) + 1; /* android valid range is 1 to 100. ext isp provides 0 to 99 */
        addr += 2;
        // skip the angle
        addr += 2;
    }

    // get the needLLS
    extended_face_metadata.needLLS = getU16fromFrame(nv12meta, NV12_META_NEED_LLS_ADDR);

    // handle low light status internally ...
    if (mAutoLowLightReporting &&
        mLastLowLightValue != extended_face_metadata.needLLS) {
        mPostProcDoneCallback->lowLightDetected(extended_face_metadata.needLLS);
        mLastLowLightValue = extended_face_metadata.needLLS;
    }

    // ...and send face info towards the application
    mpListener->facesDetected(&extended_face_metadata);

    return OK;
}

void PostProcThread::orientationChanged(int orientation)
{
    LOG2("@%s: orientation = %d", __FUNCTION__, orientation);
    Message msg;
    msg.id = MESSAGE_ID_SET_ROTATION;
    msg.data.config.value = orientation;
    mMessageQueue.send(&msg);
}

}
