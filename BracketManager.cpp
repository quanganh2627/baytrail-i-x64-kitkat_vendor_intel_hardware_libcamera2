/*
 * Copyright (C) 2012 The Android Open Source Project
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
#define LOG_TAG "Camera_BracketManager"
//#define LOG_NDEBUG 0

#include "LogHelper.h"
#include "BracketManager.h"

namespace android {

BracketManager::BracketManager(AtomISP *isp) :
    Thread(false)
    ,mAAA(AtomAAA::getInstance())
    ,mISP(isp)
    ,mState(STATE_STOPPED)
    ,mMessageQueue("BracketManager", (int) MESSAGE_ID_MAX)
    ,mThreadRunning(false)
{
    LOG1("@%s", __FUNCTION__);
    mBracketing.mode = BRACKET_NONE;
}

BracketManager::~BracketManager()
{
    LOG1("@%s", __FUNCTION__);
}

status_t BracketManager::skipFrames(size_t numFrames, size_t doBracket)
{
    LOG1("@%s: numFrames=%d, doBracket=%d", __FUNCTION__, numFrames, doBracket);
    status_t status = NO_ERROR;
    AtomBuffer snapshotBuffer, postviewBuffer;

    for (size_t i = 0; i < numFrames; i++) {
        if (i < doBracket) {
            status = applyBracketingParams();
            if (status != NO_ERROR) {
                LOGE("Error applying bracketing in skip frame %d!", i);
                return status;
            }
        } else if (mBracketing.mode != BRACKET_NONE) {
            // poll and dequeue SOF event
            mISP->pollFrameSyncEvent();
        }
        if ((status = mISP->getSnapshot(&snapshotBuffer, &postviewBuffer)) != NO_ERROR) {
            LOGE("Error in grabbing warm-up frame %d!", i);
            return status;
        }
        status = mISP->putSnapshot(&snapshotBuffer, &postviewBuffer);
        if (status == DEAD_OBJECT) {
            LOG1("Stale snapshot buffer returned to ISP");
        } else if (status != NO_ERROR) {
            LOGE("Error in putting skip frame %d!", i);
            return status;
        }
    }
    return status;
}

/**
 *  For Exposure Bracketing, the applied exposure value will be available in
 *  current frame + 2. Therefore, in order to do a correct exposure bracketing
 *  we need to skip 2 frames. But, when burst-skip-frames parameter is set
 *  (>0) we have some special cases, described below.
 *
 *  We apply bracketing only for the first skipped frames, so the
 *  desired result will be available in the real needed frame.
 *  Below is the explanation:
 *  (S stands for skipped frame)
 *  (F stands for forced skipped frame, in order to get the desired exposure
 *   in the next real frame)
 *
 *  For burst-skip-frames=1
 *  Applied exposure value   EV0     EV1     EV2     EV3     EV4     EV5
 *  Frame number             FS0  S1   2  S3   4  S5   6  S7   8  S9  10 S11
 *  Output exposure value            EV0 EV0 EV1 EV1 EV2 EV2 EV3 EV3 EV4 EV4
 *  Explanation: in the beginning, we need to force one frame skipping, so
 *  that the applied exposure will be available in frame 2. Continuing the
 *  burst, we don't need to force skip frames, because we will apply the
 *  bracketing exposure in burst sequence (see the timeline above).
 *
 *  For burst-skip-frames=3
 *  Applied exposure value   EV0             EV1             EV2
 *  Frame number              S0  S1  S2   3  S4  S5  S6   7  S8  S9 S10  11
 *  Output exposure value            EV0 EV0 EV0 EV0 EV1 EV1 EV1 EV1 EV2 EV2
 *  Explanation: for burst-skip-frames >= 2, it's enough to apply the exposure
 *  bracketing in the first skipped frame in order to get the applied exposure
 *  in the next real frame (see the timeline above).
 *
 *  Exposure Bracketing and HDR:
 *  Currently there is an assumption in the HDR firmware in the ISP
 *  that the order how the frames are presented to the algorithm have the following
 *  exposures: MIN,0,MAX
 *  If the order of the exposure bracketing changes HDR firmware needs to be
 *  modified.
 *  This was noticed when this changed from libcamera to libcamera2.
 */

