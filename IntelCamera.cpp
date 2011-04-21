/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "IntelCamera"
#include <utils/Log.h>

#include "IntelCamera.h"
#include <atomisp_features.h>
#include <sys/mman.h>

#define BPP 2

namespace android {

/* Debug Use Only */
static void write_image(const void *data, const int size, int width, int height,
                       const char *name)
{
    char filename[80];
    static unsigned int count = 0;
    size_t bytes;
    FILE *fp;

    snprintf(filename, 50, "/data/dump_%d_%d_00%u_%s", width,
             height, count, name);

    fp = fopen (filename, "w+");
    if (fp == NULL) {
        LOGE ("open file %s failed %s\n", filename, strerror (errno));
        return ;
    }

    LOG1 ("Begin write image %s\n", filename);
    if ((bytes = fwrite (data, size, 1, fp)) < (size_t)size)
        LOGW ("Write less bytes to %s: %d, %d\n", filename, size, bytes);
    count++;

    fclose (fp);
}

static void
dump_v4l2_buffer(int fd, struct v4l2_buffer *buffer, char *name)
{
    void *data;
    static unsigned int i = 0;
    int image_width = 640, image_height=480;
    if (memory_userptr)
        data = (void *) buffer->m.userptr;
    else
        data = mmap (NULL, buffer->length, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd, buffer->m.offset);

    write_image(data, buffer->length, image_width, image_height, name);

    if (!memory_userptr)
        munmap(data, buffer->length);
}
///////////////////////////////////////////////////////////////////////////////////
IntelCamera::IntelCamera()
    :m_flag_init(0),
 /*mSensorInfo(0),*/
 /*mAdvanceProcess(NULL),*/
     zoom_val(0)
{
    LOGV("%s() called!\n", __func__);

    m_camera_id = DEFAULT_CAMERA_SENSOR;
    num_buffers = DEFAULT_NUM_BUFFERS;

    video_fds[V4L2_FIRST_DEVICE] = -1;
    video_fds[V4L2_SECOND_DEVICE] = -1;
    main_fd = -1;
    m_flag_camera_start[0] = 0;
    m_flag_camera_start[1] = 0;
    mStillAfRunning = false;
    mFlashNecessary = false;
}

IntelCamera::~IntelCamera()
{
    LOGV("%s() called!\n", __func__);

    // color converter
}

int IntelCamera::initCamera(int camera_id)
{
    int ret = 0;
    LOG1("%s :", __func__);

    switch (camera_id) {
    case CAMERA_ID_FRONT:
        m_preview_max_width   = MAX_FRONT_CAMERA_PREVIEW_WIDTH;
        m_preview_max_height  = MAX_FRONT_CAMERA_PREVIEW_HEIGHT;
        m_recorder_max_width = MAX_FRONT_CAMERA_VIDEO_WIDTH;
        m_recorder_max_height = MAX_FRONT_CAMERA_VIDEO_HEIGHT;
        m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
        m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
        break;
    case CAMERA_ID_BACK:
        m_preview_max_width   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
        m_preview_max_height  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
        m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
        m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
        m_recorder_max_width = MAX_BACK_CAMERA_VIDEO_WIDTH;
        m_recorder_max_height = MAX_BACK_CAMERA_VIDEO_HEIGHT;
        break;
    default:
        LOGE("ERR(%s)::Invalid camera id(%d)\n", __func__, camera_id);
        return -1;
    }
    m_camera_id = camera_id;

    m_preview_width = 640;
    m_preview_pad_width = 640;
    m_preview_height = 480;
    m_preview_v4lformat = V4L2_PIX_FMT_RGB565;

    m_postview_width = 640;
    m_postview_height = 480;
    m_postview_v4lformat = V4L2_PIX_FMT_YUV420;

    m_snapshot_width = 2560;
    m_snapshot_pad_width = 2560;
    m_snapshot_height = 1920;
    m_snapshot_v4lformat = V4L2_PIX_FMT_RGB565;

    m_recorder_width = 1920;
    m_recorder_pad_width = 1920;
    m_recorder_height = 1080;
    m_recorder_v4lformat = V4L2_PIX_FMT_NV12;

    // Do the basic init before open here
    if (!m_flag_init) {
        mAAA = new AAAProcess(ENUM_SENSOR_TYPE_RAW);
        if (!mAAA) {
            LOGE("ERR(%s): Allocate AAAProcess failed\n", __func__);
            return -1;
        }
        mAAA->Init();
        //Parse the configure from file
        atomisp_parse_cfg_file();
        m_flag_init = 1;
    }
    return ret;
}

int IntelCamera::deinitCamera(void)
{
    if (m_flag_init) {
        mAAA->Uninit();
        delete mAAA;
        m_flag_init = 0;
    }
    LOG1("%s :", __func__);
    return 0;
}

//Preview Control
int IntelCamera::startCameraPreview(void)
{
    LOG1("%s\n", __func__);
    int ret;
    int w = m_preview_pad_width;
    int h = m_preview_height;
    int fourcc = m_preview_v4lformat;
    int device = V4L2_FIRST_DEVICE;

    run_mode = PREVIEW_MODE;
    ret = openDevice(run_mode);
    if (ret < 0)
        return ret;

    if (zoom_val != 0)
        set_zoom_val_real(zoom_val);
    ret = configureDevice(device, w, h, fourcc);
    if (ret < 0)
        return ret;

    if (use_texture_streaming) {
        void *ptrs[PREVIEW_NUM_BUFFERS];
        int i;
        for (i = 0; i < PREVIEW_NUM_BUFFERS; i++) {
            ptrs[i] = v4l2_buf_pool[device].bufs[i].data;
        }
        v4l2_register_bcd(video_fds[device], PREVIEW_NUM_BUFFERS,
                          ptrs, w, h, fourcc, m_frameSize(fourcc, w, h));
    }

    ret = startCapture(device, PREVIEW_NUM_BUFFERS);
    if (ret < 0)
        return ret;

    return ret;
}

void IntelCamera::stopCameraPreview(void)
{
    LOG1("%s\n", __func__);
    int device = V4L2_FIRST_DEVICE;
    if (!m_flag_camera_start[device]) {
        LOG1("%s: doing nothing because m_flag_camera_start is zero", __func__);
        usleep(100);
        return ;
    }
    int fd = video_fds[device];

    if (fd <= 0) {
        LOGD("(%s):Camera was already closed\n", __func__);
        return ;
    }

    Mutex::Autolock lock(mFlashLock);
    // If we stop for picture capture, we need do someflash process
    if (mFlashForCapture)
        runPreFlashSequence();

    if (use_texture_streaming) {
        v4l2_release_bcd(video_fds[V4L2_FIRST_DEVICE]);
    }

    stopCapture(device);
    closeDevice();
}

int IntelCamera::getPreview(void **data)
{
    int device = V4L2_FIRST_DEVICE;
    int index = grabFrame(device);
    *data = v4l2_buf_pool[device].bufs[index].data;
    //Tell Still AF that a frame is ready
    if (mStillAfRunning)
        mStillAfCondition.signal();
    return index;
}

int IntelCamera::putPreview(int index)
{
    int device = V4L2_FIRST_DEVICE;
    int fd = video_fds[device];
    return v4l2_capture_qbuf(fd, index, &v4l2_buf_pool[device].bufs[index]);
}

//Snapsshot Control
//Snapshot and video recorder has the same flow. We can combine them together
int IntelCamera::startSnapshot(void)
{
    LOG1("%s\n", __func__);
    int ret;
    run_mode = STILL_IMAGE_MODE;
    ret = openDevice(run_mode);
    if (ret < 0)
        return ret;

    //0 is the default. I don't need zoom.
    if (zoom_val != 0)
        set_zoom_val_real(zoom_val);

    ret = configureDevice(V4L2_FIRST_DEVICE, m_snapshot_width,
                          m_snapshot_height, m_snapshot_v4lformat);
    if (ret < 0)
        goto configure1_error;

    ret = configureDevice(V4L2_SECOND_DEVICE, m_postview_width,
                          m_postview_height, m_postview_v4lformat);
    if (ret < 0)
        goto configure2_error;


    ret = startCapture(V4L2_FIRST_DEVICE, SNAPSHOT_NUM_BUFFERS);
    if (ret < 0)
        goto start1_error;


    ret = startCapture(V4L2_SECOND_DEVICE, SNAPSHOT_NUM_BUFFERS);
    if (ret < 0)
        goto start2_error;
    return ret;

start2_error:
    stopCapture(V4L2_FIRST_DEVICE);
start1_error:
configure2_error:
configure1_error:
    closeDevice();
    return ret;
}

void IntelCamera::stopSnapshot(void)
{
    stopDualStreams();
}

int IntelCamera::putDualStreams(int index)
{
    LOG2("%s index %d\n", __func__, index);
    int ret0, ret1, device;

    device = V4L2_FIRST_DEVICE;
    ret0 = v4l2_capture_qbuf(video_fds[device], index,
                      &v4l2_buf_pool[device].bufs[index]);

    device = V4L2_SECOND_DEVICE;
    ret1 = v4l2_capture_qbuf(video_fds[device], index,
                      &v4l2_buf_pool[device].bufs[index]);
    if (ret0 < 0 || ret1 < 0)
        return -1;
    return 0;
}

// We have a workaround here that the preview_out is not the real preview
// output from the driver. It is converted to RGB565.
int IntelCamera::getSnapshot(void **main_out, void *postview)
{
    LOG1("%s\n", __func__);
    //Running flash before the snapshot
    if (mFlashNecessary) {
       captureFlashOnCertainDuration(0, 500000, 15); // software trigger, 500ms, intensity 15
        putSnapshot(0);
    }

    int index0 = grabFrame(V4L2_FIRST_DEVICE);
    int index1 = grabFrame(V4L2_SECOND_DEVICE);
    void *preview_out;

    if (index0 < 0 || index1 < 0 || index0 != index1) {
        LOGE("%s error\n", __func__);
        return -1;
    }

    *main_out = v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index0].data;
    preview_out = v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index0].data;

    if (need_dump_snapshot) {
        struct v4l2_buffer_info *buf0 =
            &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index0];
        struct v4l2_buffer_info *buf1 =
            &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index0];
        const char *name0 = "snap_v0.rgb";
        const char *name1 = "snap_v1.rgb";
        write_image(*main_out, buf0->length, buf0->width, buf0->height, name0);
        write_image(preview_out, buf1->length, buf1->width, buf1->height, name1);
    }

    //convert preview output from YUV420 to RGB565
    yuv420_to_rgb565(m_postview_width, m_postview_height,
                     (unsigned char*)preview_out, (unsigned short*)postview);

    return index0;
}

