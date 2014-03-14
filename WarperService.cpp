/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2011,2012,2013,2014 Intel Corporation
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

#define LOG_TAG "Camera_WarperService"

#include "WarperService.h"
#include "LogHelper.h"

namespace android {

WarperService::WarperService() :
        Thread(false)
        ,mMessageQueue("WarperService", MESSAGE_ID_MAX)
        ,mThreadRunning(false)
        ,mGPUWarper(NULL)
{
    LOG1("@%s", __FUNCTION__);
}

WarperService::~WarperService() {
    LOG1("@%s", __FUNCTION__);
    delete mGPUWarper;
    mGPUWarper = NULL;
}

bool WarperService::threadLoop() {
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning) {
        status = waitForAndExecuteMessage();
    }

    return false;
}

status_t WarperService::warpBackFrame(AtomBuffer *frame, double projective[PROJ_MTRX_DIM][PROJ_MTRX_DIM]) {
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_WARP_BACK_FRAME;
    if (frame) {
        msg.data.messageWarpBackFrame.frame = frame;
    } else {
        LOGE("Can not access frame data.");
        return INVALID_OPERATION;
    }
    if (projective) {
        for (int i = 0; i < PROJ_MTRX_DIM; i++) {
            for (int j = 0; j < PROJ_MTRX_DIM; j++) {
                msg.data.messageWarpBackFrame.projective[i][j] = projective[i][j];
            }
        }
    } else {
        LOGE("Projective matrix is not initialized.");
        return INVALID_OPERATION;
    }
    return mMessageQueue.send(&msg, MESSAGE_ID_WARP_BACK_FRAME);
}

status_t WarperService::handleMessageWarpBackFrame(MessageWarpBackFrame &msg) {
    LOG2("@%s", __FUNCTION__);

    status_t status;

    if (mGPUWarper == NULL) {
        mGPUWarper = new GPUWarper(msg.frame->width, msg.frame->height, 64);
        if (mGPUWarper == NULL) {
            LOGE("Failed to create GPUWarper");
            mMessageQueue.reply(MESSAGE_ID_WARP_BACK_FRAME, UNKNOWN_ERROR);
            return NO_MEMORY;
        }

        status = mGPUWarper->init();
        if (status != NO_ERROR) {
            mMessageQueue.reply(MESSAGE_ID_WARP_BACK_FRAME, status);
            return status;
        }

    }

    status = mGPUWarper->warpBackFrame(msg.frame, msg.projective);
    if (status != NO_ERROR) {
        mMessageQueue.reply(MESSAGE_ID_WARP_BACK_FRAME, status);
        return status;
    }

    mMessageQueue.reply(MESSAGE_ID_WARP_BACK_FRAME, OK);
    return OK;
}

status_t WarperService::waitForAndExecuteMessage() {
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {
    case MESSAGE_ID_EXIT:
        status = handleMessageExit();
        break;
    case MESSAGE_ID_WARP_BACK_FRAME:
        status = handleMessageWarpBackFrame(msg.data.messageWarpBackFrame);
        break;
    default:
        status = BAD_VALUE;
        break;
    };
    return status;
}

status_t WarperService::handleMessageExit() {
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;

    delete mGPUWarper;
    mGPUWarper = NULL;

    return status;
}

status_t WarperService::requestExitAndWait() {
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_EXIT;

    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

} /* namespace android */