status_t BracketManager::initBracketing(int length, int skip, float *bracketValues)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mBracketing.mode);
    status_t status = NO_ERROR;
    ia_3a_af_lens_range lensRange;
    int currentFocusPos;

    mBurstLength = length;
    mFpsAdaptSkip = skip;
    mBurstCaptureNum = 0;
    mSnapshotReqNum = 0;
    mBracketNum = 0;
    mBracketingParams.clear();

    switch (mBracketing.mode) {
    case BRACKET_EXPOSURE:
        if (mBurstLength > 1) {
            mAAA->setAeMode(CAM_AE_MODE_MANUAL);

            mBracketing.values = new float[length];
            if (bracketValues != NULL) {
                // Using custom bracketing sequence
                for (int i = 0; i < length; i++) {
                    if (bracketValues[i] <= EV_MAX && bracketValues[i] >= EV_MIN) {
                        mBracketing.values[i] = bracketValues[i];
                    } else if (bracketValues[i] > EV_MAX) {
                        LOGW("Too high exposure value: %.2f", bracketValues[i]);
                        mBracketing.values[i] = EV_MAX;
                    } else if (bracketValues[i] < EV_MIN) {
                        LOGW("Too low exposure value: %.2f", bracketValues[i]);
                        mBracketing.values[i] = EV_MIN;
                    }
                    LOG1("Setting exposure bracketing parameter %d EV value: %.2f", i, mBracketing.values[i]);
                }
            } else {
                // Using standard bracketing sequence: EV_MIN ---> EV_MAX
                float bracketingStep = ((float)(EV_MAX - EV_MIN)) / (mBurstLength - 1);
                float exposureValue = EV_MIN;
                for (int i = 0; i < length; i++) {
                    mBracketing.values[i] = exposureValue;
                    exposureValue += bracketingStep;
                    LOG1("Setting exposure bracketing parameter %d EV value: %.2f", i, mBracketing.values[i]);
                }
            }
            mBracketing.currentValue = mBracketing.values[mBracketNum];
        } else {
            LOG1("Can't do bracketing with only one capture, disable bracketing!");
            mBracketing.mode = BRACKET_NONE;
        }
        break;
    case BRACKET_FOCUS:
        if (mBurstLength > 1) {
            status = mAAA->getAfLensPosRange(&lensRange);
            if (status == NO_ERROR) {
                status = mAAA->getCurrentFocusPosition(&currentFocusPos);
            }
            if (status == NO_ERROR) {
                status = mAAA->setAeMode(CAM_AE_MODE_MANUAL);
            }
            if (status == NO_ERROR) {
                mAAA->setAfMode(CAM_AF_MODE_MANUAL);
            }
            mBracketing.currentValue = lensRange.macro;
            mBracketing.minValue = lensRange.macro;
            mBracketing.maxValue = lensRange.infinity;
            mBracketing.step = (lensRange.infinity - lensRange.macro) / (mBurstLength - 1);
            mBracketing.values = NULL;
            // Initialize the current focus position and increment
            if (status == NO_ERROR) {
                /*
                 * For focus we need to bring the focus position
                 * to the initial position in the bracketing sequence.
                 */
                status = mAAA->getCurrentFocusPosition(&currentFocusPos);
                if (status == NO_ERROR) {
                    status = mAAA->setManualFocusIncrement(mBracketing.minValue - currentFocusPos);
                }
                if (status == NO_ERROR) {
                    status = mAAA->updateManualFocus();
                }
            }
            if (status == NO_ERROR) {
                LOG1("Initialized Focus Bracketing to: (min: %.2f, max:%.2f, step:%.2f)",
                        mBracketing.minValue,
                        mBracketing.maxValue,
                        mBracketing.step);
            }
        } else {
            LOG1("Can't do bracketing with only one capture, disable bracketing!");
            mBracketing.mode = BRACKET_NONE;
        }
        break;
    case BRACKET_NONE:
        // Do nothing here
        break;
    }

    // Enable Start-Of-Frame event
    mISP->enableFrameSyncEvent(true);

    // Allocate internal buffers for captured frames
    mSnapshotBufs = new AtomBuffer[length];
    mPostviewBufs = new AtomBuffer[length];

    return status;
}