int IntelCamera::putSnapshot(int index)
{
    return putDualStreams(index);
}

//video recording Control
int IntelCamera::startCameraRecording(void)
{
    int ret = 0;
    LOG1("%s\n", __func__);
    run_mode = VIDEO_RECORDING_MODE;
    ret = openDevice(run_mode);
    if (ret < 0)
        return ret;

    if ((zoom_val != 0) && (m_recorder_width != 1920))
        set_zoom_val_real(zoom_val);

    ret = configureDevice(V4L2_FIRST_DEVICE, m_recorder_width,
                          m_recorder_height, m_recorder_v4lformat);
    if (ret < 0)
        goto configure1_error;

    //176x144 using pad width
    ret = configureDevice(V4L2_SECOND_DEVICE, m_preview_pad_width,
                          m_preview_height, m_preview_v4lformat);
    if (ret < 0)
        goto configure2_error;


    ret = startCapture(V4L2_FIRST_DEVICE, VIDEO_NUM_BUFFERS);
    if (ret < 0)
        goto start1_error;

    if (use_texture_streaming) {
        int w = m_preview_pad_width;
        int h = m_preview_height;
        int fourcc = m_preview_v4lformat;
        void *ptrs[VIDEO_NUM_BUFFERS];
        int i, device = V4L2_SECOND_DEVICE;

        for (i = 0; i < VIDEO_NUM_BUFFERS; i++) {
            ptrs[i] = v4l2_buf_pool[device].bufs[i].data;
        }
        v4l2_register_bcd(video_fds[device], PREVIEW_NUM_BUFFERS,
                          ptrs, w, h, fourcc, m_frameSize(fourcc, w, h));
    }

    ret = startCapture(V4L2_SECOND_DEVICE, VIDEO_NUM_BUFFERS);
    if (ret < 0)
        goto start2_error;

    return ret;

start2_error:
    stopCapture(V4L2_FIRST_DEVICE);
start1_error:
configure2_error:
configure1_error:
    closeDevice();
    return ret;
}

