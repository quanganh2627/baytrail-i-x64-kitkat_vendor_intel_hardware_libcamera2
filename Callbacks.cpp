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

#define LOG_TAG "Atom_Callbacks"

#include "LogHelper.h"
#include "Callbacks.h"

namespace android {

Callbacks::Callbacks() :
    mNotifyCB(NULL)
    ,mDataCB(NULL)
    ,mDataCBTimestamp(NULL)
    ,mGetMemoryCB(NULL)
    ,mUserToken(NULL)
{
    LogEntry(LOG_TAG, __FUNCTION__);
}

Callbacks::~Callbacks()
{
    LogEntry(LOG_TAG, __FUNCTION__);
}

void Callbacks::setCallbacks(camera_notify_callback notify_cb,
                             camera_data_callback data_cb,
                             camera_data_timestamp_callback data_cb_timestamp,
                             camera_request_memory get_memory,
                             void* user)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    mNotifyCB = notify_cb;
    mDataCB = data_cb;
    mDataCBTimestamp = data_cb_timestamp;
    mGetMemoryCB = get_memory;
    mUserToken = user;
}

void Callbacks::enableMsgType(int32_t msgType)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("msgType = %d", msgType);
    mMessageFlags |= msgType;
}

void Callbacks::disableMsgType(int32_t msgType)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("msgType = %d", msgType);
    mMessageFlags &= ~msgType;
}

bool Callbacks::msgTypeEnabled(int32_t msgType)
{
    return (mMessageFlags & msgType) != 0;
}

void Callbacks::previewFrameDone(AtomBuffer *buff)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if ((mMessageFlags & CAMERA_MSG_PREVIEW_FRAME) && mDataCB != NULL) {
        // TODO: may need to make a memcpy
        LogDetail("Sending message: CAMERA_MSG_PREVIEW_FRAME");
        mDataCB(CAMERA_MSG_PREVIEW_FRAME, buff->buff, 0, NULL, mUserToken);
    }
}

void Callbacks::videoFrameDone(AtomBuffer *buff, nsecs_t timestamp)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if ((mMessageFlags & CAMERA_MSG_VIDEO_FRAME) && mDataCBTimestamp != NULL) {
        LogDetail("Sending message: CAMERA_MSG_VIDEO_FRAME");
        mDataCBTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, buff->buff, 0, mUserToken);
    }
}

void Callbacks::cameraError(int err)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if ((mMessageFlags & CAMERA_MSG_ERROR) && mNotifyCB != NULL) {
        LogDetail("Sending message: CAMERA_MSG_ERROR");
        mNotifyCB(CAMERA_MSG_ERROR, err, 0, mUserToken);
    }
}

void Callbacks::allocateMemory(AtomBuffer *buff, int size)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    buff->buff = NULL;
    if (mGetMemoryCB != NULL)
        buff->buff = mGetMemoryCB(-1, size, 1, mUserToken);
}

void Callbacks::autofocusDone(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mMessageFlags & CAMERA_MSG_FOCUS)
        mNotifyCB(CAMERA_MSG_FOCUS, 1, 0, mUserToken);
}
};
