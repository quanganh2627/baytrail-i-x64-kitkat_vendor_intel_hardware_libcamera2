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

#include "LogHelper.h"
#include "AtomISP.h"
#include "Callbacks.h"
#include "ColorConverter.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>

#define CLEAR(x) memset (&(x), 0, sizeof (x))
#define main_fd video_fds[V4L2_FIRST_DEVICE]

#define DEFAULT_SENSOR_FPS      15.0

#define RESOLUTION_14MP_WIDTH   4352
#define RESOLUTION_14MP_HEIGHT  3264
#define RESOLUTION_8MP_WIDTH    3264
#define RESOLUTION_8MP_HEIGHT   2448
#define RESOLUTION_5MP_WIDTH    2560
#define RESOLUTION_5MP_HEIGHT   1920
#define RESOLUTION_1080P_WIDTH  1920
#define RESOLUTION_1080P_HEIGHT 1080
#define RESOLUTION_720P_WIDTH   1280
#define RESOLUTION_720P_HEIGHT  720
#define RESOLUTION_480P_WIDTH   768
#define RESOLUTION_480P_HEIGHT  480
#define RESOLUTION_VGA_WIDTH    640
#define RESOLUTION_VGA_HEIGHT   480
#define RESOLUTION_POSTVIEW_WIDTH    320
#define RESOLUTION_POSTVIEW_HEIGHT   240

#define RESOLUTION_14MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1080,2048x1536,2560x1920,3264x2448,3648x2736,4096x3072,4352x3264"

#define RESOLUTION_8MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1080,2048x1536,2560x1920,3264x2448"

#define RESOLUTION_5MP_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1080,2048x1536,2560x1920"

#define RESOLUTION_1080P_TABLE   \
        "320x240,640x480,1024x768,1280x720,1920x1080"

#define RESOLUTION_720P_TABLE   \
        "320x240,640x480,1280x720,1280x960"

#define RESOLUTION_VGA_TABLE   \
        "320x240,640x480"

#define MAX_BACK_CAMERA_PREVIEW_WIDTH   1280
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT  720
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH  4352
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT 3264
#define MAX_BACK_CAMERA_VIDEO_WIDTH   1920
#define MAX_BACK_CAMERA_VIDEO_HEIGHT  1080

#define MAX_FRONT_CAMERA_PREVIEW_WIDTH  1280
#define MAX_FRONT_CAMERA_PREVIEW_HEIGHT 720
#define MAX_FRONT_CAMERA_SNAPSHOT_WIDTH 1920
#define MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT    1080
#define MAX_FRONT_CAMERA_VIDEO_WIDTH   1920
#define MAX_FRONT_CAMERA_VIDEO_HEIGHT  1080

#define ATOMISP_POLL_TIMEOUT (3 * 1000)
#define ATOMISP_FILEINPUT_POLL_TIMEOUT (20 * 1000)

namespace android {

////////////////////////////////////////////////////////////////////
//                          STATIC DATA
////////////////////////////////////////////////////////////////////

static const char *dev_name_array[3] = {"/dev/video0",
                                  "/dev/video1",
                                  "/dev/video2"};

AtomISP::cameraInfo AtomISP::camInfo[MAX_CAMERAS];
int AtomISP::numCameras = 0;

const camera_info AtomISP::mCameraInfo[MAX_CAMERAS] = {
    {
        CAMERA_FACING_BACK,
        180,
    },
    {
        CAMERA_FACING_FRONT,
        180,
    }
};

static const char *resolution_tables[] = {
    RESOLUTION_VGA_TABLE,
    RESOLUTION_720P_TABLE,
    RESOLUTION_1080P_TABLE,
    RESOLUTION_5MP_TABLE,
    RESOLUTION_8MP_TABLE,
    RESOLUTION_14MP_TABLE
};

////////////////////////////////////////////////////////////////////
//                          PUBLIC METHODS
////////////////////////////////////////////////////////////////////

AtomISP::AtomISP(int camera_id) :
    mMode(MODE_NONE)
    ,mCallbacks(NULL)
    ,mNumBuffers(NUM_DEFAULT_BUFFERS)
    ,mPreviewBuffers(NULL)
    ,mRecordingBuffers(NULL)
    ,mClientRecordingBuffers(NULL)
    ,mUsingClientRecordingBuffers(false)
    ,mNumPreviewBuffersQueued(0)
    ,mNumRecordingBuffersQueued(0)
    ,mPreviewDevice(V4L2_FIRST_DEVICE)
    ,mRecordingDevice(V4L2_FIRST_DEVICE)
    ,mSessionId(0)
    ,mCameraId(0)
{
    LOG_FUNCTION
    int camera_idx = -1;

    video_fds[V4L2_FIRST_DEVICE] = -1;
    video_fds[V4L2_SECOND_DEVICE] = -1;
    video_fds[V4L2_THIRD_DEVICE] = -1;

    mConfig.fps = 30;
    mConfig.num_snapshot = 1;
    mConfig.zoom = 0;

    /* The back facing camera is assumed to be the high resolution camera which
     * uses the primary MIPI CSI2 port. */
    for (int i = 0; i < getNumberOfCameras(); i++) {
        if ((camera_id == CAMERA_FACING_BACK  && camInfo[i].port == ATOMISP_CAMERA_PORT_PRIMARY) ||
                (camera_id == CAMERA_FACING_FRONT && camInfo[i].port == ATOMISP_CAMERA_PORT_SECONDARY)) {
            camera_idx = i;
            break;
        }
    }
    if (camera_idx == -1) {
        LogError("Didn't find %s camera. Using default camera!",
                camera_id == CAMERA_FACING_BACK ? "back" : "front");
        camera_idx = 0;
    }
    mCameraId = camera_idx;
    // Open the main device first
    int ret = openDevice(V4L2_FIRST_DEVICE);
    if (ret < 0) {
        LogError("Failed to open first device!");
        return;
    }

    ret = detectDeviceResolutions();
    if (ret) {
        LogError("Failed to detect camera %d, resolution! Use default settings", camera_id);
        switch (camera_id) {
            case CAMERA_FACING_FRONT:
                mConfig.snapshot.maxWidth  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
                mConfig.snapshot.maxHeight = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
                break;
            case CAMERA_FACING_BACK:
                mConfig.snapshot.maxWidth  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
                mConfig.snapshot.maxHeight = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
                break;
        }
    }
    else {
        LogDetail("Camera %d: Max-resolution detected: %dx%d", camera_id,
                mConfig.snapshot.maxWidth,
                mConfig.snapshot.maxHeight);
    }

    switch (camera_id) {
        case CAMERA_FACING_FRONT:
            mConfig.preview.maxWidth   = MAX_FRONT_CAMERA_PREVIEW_WIDTH;
            mConfig.preview.maxHeight  = MAX_FRONT_CAMERA_PREVIEW_HEIGHT;
            mConfig.recording.maxWidth = MAX_FRONT_CAMERA_VIDEO_WIDTH;
            mConfig.recording.maxHeight = MAX_FRONT_CAMERA_VIDEO_HEIGHT;
            break;
        case CAMERA_FACING_BACK:
            mConfig.preview.maxWidth   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
            mConfig.preview.maxHeight  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
            mConfig.recording.maxWidth = MAX_BACK_CAMERA_VIDEO_WIDTH;
            mConfig.recording.maxHeight = MAX_BACK_CAMERA_VIDEO_HEIGHT;
            break;
        default:
            LogError("Invalid camera id: %d", camera_id);
    }

    // Initialize the frame sizes
    setPreviewFrameFormat(RESOLUTION_VGA_WIDTH, RESOLUTION_VGA_HEIGHT, V4L2_PIX_FMT_NV12);
    setPostviewFrameFormat(RESOLUTION_POSTVIEW_WIDTH, RESOLUTION_POSTVIEW_HEIGHT, V4L2_PIX_FMT_NV12);
    setSnapshotFrameFormat(RESOLUTION_5MP_WIDTH, RESOLUTION_5MP_HEIGHT, V4L2_PIX_FMT_NV12);
    setVideoFrameFormat(RESOLUTION_VGA_WIDTH, RESOLUTION_VGA_HEIGHT, V4L2_PIX_FMT_NV12);

    mIspTimeout = 0;

    closeDevice(V4L2_FIRST_DEVICE);
}

AtomISP::~AtomISP()
{
    LOG_FUNCTION
}

void AtomISP::setCallbacks(Callbacks *callbacks)
{
    LOG_FUNCTION
    mCallbacks = callbacks;
}

void AtomISP::getDefaultParameters(CameraParameters *params)
{
    LOG_FUNCTION2
    if (!params) {
        LogError("params is null!");
        return;
    }

    /**
     * PREVIEW
     */
    params->setPreviewSize(mConfig.preview.width, mConfig.preview.height);
    params->setPreviewFrameRate(30);
    params->setPreviewFormat(cameraParametersFormat(mConfig.preview.format));
    char previewFormats[100] = {0};
    if (snprintf(previewFormats, sizeof(previewFormats),
            "%s%s",
            cameraParametersFormat(V4L2_PIX_FMT_NV12),
            cameraParametersFormat(V4L2_PIX_FMT_YUV420)) < 0) {
        LogError("Could not generate preview formats string: %s", strerror(errno));
        return;
    }
    params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, previewFormats);
    params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, "320x240,640x360,640x480,1280x720");

    params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,"30,15,10");
    params->set(CameraParameters::KEY_PREVIEW_FPS_RANGE,"10500,30304");
    params->set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,"(10500,30304),(11000,30304),(11500,30304)");

    /**
     * RECORDING
     */
    params->setVideoSize(mConfig.recording.width, mConfig.recording.height);
    params->set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO, "640x480");
    params->set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES, "320x240,640x480,1280x720,1920x1080");
    params->set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
            cameraParametersFormat(V4L2_PIX_FMT_NV12));

    /**
     * SNAPSHOT
     */
    const char *picSizes = getMaxSnapShotResolution();
    params->set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, picSizes);
    params->setPictureSize(mConfig.snapshot.width, mConfig.snapshot.height);
    params->set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH,"320");
    params->set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,"240");
    params->set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,"640x480,512x384,320x240,0x0");

    /**
     * ZOOM
     */
    params->set(CameraParameters::KEY_ZOOM, 0);
    params->set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
    getZoomRatios(MODE_PREVIEW, params);

    /**
     * FOCUS
     */
    if (mCameraId == CAMERA_FACING_BACK) {
        // For main back camera
        // flash mode option
        params->set(CameraParameters::KEY_FLASH_MODE,"off");
        params->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,"auto,off,on,torch,slow-sync,day-sync");
    } else {
        // For front camera
        // No flash present
        params->set(CameraParameters::KEY_FLASH_MODE,"off");
        params->set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,"off");
    }
    params->set(CameraParameters::KEY_FOCUS_MODE, "auto");
    params->set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto");

    /**
     * MISCELLANEOUS
     */
    if(mCameraId == CAMERA_FACING_BACK)
        params->set(CameraParameters::KEY_FOCAL_LENGTH,"5.56");
    else
        params->set(CameraParameters::KEY_FOCAL_LENGTH,"2.78");

    params->set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE,"42.5");
    params->set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE,"54.8");
}