void IntelCamera::stopCameraRecording(void)
{
    LOG1("%s\n", __func__);

    if (use_texture_streaming) {
        v4l2_release_bcd(video_fds[V4L2_SECOND_DEVICE]);
    }

    stopDualStreams();
}

void IntelCamera::stopDualStreams(void)
{
    LOG1("%s\n", __func__);
    if (m_flag_camera_start == 0) {
        LOGD("%s: doing nothing because m_flag_camera_start is 0", __func__);
        usleep(10);
        return ;
    }

    if (main_fd <= 0) {
        LOGW("%s:Camera was closed\n", __func__);
        return ;
    }

    stopCapture(V4L2_FIRST_DEVICE);
    stopCapture(V4L2_SECOND_DEVICE);
    closeDevice();
}

int IntelCamera::trimRecordingBuffer(void *buf)
{
    int size = m_frameSize(V4L2_PIX_FMT_NV12, m_recorder_width,
                           m_recorder_height);
    int padding_size = m_frameSize(V4L2_PIX_FMT_NV12, m_recorder_pad_width,
                                   m_recorder_height);
    void *tmp_buffer = malloc(padding_size);
    if (tmp_buffer == NULL) {
        LOGE("%s: Error to allocate memory \n", __func__);
        return -1;
    }
    //The buf should bigger enougth to hold the extra padding
    memcpy(tmp_buffer, buf, padding_size);
    trimNV12((unsigned char *)tmp_buffer, (unsigned char *)buf,
             m_recorder_pad_width, m_recorder_height,
             m_recorder_width, m_recorder_height);
    free(tmp_buffer);
    return 0;
}

int IntelCamera::getRecording(void **main_out, void **preview_out)
{
    LOG2("%s\n", __func__);
    int index0 = grabFrame(V4L2_FIRST_DEVICE);
    int index1 = grabFrame(V4L2_SECOND_DEVICE);
    if (index0 < 0 || index1 < 0 || index0 != index1) {
        LOGE("%s error\n", __func__);
        return -1;
    }

    *main_out = v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index0].data;
    *preview_out = v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index0].data;
    if (need_dump_recorder) {
        struct v4l2_buffer_info *buf0 =
            &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index0];
        struct v4l2_buffer_info *buf1 =
            &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index0];
        const char *name0 = "record_v0.rgb";
        const char *name1 = "record_v1.rgb";
        write_image(*main_out, buf0->length, buf0->width, buf0->height, name0);
        write_image(*preview_out, buf1->length, buf1->width, buf1->height, name1);
    }

    //Padding remove for 176x144
    if (m_recorder_width != m_recorder_pad_width) {
        trimRecordingBuffer(*main_out);
    }

    return index0;
}

