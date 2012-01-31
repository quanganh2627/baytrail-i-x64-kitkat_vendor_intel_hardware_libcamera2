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
#include "AtomCommon.h"

namespace android {

struct FileInput {
    char *name;
    unsigned int width;
    unsigned int height;
    unsigned int size;
    int format;
    int bayer_order;
    char *mapped_addr;
};

//v4l2 buffer in pool
struct v4l2_buffer_info {
    void *data;
    size_t length;
    int width;
    int height;
    int format;
    int flags; //You can use to to detern the buf status
    struct v4l2_buffer vbuffer;
};

struct v4l2_buffer_pool {
    int active_buffers;
    int width;
    int height;
    int format;
    struct v4l2_buffer_info bufs [MAX_V4L2_BUFFERS];
};

class Callbacks;

class AtomISP {

// public types
public:

    enum Mode {
        MODE_NONE = -1,
        MODE_PREVIEW = 0,
        MODE_CAPTURE = 1,
        MODE_VIDEO = 2,
    };

    struct Config {
        int cameraId;         // ID of the selected camera
        FrameInfo preview;    // preview
        FrameInfo postview;   // postview (thumbnail for capture)
        FrameInfo recording;  // recording
        int       fps;        // preview/recording (shared)
        FrameInfo snapshot;   // snapshot
        int num_snapshot;     // number of snapshots to take
        int zoom;             // zoom value
    };

// constructor/destructor
public:
    AtomISP(int camera_id);
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
    status_t putRecordingFrame(AtomBuffer *buff);

    status_t getSnapshot(AtomBuffer **snaphotBuf, AtomBuffer **postviewBuf);
    status_t putSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf);

    bool dataAvailable();

    status_t setPreviewFrameFormat(int width, int height, int format);
    FrameInfo getPreviewFrameFormat();

    status_t setPostviewFrameFormat(int width, int height, int format);
    FrameInfo getPostviewFrameFormat();

    status_t setSnapshotFrameFormat(int width, int height, int format);
    FrameInfo getSnapshotFrameFormat();

    status_t setVideoFrameFormat(int width, int height, int format);
    FrameInfo getVideoFrameFormat();

    status_t setSnapshotNum(int num);

    int getMaxSnapshotSize(int *width, int *height);
    const char* getMaxSnapShotResolution();

    status_t setZoom(int zoom);

    // camera hardware information
    static int getNumberOfCameras();
    static status_t getCameraInfo(int cameraId, camera_info *cameraInfo);

// private methods
private:

    status_t startPreview();
    status_t stopPreview();
    status_t startRecording();
    status_t stopRecording();
    status_t startCapture();
    status_t stopCapture();

    status_t allocatePreviewBuffers();
    status_t allocateRecordingBuffers();
    status_t allocateSnapshotBuffers();
    status_t freePreviewBuffers();
    status_t freeRecordingBuffers();
    status_t freeSnapshotBuffers();

    int  openDevice(int device);
    void closeDevice(int device);
    status_t v4l2_capture_open(int device);
    status_t v4l2_capture_close(int fd);
    status_t v4l2_capture_querycap(int device, struct v4l2_capability *cap);
    status_t v4l2_capture_s_input(int fd, int index);
    int detectDeviceResolutions();
    int atomisp_set_capture_mode(int deviceMode);
    int v4l2_capture_try_format(int device, int *w, int *h, int *format);
    int configureDevice(int device, int deviceMode, int w, int h, int format, bool raw);
    int v4l2_capture_g_framerate(int fd, float * framerate, int width,
                                          int height, int pix_fmt);
    int v4l2_capture_s_format(int fd, int device, int w, int h, int format, bool raw);
    void stopDevice(int device);
    int v4l2_capture_streamoff(int fd);
    void destroyBufferPool(int device);
    int v4l2_capture_free_buffer(int device, struct v4l2_buffer_info *buf_info);
    int v4l2_capture_release_buffers(int device);
    int v4l2_capture_request_buffers(int device, uint num_buffers);
    int startDevice(int device, int buffer_count);
    int createBufferPool(int device, int buffer_count);
    int v4l2_capture_new_buffer(int device, int index, struct v4l2_buffer_info *buf);
    int activateBufferPool(int device);
    int v4l2_capture_streamon(int fd);
    int v4l2_capture_qbuf(int fd, int index, struct v4l2_buffer_info *buf);
    int grabFrame(int device, struct v4l2_buffer *buf);
    int v4l2_capture_dqbuf(int fd, struct v4l2_buffer *buf);
    int atomisp_set_attribute (int fd, int attribute_num,
                               const int value, const char *name);
    int  atomisp_set_zoom (int fd, int zoom);
// private members
private:

    static const camera_info mCameraInfo[MAX_CAMERAS];

    Mode mMode;
    Callbacks *mCallbacks;
    AtomBuffer mPreviewBuffers[NUM_ATOM_BUFFERS];
    AtomBuffer mRecordingBuffers[NUM_ATOM_BUFFERS];
    AtomBuffer mSnapshotBuffers[SNAPSHOT_MAX_NUM_BUFFERS];
    AtomBuffer mPostviewBuffers[SNAPSHOT_MAX_NUM_BUFFERS];
    int mNumPreviewBuffersQueued;
    int mNumRecordingBuffersQueued;
    Config mConfig;

    int video_fds[V4L2_DEVICE_NUM];

    struct v4l2_capability cap;
    struct v4l2_buffer_pool v4l2_buf_pool[V4L2_DEVICE_NUM]; //pool[0] for device0 pool[1] for device1

    float framerate;

    int mIspTimeout;
    struct FileInput mFileImage;

    int mPreviewDevice;
    int mRecordingDevice;

    int mSessionId; // uniquely identify each session

    Vector<FrameSize> mSupportedSnapshotSizes;

}; // class AtomISP

}; // namespace android

#endif // ANDROID_LIBCAMERA_ATOM_ISP