const char* AtomISP::getMaxSnapShotResolution()
{
    LOG_FUNCTION
    int index = RESOLUTION_14MP;

    if (mConfig.snapshot.maxWidth < RESOLUTION_14MP_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_14MP_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_8MP_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_8MP_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_5MP_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_5MP_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_1080P_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_1080P_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_720P_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_720P_HEIGHT)
            index--;
    if (mConfig.snapshot.maxWidth < RESOLUTION_VGA_WIDTH || mConfig.snapshot.maxHeight < RESOLUTION_VGA_HEIGHT)
            index--;
    if (index < 0)
            index = 0;

    return resolution_tables[index];
}

status_t AtomISP::start(Mode mode)
{
    LOG_FUNCTION
    LogDetail("mode = %d", mode);
    status_t status = NO_ERROR;

    switch (mode) {
    case MODE_PREVIEW:
        status = startPreview();
        break;

    case MODE_VIDEO:
        status = startRecording();
        break;

    case MODE_CAPTURE:
        status = startCapture();
        break;

    default:
        break;
    };

    if (status == NO_ERROR) {
        mMode = mode;
        mSessionId++;
    }

    return status;
}

status_t AtomISP::stop()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    switch (mMode) {
    case MODE_PREVIEW:
        status = stopPreview();
        break;

    case MODE_VIDEO:
        status = stopRecording();
        break;

    case MODE_CAPTURE:
        status = stopCapture();
        break;

    default:
        break;
    };

    if (status == NO_ERROR)
        mMode = MODE_NONE;

    return status;
}