int IntelCamera::putRecording(int index)
{
    return putDualStreams(index);
}

int IntelCamera::openDevice(int mode)
{
    LOG1("%s\n", __func__);
    int ret;

    if (video_fds[V4L2_FIRST_DEVICE] > 0) {
        LOGW("%s: Already opened\n", __func__);
        return video_fds[V4L2_FIRST_DEVICE];
    }

    //Open the first device node
    int device = V4L2_FIRST_DEVICE;
    video_fds[device] = v4l2_capture_open(device);

    if (video_fds[device] < 0)
        return -1;

    // Query and check the capabilities
    if (v4l2_capture_querycap(video_fds[device], &cap) < 0)
        goto error0;

    main_fd = video_fds[device];
    mAAA->IspSetFd(main_fd);
    //Do some other intialization here after open

    //Choose the camera sensor
    ret = v4l2_capture_s_input(video_fds[device], m_camera_id);
    if (ret < 0)
        return ret;
    if (mode == PREVIEW_MODE)
        return video_fds[device];

    //Open the second device node
    device = V4L2_SECOND_DEVICE;
    video_fds[device] = v4l2_capture_open(device);

    if (video_fds[device] < 0) {
        goto error0;
    }
    // Query and check the capabilities
    if (v4l2_capture_querycap(video_fds[device], &cap) < 0)
        goto error1;

    return video_fds[device];

error1:
    v4l2_capture_close(video_fds[V4L2_SECOND_DEVICE]);
error0:
    v4l2_capture_close(video_fds[V4L2_FIRST_DEVICE]);
    video_fds[V4L2_FIRST_DEVICE] = -1;
    video_fds[V4L2_SECOND_DEVICE] = -1;
    return -1;
}

void IntelCamera::closeDevice(void)
{
    LOG1("%s\n", __func__);
    if (video_fds[V4L2_FIRST_DEVICE] < 0) {
        LOGW("%s: Already closed\n", __func__);
        return;
    }

    v4l2_capture_close(video_fds[V4L2_FIRST_DEVICE]);

    video_fds[V4L2_FIRST_DEVICE] = -1;
    main_fd = -1;

    mAAA->IspSetFd(-1);

    //Close the second device
    if (video_fds[V4L2_SECOND_DEVICE] < 0)
        return ;
    v4l2_capture_close(video_fds[V4L2_SECOND_DEVICE]);
    video_fds[V4L2_SECOND_DEVICE] = -1;

}

int IntelCamera::configureDevice(int device, int w, int h, int fourcc)
{
    int ret;
    LOG1("%s device %d, width:%d, height%d, mode%d format%d\n", __func__, device,
         w, h, run_mode, fourcc);

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LOGE("ERR(%s): Wrong device %d\n", __func__, device);
        return -1;
    }

    if ((w <= 0) || (h <= 0)) {
        LOGE("ERR(%s): Wrong Width %d or Height %d\n", __func__, w, h);
        return -1;
    }

    //Only update the configure for device
    if (device == V4L2_FIRST_DEVICE)
        atomisp_set_cfg_from_file(video_fds[device]);

    int fd = video_fds[device];

    //Stop the device first if it is started
    if (m_flag_camera_start[device])
        stopCapture(device);

    //Switch the Mode before set the format. This is the requirement of
    //atomisp
    ret = set_capture_mode(run_mode);
    if (ret < 0)
        return ret;

    //Set the format
    ret = v4l2_capture_s_format(fd, w, h, fourcc);
    if (ret < 0)
        return ret;

    current_w[device] = w;
    current_h[device] = h;
    current_v4l2format[device] = fourcc;

    /* 3A related initialization*/
    //Reallocate the grid for 3A after format change
    if (device == V4L2_FIRST_DEVICE) {
        mAAA->SwitchMode(run_mode);
        if (run_mode == STILL_IMAGE_MODE) {
            LOGV("3A is not run in still image capture mode\n");
        } else {
            ret = mAAA->ModeSpecInit();
            if (ret < 0) {
                LOGE("ModeSpecInit failed from 3A\n");
                return ret;
            }
            mAAA->SetAfEnabled(true);
            mAAA->SetAeEnabled(true);
            mAAA->SetAwbEnabled(true);
        }
    }

    if (run_mode == STILL_IMAGE_MODE) {
        //We stop the camera before the still image camera.
        //All the settings lost. Applied it here
        ;
    }

    //We need apply all the parameter settings when do the camera reset
    return ret;
}

int IntelCamera::createBufferPool(int device, int buffer_count)
{
    LOG1("%s device %d\n", __func__, device);
    int i, ret;

    int fd = video_fds[device];
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];
    num_buffers = v4l2_capture_request_buffers(fd, buffer_count);

    if (num_buffers <= 0)
        return -1;

    pool->active_buffers = num_buffers;

    for (i = 0; i < num_buffers; i++) {
        pool->bufs[i].width = current_w[device];
        pool->bufs[i].height = current_h[device];
        pool->bufs[i].fourcc = current_v4l2format[device];
        ret = v4l2_capture_new_buffer(fd, i, &pool->bufs[i]);
        if (ret < 0)
            goto error;
    }
    return 0;

