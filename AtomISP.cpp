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
#define LOG_TAG "Atom_ISP"

#include "AtomISP.h"
#include <utils/Log.h>
#include "Callbacks.h"

namespace android {

////////////////////////////////////////////////////////////////////
//                          STATIC DATA
////////////////////////////////////////////////////////////////////

const camera_info AtomISP::mCameraInfo[MAX_CAMERAS] = {
    {
        CAMERA_FACING_FRONT,
        180,
    },
    {
        CAMERA_FACING_BACK,
        0,
    },
};

////////////////////////////////////////////////////////////////////
//                          PUBLIC METHODS
////////////////////////////////////////////////////////////////////

AtomISP::AtomISP() :
    mMode(MODE_NONE)
    ,mCallbacks(NULL)
    ,mPreviewCount(0)
    ,mRecordingCount(0)
{
    // TODO: initialize class (if needed)

    mConfig.previewWidth = 640;
    mConfig.previewHeight = 480;
    mConfig.previewFormat = CameraParameters::PIXEL_FORMAT_YUV420SP;

    mConfig.recordingWidth = 640;
    mConfig.recordingHeight = 480;
    mConfig.recordingFormat = CameraParameters::PIXEL_FORMAT_YUV420SP;

    mConfig.fps = 30;
}

AtomISP::~AtomISP()
{
    // TODO: deinit class/kernel driver (if needed)
}

void AtomISP::setCallbacks(Callbacks *callbacks)
{
    mCallbacks = callbacks;
}

status_t AtomISP::setConfig(Config *config)
{
    // TODO: validate mode
    status_t status = NO_ERROR;
    mConfig = *config;
    return status;
}

status_t AtomISP::start(Mode mode)
{
    status_t status = NO_ERROR;
    int i;

    allocatePreviewBuffers();
    allocateRecordingBuffers();

    // TODO: start driver

    mMode = mode;

    return status;
}

status_t AtomISP::stop()
{
    status_t status = NO_ERROR;
    // TODO: stop driver

    mPreviewCount = 0;
    mRecordingCount = 0;

    freePreviewBuffers();
    freeRecordingBuffers();

    mMode = MODE_NONE;

    return status;
}

status_t AtomISP::getPreviewFrame(AtomBuffer **buff)
{
    if (mMode == MODE_NONE)
        return INVALID_OPERATION;

    // TODO: dequeue frame from corresponding camera device

    (*buff)->id = mPreviewCount++;

    return NO_ERROR;
}

status_t AtomISP::putPreviewFrame(AtomBuffer *buff)
{
    if (mMode == MODE_NONE)
        return INVALID_OPERATION;

    // TODO: queue frame

    return NO_ERROR;
}

status_t AtomISP::getRecordingFrame(AtomBuffer **buff, nsecs_t *timestamp)
{
    if (mMode != MODE_PREVIEW_VIDEO)
        return INVALID_OPERATION;

    // TODO: dequeue frame from device

    (*buff)->id = mRecordingCount++;
    *timestamp = systemTime();

    return NO_ERROR;
}

status_t AtomISP::putRecordingFrame(void *buff)
{
    AtomBuffer *abuff = findBuffer(mRecordingBuffers,
                                   ATOM_RECORDING_BUFFERS,
                                   buff);

    if (!abuff)
        return UNKNOWN_ERROR;

    // TODO: queue frame to device

    return NO_ERROR;
}

////////////////////////////////////////////////////////////////////
//                          PRIVATE METHODS
////////////////////////////////////////////////////////////////////

void AtomISP::allocatePreviewBuffers()
{
    int size = mConfig.previewWidth * mConfig.previewHeight * 3 / 2;
    for (int i = 0; i < ATOM_PREVIEW_BUFFERS; i++)
         mCallbacks->allocateMemory(&mPreviewBuffers[i], size);
}

void AtomISP::allocateRecordingBuffers()
{
    int size = mConfig.recordingWidth * mConfig.recordingHeight * 3 / 2;
    for (int i = 0; i < ATOM_RECORDING_BUFFERS; i++)
        mCallbacks->allocateMemory(&mRecordingBuffers[i], size);
}

void AtomISP::freePreviewBuffers()
{
    for (int i = 0 ; i < ATOM_PREVIEW_BUFFERS; i++) {
        mPreviewBuffers[i].buff->release(mPreviewBuffers[i].buff);
        mPreviewBuffers[i].buff = NULL;
    }
}

void AtomISP::freeRecordingBuffers()
{
    for (int i = 0 ; i < ATOM_RECORDING_BUFFERS; i++) {
        mRecordingBuffers[i].buff->release(mRecordingBuffers[i].buff);
        mRecordingBuffers[i].buff = NULL;
    }
}

AtomBuffer *AtomISP::findBuffer(AtomBuffer buffers[],
                                int numBuffers,
                                void *findMe)
{
    for (int i = 0; i < numBuffers; i++) {
        if (buffers[i].buff->data == findMe)
            return &buffers[i];
    }
    return NULL;
}

int AtomISP::getNumberOfCameras()
{
    // TODO: implement
    return MAX_CAMERAS;
}

status_t AtomISP::getCameraInfo(int cameraId, camera_info *cameraInfo)
{
    if (cameraId >= MAX_CAMERAS)
        return BAD_VALUE;

    memcpy(cameraInfo, &mCameraInfo[cameraId], sizeof(camera_info));
    return NO_ERROR;
}

} // namespace android
