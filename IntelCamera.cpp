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
    video_fds[V4L2_THIRD_DEVICE] = -1;
    main_fd = -1;
    m_flag_camera_start[0] = 0;
    m_flag_camera_start[1] = 0;
    mStillAfRunning = false;
    mFlashNecessary = false;
    mInitGamma = false;

    // init ISP settings
    mIspSettings.contrast = 256;			// 1.0
    mIspSettings.brightness = 0;
    mIspSettings.inv_gamma = false;		// no inverse

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
    m_postview_v4lformat = V4L2_PIX_FMT_NV12;

    m_snapshot_width = 2560;
    m_snapshot_pad_width = 2560;
    m_snapshot_height = 1920;
    m_snapshot_v4lformat = V4L2_PIX_FMT_RGB565;

    m_recorder_width = 1920;
    m_recorder_pad_width = 1920;
    m_recorder_height = 1080;
    m_recorder_v4lformat = V4L2_PIX_FMT_NV12;

    mColorEffect = DEFAULT_COLOR_EFFECT;
    mXnrOn = DEFAULT_XNR;
    mTnrOn = DEFAULT_TNR;
    mMacc = DEFAULT_MACC;
    mNrEeOn = DEFAULT_NREE;

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

//File Input
int IntelCamera::initFileInput()
{
    int ret;

    // open the third device
    int device = V4L2_THIRD_DEVICE;
    video_fds[device] = v4l2_capture_open(device);

    if (video_fds[device] < 0)
        return -1;

    // Query and check the capabilities
    if (v4l2_capture_querycap(video_fds[device], device, &cap) < 0)
        goto error0;

    // Set ISP parameter
    if (v4l2_capture_s_parm(video_fds[device], device, &parm) < 0)
        goto error0;

    return 0;

error0:
    v4l2_capture_close(video_fds[device]);
    video_fds[V4L2_THIRD_DEVICE] = -1;
    return -1;
}

int IntelCamera::deInitFileInput()
{
    int device = V4L2_THIRD_DEVICE;

    if (video_fds[device] < 0) {
        LOGW("%s: Already closed\n", __func__);
        return 0;
    }

    destroyBufferPool(device);

    v4l2_capture_close(video_fds[device]);

    video_fds[device] = -1;
    return 0;
}

