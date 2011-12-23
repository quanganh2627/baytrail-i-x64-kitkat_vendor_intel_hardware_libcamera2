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

#ifndef ANDROID_LIBCAMERA_ATOM_ISP
#define ANDROID_LIBCAMERA_ATOM_ISP

#include <utils/Timers.h>
#include <utils/Errors.h>
#include <utils/Vector.h>
#include <utils/Errors.h>
#include <utils/threads.h>
#include <CameraParameters.h>
#include <camera.h>
#include "AtomCommon.h"

namespace android {

class Callbacks;

class AtomISP {

// public types
public:

    enum Mode {
        MODE_NONE,
        MODE_PREVIEW_STILL,
        MODE_PREVIEW_VIDEO,
    };

    struct Config {

        // preview
        int previewWidth;
        int previewHeight;
        const char *previewFormat; // see CameraParameters.h

        // recording
        int recordingWidth;
        int recordingHeight;
        const char *recordingFormat; // see CameraParameters.h

        // preview/recording (shared)
        int fps;

        // snapshot
        // TODO: add snapshot params
    };

// constructor/destructor
public:
    AtomISP();
    ~AtomISP();

// public methods
public:

    void setCallbacks(Callbacks *callbacks);

    status_t setConfig(Config *config);

    status_t start(Mode mode);
    status_t stop();

    status_t getPreviewFrame(AtomBuffer **buff);
    status_t putPreviewFrame(AtomBuffer *buff);

    status_t getRecordingFrame(AtomBuffer **buff, nsecs_t *timestamp);
    status_t putRecordingFrame(void *buff);

    // camera hardware information
    static int getNumberOfCameras();
    static status_t getCameraInfo(int cameraId, camera_info *cameraInfo);

// private methods
private:

    void allocatePreviewBuffers();
    void allocateRecordingBuffers();
    void freePreviewBuffers();
    void freeRecordingBuffers();
    AtomBuffer *findBuffer(AtomBuffer buffers[],
                           int numBuffers,
                           void *findMe);

// private members
private:

    static const int MAX_CAMERAS = 2;
    static const camera_info mCameraInfo[MAX_CAMERAS];

    Mode mMode;
    Callbacks *mCallbacks;
    AtomBuffer mPreviewBuffers[ATOM_PREVIEW_BUFFERS];
    AtomBuffer mRecordingBuffers[ATOM_RECORDING_BUFFERS];
    Config mConfig;

    int mPreviewCount;
    int mRecordingCount;

}; // class AtomISP

}; // namespace android

#endif // ANDROID_LIBCAMERA_ATOM_ISP