error:
    for (int j = 0; j < i; j++)
        v4l2_capture_free_buffer(fd, &pool->bufs[j]);
    return ret;
}

void IntelCamera::destroyBufferPool(int device)
{
    LOG1("%s device %d\n", __func__, device);

    int fd = video_fds[device];
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];

    for (int i = 0; i < pool->active_buffers; i++)
        v4l2_capture_free_buffer(fd, &pool->bufs[i]);
    v4l2_capture_release_buffers(fd);
}

int IntelCamera::activateBufferPool(int device)
{
    LOG1("%s device %d\n", __func__, device);

    int fd = video_fds[device];
    int ret;
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];

    for (int i = 0; i < pool->active_buffers; i++) {
        ret = v4l2_capture_qbuf(fd, i, &pool->bufs[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

int IntelCamera::startCapture(int device, int buffer_count)
{
    LOG1("%s device %d\n", __func__, device);

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LOGE("ERR(%s): Wrong device %d\n", __func__, device);
        return -1;
    }

    int i, ret;
    int fd = video_fds[device];

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
    m_flag_camera_start[device] = 1;

    //Apply the 3A result from preview for still image capture
    //Reinitialize the semAAA
    if (device == V4L2_FIRST_DEVICE) {
        ret = sem_init(&semAAA, 0, 0);
        if (ret) {
            LOGE("ERR(%s): sem_init failed\n", __func__);
            if (ret < 0)
                goto aaa_error;
        }

        if (run_mode == STILL_IMAGE_MODE)
            update3Aresults();
    }
    return 0;

aaa_error:
    v4l2_capture_streamoff(fd);
streamon_failed:
activate_error:
    destroyBufferPool(device);
    m_flag_camera_start[device] = 0;
    return ret;
}

void IntelCamera::stopCapture(int device)
{
    //stop 3A here
    mAAA->SetAfEnabled(false);
    mAAA->SetAeEnabled(false);
    mAAA->SetAwbEnabled(false);

    LOG1("%s\n", __func__);
    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LOGE("ERR(%s): Wrong device %d\n", __func__, device);
        return;
    }
    int fd = video_fds[device];

    //stream off
    v4l2_capture_streamoff(fd);

    destroyBufferPool(device);

    sem_close(&semAAA);

    m_flag_camera_start[device] = 0;
}

int IntelCamera::grabFrame(int device)
{
    int ret;
    struct v4l2_buffer buf;
    //Must start first
    if (!m_flag_camera_start[device])
        return -1;

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LOGE("ERR(%s): Wrong device %d\n", __func__, device);
        return -1;
    }

    ret = v4l2_capture_dqbuf(video_fds[device], &buf);

    if (ret < 0) {
        LOGD("%s: DQ error, reset the camera\n", __func__);
        ret = resetCamera();
        if (ret < 0) {
            LOGE("ERR(%s): Reset camera error\n", __func__);
            return ret;
        }
        ret = v4l2_capture_dqbuf(video_fds[device], &buf);
        if (ret < 0) {
            LOGE("ERR(%s): Reset camera error again\n", __func__);
            return ret;
        }
    }
    return buf.index;
}

int IntelCamera::resetCamera(void)
{
    LOG1("%s\n", __func__);
    int ret = 0;
    if (memory_userptr)
		memcpy(v4l2_buf_pool_reserve, v4l2_buf_pool, sizeof(v4l2_buf_pool));

    switch (run_mode) {
    case PREVIEW_MODE:
        stopCameraPreview();
        if (memory_userptr)
	    	memcpy(v4l2_buf_pool, v4l2_buf_pool_reserve, sizeof(v4l2_buf_pool));
        ret = startCameraPreview();
        break;
    case STILL_IMAGE_MODE:
        stopSnapshot();
        if (memory_userptr)
	    	memcpy(v4l2_buf_pool, v4l2_buf_pool_reserve, sizeof(v4l2_buf_pool));
        ret = startSnapshot();
        break;
    case VIDEO_RECORDING_MODE:
        stopCameraRecording();
        if (memory_userptr)
	    	memcpy(v4l2_buf_pool, v4l2_buf_pool_reserve, sizeof(v4l2_buf_pool));
        ret = startCameraRecording();
        break;
    default:
        LOGE("%s: Wrong mode\n", __func__);
        break;
    }
    return ret;
}

//YUV420 to RGB565 for the postview output. The RGB565 from the preview stream
//is bad
void IntelCamera::yuv420_to_rgb565(int width, int height, unsigned char *src,
                                   unsigned short *dst)
{
    int line, col, linewidth;
    int y, u, v, yy, vr, ug, vg, ub;
    int r, g, b;
    const unsigned char *py, *pu, *pv;

    linewidth = width >> 1;
    py = src;
    pu = py + (width * height);
    pv = pu + (width * height) / 4;

    y = *py++;
    yy = y << 8;
    u = *pu - 128;
    ug = 88 * u;
    ub = 454 * u;
    v = *pv - 128;
    vg = 183 * v;
    vr = 359 * v;

    for (line = 0; line < height; line++) {
        for (col = 0; col < width; col++) {
            r = (yy + vr) >> 8;
            g = (yy - ug - vg) >> 8;
            b = (yy + ub ) >> 8;

            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            *dst++ = (((unsigned short)r>>3)<<11) | (((unsigned short)g>>2)<<5)
                     | (((unsigned short)b>>3)<<0);

            y = *py++;
            yy = y << 8;
            if (col & 1) {
                pu++;
                pv++;

                u = *pu - 128;
                ug = 88 * u;
                ub = 454 * u;
                v = *pv - 128;
                vg = 183 * v;
                vr = 359 * v;
            }
        }
        if ((line & 1) == 0) {
            pu -= linewidth;
            pv -= linewidth;
        }
    }
}