status_t AtomISP::startPreview()
{
    LOG_FUNCTION
    int ret = 0;
    status_t status = NO_ERROR;

    mPreviewDevice = V4L2_FIRST_DEVICE;

    if ((status = allocatePreviewBuffers()) != NO_ERROR)
        return status;

    ret = openDevice(mPreviewDevice);
    if (ret < 0) {
        LogError("Open preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitFree;
    }

    ret = configureDevice(
            mPreviewDevice,
            CI_MODE_PREVIEW,
            mConfig.preview.padding,
            mConfig.preview.height,
            mConfig.preview.format,
            false);
    if (ret < 0) {
        LogError("Configure preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitClose;
    }

    // need to resend the current zoom value
    atomisp_set_zoom(main_fd, mConfig.zoom);

    ret = startDevice(mPreviewDevice, mNumBuffers);
    if (ret < 0) {
        LogError("Start preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitClose;
    }

    mNumPreviewBuffersQueued = mNumBuffers;

    return status;

exitClose:
    closeDevice(mPreviewDevice);
exitFree:
    freePreviewBuffers();
    return status;
}

status_t AtomISP::stopPreview()
{
    LOG_FUNCTION

    stopDevice(mPreviewDevice);
    closeDevice(mPreviewDevice);
    freePreviewBuffers();

    return NO_ERROR;
}

status_t AtomISP::startRecording() {
    LOG_FUNCTION
    int ret = 0;
    status_t status = NO_ERROR;

    mPreviewDevice = V4L2_SECOND_DEVICE;

    if ((status = allocateRecordingBuffers()) != NO_ERROR)
        return status;

    if ((status = allocatePreviewBuffers()) != NO_ERROR)
        goto exitFreeRec;

    ret = openDevice(mRecordingDevice);
    if (ret < 0) {
        LogError("Open recording device failed!");
        status = UNKNOWN_ERROR;
        goto exitFreePrev;
    }

    ret = openDevice(mPreviewDevice);
    if (ret < 0) {
        LogError("Open preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitCloseRec;
    }

    ret = configureDevice(
            mRecordingDevice,
            CI_MODE_VIDEO,
            mConfig.recording.padding,
            mConfig.recording.height,
            mConfig.recording.format,
            false);
    if (ret < 0) {
        LogError("Configure recording device failed!");
        status = UNKNOWN_ERROR;
        goto exitClosePrev;
    }

    ret = configureDevice(
            mPreviewDevice,
            CI_MODE_VIDEO,
            mConfig.preview.padding,
            mConfig.preview.height,
            mConfig.preview.format,
            false);
    if (ret < 0) {
        LogError("Configure recording device failed!");
        status = UNKNOWN_ERROR;
        goto exitClosePrev;
    }

    ret = startDevice(mRecordingDevice, mNumBuffers);
    if (ret < 0) {
        LogError("Start recording device failed");
        status = UNKNOWN_ERROR;
        goto exitClosePrev;
    }

    ret = startDevice(mPreviewDevice, mNumBuffers);
    if (ret < 0) {
        LogError("Start preview device failed!");
        status = UNKNOWN_ERROR;
        goto exitStopRec;
    }

    mNumPreviewBuffersQueued = mNumBuffers;
    mNumRecordingBuffersQueued = mNumBuffers;

    return status;

exitStopRec:
    stopDevice(mRecordingDevice);
exitClosePrev:
    closeDevice(mPreviewDevice);
exitCloseRec:
    closeDevice(mRecordingDevice);
exitFreePrev:
    freePreviewBuffers();
exitFreeRec:
    freeRecordingBuffers();
    return status;
}

status_t AtomISP::stopRecording()
{
    LOG_FUNCTION

    freeRecordingBuffers();
    stopDevice(mRecordingDevice);
    closeDevice(mRecordingDevice);

    freePreviewBuffers();
    stopDevice(mPreviewDevice);
    closeDevice(mPreviewDevice);

    return NO_ERROR;
}

status_t AtomISP::startCapture()
{
    LOG_FUNCTION
    int ret;
    status_t status = NO_ERROR;

    if ((status = allocateSnapshotBuffers()) != NO_ERROR)
        return status;

    ret = openDevice(V4L2_FIRST_DEVICE);
    if (ret < 0) {
        LogError("Open second device failed!");
        status = UNKNOWN_ERROR;
        goto errorFreeBuf;
    }
    ret = configureDevice(
            V4L2_FIRST_DEVICE,
            CI_MODE_STILL_CAPTURE,
            mConfig.snapshot.width,
            mConfig.snapshot.height,
            mConfig.snapshot.format,
            false);
    if (ret < 0) {
        LogError("configure first device failed!");
        status = UNKNOWN_ERROR;
        goto errorCloseFirst;
    }

    ret = openDevice(V4L2_SECOND_DEVICE);
    if (ret < 0) {
        LogError("Open second device failed!");
        status = UNKNOWN_ERROR;
        goto errorCloseFirst;
    }

    ret = configureDevice(
            V4L2_SECOND_DEVICE,
            CI_MODE_STILL_CAPTURE,
            mConfig.postview.width,
            mConfig.postview.height,
            mConfig.postview.format,
            false);
    if (ret < 0) {
        LogError("configure second device failed!");
        status = UNKNOWN_ERROR;
        goto errorCloseSecond;
    }

    // need to resend the current zoom value
    atomisp_set_zoom(main_fd, mConfig.zoom);

    ret = startDevice(V4L2_FIRST_DEVICE, mConfig.num_snapshot);
    if (ret < 0) {
        LogError("start capture on first device failed!");
        status = UNKNOWN_ERROR;
        goto errorCloseSecond;
    }

    ret = startDevice(V4L2_SECOND_DEVICE, mConfig.num_snapshot);
    if (ret < 0) {
        LogError("start capture on second device failed!");
        status = UNKNOWN_ERROR;
        goto errorStopFirst;
    }

    return status;

errorStopFirst:
    stopDevice(V4L2_FIRST_DEVICE);
errorCloseSecond:
    closeDevice(V4L2_SECOND_DEVICE);
errorCloseFirst:
    closeDevice(V4L2_FIRST_DEVICE);
errorFreeBuf:
    freeSnapshotBuffers();
    return status;
}

status_t AtomISP::stopCapture()
{
    LOG_FUNCTION
    stopDevice(V4L2_SECOND_DEVICE);
    stopDevice(V4L2_FIRST_DEVICE);
    closeDevice(V4L2_SECOND_DEVICE);
    closeDevice(V4L2_FIRST_DEVICE);
    freeSnapshotBuffers();
    return NO_ERROR;
}

int AtomISP::configureDevice(int device, int deviceMode, int w, int h, int format, bool raw)
{
    LOG_FUNCTION
    int ret = 0;
    LogDetail("device: %d, width:%d, height:%d, deviceMode:%d format:%d raw:%d", device,
        w, h, deviceMode, format, raw);

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LogError("Wrong device: %d", device);
        return -1;
    }

    if ((w <= 0) || (h <= 0)) {
        LogError("Wrong Width %d or Height %d", w, h);
        return -1;
    }

    //Only update the configure for device
    int fd = video_fds[device];

    //Switch the Mode before set the format. This is the requirement of
    //atomisp
    ret = atomisp_set_capture_mode(deviceMode);
    if (ret < 0)
        return ret;

    //Set the format
    ret = v4l2_capture_s_format(fd, device, w, h, format, raw);
    if (ret < 0)
        return ret;

    v4l2_buf_pool[device].width = w;
    v4l2_buf_pool[device].height = h;
    v4l2_buf_pool[device].format = format;

    /* 3A related initialization*/
    //Reallocate the grid for 3A after format change
    if (device == V4L2_FIRST_DEVICE) {
        ret = v4l2_capture_g_framerate(fd, &mConfig.fps, w, h, format);
        if (ret < 0) {
        /*Error handler: if driver does not support FPS achieving,
                       just give the default value.*/
            mConfig.fps = DEFAULT_SENSOR_FPS;
            ret = 0;
        }
    }

    //We need apply all the parameter settings when do the camera reset
    return ret;
}

int AtomISP::startDevice(int device, int buffer_count)
{
    LOG_FUNCTION
    LogDetail("device = %d", device);

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LogError("Wrong device: %d", device);
        return -1;
    }

    int i, ret;
    int fd = video_fds[device];
    LogDetail(" startDevice fd = %d", fd);

    //parameter intialized before the streamon
    //request, query and mmap the buffer and save to the pool
    ret = createBufferPool(device, buffer_count);
    if (ret < 0)
        return ret;

    //Qbuf
    ret = activateBufferPool(device);
    if (ret < 0)
        goto activate_error;

    //stream on
    ret = v4l2_capture_streamon(fd);
    if (ret < 0)
       goto streamon_failed;

    //we are started now
    return 0;

aaa_error:
    v4l2_capture_streamoff(fd);
streamon_failed:
activate_error:
    destroyBufferPool(device);
    return ret;
}

int AtomISP::activateBufferPool(int device)
{
    LOG_FUNCTION
    LogDetail("device = %d", device);

    int fd = video_fds[device];
    LogDetail(" activateBufferPool fd = %d", fd);
    int ret;
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];

    for (int i = 0; i < pool->active_buffers; i++) {
        ret = v4l2_capture_qbuf(fd, i, &pool->bufs[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

int AtomISP::createBufferPool(int device, int buffer_count)
{
    LOG_FUNCTION
    LogDetail("device = %d", device);
    int i, ret;

    int fd = video_fds[device];
    LogDetail(" createBufferPool fd = %d", fd);
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];
    int num_buffers = v4l2_capture_request_buffers(device, buffer_count);
    LogDetail("num_buffers = %d", num_buffers);

    if (num_buffers <= 0)
        return -1;

    pool->active_buffers = num_buffers;

    for (i = 0; i < num_buffers; i++) {
        pool->bufs[i].width = pool->width;
        pool->bufs[i].height = pool->height;
        pool->bufs[i].format = pool->format;
        ret = v4l2_capture_new_buffer(device, i, &pool->bufs[i]);
        if (ret < 0)
            goto error;
    }
    return 0;

error:
    for (int j = 0; j < i; j++)
        v4l2_capture_free_buffer(device, &pool->bufs[j]);
    pool->active_buffers = 0;
    return ret;
}

void AtomISP::stopDevice(int device)
{
    LOG_FUNCTION
    LogDetail("device = %d", device);
    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LogError("Wrong device: %d", device);
        return;
    }
    int fd = video_fds[device];

    //stream off
    v4l2_capture_streamoff(fd);
    destroyBufferPool(device);
}

void AtomISP::destroyBufferPool(int device)
{
    LOG_FUNCTION
    LogDetail("device = %d", device);

    int fd = video_fds[device];
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];

    for (int i = 0; i < pool->active_buffers; i++)
        v4l2_capture_free_buffer(device, &pool->bufs[i]);
    pool->active_buffers = 0;
    v4l2_capture_release_buffers(device);
}

int AtomISP::openDevice(int device)
{
    LOG_FUNCTION
    int ret = 0;

    if (video_fds[device] > 0) {
        LogWarning("Device already opened!");
        return video_fds[device];
    }

    video_fds[device] = v4l2_capture_open(device);

    if (video_fds[device] < 0) {
        LogError("V4L2: capture_open failed: %s", strerror(errno));
        return -1;
    }

    // Query and check the capabilities
    if (v4l2_capture_querycap(device, &cap) < 0) {
        LogError("V4L2: capture_querycap failed: %s", strerror(errno));
        v4l2_capture_close(video_fds[device]);
        video_fds[device] = -1;
        return -1;
    }

    if (device == V4L2_FIRST_DEVICE && mCameraId != -1) {

        //Choose the camera sensor
        LogDetail("Selecting camera sensor: %d", mCameraId);
        ret = v4l2_capture_s_input(video_fds[device], mCameraId);
        if (ret < 0) {
            LogError("V4L2: capture_s_input failed: %s", strerror(errno));
            v4l2_capture_close(video_fds[device]);
            video_fds[device] = -1;
            return -1;
        }
    }
    return ret;
}

void AtomISP::closeDevice(int device)
{
    LOG_FUNCTION

    if (video_fds[device] < 0) {
        LogDetail("Device %d already closed. Do nothing.", device);
        return;
    }

    v4l2_capture_close(video_fds[device]);

    video_fds[device] = -1;
}

int AtomISP::detectDeviceResolutions()
{
    LOG_FUNCTION
    int ret = 0;
    struct v4l2_frmsizeenum frame_size;
    int device = V4L2_FIRST_DEVICE;

    //Switch the Mode before try the format.
    ret = atomisp_set_capture_mode(MODE_CAPTURE);
    if (ret < 0)
        return ret;

    int i = 0;
    while (true) {
        memset(&frame_size, 0, sizeof(frame_size));
        frame_size.index = i++;
        frame_size.pixel_format = mConfig.snapshot.format;
        /* TODO: Currently VIDIOC_ENUM_FRAMESIZES is returning with Invalid argument
         * Need to know why the driver is not supporting this V4L2 API call
         */
        if (ioctl(video_fds[V4L2_FIRST_DEVICE], VIDIOC_ENUM_FRAMESIZES, &frame_size) < 0) {
            break;
        }
        ret++;
        float fps = 0;
        v4l2_capture_g_framerate(
                video_fds[V4L2_FIRST_DEVICE],
                &fps,
                frame_size.discrete.width,
                frame_size.discrete.height,
                frame_size.pixel_format);
        LogDetail("Supported frame size: %ux%u@%dfps",
                frame_size.discrete.width,
                frame_size.discrete.height,
                static_cast<int>(fps));
    }

    // Get the maximum format supported
    mConfig.snapshot.maxWidth = 0xffff;
    mConfig.snapshot.maxHeight = 0xffff;
    ret = v4l2_capture_try_format(V4L2_FIRST_DEVICE, &mConfig.snapshot.maxWidth, &mConfig.snapshot.maxHeight, &mConfig.snapshot.format);
    if (ret < 0)
        return ret;
    return 0;
}

status_t AtomISP::setPreviewFrameFormat(int width, int height, int format)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    if (width > mConfig.preview.maxWidth || width <= 0)
        width = mConfig.preview.maxWidth;
    if (height > mConfig.preview.maxHeight || height <= 0)
        height = mConfig.preview.maxHeight;
    mConfig.preview.width = width;
    mConfig.preview.height = height;
    mConfig.preview.format = format;
    mConfig.preview.padding = paddingWidth(format, width, height);
    mConfig.preview.size = frameSize(format, width, height);
    if (mConfig.preview.size == 0)
        mConfig.preview.size = mConfig.preview.width * mConfig.preview.height * BPP;
    LogDetail("width(%d), height(%d), pad_width(%d), size(%d), format(%d)",
        width, height, mConfig.preview.padding, mConfig.preview.size, format);
    return status;
}

status_t AtomISP::setPostviewFrameFormat(int width, int height, int format)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    LogDetail("width(%d), height(%d), format(%d)",
         width, height, format);
    mConfig.postview.width = width;
    mConfig.postview.height = height;
    mConfig.postview.format = format;
    mConfig.postview.padding = paddingWidth(format, width, height);
    mConfig.postview.size = frameSize(format, width, height);
    if (mConfig.postview.size == 0)
        mConfig.postview.size = mConfig.postview.width * mConfig.postview.height * BPP;
    LogDetail("width(%d), height(%d), pad_width(%d), size(%d), format(%d)",
            width, height, mConfig.postview.padding, mConfig.postview.size, format);
    return status;
}

