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
#include "FeatureData.h"
#include <system/camera.h>

namespace android {

PostProcThread::PostProcThread(ICallbackPostProc *postProcDone, PanoramaThread *panoramaThread) :
    IFaceDetector(CallbacksThread::getInstance())
    ,Thread(true) // callbacks may call into java
    ,mFaceDetector(NULL)
    ,mPanoramaThread(panoramaThread)
    ,mMessageQueue("PostProcThread", (int) MESSAGE_ID_MAX)
    ,mLastReportedNumberOfFaces(0)
    ,mCallbacks(Callbacks::getInstance())
    ,mPostProcDoneCallback(postProcDone)
    ,mThreadRunning(false)
    ,mFaceDetectionRunning(false)
    ,mFaceRecognitionRunning(false)
    ,mFaceAAAFlags(AAA_FLAG_ALL)
    ,mOldAfMode(CAM_AF_MODE_NOT_SET)
    ,mOldAeMeteringMode(CAM_AE_METERING_MODE_NOT_SET)
{
    LOG1("@%s", __FUNCTION__);

    //init SmartShutter, must match defaultParams
    mSmartShutter.smartRunning = false;
    mSmartShutter.smileRunning = false;
    mSmartShutter.blinkRunning = false;
    mSmartShutter.captureOnTrigger = false;
    mSmartShutter.captureTriggered = false;
    mSmartShutter.smileThreshold = SMILE_THRESHOLD;
    mSmartShutter.blinkThreshold = BLINK_THRESHOLD;
}

PostProcThread::~PostProcThread()
{
    LOG1("@%s", __FUNCTION__);
    if (mFaceDetector != NULL) {
        mFaceDetector->requestExitAndWait();
        mFaceDetector.clear();
    }
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

    if (mFaceDetector->run() != NO_ERROR) {
        LOGE("Error starting FaceDetector thread!");
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
    params->set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW, MAX_FACES_DETECTABLE);
    intel_params->set(IntelCameraParameters::KEY_SMILE_SHUTTER_THRESHOLD, STRINGIFY(SMILE_THRESHOLD));
    intel_params->set(IntelCameraParameters::KEY_BLINK_SHUTTER_THRESHOLD, STRINGIFY(BLINK_THRESHOLD));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_SMILE_SHUTTER, FeatureData::smileShutterSupported(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_BLINK_SHUTTER, FeatureData::blinkShutterSupported(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_FACE_DETECTION, FeatureData::faceDetectionSupported(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_FACE_RECOGNITION, FeatureData::faceRecognitionSupported(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_SCENE_DETECTION, FeatureData::sceneDetectionSupported(cameraId));
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

    if (mFaceDetectionRunning || mPanoramaThread->getState() == PANORAMA_DETECTING_OVERLAP) {
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

    mLastReportedNumberOfFaces = 0;
    mFaceDetectionRunning = true;
    return status;
}

void PostProcThread::stopFaceDetection(bool wait)
{
    LOG1("@%s", __FUNCTION__);
    if (mFaceDetectionRunning) {
        Message msg;
        msg.id = MESSAGE_ID_STOP_FACE_DETECTION;

        if (wait) {
            mMessageQueue.send(&msg, MESSAGE_ID_STOP_FACE_DETECTION); // wait for reply
        } else {
            mMessageQueue.send(&msg);
        }
    }
}

status_t PostProcThread::handleMessageStopFaceDetection()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mFaceDetectionRunning = false;
    mOldAfMode = CAM_AF_MODE_NOT_SET;
    mOldAeMeteringMode = CAM_AE_METERING_MODE_NOT_SET;

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
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageStartSmartShutter(MessageSmartShutter params)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (!mFaceDetectionRunning) {
        LOGE("%s: Face Detection must be running", __FUNCTION__);
        return INVALID_OPERATION;
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
        return INVALID_OPERATION;
    }
    if (mSmartShutter.smileRunning || mSmartShutter.blinkRunning)
        mSmartShutter.smartRunning = true;

    LOG1("%s: mode: %d Active Mode: (smile %d (%d) , blink %d (%d), smart %d)", __FUNCTION__, params.mode, mSmartShutter.smileRunning, mSmartShutter.smileThreshold, mSmartShutter.blinkRunning, mSmartShutter.blinkThreshold, mSmartShutter.smartRunning);

    return status;
}

void PostProcThread::stopSmartShutter(SmartShutterMode mode)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STOP_SMART_SHUTTER;
    msg.data.smartShutterParam.mode = mode;
    msg.data.smartShutterParam.level = 0;
    mMessageQueue.send(&msg);
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

bool PostProcThread::isSmartRunning()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_IS_SMART_RUNNING;
    mMessageQueue.send(&msg, MESSAGE_ID_IS_SMART_RUNNING); // waiting for reply
    return mSmartShutter.smartRunning;
}

status_t PostProcThread::handleMessageIsSmartRunning()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mMessageQueue.reply(MESSAGE_ID_IS_SMART_RUNNING, status);
    return status;
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
    mThreadRunning = false;
    mFaceDetectionRunning = false;
    return status;
}

status_t PostProcThread::setZoom(int zoomRatio)
{
    Message msg;
    msg.id = MESSAGE_ID_SET_ZOOM;
    msg.data.config.value = zoomRatio;
    return mMessageQueue.send(&msg);
}

status_t PostProcThread::setRotation(int rotation)
{
    Message msg;
    msg.id = MESSAGE_ID_SET_ROTATION;
    msg.data.config.value = rotation;
    return mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageSetZoom(MessageConfig &msg)
{
    mZoomRatio = msg.value;
    return NO_ERROR;
}

status_t PostProcThread::handleMessageSetRotation(MessageConfig &msg)
{
    mRotation = msg.value;
    return NO_ERROR;
}

int PostProcThread::sendFrame(AtomBuffer *img)
{
    LOG2("@%s: buf=%p, width=%d height=%d rotation=%d", __FUNCTION__, img, img->width , img->height, img->rotation);
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

void PostProcThread::enableFaceAAA(AAAFlags flags)
{
    LOG1("@%s: flags = %d" , __FUNCTION__, (int)flags);
    Message msg;
    msg.id = MESSAGE_ID_ENABLE_FACE_AAA;
    msg.data.faceAAA.flags = flags;

    mMessageQueue.send(&msg);
}

void PostProcThread::disableFaceAAA(AAAFlags flags)
{
    LOG1("@%s: flags = %d", __FUNCTION__, (int)flags);
    Message msg;
    msg.id = MESSAGE_ID_DISABLE_FACE_AAA;
    msg.data.faceAAA.flags = flags;

    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageEnableFaceAAA(const MessageFaceAAA& msg)
{
    mFaceAAAFlags = mFaceAAAFlags | msg.flags;
    LOG1("@%s: enabled %d flags, after mFaceAAAFlags is %d", __FUNCTION__,  (int)msg.flags, (int)mFaceAAAFlags);

    return NO_ERROR;
}

status_t PostProcThread::handleMessageDisableFaceAAA(const MessageFaceAAA& msg)
{
    mFaceAAAFlags = mFaceAAAFlags & ~msg.flags;
    LOG1("@%s: disabled %d flags, after mFaceAAAFlags is %d", __FUNCTION__, (int)msg.flags, (int)mFaceAAAFlags);
    return NO_ERROR;
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
        case MESSAGE_ID_IS_SMART_RUNNING:
            status = handleMessageIsSmartRunning();
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
        case MESSAGE_ID_ENABLE_FACE_AAA:
            status = handleMessageEnableFaceAAA(msg.data.faceAAA);
            break;
        case MESSAGE_ID_DISABLE_FACE_AAA:
            status = handleMessageDisableFaceAAA(msg.data.faceAAA);
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
        case MESSAGE_ID_SET_ZOOM:
            status = handleMessageSetZoom(msg.data.config);
            break;
        case MESSAGE_ID_SET_ROTATION:
            status = handleMessageSetRotation(msg.data.config);
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

    if (mFaceDetectionRunning) {
        LOG2("%s: Face detection executing", __FUNCTION__);
        int num_faces;
        bool smile = false;
        bool blink = true;
        unsigned char *src;
        if (frame.img.type == ATOM_BUFFER_PREVIEW) {
            src = (unsigned char*) frame.img.buff->data;
        } else {
            src = (unsigned char*) frame.img.gfxData;
        }
        ia_frame frameData;
        frameData.data = src;
        frameData.size = frame.img.size;
        frameData.width = frame.img.width;
        frameData.height = frame.img.height;
        frameData.stride = frame.img.stride;

        // frame rotation is counter clock wise in libia_face,
        // while it is clock wise for android (valid for CTP)
        if (frame.img.rotation == 90)
            frameData.rotation = 270;
        else if (frame.img.rotation == 270)
            frameData.rotation = 90;
        else
            frameData.rotation = frame.img.rotation;

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
        camera_frame_metadata_t face_metadata;

        ia_face_state faceState;
        faceState.faces = new ia_face[num_faces];
        if (faceState.faces == NULL) {
            LOGE("Error allocation memory");
            return NO_MEMORY;
        }

        face_metadata.number_of_faces = mFaceDetector->getFaces(faces, frameData.width, frameData.height);
        face_metadata.faces = faces;
        mFaceDetector->getFaceState(&faceState, frameData.width, frameData.height, mZoomRatio);

        // call face detection listener and pass faces for 3A (AF) and smart scene detection
        if ((face_metadata.number_of_faces > 0) || (mLastReportedNumberOfFaces != 0)) {
            mLastReportedNumberOfFaces = face_metadata.number_of_faces;
            useFacesForAAA(face_metadata);
            mpListener->facesDetected(face_metadata);
            mPostProcDoneCallback->facesDetected(&faceState);
        }

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
                || ((!blink && mSmartShutter.blinkRunning) && !mSmartShutter.smileRunning)) {
                mSmartShutter.captureOnTrigger = false;
                mPostProcDoneCallback->postProcCaptureTrigger();
                mSmartShutter.captureTriggered = true;
            }
        }
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

void PostProcThread::setFocusAreas(const CameraWindow* windows, size_t winCount)
{
    LOG2("@%s", __FUNCTION__);
    AfMode newAfMode = CAM_AF_MODE_FACE;

    AtomAAA* aaa = AtomAAA::getInstance();
    if (aaa->setAfWindows(windows, winCount) == NO_ERROR) {
        AfMode curAfMode = aaa->getAfMode();
        // See if we have to change the actual mode (it could be correct already)
        if (curAfMode != newAfMode) {
            mOldAfMode = curAfMode;
            aaa->setAfMode(newAfMode);
            LOG2("Set to face focus mode (%d) from current (%d)", newAfMode, curAfMode);
        }
    }
    return;
}

void PostProcThread::setAeMeteringArea(const CameraWindow* window)
{
    LOG2("@%s", __FUNCTION__);
    AtomAAA* aaa = AtomAAA::getInstance();

    if (aaa->setAeWindow(window) == NO_ERROR) {
        MeteringMode curAeMeteringMode = aaa->getAeMeteringMode();
        if (curAeMeteringMode != CAM_AE_METERING_MODE_SPOT) {
            LOG2("Setting AE metering mode to spot for face exposure");
            mOldAeMeteringMode = aaa->getAeMeteringMode();
            aaa->setAeMeteringMode(CAM_AE_METERING_MODE_SPOT);
        }
    }
}

void PostProcThread::useFacesForAAA(const camera_frame_metadata_t& face_metadata)
{
    LOG2("@%s", __FUNCTION__);
    if (face_metadata.number_of_faces <= 0) {
        resetToOldAAAValues();
        return;
    }

    if (mFaceAAAFlags & AAA_FLAG_AF || mFaceAAAFlags & AAA_FLAG_AE) {
        CameraWindow *windows = new CameraWindow[face_metadata.number_of_faces];
        int highestScoreInd = 0;
        AtomAAA *aaa = AtomAAA::getInstance();
        AAAWindowInfo aaaWindow;
        aaa->getGridWindow(aaaWindow);
        for (int i = 0; i < face_metadata.number_of_faces; i++) {
            camera_face_t face = face_metadata.faces[i];
            windows[i].x_left = face.rect[0];
            windows[i].y_top = face.rect[1];
            windows[i].x_right = face.rect[2];
            windows[i].y_bottom = face.rect[3];
            convertFromAndroidCoordinates(windows[i], windows[i], aaaWindow);
            LOG2("Face window: (%d,%d,%d,%d)",
                windows[i].x_left,
                windows[i].y_top,
                windows[i].x_right,
                windows[i].y_bottom);

            // Get the highest scored face window index:
            if (i > 0 && face.score > face_metadata.faces[i - 1].score) {
                highestScoreInd = i;
            }
        }
        // Apply AF window, if needed:
        if (mFaceAAAFlags & AAA_FLAG_AF)
            setFocusAreas(windows, face_metadata.number_of_faces);
        // Apply AE window if needed:
        if (mFaceAAAFlags & AAA_FLAG_AE) {
            // Use the highest score window for AE metering:
            // TODO: Better logic needed for picking face AE metering area..?
            CameraWindow aeWindow = windows[highestScoreInd];
            aeWindow.weight = 5;
            setAeMeteringArea(&aeWindow);
        }
    }

    //TODO: spec says we need also do AWB. Currently no support.
}

void PostProcThread::resetToOldAAAValues()
{
        // No faces detected, reset to previous 3A values:
        AtomAAA* aaa = AtomAAA::getInstance();

        // Auto-focus:
        if ((mFaceAAAFlags & AAA_FLAG_AF) && mOldAfMode != CAM_AF_MODE_NOT_SET) {
            LOG2("Reset to old focus mode (%d)", mOldAfMode);
            aaa->setAfMode(mOldAfMode);
            mOldAfMode = CAM_AF_MODE_NOT_SET;
        }

        // Auto-exposure metering mode:
        if ((mFaceAAAFlags & AAA_FLAG_AE) && mOldAeMeteringMode != CAM_AE_METERING_MODE_NOT_SET) {
            LOG2("Reset to old AE metering mode (%d)", mOldAeMeteringMode);
            aaa->setAeMeteringMode(mOldAeMeteringMode);
            mOldAeMeteringMode = CAM_AE_METERING_MODE_NOT_SET;
        }

        // TODO: Reset AWB also, once taken into use above.
}

}
