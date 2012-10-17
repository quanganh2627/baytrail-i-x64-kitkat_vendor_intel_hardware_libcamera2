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
#define LOG_TAG "Camera_AAAThread"

#include <time.h>
#include "LogHelper.h"
#include "CallbacksThread.h"
#include "AAAThread.h"
#include "AtomAAA.h"
#include "FaceDetector.h"

namespace android {

AAAThread::AAAThread(ICallbackAAA *aaaDone, AtomDvs *dvs) :
    Thread(false)
    ,mMessageQueue("AAAThread", (int) MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mAAA(AtomAAA::getInstance())
    ,mCallbacks(CallbacksThread::getInstance())
    ,mDvs(dvs)
    ,mAAADoneCallback(aaaDone)
    ,m3ARunning(false)
    ,mDVSRunning(false)
    ,mStartAF(false)
    ,mStopAF(false)
    ,mPreviousCafStatus(ia_3a_af_status_idle)
    ,mForceAeLock(false)
    ,mForceAwbLock(false)
    ,mSmartSceneMode(0)
    ,mSmartSceneHdr(false)
    ,mCurrentZoom(0)
{
    LOG1("@%s", __FUNCTION__);
    mFaceMetadata.faces = new camera_face_t[MAX_FACES_DETECTABLE];
    memset(mFaceMetadata.faces, 0, MAX_FACES_DETECTABLE * sizeof(camera_face_t));
    mFaceMetadata.number_of_faces = 0;
}

AAAThread::~AAAThread()
{
    LOG1("@%s", __FUNCTION__);
    delete [] mFaceMetadata.faces;
    mFaceMetadata.faces = NULL;
}

status_t AAAThread::enable3A()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ENABLE_AAA;
    return mMessageQueue.send(&msg, MESSAGE_ID_ENABLE_AAA);
}

status_t AAAThread::enableDVS(bool en)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ENABLE_DVS;
    msg.data.enable.enable = en;
    return mMessageQueue.send(&msg);
}

/**
 * Sets AE lock status.
 *
 * This lock status is maintained also outside the autofocus
 * sequence.
 */
status_t AAAThread::lockAe(bool en)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ENABLE_AE_LOCK;
    msg.data.enable.enable = en;
    return mMessageQueue.send(&msg, MESSAGE_ID_ENABLE_AE_LOCK);
}

/**
 * Sets AWB lock status.
 *
 * This lock status is maintained also outside the autofocus
 * sequence.
 */
status_t AAAThread::lockAwb(bool en)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_ENABLE_AWB_LOCK;
    msg.data.enable.enable = en;
    return mMessageQueue.send(&msg, MESSAGE_ID_ENABLE_AWB_LOCK);
}

status_t AAAThread::autoFocus()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::cancelAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::newFrame(struct timeval capture_timestamp)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_NEW_FRAME;
    msg.data.frame.capture_timestamp = capture_timestamp;
    status = mMessageQueue.send(&msg);
    return status;
}

status_t AAAThread::setFaces(camera_frame_metadata_t *face_metadata, int zoom)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int num_faces;
    if (face_metadata->number_of_faces > MAX_FACES_DETECTABLE) {
        LOGW("@%s: %d faces detected, limiting to %d", __FUNCTION__,
            face_metadata->number_of_faces, MAX_FACES_DETECTABLE);
        num_faces = MAX_FACES_DETECTABLE;
    } else {
        num_faces = face_metadata->number_of_faces;
    }
    mFaceMetadata.number_of_faces = num_faces;
    memcpy(mFaceMetadata.faces, face_metadata->faces, mFaceMetadata.number_of_faces * sizeof(camera_face_t));
    mCurrentZoom = zoom;
    return status;
}

status_t AAAThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    m3ARunning = false;
    mDVSRunning = false;

    return status;
}

status_t AAAThread::handleMessageEnable3A()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    m3ARunning = true;
    mMessageQueue.reply(MESSAGE_ID_ENABLE_AAA, status);
    return status;
}

status_t AAAThread::handleMessageEnableDVS(MessageEnable* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mDVSRunning = msg->enable;
    return status;
}

status_t AAAThread::handleMessageEnableAeLock(MessageEnable* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mForceAeLock = msg->enable;

    // during AF, AE lock is controlled by AF, otherwise
    // set the value here
    if (mStartAF != true)
        mAAA->setAeLock(msg->enable);

    mMessageQueue.reply(MESSAGE_ID_ENABLE_AE_LOCK, status);
    return status;
}

status_t AAAThread::handleMessageEnableAwbLock(MessageEnable* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mForceAwbLock = msg->enable;

    // during AF, AWB lock is controlled by AF, otherwise
    // set the value here
    if (mStartAF != true)
        mAAA->setAwbLock(msg->enable);

    mMessageQueue.reply(MESSAGE_ID_ENABLE_AWB_LOCK, status);
    return status;
}

status_t AAAThread::handleMessageAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mAAA->is3ASupported()) {
        mAAA->setAfEnabled(true);

        // state of client requested 3A locks is kept, so it
        // is safe to override the values here
        mAAA->setAeLock(true);
        mAAA->setAwbLock(true);

        mAAA->startStillAf();
        mFramesTillAfComplete = 0;
        mStartAF = true;
        mStopAF = false;
    } else {
        mCallbacks->autofocusDone(true);
    }

    return status;
}