status_t AtomISP::setSnapshotFrameFormat(int width, int height, int format)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    if (width > mConfig.snapshot.maxWidth || width <= 0)
        width = mConfig.snapshot.maxWidth;
    if (height > mConfig.snapshot.maxHeight || height <= 0)
        height = mConfig.snapshot.maxHeight;
    mConfig.snapshot.width  = width;
    mConfig.snapshot.height = height;
    mConfig.snapshot.format = format;
    mConfig.snapshot.padding = paddingWidth(format, width, height);
    mConfig.snapshot.size = frameSize(format, width, height);;
    if (mConfig.snapshot.size == 0)
        mConfig.snapshot.size = mConfig.snapshot.width * mConfig.snapshot.height * BPP;
    LogDetail("width(%d), height(%d), pad_width(%d), size(%d), format(%d)",
        width, height, mConfig.snapshot.padding, mConfig.snapshot.size, format);
    return status;
}

status_t AtomISP::setSnapshotNum(int num)
{
    LOG_FUNCTION
    mConfig.num_snapshot = num;
    LogDetail("mConfig.num_snapshot = %d", mConfig.num_snapshot);
    return NO_ERROR;
}

status_t AtomISP::setVideoFrameFormat(int width, int height, int format)
{
    LOG_FUNCTION
    int ret = 0;
    status_t status = NO_ERROR;

    if (mConfig.recording.width == width &&
        mConfig.recording.height == height &&
        mConfig.recording.format == format) {
        // Do nothing
        return status;
    }

    if (mMode == MODE_VIDEO) {
        LogError("Reconfiguration in video mode unsupported. Stop the ISP first");
        return INVALID_OPERATION;
    }

    if (width > mConfig.recording.maxWidth || width <= 0) {
        LogError("invalid recording width %d. override to %d", width, mConfig.recording.maxWidth);
        width = mConfig.recording.maxWidth;
    }
    if (height > mConfig.recording.maxHeight || height <= 0) {
        LogError("invalid recording height %d. override to %d", height, mConfig.recording.maxHeight);
        height = mConfig.recording.maxHeight;
    }
    mConfig.recording.width = width;
    mConfig.recording.height = height;
    mConfig.recording.format = format;
    mConfig.recording.padding = paddingWidth(format, width, height);
    mConfig.recording.size = frameSize(format, width, height);
    if (mConfig.recording.size == 0)
        mConfig.recording.size = mConfig.recording.width * mConfig.recording.height * BPP;
    LogDetail("width(%d), height(%d), pad_width(%d), format(%d)",
            width, height, mConfig.recording.padding, format);

    return status;
}

void AtomISP::getZoomRatios(Mode mode, CameraParameters *params)
{
    if (params) {
        if (mode == MODE_PREVIEW || mode == MODE_CAPTURE) {
            params->set(CameraParameters::KEY_MAX_ZOOM, "60"); // max zoom index for 61 raitos is 60
            params->set(CameraParameters::KEY_ZOOM_RATIOS,
                    "100,125,150,175,200,225,250,275,300,325,350,375,400,425,450,475,500,525,"
                    "550,575,600,625,650,675,700,725,750,775,800,825,850,875,900,925,950,975,"
                    "1000,1025,1050,1075,1100,1125,1150,1175,1200,1225,1250,1275,1300,1325,"
                    "1350,1375,1400,1425,1450,1475,1500,1525,1550,1575,1600");
        } else {
            // zoom is not supported. this is indicated by placing a single zoom ratio in params
            params->set(CameraParameters::KEY_MAX_ZOOM, "0"); // zoom index 0 indicates first (and only) zoom ratio
            params->set(CameraParameters::KEY_ZOOM_RATIOS, "100");
        }
    }
}