int IntelCamera::get_num_buffers(void)
{
    return num_buffers;
}

void IntelCamera::setPreviewUserptr(int index, void *addr)
{
    if (index > PREVIEW_NUM_BUFFERS) {
        LOGE("%s:index %d is out of range\n", __func__, index);
        return ;
    }
    v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index].data = addr;
}

void IntelCamera::setRecorderUserptr(int index, void *preview, void *recorder)
{
    if (index > VIDEO_NUM_BUFFERS) {
        LOGE("%s:index %d is out of range\n", __func__, index);
        return ;
    }
    v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index].data = recorder;
    v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index].data = preview;
}

void IntelCamera::setFlash(void)
{
    Mutex::Autolock lock(mFlashLock);
    mFlashForCapture = true;
}

void IntelCamera::clearFlash(void)
{
    Mutex::Autolock lock(mFlashLock);
    mFlashForCapture = false;
}

void IntelCamera::getFlashStatus(bool *flash_status)
{
    Mutex::Autolock lock(mFlashLock);
    *flash_status = mFlashNecessary;
}

void IntelCamera::setFlashStatus(bool flash_status)
{
    Mutex::Autolock lock(mFlashLock);
    mFlashNecessary = flash_status;
}

void IntelCamera::captureFlashOff(void)
{
    cam_driver_led_flash_off (main_fd);
}

void IntelCamera::captureFlashOnCertainDuration(int mode,  int duration, int intensity)
{
    cam_driver_led_flash_trigger (main_fd, mode, duration, intensity);
}

void IntelCamera::runPreFlashSequence(void)
{
    //We hold the mFlashLock here
    int index;
    void *data;
    mAAA->AeIsFlashNecessary (&mFlashNecessary);
    if (!mFlashNecessary)
        return ;
    mAAA->SetAeFlashEnabled (true);
    mAAA->SetAwbFlashEnabled (true);

    // pre-flash process
    index = getPreview(&data);
    if (index < 0) {
        LOGE("%s: Error to get frame\n", __func__);
        return ;
    }
    mAAA->GetStatistics();
    mAAA->AeCalcForFlash();

    // pre-flash
//    captureFlashOff();
    putPreview(index);
    index = getPreview(&data);
    if (index < 0) {
        LOGE("%s: Error to get frame\n", __func__);
        return ;
    }
    mAAA->GetStatistics();
    mAAA->AeCalcWithoutFlash();

    // main flash
    captureFlashOnCertainDuration(0, 100000, 1);  // software trigger, 100ms, intensity 1
    mAAA->AwbApplyResults();
    putPreview(index);
    index = getPreview(&data);
    if (index < 0) {
        LOGE("%s: Error to get frame\n", __func__);
        return ;
    }
    mAAA->GetStatistics();
    mAAA->AeCalcWithFlash();
    mAAA->AwbCalcFlash();

    mAAA->SetAeFlashEnabled (false);
    mAAA->SetAwbFlashEnabled (false);
    putPreview(index);
}

//limit it to 56 because bigger value easy to cause ISP timeout
#define MAX_ZOOM_LEVEL	56
#define MIN_ZOOM_LEVEL	0

//Use flags to detern whether it is called from the snapshot
int IntelCamera::set_zoom_val_real(int zoom)
{
    /* Zoom is 100,150,200,250,300,350,400 */
    /* AtomISP zoom range is 1 - 64 */
    if (main_fd < 0) {
        LOGV("%s: device not opened\n", __func__);
        return 0;
    }

    if (zoom < MIN_ZOOM_LEVEL)
        zoom = MIN_ZOOM_LEVEL;
    if (zoom > MAX_ZOOM_LEVEL)
        zoom = MAX_ZOOM_LEVEL;

    zoom = ((zoom - MIN_ZOOM_LEVEL) * (MAX_ZOOM_LEVEL - 1) /
            (MAX_ZOOM_LEVEL - MIN_ZOOM_LEVEL)) + 1;
    return cam_driver_set_zoom (main_fd, zoom);
}

int IntelCamera::set_zoom_val(int zoom)
{
    if (zoom == zoom_val)
        return 0;
    zoom_val = zoom;
    if (run_mode == STILL_IMAGE_MODE)
        return 0;
    return set_zoom_val_real(zoom);
}

int IntelCamera::get_zoom_val(void)
{
    return zoom_val;
}