status_t BracketManager::applyBracketing()
{
    LOG1("@%s: mode = %d", __FUNCTION__, mBracketing.mode);
    status_t status = NO_ERROR;

    if  (mFpsAdaptSkip > 0) {
        LOG1("Skipping %d burst frames", mFpsAdaptSkip);
        int doBracketNum = 0;
        int skipNum = 0;
        skipNum += mFpsAdaptSkip;
        if (mBracketing.mode == BRACKET_EXPOSURE && mFpsAdaptSkip >= 2) {
            // In Exposure Bracket, if mFpsAdaptSkip >= 2 apply bracketing every first skipped frame
            // This is because, exposure needs 2 frames for the exposure value to take effect
            doBracketNum += 1;
        } else if (mBracketing.mode == BRACKET_FOCUS && mFpsAdaptSkip >= 1) {
            // In Focus Bracket, if mFpsAdaptSkip >= 1 apply bracketing every first skipped frame
            // This is because focus needs only 1 frame for the focus position to take effect
            doBracketNum += 1;
        }
        if ((status = skipFrames(skipNum, doBracketNum)) != NO_ERROR) {
            LOGE("Error skipping burst frames!");
            return status;
        }
    }

    // If mFpsAdaptSkip < 2, apply exposure bracketing every real frame
    // If mFpsAdaptSkip < 1, apply focus bracketing every real frame
    if ((mFpsAdaptSkip < 2 && mBracketing.mode == BRACKET_EXPOSURE) ||
        (mFpsAdaptSkip < 1 && mBracketing.mode == BRACKET_FOCUS)) {

        if ((status = applyBracketingParams()) != NO_ERROR) {
            LOGE("Error applying bracketing params!");
            return status;
        }
    }

    status = mISP->getSnapshot(&mSnapshotBufs[mBurstCaptureNum], &mPostviewBufs[mBurstCaptureNum]);
    mBurstCaptureNum++;

    LOG1("@%s: Captured frame %d", __FUNCTION__, mBurstCaptureNum);
    if (mBurstCaptureNum == mBurstLength) {
        LOG1("@%s: All frames captured", __FUNCTION__);
        // Last setting applied, disable SOF event
        mISP->enableFrameSyncEvent(false);
        mState = STATE_CAPTURE;
    }

    return status;
}

status_t BracketManager::applyBracketingParams()
{
    LOG1("@%s: mode = %d", __FUNCTION__, mBracketing.mode);
    status_t status = NO_ERROR;
    int currentFocusPos;
    SensorAeConfig aeConfig;
    memset(&aeConfig, 0, sizeof(aeConfig));

    // Poll frame sync event
    mISP->pollFrameSyncEvent();

    switch (mBracketing.mode) {
    case BRACKET_EXPOSURE:
        if (mBracketNum < mBurstLength) {
            LOG1("Applying Exposure Bracketing: %.2f", mBracketing.currentValue);
            status = mAAA->applyEv(mBracketing.currentValue);
            if (status != NO_ERROR) {
                LOGE("Error applying exposure bracketing value EV = %.2f", mBracketing.currentValue);
                return status;
            }
            mAAA->getExposureInfo(aeConfig);
            aeConfig.evBias = mBracketing.currentValue;

            LOG1("Adding aeConfig to list (size=%d+1)", mBracketingParams.size());
            mBracketingParams.push_front(aeConfig);

            mBracketNum++;
            if (mBracketNum < mBurstLength) {
                mBracketing.currentValue = mBracketing.values[mBracketNum];
                LOG1("@%s: setting next exposure value = %.2f", __FUNCTION__, mBracketing.values[mBracketNum]);
            }
        }
        break;
    case BRACKET_FOCUS:
        if (mBracketing.currentValue + mBracketing.step <= mBracketing.maxValue) {
            status = mAAA->setManualFocusIncrement(mBracketing.step);
        }
        if (status == NO_ERROR) {
            mBracketing.currentValue += mBracketing.step;
            status = mAAA->updateManualFocus();
            mAAA->getCurrentFocusPosition(&currentFocusPos);
            LOG1("Applying Focus Bracketing: %d", currentFocusPos);
        }
        break;
    case BRACKET_NONE:
        // Do nothing here
        break;
    }

    return status;
}

void BracketManager::setBracketMode(BracketingMode mode)
{
    LOG1("@%s", __FUNCTION__);
    mBracketing.mode = mode;
}

BracketingMode BracketManager::getBracketMode()
{
    LOG1("@%s", __FUNCTION__);
    return mBracketing.mode;
}

void BracketManager::getNextAeConfig(SensorAeConfig *aeConfig) {
    LOG1("@%s", __FUNCTION__);

    if (!mBracketingParams.empty()) {
        LOG1("Popping sensorAeConfig from list (size=%d-1)", mBracketingParams.size());
        if (aeConfig)
            *aeConfig = *(--mBracketingParams.end());
        mBracketingParams.erase(--mBracketingParams.end());
    }
}