status_t AtomISP::setFlash(int numFrames)
{
    LOG_FUNCTION
    if (camInfo[mCameraId].port != ATOMISP_CAMERA_PORT_PRIMARY) {
        LogError("Flash is supported only for primary camera!");
        return INVALID_OPERATION;
    }
    LogDetail("numFrames = %d", numFrames);
    if (atomisp_set_attribute(main_fd, V4L2_CID_REQUEST_FLASH, numFrames, "request flash") < 0)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

status_t AtomISP::setZoom(int zoom)
{
    LOG_FUNCTION
    LogDetail("zoom = %d", zoom);
    if (zoom == mConfig.zoom)
        return NO_ERROR;
    if (mMode == MODE_CAPTURE)
        return NO_ERROR;

    int ret = atomisp_set_zoom(main_fd, zoom);
    if (ret < 0) {
        LogError("Error setting zoom to %d", zoom);
        return UNKNOWN_ERROR;
    }
    mConfig.zoom = zoom;
    return NO_ERROR;
}

int AtomISP::atomisp_set_zoom (int fd, int zoom)
{
    LOG_FUNCTION
    if (fd < 0) {
        LogDetail("Device not opened!");
        return 0;
    }

    //Map 8x to 56. The real effect is 64/(64 - zoom) in the driver.
    //Max zoom is 60 because we only support 16x not 64x
    if (zoom != 0)
        zoom = 64 - (64 / (((zoom * 16 + 59)/ 60 )));

    LogDetail("set zoom to %d", zoom);
    return atomisp_set_attribute (fd, V4L2_CID_ZOOM_ABSOLUTE, zoom, "zoom");
}

int AtomISP::atomisp_set_attribute (int fd, int attribute_num,
                                             const int value, const char *name)
{
    LOG_FUNCTION
    struct v4l2_control control;
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control ext_control;

    LogDetail("setting attribute [%s] to %d", name, value);

    if (fd < 0)
        return -1;

    control.id = attribute_num;
    control.value = value;
    controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
    controls.count = 1;
    controls.controls = &ext_control;
    ext_control.id = attribute_num;
    ext_control.value = value;

    if (ioctl(fd, VIDIOC_S_CTRL, &control) == 0)
        return 0;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) == 0)
        return 0;

    controls.ctrl_class = V4L2_CTRL_CLASS_USER;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) == 0)
        return 0;

    LogError("Failed to set value %d for control %s (%d) on device '%d', %s",
        value, name, attribute_num, fd, strerror(errno));
    return -1;
}

int AtomISP::v4l2_capture_streamon(int fd)
{
    LOG_FUNCTION
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LogError("VIDIOC_STREAMON returned: %d (%s)", ret, strerror(errno));
        return ret;
    }
    return ret;
}

int AtomISP::v4l2_capture_streamoff(int fd)
{
    LOG_FUNCTION
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (fd < 0){ //Device is closed
        LogError("Device is closed!");
        return 0;
    }
    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LogError("VIDIOC_STREAMOFF returned: %d (%s)", ret, strerror(errno));
        return ret;
    }

    return ret;
}

/* Unmap the buffer or free the userptr */
int AtomISP::v4l2_capture_free_buffer(int device, struct v4l2_buffer_info *buf_info)
{
    LOG_FUNCTION
    int ret = 0;
    void *addr = buf_info->data;
    size_t length = buf_info->length;

    if (device == V4L2_THIRD_DEVICE &&
        (ret = munmap(addr, length)) < 0) {
            LogError("munmap returned: %d (%s)", ret, strerror(errno));
            return ret;
        }

    return ret;
}

int AtomISP::v4l2_capture_release_buffers(int device)
{
    LOG_FUNCTION
    return v4l2_capture_request_buffers(device, 0);
}

int AtomISP::v4l2_capture_request_buffers(int device, uint num_buffers)
{
    LOG_FUNCTION
    struct v4l2_requestbuffers req_buf;
    int ret;
    CLEAR(req_buf);

    int fd = video_fds[device];

    if (fd < 0)
        return 0;

    req_buf.memory = V4L2_MEMORY_USERPTR;
    req_buf.count = num_buffers;
    req_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (device == V4L2_THIRD_DEVICE) {
        req_buf.memory = V4L2_MEMORY_MMAP;
        req_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    }

    LogDetail("VIDIOC_REQBUFS, count=%d", req_buf.count);
    ret = ioctl(fd, VIDIOC_REQBUFS, &req_buf);

    if (ret < 0) {
        LogError("VIDIOC_REQBUFS(%d) returned: %d (%s)",
            num_buffers, ret, strerror(errno));
        return ret;
    }

    if (req_buf.count < num_buffers)
        LogWarning("Got less buffers than requested!");

    return req_buf.count;
}

int AtomISP::v4l2_capture_new_buffer(int device, int index, struct v4l2_buffer_info *buf)
{
    LOG_FUNCTION
    void *data;
    int ret;
    int fd = video_fds[device];
    struct v4l2_buffer *vbuf = &buf->vbuffer;
    vbuf->flags = 0x0;

    if (device == V4L2_THIRD_DEVICE) {
        vbuf->index = index;
        vbuf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        vbuf->memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fd, VIDIOC_QUERYBUF, vbuf);
        if (ret < 0) {
            LogError("VIDIOC_QUERYBUF failed: %s", strerror(errno));
            return -1;
        }

        data = mmap(NULL, vbuf->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    vbuf->m.offset);

        if (MAP_FAILED == data) {
            LogError("mmap failed: %s", strerror(errno));
            return -1;
        }

        buf->data = data;
        buf->length = vbuf->length;

        memcpy(data, mFileImage.mapped_addr, mFileImage.size);
        return 0;
    }

    vbuf->memory = V4L2_MEMORY_USERPTR;

    vbuf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf->index = index;
    ret = ioctl(fd , VIDIOC_QUERYBUF, vbuf);

    if (ret < 0) {
        LogError("VIDIOC_QUERYBUF failed: %s", strerror(errno));
        return ret;
    }

    vbuf->m.userptr = (unsigned int)(buf->data);

    buf->length = vbuf->length;
    LogDetail("index %u", vbuf->index);
    LogDetail("type %d", vbuf->type);
    LogDetail("bytesused %u", vbuf->bytesused);
    LogDetail("flags %08x", vbuf->flags);
    LogDetail("memory %u", vbuf->memory);
    LogDetail("userptr:  %lu", vbuf->m.userptr);
    LogDetail("length %u", vbuf->length);
    LogDetail("input %u", vbuf->input);
    return ret;
}

int AtomISP::v4l2_capture_g_framerate(int fd, float *framerate, int width,
                                         int height, int pix_fmt)
{
    LOG_FUNCTION
    int ret;
    struct v4l2_frmivalenum frm_interval;

    if (NULL == framerate)
        return -EINVAL;

    assert(fd > 0);
    CLEAR(frm_interval);
    frm_interval.pixel_format = pix_fmt;
    frm_interval.width = width;
    frm_interval.height = height;
    *framerate = -1.0;

    ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frm_interval);
    if (ret < 0) {
        LogWarning("ioctl failed: %s", strerror(errno));
        return ret;
    }

    assert(0 != frm_interval.discrete.denominator);

    *framerate = 1.0 / (1.0 * frm_interval.discrete.numerator / frm_interval.discrete.denominator);

    return 0;
}