int IntelCamera::configureFileInput(const struct file_input *image)
{
    int ret = 0;
    int device = V4L2_THIRD_DEVICE;
    int fd = video_fds[device];
    int buffer_count = 1;

    if (NULL == image) {
        LOGE("%s, struct file_input NULL pointer\n", __func__);
        return -1;
    }

    if (NULL == image->name) {
        LOGE("%s, file_name NULL pointer\n", __func__);
        return -1;
    }

    if (read_file(image->name,
                  image->width,
                  image->height,
                  image->format,
                  image->bayer_order) < 0)
        return -1;

    //Set the format
    ret = v4l2_capture_s_format(fd, device, image->width, image->height, image->format);
    if (ret < 0)
        return ret;

    current_w[device] = image->width;
    current_h[device] = image->height;
    current_v4l2format[device] = image->format;

    //request, query and mmap the buffer and save to the pool
    ret = createBufferPool(device, buffer_count);
    if (ret < 0)
        return ret;

    // QBUF
    ret = activateBufferPool(device);
    if (ret < 0)
        return ret;

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

    if (use_texture_streaming) {
        int device = V4L2_SECOND_DEVICE;
        int w = m_postview_width;
        int h = m_postview_height;
        int fourcc = m_postview_v4lformat;

        void *ptrs[SNAPSHOT_NUM_BUFFERS];
        int i;
        for (i = 0; i < SNAPSHOT_NUM_BUFFERS; i++) {
            ptrs[i] = v4l2_buf_pool[device].bufs[i].data;
        }
        v4l2_register_bcd(video_fds[device], SNAPSHOT_NUM_BUFFERS,
                          ptrs, w, h, fourcc, m_frameSize(fourcc, w, h));
    }

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

void IntelCamera::releasePostviewBcd(void)
{
    if (use_texture_streaming) {
        v4l2_release_bcd(video_fds[V4L2_SECOND_DEVICE]);
    }
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

int IntelCamera::SnapshotPostProcessing(void *img_data)
{
    // do red eye removal
    int img_size;

    // FIXME:
    // currently, if capture resolution more than 5M, camera will hang if ShRedEye_Remove() is called in 3A library
    // to workaround and make system not crash, maximum resolution for red eye removal is restricted to be 5M
    if (m_snapshot_width > 2560 || m_snapshot_height > 1920)
    {
        LOGD(" Bug here: picture size must not more than 5M for red eye removal\n");
        return -1;
    }

    img_size = m_frameSize (m_snapshot_v4lformat,
                                               m_snapshot_width,
                                               m_snapshot_height);

    mAAA->DoRedeyeRemoval (img_data,
                                                     img_size,
                                                     m_snapshot_width,
                                                     m_snapshot_height,
                                                     m_snapshot_v4lformat);

    return 0;
}

// We have a workaround here that the preview_out is not the real preview
// output from the driver. It is converted to RGB565.
// postview_rgb565: if it is NULL, we will not output RGB565 postview data
// postview_rgb565: if it is a pointer, write RGB565 data to this pointer
int IntelCamera::getSnapshot(void **main_out, void **postview, void *postview_rgb565)
{
    LOG1("%s\n", __func__);
    //Running flash before the snapshot
    if (mFlashNecessary) {
        captureFlashOnCertainDuration(0, 500, 15*625); /* software trigger, 500ms, intensity 15*/
        putSnapshot(0);
    }

    int index0 = grabFrame(V4L2_FIRST_DEVICE);
    if (index0 < 0) {
        LOGE("%s error\n", __func__);
        return -1;
    }

    int index1 = grabFrame(V4L2_SECOND_DEVICE);
    if (index1 < 0) {
        LOGE("%s error\n", __func__);
        return -1;
    }
    if (index0 != index1) {
        LOGE("%s error\n", __func__);
        return -1;
    }

    *main_out = v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index0].data;
    *postview = v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index0].data;

    if (need_dump_snapshot) {
        struct v4l2_buffer_info *buf0 =
            &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index0];
        struct v4l2_buffer_info *buf1 =
            &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index0];
        const char *name0 = "snap_v0.rgb";
        const char *name1 = "snap_v1.nv12";
        write_image(*main_out, buf0->length, buf0->width, buf0->height, name0);
        write_image(*postview, buf1->length, buf1->width, buf1->height, name1);
    }

    if(postview_rgb565) {
        toRGB565(m_postview_width, m_postview_height, m_postview_v4lformat, (unsigned char *)(*postview),
            (unsigned char *)postview_rgb565);
        LOG1("postview w:%d, h:%d, dstaddr:0x%x",
            m_postview_width, m_postview_height, (int)postview_rgb565);
    }

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

    ret = configureDevice(V4L2_FIRST_DEVICE, m_recorder_pad_width,
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
    if (index0 < 0) {
        LOGE("%s error\n", __func__);
        return -1;
    }

    int index1 = grabFrame(V4L2_SECOND_DEVICE);
    if (index1 < 0) {
        LOGE("%s error\n", __func__);
        return -1;
    }

    if (index0 != index1) {
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

    //Padding remove for not 64 align width
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
    if (v4l2_capture_querycap(video_fds[device], device, &cap) < 0)
        goto error0;

    main_fd = video_fds[device];
    mAAA->IspSetFd(main_fd);

    // load init gamma table only once
    if (mInitGamma == false)
    {
        cam_driver_init_gamma (main_fd, mIspSettings.contrast, mIspSettings.brightness, mIspSettings.inv_gamma);
        mInitGamma = true;
    }

    //Do some other intialization here after open
    flushISPParameters();

    //Choose the camera sensor
    //TODO: Following change is not valid and reasonable in normal case, 
    //if the power sequence of camera sensors is following android request(
    //0-main camera, 1--secondary camera).
    //Before we get Intel fw fix for power sequence, we use this as one work
    //around.
    if(CAMERA_ID_FRONT == m_camera_id)
    {
	    ret = v4l2_capture_s_input(video_fds[device], 0);
	    if (ret < 0)
		    return ret;
    }
    else
    {
	    ret = v4l2_capture_s_input(video_fds[device], 1);
	    if (ret < 0)
		    return ret;
    }

    if (mode == PREVIEW_MODE)
        return video_fds[device];

    //Open the second device node
    device = V4L2_SECOND_DEVICE;
    video_fds[device] = v4l2_capture_open(device);

    if (video_fds[device] < 0) {
        goto error0;
    }
    // Query and check the capabilities
    if (v4l2_capture_querycap(video_fds[device], device, &cap) < 0)
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
    int framerate;
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
    ret = v4l2_capture_s_format(fd, device, w, h, fourcc);
    if (ret < 0)
        return ret;

    current_w[device] = w;
    current_h[device] = h;
    current_v4l2format[device] = fourcc;

    /* 3A related initialization*/
    //Reallocate the grid for 3A after format change
    if (device == V4L2_FIRST_DEVICE) {
        ret = v4l2_capture_g_framerate(fd, &framerate);
        if (ret<0)
            return ret;
        mAAA->SwitchMode(run_mode, framerate);
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
    num_buffers = v4l2_capture_request_buffers(fd, device, buffer_count);

    if (num_buffers <= 0)
        return -1;

    pool->active_buffers = num_buffers;

    for (i = 0; i < num_buffers; i++) {
        pool->bufs[i].width = current_w[device];
        pool->bufs[i].height = current_h[device];
        pool->bufs[i].fourcc = current_v4l2format[device];
        ret = v4l2_capture_new_buffer(fd, device, i, &pool->bufs[i]);
        if (ret < 0)
            goto error;
    }
    return 0;

error:
    for (int j = 0; j < i; j++)
        v4l2_capture_free_buffer(fd, device, &pool->bufs[j]);
    return ret;
}

void IntelCamera::destroyBufferPool(int device)
{
    LOG1("%s device %d\n", __func__, device);

    int fd = video_fds[device];
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];

    for (int i = 0; i < pool->active_buffers; i++)
        v4l2_capture_free_buffer(fd, device, &pool->bufs[i]);
    v4l2_capture_release_buffers(fd, device);
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
    if (device == V4L2_FIRST_DEVICE) {
        if (run_mode == STILL_IMAGE_MODE)
            update3Aresults();
    }

    if (device == V4L2_FIRST_DEVICE) {
        // flush 3A manual control to hardware
        mAAA->FlushManualSettings ();
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

void IntelCamera::nv12_to_rgb565(int width, int height, unsigned char* yuvs, unsigned char* rgbs) {
    //the end of the luminance data
    int lumEnd = width * height;
    //points to the next luminance value pair
    int lumPtr = 0;
    //points to the next chromiance value pair
    int chrPtr = lumEnd;
    //points to the next byte output pair of RGB565 value
    int outPtr = 0;
    //the end of the current luminance scanline
    int lineEnd = width;

    while (true) {
        //skip back to the start of the chromiance values when necessary
        if (lumPtr == lineEnd) {
            if (lumPtr == lumEnd) break; //we've reached the end
            //division here is a bit expensive, but's only done once per scanline
            chrPtr = lumEnd + ((lumPtr  >> 1) / width) * width;
            lineEnd += width;
        }

        //read the luminance and chromiance values
        int Y1 = yuvs[lumPtr++] & 0xff;
        int Y2 = yuvs[lumPtr++] & 0xff;
        int Cb = (yuvs[chrPtr++] & 0xff) - 128;
        int Cr = (yuvs[chrPtr++] & 0xff) - 128;
        int R, G, B;

        //generate first RGB components
        B = Y1 + ((454 * Cb) >> 8);
        if(B < 0) B = 0; else if(B > 255) B = 255;
        G = Y1 - ((88 * Cb + 183 * Cr) >> 8);
        if(G < 0) G = 0; else if(G > 255) G = 255;
        R = Y1 + ((359 * Cr) >> 8);
        if(R < 0) R = 0; else if(R > 255) R = 255;
        //NOTE: this assume little-endian encoding
        rgbs[outPtr++]  = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
        rgbs[outPtr++]  = (unsigned char) ((R & 0xf8) | (G >> 5));

        //generate second RGB components
        B = Y2 + ((454 * Cb) >> 8);
        if(B < 0) B = 0; else if(B > 255) B = 255;
        G = Y2 - ((88 * Cb + 183 * Cr) >> 8);
        if(G < 0) G = 0; else if(G > 255) G = 255;
        R = Y2 + ((359 * Cr) >> 8);
        if(R < 0) R = 0; else if(R > 255) R = 255;
        //NOTE: this assume little-endian encoding
        rgbs[outPtr++]  = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
        rgbs[outPtr++]  = (unsigned char) ((R & 0xf8) | (G >> 5));
    }
}

void IntelCamera::toRGB565(int width, int height, int fourcc, void *src, void *dst)
{
    void *buffer;
    int size = width * height * 2;

    if (src == NULL || dst == NULL) {
        LOGE("%s, NULL pointer\n", __func__);
        return;
    }

    if (src == dst) {
        buffer = calloc(1, size);
        if (buffer == NULL) {
            LOGE("%s, error calloc\n", __func__);
            return;
        }
    } else
        buffer = dst;

    switch(fourcc) {
    case V4L2_PIX_FMT_YUV420:
        LOG1("%s, yuv420 to rgb565 conversion\n", __func__);
        yuv420_to_rgb565(width, height, (unsigned char*)src, (unsigned short*)buffer);
        break;
    case V4L2_PIX_FMT_NV12:
        LOG1("%s, nv12 to rgb565 conversion\n", __func__);
        nv12_to_rgb565(width, height, (unsigned char*)src, (unsigned char*)buffer);
        break;
    case V4L2_PIX_FMT_RGB565:
        break;
    default:
        LOGE("%s, unknown format\n", __func__);
        break;
    }

    if (src == dst) {
        memcpy(dst, buffer, size);
        free(buffer);
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

//Update the userptr from the hardware encoder for buffer sharing
//this function is called in the preview thread. So don't worry the
//contenstion with preview thread
int IntelCamera::updateRecorderUserptr(int num, unsigned char *recorder[])
{
    LOG1("%s start\n", __func__);
    int i, index, ret, last_index = 0;
    if (num > VIDEO_NUM_BUFFERS) {
        LOGE("%s:buffer number %d is out of range\n", __func__, num);
        return -1;
    }
    //DQ all buffer out
    for (i = 0; i < num; i++) {
        ret = grabFrame(V4L2_FIRST_DEVICE);
        if (ret < 0) {
            LOGE("%s error\n", __func__);
            goto error;
        }
        ret = grabFrame(V4L2_SECOND_DEVICE);
        if (ret < 0) {
            LOGE("%s error\n", __func__);
            goto error;
        }
        last_index = ret;
    }
    //Stop the DQ thread
    v4l2_capture_control_dq(main_fd, 0);

    //Update the main stream userptr
    for (i = 0; i < num; i++) {
        v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[i].data = recorder[i];
        v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[i].vbuffer.m.userptr =
            (long unsigned int)recorder[i];
    }

    //Q all of the new buffers to driver
    for (i = 0; i < num; i++) {
        //Qbuf use the same order as DQ
        index = (i + last_index + 1) % num;
        LOG1("Update new userptr %p\n", recorder[index]);
        ret = v4l2_capture_qbuf(video_fds[V4L2_FIRST_DEVICE], index,
                          &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index]);
        LOG1("Update new userptr %p finished\n", recorder[index]);

        ret = v4l2_capture_qbuf(video_fds[V4L2_SECOND_DEVICE], index,
                          &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index]);
    }
    //Start the DQ thread
    v4l2_capture_control_dq(main_fd, 1);
    LOG1("%s done\n", __func__);
    return 0;
error2:
    v4l2_capture_control_dq(main_fd, 1);
error:
    LOGE("%s error\n", __func__);
    return -1;
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

void IntelCamera::setIndicatorIntensity(int percent_time_100)
{
	if(CAMERA_ID_FRONT == m_camera_id) return;

    Mutex::Autolock lock(mFlashLock);
    cam_driver_led_indicator_trigger (main_fd, percent_time_100);
}

void IntelCamera::setAssistIntensity(int percent_time_100)
{
	if(CAMERA_ID_FRONT == m_camera_id) return;

    Mutex::Autolock lock(mFlashLock);
    cam_driver_led_assist_trigger (main_fd, percent_time_100);
}

void IntelCamera::setFlashMode(int mode)
{
    Mutex::Autolock lock(mFlashLock);
    mFlashMode=mode;
}

int IntelCamera::getFlashMode()
{
    Mutex::Autolock lock(mFlashLock);
    return mFlashMode;
}

int IntelCamera::calculateLightLevel()
{
    return mAAA->AeIsFlashNecessary (&mFlashNecessary);
}

void IntelCamera::captureFlashOff(void)
{
    cam_driver_led_flash_off (main_fd);
}

void IntelCamera::captureFlashOnCertainDuration(int mode,  int duration, int percent_time_100)
{
	if(CAMERA_ID_FRONT == m_camera_id) return;

    cam_driver_led_flash_trigger (main_fd, mode, duration, percent_time_100);
}

void IntelCamera::runPreFlashSequence(void)
{
    //We hold the mFlashLock here
    int index;
    void *data;

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
    captureFlashOnCertainDuration(0, 100, 1*625);  /* software trigger, 100ms, intensity 1*/
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

//limit it to 63 because bigger value easy to cause ISP timeout
#define MAX_ZOOM_LEVEL	63
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
    LOG1("%s: set zoom to %d", __func__, zoom);
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

void IntelCamera::setSnapshotUserptr(int index, void *pic_addr, void *pv_addr)
{
    if (index > SNAPSHOT_NUM_BUFFERS) {
        LOGE("%s:index %d is out of range\n", __func__, index);
        return ;
    }

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
    int af_mode;
    mAAA->AfGetMode (&af_mode);
    if (af_mode != CAM_AF_MODE_MANUAL)
        mAAA->AfApplyResults();
}

void IntelCamera::runAeAfAwb(void)
{
    //3A should be initialized
    mAAA->GetStatistics();
    //DVS for video
    if (run_mode == VIDEO_RECORDING_MODE) {
        mAAA->DisReadStatistics();
        mAAA->DisProcess(&mAAA->dvs_vector);
        mAAA->UpdateDisResults();
    }

    mAAA->AeProcess();

    int af_mode;
    mAAA->AfGetMode (&af_mode);
    if (af_mode != CAM_AF_MODE_MANUAL)
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
        mAAA->AfStillIsComplete(&af_status);
        if (af_status)
        {
            LOGD("==== still AF converge frame number %d\n", i);
            break;
        }
    }
    LOGD("==== still Af status (1: success; 0: failed) = %d\n", af_status);

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


int IntelCamera::setColorEffect(int effect)
{
    int ret;
    mColorEffect = effect;
    if (main_fd < 0){
        LOGD("%s:Set Color Effect failed. "
                "will set after device is open.\n", __FUNCTION__);
        return 0;
    }
    ret = cam_driver_set_tone_mode(main_fd,
                        (enum v4l2_colorfx)effect);
    if (ret) {
        LOGE("Error setting color effect:%d, fd:%d\n", effect, main_fd);
        return -1;
    }

    bool b_update = false;      // update only if necessary, avoid reloading the same settings
    switch(effect)
    {
    case V4L2_COLORFX_NEGATIVE:
        if (mIspSettings.inv_gamma == false)
        {
            mIspSettings.inv_gamma = true;
            b_update = true;
        }
        break;
    default:
        if (mIspSettings.inv_gamma == true)
        {
            mIspSettings.inv_gamma = false;
            b_update = true;
        }
    }
    if (b_update == true)
    {
        ret = cam_driver_set_contrast_bright(main_fd, mIspSettings.contrast, mIspSettings.brightness, mIspSettings.inv_gamma);
        if (ret != CAM_ERR_NONE)
        {
            LOGE("Error setting contrast and brightness in color effect:%d, fd:%d\n", effect, main_fd);
            return -1;
        }
    }

    return 0;
}

int IntelCamera::setXNR(bool on)
{
    int ret;
    mXnrOn= on;
    if (main_fd < 0){
        LOGD("%s:Set XNR failed. "
                "will set after device is open.\n", __FUNCTION__);
        return 0;
    }
    ret = cam_driver_set_xnr(main_fd, on);
    if (ret) {
        LOGE("Error setting xnr:%d, fd:%d\n", on, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::setTNR(bool on)
{
    int ret;
    mTnrOn= on;
    if (main_fd < 0){
        LOGD("%s:Set TNR failed."
                " will set after device is open.\n", __FUNCTION__);
        return 0;
    }
    ret = cam_driver_set_tnr(main_fd, on);
    if (ret) {
        LOGE("Error setting tnr:%d, fd:%d\n", on, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::setNREE(bool on)
{
    int ret, ret2;
    mNrEeOn= on;
    if (main_fd < 0){
        LOGD("%s:Set NR/EE failed."
                " will set after device is open.\n", __FUNCTION__);
        return 0;
    }
    ret = cam_driver_set_ee(main_fd,on);
    ret2 = cam_driver_set_bnr(main_fd,on);

    if (ret || ret2) {
        LOGE("Error setting NR/EE:%d, fd:%d\n", on, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::setMACC(int macc)
{
    int ret;
    mMacc= macc;
    if (main_fd < 0){
        LOGD("%s:Set MACC failed. "
                "will set after device is open.\n", __FUNCTION__);
        return 0;
    }
    ret = cam_driver_set_macc(main_fd,1,macc);
    if (ret) {
        LOGE("Error setting MACC:%d, fd:%d\n", macc, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::flushISPParameters ()
{
    int ret, ret2;

    if (main_fd < 0){
        LOGD("%s:flush Color Effect failed."
                " will set after device is open.\n", __FUNCTION__);
        return 0;
    }

    //flush color effect
    if (mColorEffect != DEFAULT_COLOR_EFFECT){
        ret = cam_driver_set_tone_mode(main_fd,
                (enum v4l2_colorfx)mColorEffect);
        if (ret) {
            LOGE("Error setting color effect:%d, fd:%d\n",
                            mColorEffect, main_fd);
        }
        else {
            LOGE("set color effect success to %d in %s.\n", mColorEffect, __FUNCTION__);
        }
    }
    else  LOGD("ignore color effect setting");

	// do gamma inverse only if start status is negative effect
    if (mColorEffect == V4L2_COLORFX_NEGATIVE)
    {
        mIspSettings.inv_gamma = true;
        ret = cam_driver_set_contrast_bright(main_fd, mIspSettings.contrast, mIspSettings.brightness, mIspSettings.inv_gamma);
        if (ret != CAM_ERR_NONE)
        {
            LOGE("Error setting contrast and brightness in color effect flush:%d, fd:%d\n", mColorEffect, main_fd);
            return -1;
        }
    }


    //flush xnr
    if (mXnrOn != DEFAULT_XNR){
        ret = cam_driver_set_xnr(main_fd, mXnrOn);
        if (ret) {
            LOGE("Error setting xnr:%d, fd:%d\n",  mXnrOn, main_fd);
            return -1;
        }
        mColorEffect = DEFAULT_COLOR_EFFECT;
    }
    else  LOGD("ignore xnr setting");

    //flush tnr
    if (mTnrOn != DEFAULT_TNR){
        ret = cam_driver_set_tnr(main_fd, mTnrOn);
        if (ret) {
            LOGE("Error setting xnr:%d, fd:%d\n", mTnrOn, main_fd);
            return -1;
        }
    }

    //flush nr/ee
    if (mNrEeOn != DEFAULT_NREE){
        ret = cam_driver_set_ee(main_fd,mNrEeOn);
        ret2 = cam_driver_set_bnr(main_fd,mNrEeOn);

        if (ret || ret2) {
            LOGE("Error setting NR/EE:%d, fd:%d\n", mNrEeOn, main_fd);
            return -1;
        }
    }

    //flush macc
    if (mMacc != DEFAULT_MACC){
        ret = cam_driver_set_macc(main_fd,1,mMacc);
        if (ret) {
            LOGE("Error setting NR/EE:%d, fd:%d\n", mMacc, main_fd);
        }
    }

    return 0;
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
