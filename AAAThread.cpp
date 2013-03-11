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
#include "CameraDump.h"
#include "PerformanceTraces.h"

namespace android {

#define FLASH_FRAME_TIMEOUT 5

AAAThread::AAAThread(ICallbackAAA *aaaDone, AtomDvs *dvs, I3AControls *aaaControls) :
    Thread(false)
    ,mMessageQueue("AAAThread", (int) MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,m3AControls(aaaControls)
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
    ,mPreviousFaceCount(0)
    ,mFlashStage(FLASH_STAGE_NA)
    ,mFramesTillExposed(0)
{
    LOG1("@%s", __FUNCTION__);
    mFaceState.faces = new ia_face[MAX_FACES_DETECTABLE];
    if (mFaceState.faces == NULL) {
        LOGE("Error allocation memory for face state");
    } else {
        memset(mFaceState.faces, 0, MAX_FACES_DETECTABLE * sizeof(ia_face));
    }
    mFaceState.num_faces = 0;
}

AAAThread::~AAAThread()
{
    LOG1("@%s", __FUNCTION__);
    delete [] mFaceState.faces;
    mFaceState.faces = NULL;
    mFaceState.num_faces = 0;
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
 * Enters to FlashStage-machine by setting the initial stage.
 * Following newFrame()'s are tracked by handleFlashSequence().
 *
 * @param blockForStage runFlashSequence() is left blocked until
 *                      FlashStage-machine enters the requested state or
 *                      there's failure in sequence.
 */
status_t AAAThread::enterFlashSequence(FlashStage blockForStage)
{
    LOG1("@%s", __FUNCTION__);
    if (blockForStage == FLASH_STAGE_EXIT)
        return BAD_VALUE;
    Message msg;
    msg.id = MESSAGE_ID_FLASH_STAGE;
    msg.data.flashStage.value = blockForStage;
    return mMessageQueue.send(&msg,
            (blockForStage != FLASH_STAGE_NA) ? MESSAGE_ID_FLASH_STAGE : (MessageId) -1);
}

/**
 * Exits or interrupts the current flash sequence
 *
 * Must always be called after runFlashSequence() since sequence
 * continues until exposed or failure and remains the state until
 * this function is called.
 *
 * e.g for normal pre-flash sequence client calls:
 *
 * 1. runFlashSequence(FLASH_STAGE_PRE_FLASH_EXPOSED);
 *  - blocks until exposed frame is received or a failure
 *
 * 2. preview keeps running in exposed state keeping 3A not processed
 *
 * 3. endFlashSequence()
 *  - client wants to exit flash sequence and let normal 3A to continue
 */
status_t AAAThread::exitFlashSequence()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FLASH_STAGE;
    msg.data.flashStage.value = FLASH_STAGE_EXIT;
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

/**
 * override for IAtomIspObserver::atomIspNotify()
 *
 * signal start of 3A processing based on preview stream notify
 */
bool AAAThread::atomIspNotify(IAtomIspObserver::Message *msg, const ObserverState state)
{
    if (msg && msg->id == IAtomIspObserver::MESSAGE_ID_FRAME)
        newFrame(msg->data.frameBuffer.buff.capture_timestamp,
                 msg->data.frameBuffer.buff.status);
    return false;
}

status_t AAAThread::newFrame(struct timeval capture_timestamp, FrameBufferStatus frameStatus)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_NEW_FRAME;
    msg.data.frame.capture_timestamp = capture_timestamp;
    msg.data.frame.status = frameStatus;
    status = mMessageQueue.send(&msg);
    return status;
}

status_t AAAThread::setFaces(const ia_face_state& faceState)
{
    LOG2("@%s", __FUNCTION__);
    status_t status(NO_ERROR);

    if (mFaceState.faces == NULL) {
        LOGE("face state not allocated");
        return NO_INIT;
    }

    if (faceState.num_faces > MAX_FACES_DETECTABLE) {
        LOGW("@%s: %d faces detected, limiting to %d", __FUNCTION__,
            faceState.num_faces, MAX_FACES_DETECTABLE);
         mFaceState.num_faces = MAX_FACES_DETECTABLE;
    } else {
         mFaceState.num_faces = faceState.num_faces;
    }

    memcpy(mFaceState.faces, faceState.faces, mFaceState.num_faces * sizeof(ia_face));

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
        m3AControls->setAeLock(msg->enable);

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
        m3AControls->setAwbLock(msg->enable);

    mMessageQueue.reply(MESSAGE_ID_ENABLE_AWB_LOCK, status);
    return status;
}

status_t AAAThread::handleMessageAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status(NO_ERROR);
    AfMode currAfMode(m3AControls->getAfMode());

   /**
    * If we are in continuous focus mode we should return immediately with
    * the current status if we  are not busy.
    */
    ia_3a_af_status cafStatus = m3AControls->getCAFStatus();
    if (currAfMode == CAM_AF_MODE_CONTINUOUS && cafStatus != ia_3a_af_status_busy) {
        mCallbacks->autofocusDone(cafStatus == ia_3a_af_status_success);
        // Also notify ControlThread that the auto-focus is finished
        mAAADoneCallback->autoFocusDone();
        return status;
    }

    if (m3AControls->isIntel3A() && currAfMode != CAM_AF_MODE_INFINITY &&
        currAfMode != CAM_AF_MODE_FIXED && currAfMode != CAM_AF_MODE_MANUAL) {
        m3AControls->setAfEnabled(true);

        // state of client requested 3A locks is kept, so it
        // is safe to override the values here
        m3AControls->setAeLock(true);
        m3AControls->setAwbLock(true);

        m3AControls->startStillAf();
        mFramesTillAfComplete = 0;
        mStartAF = true;
        mStopAF = false;
    } else {
        mCallbacks->autofocusDone(true);
        // Also notify ControlThread that the auto-focus is finished
        mAAADoneCallback->autoFocusDone();
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

status_t AAAThread::handleMessageFlashStage(MessageFlashStage *msg)
{
    status_t status = NO_ERROR;
    LOG1("@%s", __FUNCTION__);

    // handle exitFlashSequence()
    if (msg->value == FLASH_STAGE_EXIT) {
        if (mBlockForStage != FLASH_STAGE_PRE_EXPOSED &&
            mBlockForStage != FLASH_STAGE_SHOT_EXPOSED &&
            mBlockForStage != FLASH_STAGE_NA) {
            LOG1("Flash sequence interrupted");
        }
        if (mBlockForStage != FLASH_STAGE_NA) {
            mMessageQueue.reply(MESSAGE_ID_FLASH_STAGE, status);
            mBlockForStage = FLASH_STAGE_NA;
        }
        mFlashStage = FLASH_STAGE_NA;
        return NO_ERROR;
    }

    // handle enterFlashSequence()
    if (mFlashStage != FLASH_STAGE_NA) {
        status = ALREADY_EXISTS;
        LOGE("Flash sequence already started");
        if (msg->value != FLASH_STAGE_NA)
            mMessageQueue.reply(MESSAGE_ID_FLASH_STAGE, status);
        return status;
    }

    mBlockForStage = msg->value;
    if (mBlockForStage == FLASH_STAGE_SHOT_EXPOSED) {
        // TODO: Not receiving expose statuses for snapshot frames
        // ControlThread does snapshot capturing atm for this purpose.
        LOGD("Not Implemented! its a deadlock");
        mFlashStage = FLASH_STAGE_SHOT_WAITING;
    } else {
        // Enter pre-flash sequence by default
        mFlashStage = FLASH_STAGE_PRE_START;
    }
    return NO_ERROR;
}

/**
 * handles FlashStage-machine states based on newFrame()'s
 *
 * returns true if sequence is running and normal 3A should
 * not be executed
 */
bool AAAThread::handleFlashSequence(FrameBufferStatus frameStatus)
{
    static unsigned int skipForEv = 0;
    status_t status = NO_ERROR;

    if (mFlashStage == FLASH_STAGE_NA) {
        if (mBlockForStage != FLASH_STAGE_NA) {
            // should never occur
            LOG1("Releasing runFlashSequence(), sequence not started");
            mMessageQueue.reply(MESSAGE_ID_FLASH_STAGE, UNKNOWN_ERROR);
            mBlockForStage = FLASH_STAGE_NA;
        }
        return false;
    }

    LOG2("@%s : mFlashStage %d, FrameStatus %d", __FUNCTION__, mFlashStage, frameStatus);

    switch (mFlashStage) {
        case FLASH_STAGE_PRE_START:
            // hued images fix (BZ: 72908)
            if (skipForEv++ < 2)
                break;
            // Enter Stage 1
            mFramesTillExposed = 0;
            skipForEv = 2;
            status = m3AControls->applyPreFlashProcess(CAM_FLASH_STAGE_NONE);
            mFlashStage = FLASH_STAGE_PRE_PHASE1;
            break;
        case FLASH_STAGE_PRE_PHASE1:
            // Stage 1.5: Skip 2 frames to get exposure from Stage 1.
            //            First frame is for sensor to pick up the new value
            //            and second for sensor to apply it.
            if (--skipForEv <= 0)
                break;
            // Enter Stage 2
            skipForEv = 2;
            status = m3AControls->applyPreFlashProcess(CAM_FLASH_STAGE_PRE);
            mFlashStage = FLASH_STAGE_PRE_PHASE2;
            break;
        case FLASH_STAGE_PRE_PHASE2:
            // Stage 2.5: Same as above, but for Stage 2.
            if (--skipForEv <= 0)
                break;
            // Enter Stage 3: get the flash-exposed preview frame
            // and let the 3A library calculate the exposure
            // settings for the flash-exposed still capture.
            // We check the frame status to make sure we use
            // the flash-exposed frame.
            status = m3AControls->setFlash(1);
            mFlashStage = FLASH_STAGE_PRE_WAITING;
            break;
        case FLASH_STAGE_SHOT_WAITING:
        case FLASH_STAGE_PRE_WAITING:
            mFramesTillExposed++;
            if (frameStatus == FRAME_STATUS_FLASH_EXPOSED) {
                LOG1("PreFlash@Frame %d: SUCCESS    (stopping...)", mFramesTillExposed);
                mFlashStage = (mFlashStage == FLASH_STAGE_SHOT_WAITING) ?
                               FLASH_STAGE_SHOT_EXPOSED:FLASH_STAGE_PRE_EXPOSED;
                m3AControls->setFlash(0);
                m3AControls->applyPreFlashProcess(CAM_FLASH_STAGE_MAIN);
            } else if(mFramesTillExposed > FLASH_FRAME_TIMEOUT
                   || ( frameStatus != FRAME_STATUS_OK
                        && frameStatus != FRAME_STATUS_FLASH_PARTIAL) ) {
                LOG1("PreFlash@Frame %d: FAILED     (stopping...)", mFramesTillExposed);
                status = UNKNOWN_ERROR;
                m3AControls->setFlash(0);
                break;
            }
            status = NO_ERROR;
            break;
        case FLASH_STAGE_PRE_EXPOSED:
        case FLASH_STAGE_SHOT_EXPOSED:
            // staying in exposed state and not processing 3A until
            status = NO_ERROR;
            break;
        default:
        case FLASH_STAGE_EXIT:
        case FLASH_STAGE_NA:
            status = UNKNOWN_ERROR;
            break;
    }

    if (mBlockForStage == mFlashStage) {
        LOG2("Releasing runFlashSequence()");
        mMessageQueue.reply(MESSAGE_ID_FLASH_STAGE, status);
        mBlockForStage = FLASH_STAGE_NA;
    }

    if (status != NO_ERROR) {
        LOGD("Flash sequence failed!");
        mFramesTillExposed = 0;
        skipForEv = 0;
        mFlashStage = FLASH_STAGE_NA;
        if (mBlockForStage != FLASH_STAGE_NA) {
            LOG2("Releasing runFlashSequence()");
            mMessageQueue.reply(MESSAGE_ID_FLASH_STAGE, status);
            mBlockForStage = FLASH_STAGE_NA;
        }
        return false;
    }

    return true;
}

status_t AAAThread::handleMessageNewFrame(MessageNewFrame *msgFrame)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int sceneMode = 0;
    bool sceneHdr = false;
    Vector<Message> messages;
    struct timeval capture_timestamp = msgFrame->capture_timestamp;

    if (!mDVSRunning && !m3ARunning)
        return status;

    if (handleFlashSequence(msgFrame->status))
        return status;

    // 3A & DVS stats are read with proprietary ioctl that returns the
    // statistics of most recent frame done.
    // Multiple newFrames indicates we are late and 3A process is going
    // to read the statistics of the most recent frame.
    // We flush the queue and use the most recent timestamp.
    mMessageQueue.remove(MESSAGE_ID_NEW_FRAME, &messages);
    if(!messages.isEmpty()) {
        Message recent_msg = *messages.begin();
        LOGW("%d frames in 3A process queue, handling timestamp "
             "%lld instead of %lld\n", messages.size(),
        ((long long)(recent_msg.data.frame.capture_timestamp.tv_sec)*1000000LL +
         (long long)(recent_msg.data.frame.capture_timestamp.tv_usec)),
        ((long long)(capture_timestamp.tv_sec)*1000000LL +
         (long long)(capture_timestamp.tv_usec)));
        capture_timestamp = recent_msg.data.frame.capture_timestamp;
    }

    if(m3ARunning){
        // Run 3A statistics
        status = m3AControls->apply3AProcess(true, capture_timestamp);

        //dump 3A statistics
        if (CameraDump::isDumpImageEnable(CAMERA_DEBUG_DUMP_3A_STATISTICS))
            m3AControls->dumpCurrent3aStatToFile();

        // If auto-focus was requested, run auto-focus sequence
        if (status == NO_ERROR && mStartAF) {
            // Check for cancel-focus
            ia_3a_af_status afStatus = ia_3a_af_status_error;
            if (mStopAF) {
                afStatus = ia_3a_af_status_cancelled;
            } else {
                afStatus = m3AControls->isStillAfComplete();
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
                m3AControls->stopStillAf();
                m3AControls->setAeLock(mForceAeLock);
                m3AControls->setAwbLock(mForceAwbLock);
                m3AControls->setAfEnabled(false);
                mStartAF = false;
                mStopAF = false;
                mFramesTillAfComplete = 0;
                mCallbacks->autofocusDone(afStatus == ia_3a_af_status_success);
                // Also notify ControlThread that the auto-focus is finished
                mAAADoneCallback->autoFocusDone();
                /**
                 * Even if we complete AF, if the result was failure we keep
                 * trying to focus if we are in continuous focus mode.
                 *
                 */
                if((m3AControls->getAfMode() == CAM_AF_MODE_CONTINUOUS) &&
                   (afStatus != ia_3a_af_status_success) ) {
                    mStartAF = true;
                    m3AControls->setAfEnabled(true);
                }
            }
        }

        AfMode currPublicAfMode = m3AControls->getPublicAfMode();
        if (currPublicAfMode == CAM_AF_MODE_CONTINUOUS) {
            ia_3a_af_status cafStatus = m3AControls->getCAFStatus();
            LOG2("CAF move lens status: %d", cafStatus);
            if (cafStatus != mPreviousCafStatus) {
                LOG2("CAF move: %d", cafStatus == ia_3a_af_status_busy);
                // Send the callback to upper layer and inform about the CAF status.
                mCallbacks->focusMove(cafStatus == ia_3a_af_status_busy);
                mPreviousCafStatus = cafStatus;
            }

            if (cafStatus == ia_3a_af_status_success)
                PerformanceTraces::Launch2FocusLock::stop();
        }

        // Set face data to 3A only if there were detected faces and avoid unnecessary
        // setting with consecutive zero face count.
        if (!(mFaceState.num_faces == 0 && mPreviousFaceCount == 0)) {
            m3AControls->setFaces(mFaceState);
            mPreviousFaceCount = mFaceState.num_faces;
        }

        // Query the detected scene and notify the application
        if (m3AControls->getSmartSceneDetection()) {
            m3AControls->getSmartSceneMode(&sceneMode, &sceneHdr);

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
            status = handleMessageNewFrame(&msg.data.frame);
            break;

        case MESSAGE_ID_ENABLE_AE_LOCK:
            status = handleMessageEnableAeLock(&msg.data.enable);
            break;

        case MESSAGE_ID_ENABLE_AWB_LOCK:
            status = handleMessageEnableAwbLock(&msg.data.enable);
            break;

        case MESSAGE_ID_FLASH_STAGE:
            status = handleMessageFlashStage(&msg.data.flashStage);
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