int AtomISP::v4l2_capture_s_format(int fd, int device, int w, int h, int fourcc, bool raw)
{
    LOG_FUNCTION
    int ret;
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);

    if (device == V4L2_THIRD_DEVICE) {
        mIspTimeout = ATOMISP_FILEINPUT_POLL_TIMEOUT;
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        v4l2_fmt.fmt.pix.width = mFileImage.width;
        v4l2_fmt.fmt.pix.height = mFileImage.height;
        v4l2_fmt.fmt.pix.pixelformat = mFileImage.format;
        v4l2_fmt.fmt.pix.sizeimage = mFileImage.size;
        v4l2_fmt.fmt.pix.priv = mFileImage.bayer_order;

        LogDetail("VIDIOC_S_FMT: width: %d, height: %d, format: %x, size: %d, bayer_order: %d",
                mFileImage.width,
                mFileImage.height,
                mFileImage.format,
                mFileImage.size,
                mFileImage.bayer_order);
        ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
        if (ret < 0) {
            LogError("VIDIOC_S_FMT failed: %s", strerror(errno));
            return -1;
        }
        return 0;
    }

    mIspTimeout = ATOMISP_POLL_TIMEOUT;
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    LogDetail("VIDIOC_G_FMT");
    ret = ioctl (fd,  VIDIOC_G_FMT, &v4l2_fmt);
    if (ret < 0) {
        LogError("VIDIOC_G_FMT failed: %s", strerror(errno));
        return -1;
    }
    if (raw) {
        LogDetail("Choose raw dump path");
        v4l2_fmt.type = V4L2_BUF_TYPE_PRIVATE;
    } else {
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    v4l2_fmt.fmt.pix.width = w;
    v4l2_fmt.fmt.pix.height = h;
    v4l2_fmt.fmt.pix.pixelformat = fourcc;
    v4l2_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    LogDetail("VIDIOC_S_FMT: width: %d, height: %d, format: %d, field: %d",
                v4l2_fmt.fmt.pix.width,
                v4l2_fmt.fmt.pix.height,
                v4l2_fmt.fmt.pix.pixelformat,
                v4l2_fmt.fmt.pix.field);
    ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        LogError("VIDIOC_S_FMT failed: %s", strerror(errno));
        return -1;
    }
    return 0;

}

int AtomISP::v4l2_capture_qbuf(int fd, int index, struct v4l2_buffer_info *buf)
{
    LOG_FUNCTION2
    struct v4l2_buffer *v4l2_buf = &buf->vbuffer;
    int ret;

    if (fd < 0) //Device is closed
        return 0;
    ret = ioctl(fd, VIDIOC_QBUF, v4l2_buf);
    if (ret < 0) {
        LogError("VIDIOC_QBUF index %d failed: %s",
             index, strerror(errno));
        return ret;
    }
    return ret;
}

status_t AtomISP::v4l2_capture_open(int device)
{
    LOG_FUNCTION
    int fd;
    struct stat st;

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_THIRD_DEVICE)) {
        LogError("Wrong device node %d", device);
        return -1;
    }

    const char *dev_name = dev_name_array[device];
    LogDetail("---Open video device %s---", dev_name);

    if (stat (dev_name, &st) == -1) {
        LogError("Error stat video device %s: %s",
             dev_name, strerror(errno));
        return -1;
    }

    if (!S_ISCHR (st.st_mode)) {
        LogError("%s is not a device", dev_name);
        return -1;
    }

    fd = open(dev_name, O_RDWR);

    if (fd <= 0) {
        LogError("Error opening video device %s: %s",
            dev_name, strerror(errno));
        return -1;
    }

    return fd;
}