bool BracketManager::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;

    while (mThreadRunning) {
        switch (mState) {

        case STATE_STOPPED:
        case STATE_CAPTURE:
            LOG2("In %s...", (mState == STATE_STOPPED) ? "STATE_STOPPED" : "STATE_CAPTURE");
            // in the stop/capture state all we do is wait for messages
            status = waitForAndExecuteMessage();
            break;

        case STATE_BRACKETING:
            LOG2("In STATE_BRACKETING...");
            // Check if snapshot is requested and if we already have some available
            if (!mMessageQueue.isEmpty() && mBurstCaptureNum >= mSnapshotReqNum) {
                status = waitForAndExecuteMessage();
            } else {
                status = applyBracketing();
            }
            break;

        default:
            break;
        };
    }

    return false;
}

status_t BracketManager::startBracketing()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_START_BRACKETING;

    // skip initial frames
    int doBracketNum = 0;
    int skipNum = 0;
    if (mBracketing.mode == BRACKET_EXPOSURE && mFpsAdaptSkip < 2) {
        /*
         *  If we are in Exposure Bracketing, and mFpsAdaptSkip < 2, we need to
         *  skip some initial frames and apply bracketing (explanation above):
         *  2 frames for mFpsAdaptSkip == 0
         *  1 frame  for mFpsAdaptSkip == 1
         */
         skipNum += 2 - mFpsAdaptSkip;
         doBracketNum += 2 - mFpsAdaptSkip;
    } else if (mBracketing.mode == BRACKET_FOCUS && mFpsAdaptSkip < 1) {
        /*
         *  If we are in Focus Bracketing, and mFpsAdaptSkip < 1, we need to
         *  skip one initial frame w/o apply bracketing so that the focus will be
         *  positioned in the initial position.
         */
        skipNum += 1;
    }
    if (skipNum > 0) {
        if ((status = skipFrames(skipNum, doBracketNum)) != NO_ERROR) {
            LOGE("@%s: Error skipping initial frames!", __FUNCTION__);
            return status;
        }
    }
    status = mMessageQueue.send(&msg, MESSAGE_ID_START_BRACKETING);
    return status;
}

status_t BracketManager::handleMessageStartBracketing()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mState = STATE_BRACKETING;
    mMessageQueue.reply(MESSAGE_ID_START_BRACKETING, status);
    return status;
}

status_t BracketManager::stopBracketing()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_STOP_BRACKETING;
    status = mMessageQueue.send(&msg, MESSAGE_ID_STOP_BRACKETING);
    return status;
}

status_t BracketManager::handleMessageStopBracketing()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mState = STATE_STOPPED;
    delete[] mSnapshotBufs;
    delete[] mPostviewBufs;
    delete[] mBracketing.values;
    // disable SOF event
    mISP->enableFrameSyncEvent(false);

    mMessageQueue.reply(MESSAGE_ID_STOP_BRACKETING, status);
    return status;
}

status_t BracketManager::getSnapshot(AtomBuffer &snapshotBuf, AtomBuffer &postviewBuf)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;

    msg.id = MESSAGE_ID_GET_SNAPSHOT;
    if ((status = mMessageQueue.send(&msg, MESSAGE_ID_GET_SNAPSHOT)) != NO_ERROR) {
        return status;
    }

    snapshotBuf = mSnapshotBufs[mSnapshotReqNum];
    postviewBuf = mPostviewBufs[mSnapshotReqNum];
    mSnapshotReqNum++;
    LOG1("@%s: grabbing snapshot %d / %d (%d frames captured)", __FUNCTION__, mSnapshotReqNum, mBurstLength, mBurstCaptureNum);

    return status;
}

status_t BracketManager::handleMessageGetSnapshot()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mState != STATE_CAPTURE && mState != STATE_BRACKETING) {
        LOGE("@%s: wrong state (%d)", __FUNCTION__, mState);
        status = INVALID_OPERATION;
    }

    mMessageQueue.reply(MESSAGE_ID_GET_SNAPSHOT, status);
    return status;
}

status_t BracketManager::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id)
    {
        case MESSAGE_ID_EXIT:
            status = handleExit();
            break;
        case MESSAGE_ID_START_BRACKETING:
            status = handleMessageStartBracketing();
            break;
        case MESSAGE_ID_STOP_BRACKETING:
            status = handleMessageStopBracketing();
            break;
        case MESSAGE_ID_GET_SNAPSHOT:
            status = handleMessageGetSnapshot();
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

status_t BracketManager::handleExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

status_t BracketManager::requestExitAndWait()
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


} // namespace android
