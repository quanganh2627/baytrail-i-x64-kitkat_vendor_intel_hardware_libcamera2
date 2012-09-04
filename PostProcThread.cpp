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
#include "AtomAAA.h"
#include <system/camera.h>

namespace android {

PostProcThread::PostProcThread(ICallbackPostProc *postProcDone) :
    IFaceDetector(CallbacksThread::getInstance())
    ,Thread(true) // callbacks may call into java
    ,mFaceDetector(NULL)
    ,mMessageQueue("PostProcThread", (int) MESSAGE_ID_MAX)
    ,mLastReportedNumberOfFaces(0)
    ,mCallbacks(Callbacks::getInstance())
    ,mPostProcDoneCallback(postProcDone)
    ,mThreadRunning(false)
    ,mFaceDetectionRunning(false)
    ,mSmartShutterRunning(false)
{
    LOG1("@%s", __FUNCTION__);
}

PostProcThread::~PostProcThread()
{
    LOG1("@%s", __FUNCTION__);
    if (mFaceDetector)
        delete mFaceDetector;
}

void PostProcThread::getDefaultParameters(CameraParameters *params)
{
    LOG1("@%s", __FUNCTION__);
    if (!params) {
        LOGE("params is null!");
        return;
    }
    // Set maximum number of detectable faces
    params->set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW, MAX_FACES_DETECTABLE);
}

void PostProcThread::startFaceDetection()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_START_FACE_DETECTION;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageStartFaceDetection()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (!mFaceDetectionRunning) {
        mFaceDetector = new FaceDetector();
        mFaceDetectionRunning = true;
    }
    return status;
}

void PostProcThread::stopFaceDetection(bool wait)
{
    LOG1("@%s", __FUNCTION__);
    if (mFaceDetectionRunning) {
        Message msg;
        msg.id = MESSAGE_ID_STOP_FACE_DETECTION;
        mMessageQueue.remove(MESSAGE_ID_FRAME); // flush all buffers

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

    delete mFaceDetector;
    mFaceDetector = NULL;
    mMessageQueue.reply(MESSAGE_ID_STOP_FACE_DETECTION, status);
    return status;
}

void PostProcThread::startSmartShutter()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_START_SMART_SHUTTER;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageStartSmartShutter()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mSmartShutterRunning = true;
    return status;
}

void PostProcThread::stopSmartShutter()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_STOP_SMART_SHUTTER;
    mMessageQueue.send(&msg);
}

status_t PostProcThread::handleMessageStopSmartShutter()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mSmartShutterRunning = false;
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

int PostProcThread::sendFrame(AtomBuffer *img, int width, int height)
{
    LOG2("@%s: buf=%p, width=%d height=%d", __FUNCTION__, img, width, height);
    Message msg;
    msg.id = MESSAGE_ID_FRAME;
    msg.data.frame.img = *img;
    msg.data.frame.height = height;
    msg.data.frame.width = width;
    if (mMessageQueue.send(&msg) == NO_ERROR)
        return 0;
    else
        return -1;
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
        case MESSAGE_ID_START_SMART_SHUTTER:
            status = handleMessageStartSmartShutter();
            break;
        case MESSAGE_ID_STOP_SMART_SHUTTER:
            status = handleMessageStopSmartShutter();
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
        bool smile, blink;
        unsigned char *src;
        if (frame.img.type == ATOM_BUFFER_PREVIEW) {
            src = (unsigned char*) frame.img.buff->data;
        } else {
            src = (unsigned char*) frame.img.gfxData;
        }
        ia_frame frameData;
        frameData.data = src;
        frameData.size = frame.img.size;
        frameData.width = frame.width;
        frameData.height = frame.height;
        frameData.stride = frame.img.stride;
        num_faces = mFaceDetector->faceDetect(&frameData);

        if (mSmartShutterRunning) {
            smile = mFaceDetector->smileDetect(&frameData);
            blink = mFaceDetector->blinkDetect(&frameData);
        }

        camera_face_t faces[num_faces];
        camera_frame_metadata_t face_metadata;
        face_metadata.number_of_faces = mFaceDetector->getFaces(faces, frame.width, frame.height);
        face_metadata.faces = faces;

        // call face detection listener and pass faces for 3A (AF) and smart scene detection
        if ((face_metadata.number_of_faces > 0) || (mLastReportedNumberOfFaces != 0)) {
            mLastReportedNumberOfFaces = face_metadata.number_of_faces;
            mpListener->facesDetected(face_metadata);
            useFacesForAAA(face_metadata);
            mPostProcDoneCallback->facesDetected(&face_metadata);
        }
    }

    // return buffer
    if (frame.img.owner != 0) {
        frame.img.owner->returnBuffer(&frame.img);
    }

    return status;
}

void PostProcThread::setFocusAreas(const CameraWindow* windows, size_t winCount)
{
    LOG1("@%s", __FUNCTION__);
    AfMode newAfMode = CAM_AF_MODE_TOUCH;

    AtomAAA* aaa = AtomAAA::getInstance();
    if (aaa->setAfWindows(windows, winCount) == NO_ERROR) {
        // See if we have to change the actual mode (it could be correct already)
        AfMode curAfMode = aaa->getAfMode();
        if (curAfMode != newAfMode)
            aaa->setAfMode(newAfMode);
    }
    return;
}

void PostProcThread::useFacesForAAA(const camera_frame_metadata_t& face_metadata)
{
    LOG1("@%s", __FUNCTION__);
    if (face_metadata.number_of_faces <= 0) return;
    CameraWindow *windows = new CameraWindow[face_metadata.number_of_faces];
    for (int i = 0; i < face_metadata.number_of_faces; i++) {
        camera_face_t face = face_metadata.faces[i];
        windows[i].x_left = face.rect[0];
        windows[i].y_top = face.rect[1];
        windows[i].x_right = face.rect[2];
        windows[i].y_bottom = face.rect[3];
    }

    //TODO: spec says we need also do AWB and AE. Currently no support.
    setFocusAreas(windows, face_metadata.number_of_faces);
}

}