status_t AtomISP::v4l2_capture_close(int fd)
{
    LOG_FUNCTION
    /* close video device */
    LogDetail("----close device ---");
    if (fd < 0) {
        LogWarning("Device not opened!");
        return INVALID_OPERATION;
    }

    if (close(fd) < 0) {
        LogError("Close video device failed: %s", strerror(errno));
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t AtomISP::v4l2_capture_querycap(int device, struct v4l2_capability *cap)
{
    LOG_FUNCTION
    int ret = 0;
    int fd = video_fds[device];

    ret = ioctl(fd, VIDIOC_QUERYCAP, cap);

    if (ret < 0) {
        LogError("VIDIOC_QUERYCAP returned: %d (%s)", ret, strerror(errno));
        return ret;
    }

    if (device == V4L2_THIRD_DEVICE) {
        if (!(cap->capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
            LogError("No output devices");
            return -1;
        }
        return ret;
    }

    if (!(cap->capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LogError("No capture devices");
        return -1;
    }

    if (!(cap->capabilities & V4L2_CAP_STREAMING)) {
        LogError("Is not a video streaming device");
        return -1;
    }

    LogDetail( "driver:      '%s'", cap->driver);
    LogDetail( "card:        '%s'", cap->card);
    LogDetail( "bus_info:      '%s'", cap->bus_info);
    LogDetail( "version:      %x", cap->version);
    LogDetail( "capabilities:      %x", cap->capabilities);

    return ret;
}

status_t AtomISP::v4l2_capture_s_input(int fd, int index)
{
    LOG_FUNCTION
    struct v4l2_input input;
    int ret;

    LogDetail("VIDIOC_S_INPUT");
    input.index = index;

    ret = ioctl(fd, VIDIOC_S_INPUT, &input);

    if (ret < 0) {
        LogError("VIDIOC_S_INPUT index %d returned: %d (%s)",
            input.index, ret, strerror(errno));
        return ret;
    }
    return ret;
}

int AtomISP::atomisp_set_capture_mode(int deviceMode)
{
    LOG_FUNCTION
    struct v4l2_streamparm parm;

    switch (deviceMode) {
    case CI_MODE_PREVIEW:
        LogDetail("Setting CI_MODE_PREVIEW mode");
        break;;
    case CI_MODE_STILL_CAPTURE:
        LogDetail("Setting CI_MODE_STILL_CAPTURE mode");
        break;
    case CI_MODE_VIDEO:
        LogDetail("Setting CI_MODE_VIDEO mode");
        break;
    default:
        break;
    }

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.capturemode = deviceMode;
    if (ioctl(main_fd, VIDIOC_S_PARM, &parm) < 0) {
        LogError("error %s", strerror(errno));
        return -1;
    }

    return 0;
}

int AtomISP::v4l2_capture_try_format(int device, int *w, int *h,
                                         int *fourcc)
{
    LOG_FUNCTION
    int ret;
    int fd = video_fds[device];
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);

    if (device == V4L2_THIRD_DEVICE) {
        *w = mFileImage.width;
        *h = mFileImage.height;
        *fourcc = mFileImage.format;

        LogDetail("width: %d, height: %d, format: %x, size: %d, bayer_order: %d",
             mFileImage.width,
             mFileImage.height,
             mFileImage.format,
             mFileImage.size,
             mFileImage.bayer_order);

        return 0;
    }

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    v4l2_fmt.fmt.pix.width = *w;
    v4l2_fmt.fmt.pix.height = *h;
    v4l2_fmt.fmt.pix.pixelformat = *fourcc;
    v4l2_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    ret = ioctl(fd, VIDIOC_TRY_FMT, &v4l2_fmt);
    if (ret < 0) {
        LogError("VIDIOC_TRY_FMT returned: %d (%s)", ret, strerror(errno));
        return -1;
    }

    *w = v4l2_fmt.fmt.pix.width;
    *h = v4l2_fmt.fmt.pix.height;
    *fourcc = v4l2_fmt.fmt.pix.pixelformat;

    return 0;
}

status_t AtomISP::getPreviewFrame(AtomBuffer *buff)
{
    LOG_FUNCTION2
    struct v4l2_buffer buf;

    if (mMode == MODE_NONE)
        return INVALID_OPERATION;

    int index = grabFrame(mPreviewDevice, &buf);
    if(index < 0){
        LogError("Error in grabbing frame!");
        return BAD_INDEX;
    }
    LogDetail2("Device: %d. Grabbed frame of size: %d", mPreviewDevice, buf.bytesused);
    mPreviewBuffers[index].id = index;
    mPreviewBuffers[index].ispPrivate = mSessionId;
    *buff = mPreviewBuffers[index];

    mNumPreviewBuffersQueued--;

    return NO_ERROR;
}

status_t AtomISP::putPreviewFrame(AtomBuffer *buff)
{
    LOG_FUNCTION2
    if (mMode == MODE_NONE)
        return INVALID_OPERATION;

    if (buff->ispPrivate != mSessionId)
        return DEAD_OBJECT;

    if (v4l2_capture_qbuf(video_fds[mPreviewDevice],
                      buff->id,
                      &v4l2_buf_pool[mPreviewDevice].bufs[buff->id]) < 0) {
        return UNKNOWN_ERROR;
    }

    mNumPreviewBuffersQueued++;

    return NO_ERROR;
}

status_t AtomISP::setRecordingBuffers(SharedBufferType *buffs, int numBuffs)
{
    if (buffs == NULL || numBuffs <= 0)
        return BAD_VALUE;

    mClientRecordingBuffers = new void*[numBuffs];
    if (mClientRecordingBuffers == NULL)
        return NO_MEMORY;

    for (int i = 0; i < numBuffs; i++)
        mClientRecordingBuffers[i] = (void *) buffs[i].pointer;

    mUsingClientRecordingBuffers = true;
    mNumBuffers = numBuffs;

    return NO_ERROR;
}

void AtomISP::unsetRecordingBuffers()
{
    delete [] mClientRecordingBuffers;
    mUsingClientRecordingBuffers = false;
    mNumBuffers = NUM_DEFAULT_BUFFERS;
}

status_t AtomISP::getRecordingFrame(AtomBuffer *buff, nsecs_t *timestamp)
{
    LOG_FUNCTION2
    struct v4l2_buffer buf;

    if (mMode != MODE_VIDEO)
        return INVALID_OPERATION;

    int index = grabFrame(mRecordingDevice, &buf);
    LogDetail2("index = %d", index);
    if(index < 0) {
        LogError("Error in grabbing frame!");
        return BAD_INDEX;
    }
    LogDetail2("Device: %d. Grabbed frame of size: %d", mRecordingDevice, buf.bytesused);
    mRecordingBuffers[index].id = index;
    mRecordingBuffers[index].ispPrivate = mSessionId;
    *buff = mRecordingBuffers[index];
    *timestamp = systemTime();

    mNumRecordingBuffersQueued--;

    return NO_ERROR;
}

status_t AtomISP::putRecordingFrame(AtomBuffer *buff)
{
    LOG_FUNCTION2
    if (mMode != MODE_VIDEO)
        return INVALID_OPERATION;

    if (buff->ispPrivate != mSessionId)
        return DEAD_OBJECT;

    if (v4l2_capture_qbuf(video_fds[mRecordingDevice],
            buff->id,
            &v4l2_buf_pool[mRecordingDevice].bufs[buff->id]) < 0) {
        return UNKNOWN_ERROR;
    }

    mNumRecordingBuffersQueued++;

    return NO_ERROR;
}

status_t AtomISP::getSnapshot(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    struct v4l2_buffer buf;
    int snapshotIndex, postviewIndex;

    if (mMode != MODE_CAPTURE)
        return INVALID_OPERATION;

    snapshotIndex = grabFrame(V4L2_FIRST_DEVICE, &buf);
    if (snapshotIndex < 0) {
        LogError("Error in grabbing frame from 1'st device!");
        return BAD_INDEX;
    }
    LogDetail("Device: %d. Grabbed frame of size: %d", V4L2_FIRST_DEVICE, buf.bytesused);

    postviewIndex = grabFrame(V4L2_SECOND_DEVICE, &buf);
    if (postviewIndex < 0) {
        LogError("Error in grabbing frame from 2'nd device!");
        // If we failed with the second device, return the frame to the first device
        v4l2_capture_qbuf(video_fds[V4L2_FIRST_DEVICE], snapshotIndex,
                &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[snapshotIndex]);
        return BAD_INDEX;
    }
    LogDetail("Device: %d. Grabbed frame of size: %d", V4L2_SECOND_DEVICE, buf.bytesused);

    if (snapshotIndex != postviewIndex ||
            snapshotIndex >= MAX_V4L2_BUFFERS) {
        LogError("Indexes error! snapshotIndex = %d, postviewIndex = %d", snapshotIndex, postviewIndex);
        // Return the buffers back to driver
        v4l2_capture_qbuf(video_fds[V4L2_FIRST_DEVICE], snapshotIndex,
                &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[snapshotIndex]);
        v4l2_capture_qbuf(video_fds[V4L2_SECOND_DEVICE], postviewIndex,
                &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[postviewIndex]);
        return BAD_INDEX;
    }

    mSnapshotBuffers[snapshotIndex].id = snapshotIndex;
    mSnapshotBuffers[snapshotIndex].ispPrivate = mSessionId;
    *snapshotBuf = mSnapshotBuffers[snapshotIndex];

    mPostviewBuffers[postviewIndex].id = postviewIndex;
    mPostviewBuffers[postviewIndex].ispPrivate = mSessionId;
    *postviewBuf = mPostviewBuffers[postviewIndex];

    return NO_ERROR;
}

status_t AtomISP::putSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret0, ret1;

    if (mMode != MODE_CAPTURE)
        return INVALID_OPERATION;

    if (snaphotBuf->ispPrivate != mSessionId || postviewBuf->ispPrivate != mSessionId)
        return DEAD_OBJECT;

    ret0 = v4l2_capture_qbuf(video_fds[V4L2_FIRST_DEVICE], snaphotBuf->id,
                      &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[snaphotBuf->id]);

    ret1 = v4l2_capture_qbuf(video_fds[V4L2_SECOND_DEVICE], postviewBuf->id,
                      &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[postviewBuf->id]);
    if (ret0 < 0 || ret1 < 0)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

bool AtomISP::dataAvailable()
{
    LOG_FUNCTION2

    // For video/recording, make sure isp has a preview and a recording buffer
    if (mMode == MODE_VIDEO)
        return mNumRecordingBuffersQueued > 0 && mNumPreviewBuffersQueued > 0;

    // For preview, just make sure we isp has a preview buffer
    if (mMode == MODE_PREVIEW)
        return mNumPreviewBuffersQueued > 0;

    LogError("Query for data in invalid mode");

    return false;
}

int AtomISP::grabFrame(int device, struct v4l2_buffer *buf)
{
    LOG_FUNCTION2
    int ret;
    //Must start first
    if (main_fd < 0)
        return -1;

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LogError("Wrong device %d", device);
        return -1;
    }

    ret = v4l2_capture_dqbuf(video_fds[device], buf);

    if (ret < 0)
        return ret;

    return buf->index;
}

int AtomISP::v4l2_capture_dqbuf(int fd, struct v4l2_buffer *buf)
{
    LOG_FUNCTION2
    int ret, i;
    int num_tries = 500;
    struct pollfd pfd[1];

    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_USERPTR;

    pfd[0].fd = fd;
    pfd[0].events = POLLIN | POLLERR;

    for (i = 0; i < num_tries; i++) {
        ret = poll(pfd, 1, mIspTimeout);

        if (ret < 0 ) {
            LogError("Select error in DQ");
            return -1;
        }
        if (ret == 0) {
            LogError("Select timeout in DQ");
            return -1;
        }
        ret = ioctl(fd, VIDIOC_DQBUF, buf);

        if (ret >= 0)
            break;
        LogError("DQBUF returned: %d", ret);
        switch (errno) {
        case EINVAL:
            LogError("Failed to get frames from device: %s",
                 strerror(errno));
            return -1;
        case EINTR:
            LogWarning("Could not sync the buffer: %s",
                 strerror(errno));
            break;
        case EAGAIN:
            LogWarning("No buffer in the queue: %s",
                 strerror(errno));
            break;
        case EIO:
            break;
            /* Could ignore EIO, see spec. */
            /* fail through */
        default:
           return -1;
        }
    }
    if ( i == num_tries) {
        LogError("Too many tries");
        return -1;
    }
    return buf->index;
}

////////////////////////////////////////////////////////////////////
//                          PRIVATE METHODS
////////////////////////////////////////////////////////////////////

status_t AtomISP::allocatePreviewBuffers()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    int allocatedBufs = 0;
    int size = mConfig.preview.width * mConfig.preview.height * 3 / 2;

    mPreviewBuffers = new AtomBuffer[mNumBuffers];
    if (!mPreviewBuffers) {
        LOGE("Not enough mem for preview buffer array");
        status = NO_MEMORY;
        goto errorFree;
    }

    LogDetail("Allocating %d buffers of size %d", mNumBuffers, size);
    for (int i = 0; i < mNumBuffers; i++) {
         mPreviewBuffers[i].buff = NULL;
         mCallbacks->allocateMemory(&mPreviewBuffers[i], size);
         if (mPreviewBuffers[i].buff == NULL) {
             LogError("Error allocation memory for preview buffers!");
             status = NO_MEMORY;
             goto errorFree;
         }
         allocatedBufs++;
         v4l2_buf_pool[mPreviewDevice].bufs[i].data = mPreviewBuffers[i].buff->data;
    }
    return status;

errorFree:
    // On error, free the allocated buffers
    for (int i = 0 ; i < allocatedBufs; i++) {
        if (mRecordingBuffers[i].buff != NULL) {
            mRecordingBuffers[i].buff->release(mRecordingBuffers[i].buff);
            mRecordingBuffers[i].buff = NULL;
        }
    }
    if (mPreviewBuffers)
        delete [] mPreviewBuffers;
    return status;
}

status_t AtomISP::allocateRecordingBuffers()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    int allocatedBufs = 0;
    int size;

    if (mUsingClientRecordingBuffers)
        size = sizeof(void *);
    else
        size = mConfig.recording.width * mConfig.recording.height * 3 / 2;

    mRecordingBuffers = new AtomBuffer[mNumBuffers];
    if (!mRecordingBuffers) {
        LOGE("Not enough mem for recording buffer array");
        status = NO_MEMORY;
        goto errorFree;
    }

    for (int i = 0; i < mNumBuffers; i++) {
        mRecordingBuffers[i].buff = NULL;
        mCallbacks->allocateMemory(&mRecordingBuffers[i], size);
        LogDetail("allocate recording buffer[%d] shared=%d, buff=%p size=%d",
                i, (int) mUsingClientRecordingBuffers,
                mRecordingBuffers[i].buff->data,
                mRecordingBuffers[i].buff->size);
        if (mRecordingBuffers[i].buff == NULL) {
            LogError("Error allocation memory for recording buffers!");
            status = NO_MEMORY;
            goto errorFree;
        }
        allocatedBufs++;
        if (mUsingClientRecordingBuffers) {
            v4l2_buf_pool[mRecordingDevice].bufs[i].data = mClientRecordingBuffers[i];
            memcpy(mRecordingBuffers[i].buff->data, &mClientRecordingBuffers[i], sizeof(void *));
        } else {
            v4l2_buf_pool[mRecordingDevice].bufs[i].data = mRecordingBuffers[i].buff->data;
        }
    }
    return status;

errorFree:

    // On error, free the allocated buffers
    for (int i = 0 ; i < allocatedBufs; i++) {
        if (mRecordingBuffers[i].buff != NULL) {
            mRecordingBuffers[i].buff->release(mRecordingBuffers[i].buff);
            mRecordingBuffers[i].buff = NULL;
        }
    }
    if (mRecordingBuffers)
        delete [] mRecordingBuffers;
    return status;
}