status_t AAAThread::handleMessageCancelAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mStartAF) {
        mStopAF = true;
    }

    return status;
}

status_t AAAThread::handleMessageNewFrame(struct timeval capture_timestamp)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int sceneMode = 0;
    bool sceneHdr = false;

    if (!mDVSRunning && !m3ARunning)
        return status;

    if(m3ARunning){
        // Run 3A statistics
        status = mAAA->apply3AProcess(true, capture_timestamp);

        // If auto-focus was requested, run auto-focus sequence
        if (status == NO_ERROR && mStartAF) {
            // Check for cancel-focus
            ia_3a_af_status afStatus = ia_3a_af_status_error;
            if (mStopAF) {
                afStatus = ia_3a_af_status_cancelled;
            } else {
                afStatus = mAAA->isStillAfComplete();
                mFramesTillAfComplete++;
            }
            bool stopStillAf = false;
            if (afStatus == ia_3a_af_status_busy) {
                LOG1("StillAF@Frame %d: BUSY    (continuing...)", mFramesTillAfComplete);
            } else if (afStatus == ia_3a_af_status_success) {
                LOG1("StillAF@Frame %d: SUCCESS (stopping...)", mFramesTillAfComplete);
                stopStillAf = true;
            } else if (afStatus == ia_3a_af_status_error) {
                LOG1("StillAF@Frame %d: FAIL    (stopping...)", mFramesTillAfComplete);
                stopStillAf = true;
            } else if (afStatus == ia_3a_af_status_cancelled) {
                LOG1("StillAF@Frame %d: CANCEL  (stopping...)", mFramesTillAfComplete);
                stopStillAf = true;
            }

            if (stopStillAf) {
                mAAA->stopStillAf();
                mAAA->setAeLock(mForceAeLock);
                mAAA->setAwbLock(mForceAwbLock);
                mAAA->setAfEnabled(false);
                mStartAF = false;
                mStopAF = false;
                // ****** workaround begin, BZ: 58489 ******
                // we need to refresh 3A AF settings because it seems 3A forgets its mode
                // after still af sequence is run
                mAAA->setAfMode(mAAA->getAfMode());
                // ****** workaround end *******************
                mFramesTillAfComplete = 0;
                mCallbacks->autofocusDone(afStatus == ia_3a_af_status_success);
                // Also notify ControlThread that the auto-focus is finished
                mAAADoneCallback->autoFocusDone();
            }
        }

        AfMode currAfMode = mAAA->getAfMode();
        if (currAfMode == CAM_AF_MODE_CONTINUOUS) {
            ia_3a_af_status cafStatus = mAAA->getCAFStatus();
            LOG2("CAF move lens status: %d", cafStatus);
            if (cafStatus != mPreviousCafStatus) {
                LOG2("CAF move: %d", cafStatus == ia_3a_af_status_busy);
                // Send the callback to upper layer and inform about the CAF status.
                mCallbacks->focusMove(cafStatus == ia_3a_af_status_busy);
                mPreviousCafStatus = cafStatus;
            }
        }

        // Query the detected scene and notify the application
        if (mAAA->getSmartSceneDetection()) {
            if (mFaceMetadata.number_of_faces > 0)
                mAAA->setFaces(&mFaceMetadata, mCurrentZoom);
            mAAA->getSmartSceneMode(&sceneMode, &sceneHdr);

            if ((sceneMode != mSmartSceneMode) || (sceneHdr != mSmartSceneHdr)) {
                LOG1("SmartScene: new scene detected: %d, HDR: %d", sceneMode, sceneHdr);
                mSmartSceneMode = sceneMode;
                mSmartSceneHdr = sceneHdr;
                mAAADoneCallback->sceneDetected(sceneMode, sceneHdr);
            }
        }
    }

    if (mDVSRunning) {
        status = mDvs->run();
    }
    return status;
}

void AAAThread::getCurrentSmartScene(int &sceneMode, bool &sceneHdr)
{
    LOG1("@%s", __FUNCTION__);
    sceneMode = mSmartSceneMode;
    sceneHdr = mSmartSceneHdr;
}

void AAAThread::resetSmartSceneValues()
{
    LOG1("@%s", __FUNCTION__);
    mSmartSceneMode = 0;
    mSmartSceneHdr = false;
}

status_t AAAThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;

        case MESSAGE_ID_ENABLE_AAA:
            status = handleMessageEnable3A();
            break;

        case MESSAGE_ID_ENABLE_DVS:
            status = handleMessageEnableDVS(&msg.data.enable);
            break;

        case MESSAGE_ID_AUTO_FOCUS:
            status = handleMessageAutoFocus();
            break;

        case MESSAGE_ID_CANCEL_AUTO_FOCUS:
            status = handleMessageCancelAutoFocus();
            break;

        case MESSAGE_ID_NEW_FRAME:
            status = handleMessageNewFrame(msg.data.frame.capture_timestamp);
            break;

        case MESSAGE_ID_ENABLE_AE_LOCK:
            status = handleMessageEnableAeLock(&msg.data.enable);
            break;

        case MESSAGE_ID_ENABLE_AWB_LOCK:
            status = handleMessageEnableAwbLock(&msg.data.enable);
            break;

        default:
            status = BAD_VALUE;
            break;
    };
    return status;
}

bool AAAThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    return false;
}

status_t AAAThread::requestExitAndWait()
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