int IntelCamera::set_capture_mode(int mode)
{
    if (main_fd < 0) {
        LOGW("ERR(%s): not opened\n", __func__);
        return -1;
    }
    return cam_driver_set_capture_mode(main_fd, mode);
}

//These are for the frame size and format
int IntelCamera::setPreviewSize(int width, int height, int fourcc)
{
    if (width > m_preview_max_width || width <= 0)
        width = m_preview_max_width;
    if (height > m_preview_max_height || height <= 0)
        height = m_preview_max_height;
    m_preview_width = width;
    m_preview_height = height;
    m_preview_v4lformat = fourcc;
    m_preview_pad_width = m_paddingWidth(fourcc, width, height);
    LOG1("%s(width(%d), height(%d), pad_width(%d), format(%d))", __func__,
         width, height, m_preview_pad_width, fourcc);
    return 0;
}

int IntelCamera::getPreviewSize(int *width, int *height, int *frame_size,
                                int *padded_size)
{
    *width = m_preview_width;
    *height = m_preview_height;
    *frame_size = m_frameSize(m_preview_v4lformat, m_preview_width,
                              m_preview_height);
    *padded_size = m_frameSize(m_preview_v4lformat, m_preview_pad_width,
                       m_preview_height);
    LOG1("%s:width(%d), height(%d), size(%d)\n", __func__, *width, *height,
         *frame_size);
    return 0;
}

int IntelCamera::getPreviewPixelFormat(void)
{
    return m_preview_v4lformat;
}

//postview
int IntelCamera::setPostViewSize(int width, int height, int fourcc)
{
    LOG1("%s(width(%d), height(%d), format(%d))", __func__,
         width, height, fourcc);
    m_postview_width = width;
    m_postview_height = height;
    m_postview_v4lformat = fourcc;

    return 0;
}

int IntelCamera::getPostViewSize(int *width, int *height, int *frame_size)
{
    m_postview_width = m_preview_width;
    m_postview_height = m_preview_height;

    //The preview output should be small than the main output
    if (m_postview_width > m_snapshot_width)
        m_postview_width = m_snapshot_width;

    if (m_postview_height > m_snapshot_height)
        m_postview_height = m_snapshot_height;

    *width = m_postview_width;
    *height = m_postview_height;
    *frame_size = m_frameSize(m_postview_v4lformat, m_postview_width,
                              m_postview_height);
    return 0;
}

int IntelCamera::getPostViewPixelFormat(void)
{
    return m_postview_v4lformat;
}

int IntelCamera::setSnapshotSize(int width, int height, int fourcc)
{
    if (width > m_snapshot_max_width || width <= 0)
        width = m_snapshot_max_width;
    if (height > m_snapshot_max_height || height <= 0)
        height = m_snapshot_max_width;
    m_snapshot_width  = width;
    m_snapshot_height = height;
    m_snapshot_v4lformat = fourcc;
    m_snapshot_pad_width = m_paddingWidth(fourcc, width, height);
    LOG1("%s(width(%d), height(%d), pad_width(%d), format(%d))", __func__,
         width, height, m_snapshot_pad_width, fourcc);
    return 0;
}

int IntelCamera::getSnapshotSize(int *width, int *height, int *frame_size)
{
    *width  = m_snapshot_width;
    *height = m_snapshot_height;

    *frame_size = m_frameSize(m_snapshot_v4lformat, m_snapshot_width,
                              m_snapshot_height);
    if (*frame_size == 0)
        *frame_size = m_snapshot_width * m_snapshot_height * BPP;

    return 0;
}

int IntelCamera::getSnapshotPixelFormat(void)
{
    return m_snapshot_v4lformat;
}

void IntelCamera::setSnapshotUserptr(void *pic_addr, void *pv_addr)
{
    v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[0].data = pic_addr;
    v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[0].data = pv_addr;
}

int IntelCamera::setRecorderSize(int width, int height, int fourcc)
{
    LOG1("Max:W %d, MaxH: %d\n", m_recorder_max_width, m_recorder_max_height);
    if (width > m_recorder_max_width || width <= 0)
        width = m_recorder_max_width;
    if ((height > m_recorder_max_height) || (height <= 0))
        height = m_recorder_max_height;
    m_recorder_width  = width;
    m_recorder_height = height;
    m_recorder_v4lformat = fourcc;
    m_recorder_pad_width = m_paddingWidth(fourcc, width, height);
    LOG1("%s(width(%d), height(%d), pad_width(%d), format(%d))", __func__,
         width, height, m_recorder_pad_width, fourcc);
    return 0;
}

int IntelCamera::getRecorderSize(int *width, int *height, int *frame_size,
                                 int *padded_size)
{
    *width  = m_recorder_width;
    *height = m_recorder_height;

    *frame_size = m_frameSize(m_recorder_v4lformat, m_recorder_width,
                              m_recorder_height);
    if (*frame_size == 0)
        *frame_size = m_recorder_width * m_recorder_height * BPP;
    *padded_size = m_frameSize(m_recorder_v4lformat, m_recorder_pad_width,
                              m_recorder_height);

    LOG1("%s(width(%d), height(%d),size (%d))", __func__, *width, *height,
         *frame_size);
    return 0;
}

int IntelCamera::getRecorderPixelFormat(void)
{
    return m_recorder_v4lformat;
}