status_t AtomISP::allocateSnapshotBuffers()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    int allocatedSnaphotBufs = 0;
    int allocatedPostviewBufs = 0;

    LogDetail("Allocating %d buffers of size: %d (snapshot), %d (postview)",
            mConfig.num_snapshot,
            mConfig.snapshot.size,
            mConfig.postview.size);
    for (int i = 0; i < mConfig.num_snapshot; i++) {
        mSnapshotBuffers[i].buff = NULL;
        mCallbacks->allocateMemory(&mSnapshotBuffers[i], mConfig.snapshot.size);
        if (mSnapshotBuffers[i].buff == NULL) {
            LogError("Error allocation memory for snapshot buffers!");
            status = NO_MEMORY;
            goto errorFree;
        }
        allocatedSnaphotBufs++;
        v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[i].data = mSnapshotBuffers[i].buff->data;

        mPostviewBuffers[i].buff = NULL;
        mCallbacks->allocateMemory(&mPostviewBuffers[i], mConfig.postview.size);
        if (mPostviewBuffers[i].buff == NULL) {
            LogError("Error allocation memory for postview buffers!");
            status = NO_MEMORY;
            goto errorFree;
        }
        allocatedPostviewBufs++;
        v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[i].data = mPostviewBuffers[i].buff->data;
    }
    return status;

errorFree:
    // On error, free the allocated buffers
    for (int i = 0 ; i < allocatedSnaphotBufs; i++) {
        if (mSnapshotBuffers[i].buff != NULL) {
            mSnapshotBuffers[i].buff->release(mSnapshotBuffers[i].buff);
            mSnapshotBuffers[i].buff = NULL;
        }
    }
    for (int i = 0 ; i < allocatedPostviewBufs; i++) {
        if (mPostviewBuffers[i].buff != NULL) {
            mPostviewBuffers[i].buff->release(mPostviewBuffers[i].buff);
            mPostviewBuffers[i].buff = NULL;
        }
    }
    return status;
}

status_t AtomISP::freePreviewBuffers()
{
    LOG_FUNCTION
    for (int i = 0 ; i < mNumBuffers; i++) {
        if (mPreviewBuffers[i].buff != NULL) {
            mPreviewBuffers[i].buff->release(mPreviewBuffers[i].buff);
            mPreviewBuffers[i].buff = NULL;
        }
    }
    delete [] mPreviewBuffers;
    return NO_ERROR;
}

status_t AtomISP::freeRecordingBuffers()
{
    LOG_FUNCTION
    for (int i = 0 ; i < mNumBuffers; i++) {
        if (mRecordingBuffers[i].buff != NULL) {
            mRecordingBuffers[i].buff->release(mRecordingBuffers[i].buff);
            mRecordingBuffers[i].buff = NULL;
        }
    }
    delete [] mRecordingBuffers;
    return NO_ERROR;
}

status_t AtomISP::freeSnapshotBuffers()
{
    LOG_FUNCTION
    for (int i = 0 ; i < mConfig.num_snapshot; i++) {
        if (mSnapshotBuffers[i].buff != NULL) {
            mSnapshotBuffers[i].buff->release(mSnapshotBuffers[i].buff);
            mSnapshotBuffers[i].buff = NULL;
        }
        if (mPostviewBuffers[i].buff != NULL) {
            mPostviewBuffers[i].buff->release(mPostviewBuffers[i].buff);
            mPostviewBuffers[i].buff = NULL;
        }
    }
    return NO_ERROR;
}

int AtomISP::getNumberOfCameras()
{
    LOG_FUNCTION
    if (numCameras != 0)
        return numCameras;
    int ret;
    struct v4l2_input input;
    int fd = -1;

    fd = open(dev_name_array[0], O_RDWR);
    if (fd <= 0) {
        LogError("Error opening video device %s: %s",
                dev_name_array[0], strerror(errno));
        return 0;
    }

    int i;
    for (i = 0; i < MAX_CAMERAS; i++) {
        memset(&input, 0, sizeof(input));
        input.index = i;
        ret = ioctl(fd, VIDIOC_ENUMINPUT, &input);
        if (ret < 0) {
            break;
        }
        camInfo[i].port = input.reserved[1];
        strncpy(camInfo[i].name, (const char *)input.name, MAX_SENSOR_NAME_LENGTH);
    }
    close(fd);

    numCameras = i;
    return numCameras;
}

status_t AtomISP::getCameraInfo(int cameraId, camera_info *cameraInfo)
{
    LOG_FUNCTION
    if (cameraId >= MAX_CAMERAS)
        return BAD_VALUE;

    memcpy(cameraInfo, &mCameraInfo[cameraId], sizeof(camera_info));
    return NO_ERROR;
}

} // namespace android