inline int IntelCamera::m_frameSize(int format, int width, int height)
{
    int size = 0;
    switch (format) {
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_YUV411P:
    case V4L2_PIX_FMT_YUV422P:
        size = (width * height * 3 / 2);
        break;
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_Y41P:
    case V4L2_PIX_FMT_UYVY:
        size = (width * height *  2);
        break;
    case V4L2_PIX_FMT_RGB565:
        size = (width * height * BPP);
        break;
    default:
        size = (width * height * 2);
        LOGE("ERR(%s):Invalid V4L2 pixel format(%d)\n", __func__, format);
    }

    return size;
}

int IntelCamera::m_paddingWidth(int format, int width, int height)
{
    int padding = 0;
    switch (format) {
    //64bit align for 1.5byte per pixel
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_YUV411P:
    case V4L2_PIX_FMT_YUV422P:
        padding = (width + 63) / 64 * 64;
        break;
    //32bit align for 2byte per pixel
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_Y41P:
    case V4L2_PIX_FMT_UYVY:
        padding = width;
        break;
    case V4L2_PIX_FMT_RGB565:
        padding = (width + 31) / 32 * 32;
        break;
    default:
        padding = (width + 63) / 64 * 64;
        LOGE("ERR(%s):Invalid V4L2 pixel format(%d)\n", __func__, format);
    }

    return padding;
}


//3A processing
void IntelCamera::update3Aresults(void)
{
    LOG1("%s\n", __func__);
    mAAA->SetAfEnabled(true);
    mAAA->SetAeEnabled(true);
    mAAA->SetAwbEnabled(true);
    mAAA->AwbApplyResults();
    mAAA->AeApplyResults();
    mAAA->AfApplyResults();
}

void IntelCamera::runAeAfAwb(void)
{
    //3A should be initialized
    mAAA->GetStatistics();

    mAAA->AeProcess();
    mAAA->AfProcess();
    mAAA->AwbProcess();

    mAAA->AwbApplyResults();
    mAAA->AeApplyResults();
}

void IntelCamera::setStillAfStatus(bool status)
{
     Mutex::Autolock lock(mStillAfLock);
     mStillAfRunning = status;
}

bool IntelCamera::runStillAfSequence(void)
{
    //The preview thread is stopped at this point
    bool af_status = false;
    mAAA->AeLock(true);
    mAAA->SetAfEnabled(false);
    mAAA->SetAeEnabled(false);
    mAAA->SetAwbEnabled(false);
    mAAA->SetAfStillEnabled(true);
    mAAA->AfStillStart();
    // Do more than 100 time
    for (int i = 0; i < mStillAfMaxCount; i++) {
        mStillAfLock.lock();
        mStillAfCondition.wait(mStillAfLock);
        mStillAfLock.unlock();
        mAAA->GetStatistics();
        mAAA->AfProcess();
        af_status = (bool)(mAAA->AfStillIsComplete());
        if (af_status)
            break;
    }
    mAAA->AfStillStop ();
    mAAA->AeLock(false);
    mAAA->SetAfEnabled(true);
    mAAA->SetAeEnabled(true);
    mAAA->SetAwbEnabled(true);

    mAAA->SetAfStillEnabled(false);
    return af_status;
}

AAAProcess *IntelCamera::getmAAA(void) {
    return mAAA;
}

int IntelCamera::setColorEffect(unsigned int effect)
{
	int ret;

	if (main_fd < 0) {
        LOGV("%s: device not opened\n", __func__);
        return -1;
    }

	ret = cam_driver_set_tone_mode(main_fd, (enum v4l2_colorfx)effect);
	if (ret)
		LOGE("Error setting color effect:%d, fd:%d\n", effect, main_fd);\

	return ret;
}

//Remove padding for 176x144 resolution
void IntelCamera::trimRGB565(unsigned char *src, unsigned char* dst,
                             int src_width, int src_height,
                             int dst_width, int dst_height)
{
    for (int i=0; i<dst_height; i++) {
        memcpy( (unsigned char *)dst+i*2*dst_width,
                (unsigned char *)src+i*src_width,
                2*dst_width);
    }

}

void IntelCamera::trimNV12(unsigned char *src, unsigned char* dst,
                           int src_width, int src_height,
                           int dst_width, int dst_height)
{
    unsigned char *dst_y, *dst_uv;
    unsigned char *src_y, *src_uv;

    dst_y = dst;
    src_y = src;

    LOG2("%s:%s:%d", __FILE__, __func__, __LINE__);
    LOG2("%d:%d:%d:%d", src_width, src_height, dst_width, dst_height);

    for (int i = 0; i < dst_height; i++) {
        memcpy( (unsigned char *)dst_y + i * dst_width,
                (unsigned char *)src_y + i * src_width,
                dst_width);
    };

    dst_uv = dst_y + dst_width * dst_height;
    src_uv = src_y + src_width * src_height;

    for (int j=0; j<dst_height / 2; j++) {
        memcpy( (unsigned char *)dst_uv + j * dst_width,
                (unsigned char *)src_uv + j * src_width,
                dst_width);
    };
}

}; // namespace android
