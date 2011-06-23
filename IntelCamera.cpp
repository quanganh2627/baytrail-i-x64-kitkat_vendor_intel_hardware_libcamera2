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

#ifndef LOG_TAG
#define LOG_TAG "IntelCamera"
#endif
#include <utils/Log.h>

#include "IntelCamera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <utils/Log.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <math.h>
#include <sys/mman.h>
#include "CameraAAAProcess.h"

#define BPP 2

#define _xioctl(fd, ctl, arg) xioctl(fd, ctl, arg, #ctl)

#define CLEAR(x) memset (&(x), 0, sizeof (x))
#define PAGE_ALIGN(x) ((x + 0xfff) & 0xfffff000)

static char *dev_name_array[3] = {"/dev/video0", "/dev/video1",
                                        "/dev/video2"};

#define CFG_PATH "/system/etc/atomisp/atomisp.cfg"
#define LINE_BUF_SIZE	64

struct ParamList {
	unsigned int index;
	unsigned int value;
};

enum ParamIndex {
	SWITCH,
	MACC,
	SC,
	GDC,
	IE,
	GAMMA,
	BPC,
	FPN,
	BLC,
	EE,
	NR,
	XNR,
	BAYERDS,
	ZOOM,
	MF,
	ME,
	MWB,
	ISO,
	DIS,
	DVS,
	FCC,
	REDEYE,
	NUM_OF_CFG,
};


/* For General Param */
enum ParamValueIndex_General {
	FUNC_DEFAULT,
	FUNC_ON,
	FUNC_OFF,
	NUM_OF_GENERAL,
};


/* For Macc Param */
enum ParamValueIndex_Macc {
	MACC_NONE,
	MACC_GRASSGREEN,
	MACC_SKYBLUE,
	MACC_SKIN,
	NUM_OF_MACC,
} ;


/* For IE Param */
enum ParamValueIndex_Ie {
	IE_NONE,
	IE_MONO,
	IE_SEPIA,
	IE_NEGATIVE,
	NUM_OF_IE,
};


static char *FunctionKey[] = {
	"switch", /* Total switch, to decide whether enable the config file */
	"macc", /* macc config */
	"sc",	/* shading correction config */
	"gdc",	/* gdc config */
	"ie",	/* image effect */
	"gamma", /* gamma/tone-curve setting */
	"bpc",	/* bad pixel correction */
	"fpn",
	"blc",	/* black level compensation */
	"ee",	/* edge enhancement */
	"nr",	/* noise reduction */
	"xnr",	/* xnr */
	"bayer_ds",
	"zoom",
	"focus_pos",
	"expo_pos",
	"wb_mode",
	"iso",
	"dis",
	"dvs",
	"fcc",
	"redeye",
};

static char *FunctionOption_Macc[] = {
	"none",
	"grass-green",
	"sky-blue",
	"skin",
};

static char *FunctionOption_Ie[] = {
	"none",
	"mono",
	"sepia",
	"negative",
};

static char *FunctionOption_General[] = {
	"default",
	"on",
	"off",
};

unsigned int default_function_value_list[] = {
	FUNC_OFF,	/* switch */
	MACC_NONE,	/* macc */
	FUNC_OFF,	/* sc */
	FUNC_OFF,	/* GDC */
	IE_NONE,	/* IE */
	FUNC_OFF,	/* GAMMA */
	FUNC_OFF,	/* BPC */
	FUNC_OFF,	/* FPN */
	FUNC_OFF,	/* BLC */
	FUNC_OFF,	/* EE */
	FUNC_OFF,	/* NR */
	FUNC_OFF,	/* XNR */
	FUNC_OFF,	/* BAY_DS */
	0,	/* ZOOM */
	0,	/* FOCUS_POS */
	0,	/* EXPO_POS */
	0,	/* WB_MODE */
	0,	/* ISO */
	FUNC_OFF,	/* DIS */
	FUNC_OFF,	/* DVS */
	FUNC_OFF,	/* FCC */
	FUNC_OFF,	/* REDEYE */
};

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
    LOGD("%s, m_camera_id = %d\n", __func__, m_camera_id);

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
    mGDCOn = DEFAULT_GDC;

    // Do the basic init before open here
    if (!m_flag_init) {
        //Parse the configure from file
        atomisp_parse_cfg_file();

        m_flag_init = 1;
    }

    file_injection = false;
    g_isp_timeout = 0;
    //Gamma table initialization
    g_cfg_gm.GmVal = 1.5;
    g_cfg_gm.GmVal = 1.5;
    g_cfg_gm.GmToe = 123;
    g_cfg_gm.GmKne = 287;
    g_cfg_gm.GmDyr = 256;
    g_cfg_gm.GmLevelMin = 0;
    g_cfg_gm.GmLevelMax = 255;
    return ret;
}

int IntelCamera::deinitCamera(void)
{
    if (m_flag_init) {
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

    if (v4l2_read_file(image->name,
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
        ret = v4l2_register_bcd(video_fds[device], PREVIEW_NUM_BUFFERS,
                          ptrs, w, h, fourcc, m_frameSize(fourcc, w, h));
        if (ret < 0)
            return ret;
    }

    ret = startCapture(device, PREVIEW_NUM_BUFFERS);
    if (ret < 0)
        return ret;

    return main_fd;
}

void IntelCamera::stopCameraPreview(void)
{
    LOG1("%s\n", __func__);
    int device = V4L2_FIRST_DEVICE;
    if (!m_flag_camera_start[device]) {
        LOG1("%s: doing nothing because m_flag_camera_start is zero", __func__);
        return ;
    }
    int fd = video_fds[device];

    if (fd <= 0) {
        LOGD("(%s):Camera was already closed\n", __func__);
        return ;
    }

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
void IntelCamera::checkGDC(void)
{
    //flush GDC FIXME: Only have effect for 14M now.
    if (mGDCOn && m_snapshot_width == 4352 && m_snapshot_height == 3264) {
        LOGD("%s: GDC is enabled now\n", __func__);
        //Only enable GDC in still image capture mode and 14M
        if (atomisp_set_gdc(main_fd, true))
            LOGE("Error setting gdc:%d, fd:%d\n", true, main_fd);
        else
            v4l2_set_isp_timeout(ATOMISP_FILEINPUT_POLL_TIMEOUT);
    }
}

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
        ret = v4l2_register_bcd(video_fds[device], SNAPSHOT_NUM_BUFFERS,
                          ptrs, w, h, fourcc, m_frameSize(fourcc, w, h));
        if (ret < 0)
            goto registerbcd_error;
    }

    ret = startCapture(V4L2_FIRST_DEVICE, SNAPSHOT_NUM_BUFFERS);
    if (ret < 0)
        goto start1_error;


    ret = startCapture(V4L2_SECOND_DEVICE, SNAPSHOT_NUM_BUFFERS);
    if (ret < 0)
        goto start2_error;
    return main_fd;

start2_error:
    stopCapture(V4L2_FIRST_DEVICE);
start1_error:
registerbcd_error:
configure2_error:
configure1_error:
    closeDevice();
    return ret;
}

void IntelCamera::stopSnapshot(void)
{
    stopDualStreams();
    v4l2_set_isp_timeout(0);
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


// We have a workaround here that the preview_out is not the real preview
// output from the driver. It is converted to RGB565.
// postview_rgb565: if it is NULL, we will not output RGB565 postview data
// postview_rgb565: if it is a pointer, write RGB565 data to this pointer
int IntelCamera::getSnapshot(void **main_out, void **postview, void *postview_rgb565)
{
    LOG1("%s\n", __func__);

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

    //flush tnr
    if (mTnrOn != DEFAULT_TNR){
        ret = atomisp_set_tnr(main_fd, mTnrOn);
        if (ret) {
            LOGE("Error setting xnr:%d, fd:%d\n", mTnrOn, main_fd);
            return -1;
        }
    }

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

    return main_fd;

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

    // color space conversion for non-nv12 format
    if (m_recorder_v4lformat != V4L2_PIX_FMT_NV12)
        toNV12(m_recorder_width, m_recorder_height, m_recorder_v4lformat,
               (unsigned char *)(*main_out), (unsigned char *)(*main_out));

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

    // load init gamma table only once
    if (mInitGamma == false)
    {
        atomisp_init_gamma (main_fd, mIspSettings.contrast,
                            mIspSettings.brightness, mIspSettings.inv_gamma);
        mInitGamma = true;
    }

    //Do some other intialization here after open
    flushISPParameters();

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

    //Close the second device
    if (video_fds[V4L2_SECOND_DEVICE] < 0)
        return ;
    v4l2_capture_close(video_fds[V4L2_SECOND_DEVICE]);
    video_fds[V4L2_SECOND_DEVICE] = -1;
}

int IntelCamera::configureDevice(int device, int w, int h, int fourcc)
{
    int ret = 0;
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
        ret = v4l2_capture_g_framerate(fd, &framerate, w, h, fourcc);
        if (ret < 0) {
            /*Error handler: if driver does not support FPS achieving,
                        just give the default value.*/
            framerate = DEFAULT_SENSOR_FPS;
            ret = 0;
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

float IntelCamera::getFramerate(void)
{
    return framerate;
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

void IntelCamera::yuv420_to_yuv420sp(int width, int height, unsigned char *src, unsigned char *dst)
{
    unsigned char *y, *u, *v;
    unsigned char *yy, *uv;
    int h,w;

    y = src;
    u = y + (width * height);
    v = u + (width * height / 4);

    //Copy Y plane
    for (h = 0; h < height; h++)
        memcpy(dst + h * width, src + h * width, width);

    src += width * height;
    dst += width * height;

    // Change UV plane
    for (h = 0; h < height / 2; h++) {
        for (w = 0; w < width; w+=2) {
            *(dst + w) = *(u++);
            *(dst + w + 1) = *(v++);
        }
        dst += width;
    }
}

void IntelCamera::yuyv422_to_yuv420sp(int width, int height, unsigned char *bufsrc, unsigned char *bufdest)
{
    unsigned char *ptrsrcy1,*ptrsrcy2;
    unsigned char *ptrsrcy3,*ptrsrcy4;
    unsigned char *ptrsrccb1,*ptrsrccb2;
    unsigned char *ptrsrccb3,*ptrsrccb4;
    unsigned char *ptrsrccr1,*ptrsrccr2;
    unsigned char *ptrsrccr3,*ptrsrccr4;
    int srcystride,srcccstride;

    ptrsrcy1  = bufsrc ;
    ptrsrcy2  = bufsrc + (width<<1) ;
    ptrsrcy3  = bufsrc + (width<<1)*2 ;
    ptrsrcy4  = bufsrc + (width<<1)*3 ;

    ptrsrccb1 = bufsrc + 1;
    ptrsrccb2 = bufsrc + (width<<1) + 1;
    ptrsrccb3 = bufsrc + (width<<1)*2 + 1;
    ptrsrccb4 = bufsrc + (width<<1)*3 + 1;

    ptrsrccr1 = bufsrc + 3;
    ptrsrccr2 = bufsrc + (width<<1) + 3;
    ptrsrccr3 = bufsrc + (width<<1)*2 + 3;
    ptrsrccr4 = bufsrc + (width<<1)*3 + 3;

    srcystride  = (width<<1)*3;
    srcccstride = (width<<1)*3;

    unsigned char *ptrdesty1,*ptrdesty2;
    unsigned char *ptrdesty3,*ptrdesty4;
    unsigned char *ptrdestcb1,*ptrdestcb2;
    unsigned char *ptrdestcr1,*ptrdestcr2;
    int destystride,destccstride;

    ptrdesty1 = bufdest;
    ptrdesty2 = bufdest + width;
    ptrdesty3 = bufdest + width*2;
    ptrdesty4 = bufdest + width*3;

    ptrdestcb1 = bufdest + width*height;
    ptrdestcb2 = bufdest + width*height + width;

    ptrdestcr1 = bufdest + width*height + 1;
    ptrdestcr2 = bufdest + width*height + width + 1;

    destystride  = (width)*3;
    destccstride = width;

    int i,j;

    for(j=0;j<(height/4);j++)
    {
        for(i=0;i<(width/2);i++)
        {
            (*ptrdesty1++) = (*ptrsrcy1);
            (*ptrdesty2++) = (*ptrsrcy2);
            (*ptrdesty3++) = (*ptrsrcy3);
            (*ptrdesty4++) = (*ptrsrcy4);

            ptrsrcy1 += 2;
            ptrsrcy2 += 2;
            ptrsrcy3 += 2;
            ptrsrcy4 += 2;

            (*ptrdesty1++) = (*ptrsrcy1);
            (*ptrdesty2++) = (*ptrsrcy2);
            (*ptrdesty3++) = (*ptrsrcy3);
            (*ptrdesty4++) = (*ptrsrcy4);

            ptrsrcy1 += 2;
            ptrsrcy2 += 2;
            ptrsrcy3 += 2;
            ptrsrcy4 += 2;

            (*ptrdestcb1) = (*ptrsrccb1);
            (*ptrdestcb2) = (*ptrsrccb3);
            ptrdestcb1 += 2;
            ptrdestcb2 += 2;

            ptrsrccb1 += 4;
            ptrsrccb3 += 4;

            (*ptrdestcr1) = (*ptrsrccr1);
            (*ptrdestcr2) = (*ptrsrccr3);
            ptrdestcr1 += 2;
            ptrdestcr2 += 2;

            ptrsrccr1 += 4;
            ptrsrccr3 += 4;

        }


        ptrsrcy1  += srcystride;
        ptrsrcy2  += srcystride;
        ptrsrcy3  += srcystride;
        ptrsrcy4  += srcystride;

        ptrsrccb1 += srcccstride;
        ptrsrccb3 += srcccstride;

        ptrsrccr1 += srcccstride;
        ptrsrccr3 += srcccstride;


        ptrdesty1 += destystride;
        ptrdesty2 += destystride;
        ptrdesty3 += destystride;
        ptrdesty4 += destystride;

        ptrdestcb1 += destccstride;
        ptrdestcb2 += destccstride;

        ptrdestcr1 += destccstride;
        ptrdestcr2 += destccstride;
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

void IntelCamera::toNV12(int width, int height, int fourcc, void *src, void *dst)
{
    void *buffer;
    int size = width * height * 3 / 2;

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
    case V4L2_PIX_FMT_YUYV:
        LOG1("%s, yuyv422 to yuv420sp conversion\n", __func__);
        yuyv422_to_yuv420sp(width, height, (unsigned char*)src, (unsigned char*)buffer);
        break;
    case V4L2_PIX_FMT_YUV420:
        LOG1("%s, yuv420 to yuv420sp conversion\n", __func__);
        yuv420_to_yuv420sp(width, height, (unsigned char*)src, (unsigned char*)buffer);
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

void IntelCamera::setIndicatorIntensity(int percent_time_100)
{
	if(CAMERA_ID_FRONT == m_camera_id) return;

    atomisp_led_indicator_trigger (main_fd, percent_time_100);
}

void IntelCamera::setAssistIntensity(int percent_time_100)
{
	if(CAMERA_ID_FRONT == m_camera_id) return;

    atomisp_led_assist_trigger (main_fd, percent_time_100);
}

void IntelCamera::setFlashMode(int mode)
{
    mFlashMode = mode;
}

int IntelCamera::getFlashMode()
{
    return mFlashMode;
}

void IntelCamera::captureFlashOff(void)
{
    atomisp_led_flash_off (main_fd);
}

void IntelCamera::captureFlashOnCertainDuration(int mode,  int duration, int percent_time_100)
{
	if(CAMERA_ID_FRONT == m_camera_id) return;

    atomisp_led_flash_trigger (main_fd, mode, duration, percent_time_100);
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
    return atomisp_set_zoom (main_fd, zoom);
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
    return atomisp_set_capture_mode(main_fd, mode);
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

int IntelCamera::m_frameSize(int format, int width, int height)
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

int IntelCamera::setColorEffect(int effect)
{
    int ret;
    mColorEffect = effect;
    if (main_fd < 0){
        LOGD("%s:Set Color Effect failed. "
                "will set after device is open.\n", __FUNCTION__);
        return 0;
    }
    ret = atomisp_set_tone_mode(main_fd,
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
        ret = atomisp_set_contrast_bright(main_fd, mIspSettings.contrast, mIspSettings.brightness, mIspSettings.inv_gamma);
        if (ret != 0)
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
    mXnrOn = on;
    if (main_fd < 0){
        LOGD("%s:Set XNR failed. "
                "will set after device is open.\n", __FUNCTION__);
        return 0;
    }
    ret = atomisp_set_xnr(main_fd, on);
    if (ret) {
        LOGE("Error setting xnr:%d, fd:%d\n", on, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::setGDC(bool on)
{
    int ret;
    mGDCOn = on;
    //set the GDC when do the still image capture
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
    ret = atomisp_set_tnr(main_fd, on);
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
    ret = atomisp_set_ee(main_fd,on);
    ret2 = atomisp_set_bnr(main_fd,on);

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
    ret = atomisp_set_macc(main_fd,1,macc);
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
        ret = atomisp_set_tone_mode(main_fd,
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
        ret = atomisp_set_contrast_bright(main_fd, mIspSettings.contrast, mIspSettings.brightness, mIspSettings.inv_gamma);
        if (ret != 0)
        {
            LOGE("Error setting contrast and brightness in color effect flush:%d, fd:%d\n", mColorEffect, main_fd);
            return -1;
        }
    }


    //flush xnr
    if (mXnrOn != DEFAULT_XNR){
        ret = atomisp_set_xnr(main_fd, mXnrOn);
        if (ret) {
            LOGE("Error setting xnr:%d, fd:%d\n",  mXnrOn, main_fd);
            return -1;
        }
        mColorEffect = DEFAULT_COLOR_EFFECT;
    }
    else
        LOGD("ignore xnr setting");

    //flush nr/ee
    if (mNrEeOn != DEFAULT_NREE){
        ret = atomisp_set_ee(main_fd,mNrEeOn);
        ret2 = atomisp_set_bnr(main_fd,mNrEeOn);

        if (ret || ret2) {
            LOGE("Error setting NR/EE:%d, fd:%d\n", mNrEeOn, main_fd);
            return -1;
        }
    }

    //flush macc
    if (mMacc != DEFAULT_MACC){
        ret = atomisp_set_macc(main_fd,1,mMacc);
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

int IntelCamera::v4l2_capture_open(int device)
{
    int fd;
    struct stat st;

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_THIRD_DEVICE)) {
        LOGE("ERR(%s): Wrong device node %d\n", __func__, device);
        return -1;
    }

    char *dev_name = dev_name_array[device];
    LOG1("---Open video device %s---", dev_name);

    if (stat (dev_name, &st) == -1) {
        LOGE("ERR(%s): Error stat video device %s: %s", __func__,
             dev_name, strerror(errno));
        return -1;
    }

    if (!S_ISCHR (st.st_mode)) {
        LOGE("ERR(%s): %s not a device", __func__, dev_name);
        return -1;
    }

    fd = open(dev_name, O_RDWR);

    if (fd <= 0) {
        LOGE("ERR(%s): Error opening video device %s: %s", __func__,
             dev_name, strerror(errno));
        return -1;
    }

    if (device == V4L2_THIRD_DEVICE)
        file_injection = true;

    return fd;
}

void IntelCamera::v4l2_capture_close(int fd)
{
    /* close video device */
    LOG1("----close device ---");
    if (fd < 0) {
        LOGW("W(%s): Not opened\n", __func__);
        return ;
    }

    if (close(fd) < 0) {
        LOGE("ERR(%s): Close video device failed!", __func__);
        return;
    }
    file_injection = false;
}

int IntelCamera::v4l2_capture_querycap(int fd, int device, struct v4l2_capability *cap)
{
    int ret = 0;

    ret = ioctl(fd, VIDIOC_QUERYCAP, cap);

    if (ret < 0) {
        LOGE("ERR(%s): :VIDIOC_QUERYCAP failed\n", __func__);
        return ret;
    }

    if (device == V4L2_THIRD_DEVICE) {
        if (!(cap->capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
            LOGE("ERR(%s):  no output devices\n", __func__);
            return -1;
        }
        return ret;
    }

    if (!(cap->capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOGE("ERR(%s):  no capture devices\n", __func__);
        return -1;
    }

    if (!(cap->capabilities & V4L2_CAP_STREAMING)) {
        LOGE("ERR(%s): is no video streaming device", __func__);
        return -1;
    }

    LOG1( "driver:      '%s'", cap->driver);
    LOG1( "card:        '%s'", cap->card);
    LOG1( "bus_info:      '%s'", cap->bus_info);
    LOG1( "version:      %x", cap->version);
    LOG1( "capabilities:      %x", cap->capabilities);

    return ret;
}

int IntelCamera::v4l2_capture_s_input(int fd, int index)
{
    struct v4l2_input input;
    int ret;

    LOG1("VIDIOC_S_INPUT");
    input.index = index;

    ret = ioctl(fd, VIDIOC_S_INPUT, &input);

    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_INPUT index %d failed\n", __func__,
             input.index);
        return ret;
    }
    return ret;
}


int IntelCamera::v4l2_capture_s_format(int fd, int device, int w, int h, int fourcc)
{
    int ret;
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);
    LOG1("VIDIOC_S_FMT");

    if (device == V4L2_THIRD_DEVICE) {
        g_isp_timeout = ATOMISP_FILEINPUT_POLL_TIMEOUT;
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        v4l2_fmt.fmt.pix.width = file_image.width;
        v4l2_fmt.fmt.pix.height = file_image.height;
        v4l2_fmt.fmt.pix.pixelformat = file_image.format;
        v4l2_fmt.fmt.pix.sizeimage = file_image.size;
        v4l2_fmt.fmt.pix.priv = file_image.bayer_order;

        LOG2("%s, width: %d, height: %d, format: %x, size: %d, bayer_order: %d\n",
             __func__,
             file_image.width,
             file_image.height,
             file_image.format,
             file_image.size,
             file_image.bayer_order);

        ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
        if (ret < 0) {
            LOGE("ERR(%s):VIDIOC_S_FMT failed %s\n", __func__, strerror(errno));
            return -1;
        }
        return 0;
    }

    g_isp_timeout = ATOMISP_POLL_TIMEOUT;
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl (fd,  VIDIOC_G_FMT, &v4l2_fmt);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_G_FMT failed %s\n", __func__, strerror(errno));
        return -1;
    }

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    v4l2_fmt.fmt.pix.width = w;
    v4l2_fmt.fmt.pix.height = h;
    v4l2_fmt.fmt.pix.pixelformat = fourcc;
    v4l2_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_FMT failed %s\n", __func__, strerror(errno));
        return -1;
    }
    return 0;
}

int IntelCamera::v4l2_capture_g_framerate(int fd, float * framerate, int width,
                                         int height, int pix_fmt)
{
    int ret;
    struct v4l2_frmivalenum frm_interval;

    if ( NULL == framerate)
        return -EINVAL;

    assert(fd > 0);

    CLEAR(frm_interval);
    frm_interval.pixel_format = pix_fmt;
    frm_interval.width = width;
    frm_interval.height = height;
	*framerate = -1.0;

    ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frm_interval);
    if (ret < 0) {
        LOGW("WARN(%s):ioctrl failed %s\n", __func__, strerror(errno));
        return ret;
    }

    assert(0 !=frm_interval.discrete.numerator);

    *framerate = frm_interval.discrete.denominator;
    *framerate /= frm_interval.discrete.numerator;

    return 0;
}

int IntelCamera::v4l2_capture_request_buffers(int fd, int device, uint num_buffers)
{
    struct v4l2_requestbuffers req_buf;
    int ret;
    CLEAR(req_buf);

    if (memory_userptr)
        req_buf.memory = V4L2_MEMORY_USERPTR;
    else
        req_buf.memory = V4L2_MEMORY_MMAP;
    req_buf.count = num_buffers;
    req_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (device == V4L2_THIRD_DEVICE) {
        req_buf.memory = V4L2_MEMORY_MMAP;
        req_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    }

    LOG1("VIDIOC_REQBUFS, count=%d", req_buf.count);
    ret = ioctl(fd, VIDIOC_REQBUFS, &req_buf);

    if (ret < 0) {
        LOGE("ERR(%s): VIDIOC_REQBUFS %d failed %s\n", __func__,
             num_buffers, strerror(errno));
        return ret;
    }

    if (req_buf.count < num_buffers)
        LOGW("W(%s)Got buffers is less than request\n", __func__);

    return req_buf.count;
}

/* MMAP the buffer or allocate the userptr */
int IntelCamera::v4l2_capture_new_buffer(int fd, int device, int index, struct v4l2_buffer_info *buf)
{
    void *data;
    int ret;
    struct v4l2_buffer *vbuf = &buf->vbuffer;
    vbuf->flags = 0x0;

    LOG1("%s\n", __func__);

    if (device == V4L2_THIRD_DEVICE) {
        vbuf->index = index;
        vbuf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        vbuf->memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fd, VIDIOC_QUERYBUF, vbuf);
        if (ret < 0) {
            LOGE("ERR(%s):VIDIOC_QUERYBUF failed %s\n", __func__, strerror(errno));
            return -1;
        }

        data = mmap(NULL, vbuf->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    vbuf->m.offset);

        if (MAP_FAILED == data) {
            LOGE("ERR(%s):mmap failed %s\n", __func__, strerror(errno));
            return -1;
        }

        buf->data = data;
        buf->length = vbuf->length;

        memcpy(data, file_image.mapped_addr, file_image.size);
        return 0;
    }

    /* FIXME: Add the userptr here */
    if (memory_userptr)
        vbuf->memory = V4L2_MEMORY_USERPTR;
    else
        vbuf->memory = V4L2_MEMORY_MMAP;

    vbuf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf->index = index;
    ret = ioctl(fd , VIDIOC_QUERYBUF, vbuf);

    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_QUERYBUF failed %s\n", __func__, strerror(errno));
        return ret;
    }

    if (memory_userptr) {
         vbuf->m.userptr = (unsigned int)(buf->data);
    } else {
        data = mmap(NULL, vbuf->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    vbuf->m.offset);

        if (MAP_FAILED == data) {
            LOGE("ERR(%s):mmap failed %s\n", __func__, strerror(errno));
            return -1;
        }
        buf->data = data;
    }

    buf->length = vbuf->length;
    LOG2("%s: index %u\n", __func__, vbuf->index);
    LOG2("%s: type %d\n", __func__, vbuf->type);
    LOG2("%s: bytesused %u\n", __func__, vbuf->bytesused);
    LOG2("%s: flags %08x\n", __func__, vbuf->flags);
    LOG2("%s: memory %u\n", __func__, vbuf->memory);
    if (memory_userptr) {
        LOG1("%s: userptr:  %lu", __func__, vbuf->m.userptr);
    }
    else {
        LOG1("%s: MMAP offset:  %u", __func__, vbuf->m.offset);
    }

    LOG2("%s: length %u\n", __func__, vbuf->length);
    LOG2("%s: input %u\n", __func__, vbuf->input);

    return ret;
}

/* Unmap the buffer or free the userptr */
int IntelCamera::v4l2_capture_free_buffer(int fd, int device, struct v4l2_buffer_info *buf_info)
{
    int ret = 0;
    void *addr = buf_info->data;
    size_t length = buf_info->length;

    LOG1("%s: free buffers\n", __func__);

    if (device == V4L2_THIRD_DEVICE)
        if ((ret = munmap(addr, length)) < 0) {
            LOGE("ERR(%s):munmap failed %s\n", __func__, strerror(errno));
            return ret;
        }

    if (!memory_userptr) {
        if ((ret = munmap(addr, length)) < 0) {
            LOGE("ERR(%s):munmap failed %s\n", __func__, strerror(errno));
            return ret;
        }
    }
    return ret;
}

int IntelCamera::v4l2_capture_streamon(int fd)
{
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    LOG1("%s\n", __func__);

    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_STREAMON failed %s\n", __func__, strerror(errno));
        return ret;
    }

    return ret;
}

int IntelCamera::v4l2_capture_streamoff(int fd)
{
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    LOG1("%s\n", __func__);

    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_STREAMOFF failed %s\n", __func__, strerror(errno));
        return ret;
    }

    return ret;
}

int IntelCamera::v4l2_capture_qbuf(int fd, int index, struct v4l2_buffer_info *buf)
{
    struct v4l2_buffer *v4l2_buf = &buf->vbuffer;
    int ret;

    ret = ioctl(fd, VIDIOC_QBUF, v4l2_buf);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_QBUF index %d failed %s\n", __func__,
             index, strerror(errno));
        return ret;
    }
    LOG2("(%s): VIDIOC_QBUF finsihed", __func__);

    return ret;
}

int IntelCamera::v4l2_capture_control_dq(int fd, int start)
{
    struct v4l2_buffer vbuf;
    int ret;
    vbuf.memory = V4L2_MEMORY_USERPTR;
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.index = 0;

    if (start) {
        vbuf.flags &= ~V4L2_BUF_FLAG_BUFFER_INVALID;
        vbuf.flags |= V4L2_BUF_FLAG_BUFFER_VALID; /* start DQ thread */
    }
    else {
        vbuf.flags &= ~V4L2_BUF_FLAG_BUFFER_VALID;
        vbuf.flags |= V4L2_BUF_FLAG_BUFFER_INVALID; /* stop DQ thread */
    }
    ret = ioctl(fd, VIDIOC_QBUF, &vbuf); /* start DQ thread in driver*/
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_QBUF index %d failed %s\n", __func__,
             vbuf.index, strerror(errno));
        return ret;
    }
    LOG1("(%s): VIDIOC_QBUF finsihed", __func__);
    return 0;
}


int IntelCamera::v4l2_capture_g_parm(int fd, struct v4l2_streamparm *parm)
{
    int ret;
    LOG1("%s\n", __func__);

    parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_G_PARM, parm);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_G_PARM, failed %s\n", __func__, strerror(errno));
        return ret;
    }

    LOG1("%s: timeperframe: numerator %d, denominator %d\n", __func__,
         parm->parm.capture.timeperframe.numerator,
         parm->parm.capture.timeperframe.denominator);

    return ret;
}

int IntelCamera::v4l2_capture_s_parm(int fd, int device, struct v4l2_streamparm *parm)
{
    int ret;
    LOG1("%s\n", __func__);

    if (device == V4L2_THIRD_DEVICE) {
        parm->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        parm->parm.output.outputmode = OUTPUT_MODE_FILE;
    } else
        parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_S_PARM, parm);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_PARM, failed %s\n", __func__, strerror(errno));
        return ret;
    }

    return ret;
}

int IntelCamera::v4l2_capture_release_buffers(int fd, int device)
{
    return v4l2_capture_request_buffers(fd, device, 0);
}

int IntelCamera::v4l2_capture_dqbuf(int fd, struct v4l2_buffer *buf)
{
    int ret, i;
    int num_tries = 500;
    struct pollfd pfd[1];

    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (memory_userptr)
        buf->memory = V4L2_MEMORY_USERPTR;
    else
        buf->memory = V4L2_MEMORY_MMAP;

    pfd[0].fd = fd;
    pfd[0].events = POLLIN | POLLERR;

    for (i = 0; i < num_tries; i++) {
        ret = poll(pfd, 1, g_isp_timeout);

        if (ret < 0 ) {
            LOGE("ERR(%s): select error in DQ\n", __func__);
            return -1;
        }

        if (ret == 0) {
            LOGE("ERR(%s): select timeout in DQ\n", __func__);
            return -1;
        }

        ret = ioctl(fd, VIDIOC_DQBUF, buf);

        if (ret >= 0)
            break;
        LOGE("DQ error -- ret is %d\n", ret);
        switch (errno) {
        case EINVAL:
            LOGE("%s: Failed to get frames from device. %s", __func__,
                 strerror(errno));
            return -1;
        case EINTR:
            LOGW("%s: Could not sync the buffer %s\n", __func__,
                 strerror(errno));
            break;
        case EAGAIN:
            LOGW("%s: No buffer in the queue %s\n", __func__,
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
        LOGE("ERR(%s): too many tries\n", __func__);
        return -1;
    }
    LOG2("(%s): VIDIOC_DQBUF finsihed", __func__);
    return buf->index;
}

int IntelCamera::v4l2_register_bcd(int fd, int num_frames,
                      void **ptrs, int w, int h, int fourcc, int size)
{
    int ret = 0;
    int i;
    struct BC_Video_ioctl_package_TAG ioctl_package;
    bc_buf_params_t buf_param;

    buf_param.count = num_frames;
    buf_param.width = w;
    buf_param.stride = (fourcc == V4L2_PIX_FMT_YUYV) ? w << 1 : w;
    buf_param.height = h;
    buf_param.fourcc = fourcc;
    buf_param.type = BC_MEMORY_USERPTR;

    ioctl_package.ioctl_cmd = BC_Video_ioctl_request_buffers;
    ioctl_package.inputparam = (int)(&buf_param);
    ret = ioctl(fd, ATOMISP_IOC_CAMERA_BRIDGE, &ioctl_package);
    if (ret < 0) {
        LOGE("(%s): Failed to request buffers from buffer class"
             " camera driver (ret=%d).", __func__, ret);
        return -1;
    }
    LOG1("(%s): request bcd buffers count=%d, width:%d, stride:%d,"
         " height:%d, fourcc:%x", __func__, buf_param.count, buf_param.width,
         buf_param.stride, buf_param.height, buf_param.fourcc);

    bc_buf_ptr_t buf_pa;

    for (i = 0; i < num_frames; i++)
    {
        buf_pa.index = i;
        buf_pa.pa = (unsigned long)ptrs[i];
        buf_pa.size = size;
        ioctl_package.ioctl_cmd = BC_Video_ioctl_set_buffer_phyaddr;
        ioctl_package.inputparam = (int) (&buf_pa);
        ret = ioctl(fd, ATOMISP_IOC_CAMERA_BRIDGE, &ioctl_package);
        if (ret < 0) {
            LOGE("(%s): Failed to set buffer phyaddr from buffer class"
                 " camera driver (ret=%d).", __func__, ret);
            return -1;
        }
    }

    ioctl_package.ioctl_cmd = BC_Video_ioctl_get_buffer_count;
    ret = ioctl(fd, ATOMISP_IOC_CAMERA_BRIDGE, &ioctl_package);
    if (ret < 0 || ioctl_package.outputparam != num_frames)
        LOGE("(%s): check bcd buffer count error", __func__);
    LOG1("(%s): check bcd buffer count = %d",
         __func__, ioctl_package.outputparam);

    return ret;
}

int IntelCamera::v4l2_release_bcd(int fd)
{
    int ret = 0;
    struct BC_Video_ioctl_package_TAG ioctl_package;
    bc_buf_params_t buf_param;

    ioctl_package.ioctl_cmd = BC_Video_ioctl_release_buffer_device;
    ret = ioctl(fd, ATOMISP_IOC_CAMERA_BRIDGE, &ioctl_package);
    if (ret < 0) {
        LOGE("(%s): Failed to release buffers from buffer class camera"
             " driver (ret=%d).", __func__, ret);
        return -1;
    }

    return 0;
}

int IntelCamera::v4l2_read_file(char *file_name, int file_width, int file_height,
              int format, int bayer_order)
{
    int file_fd = -1;
    int file_size = 0;
    void *file_buf = NULL;
    struct stat st;

    /* Open the file we will transfer to kernel */
    if ((file_fd = open(file_name, O_RDONLY)) == -1) {
        LOGE("ERR(%s): Failed to open %s\n", __func__, file_name);
        return -1;
    }

    CLEAR(st);
    if (fstat(file_fd, &st) < 0) {
        LOGE("ERR(%s): fstat %s failed\n", __func__, file_name);
        return -1;
    }

    file_size = st.st_size;
    if (file_size == 0) {
        LOGE("ERR(%s): empty file %s\n", __func__, file_name);
        return -1;
    }

    file_buf = mmap(NULL, PAGE_ALIGN(file_size),
                    MAP_SHARED, PROT_READ, file_fd, 0);
    if (file_buf == MAP_FAILED) {
        LOGE("ERR(%s): mmap failed %s\n", __func__, file_name);
        return -1;
    }

    file_image.name = file_name;
    file_image.size = PAGE_ALIGN(file_size);
    file_image.mapped_addr = (char *)file_buf;
    file_image.width = file_width;
    file_image.height = file_height;

    LOG2("%s, mapped_addr=%p, width=%d, height=%d, size=%d\n",
        __func__, (char *)file_buf, file_width, file_height, file_image.size);

    file_image.format = format;
    file_image.bayer_order = bayer_order;

    return 0;
}

void IntelCamera::v4l2_set_isp_timeout(int timeout)
{
    g_isp_timeout = timeout;
}

int IntelCamera::xioctl (int fd, int request, void *arg, const char *name)
{
    int ret;

    LOG1 ("ioctl %s ", name);

    do {
        ret = ioctl (fd, request, arg);
    } while (-1 == ret && EINTR == errno);

    if (ret < 0)
        LOGW ("failed: %s\n", strerror (errno));
    else
        LOG1 ("ok\n");

    return ret;
}

int IntelCamera::atomisp_set_capture_mode(int fd, int mode)
{
    int binary;
    struct v4l2_streamparm parm;

    switch (mode) {
    case PREVIEW_MODE:
        binary = CI_MODE_PREVIEW;
        break;;
    case STILL_IMAGE_MODE:
        binary = CI_MODE_STILL_CAPTURE;
        break;
    case VIDEO_RECORDING_MODE:
        binary = CI_MODE_VIDEO;
        break;
    default:
        binary = CI_MODE_STILL_CAPTURE;
        break;
    }

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.capturemode = binary;

    if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
        LOGE("ERR(%s): error %s\n", __func__, strerror(errno));
        return -1;
    }

    return 0;
}


/******************************************************
 * atomisp_get_attribute():
 *   try to get the value of one specific attribute
 * return value: 0 for success
 *               others are errors
 ******************************************************/
int IntelCamera::atomisp_get_attribute (int fd, int attribute_num,
                                                 int *value, char *name)
{
    struct v4l2_control control;

    LOG1 ("getting value of attribute %d: %s\n", attribute_num, name);

    if (fd < 0)
        return -1;

    control.id = attribute_num;

    if (ioctl (fd, VIDIOC_G_CTRL, &control) < 0)
        goto ctrl_fail1;

    *value = control.value;

    return 0;

ctrl_fail1:
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_USER;
        controls.count = 1;
        controls.controls = &control;

        control.id = attribute_num;

        if (ioctl (fd, VIDIOC_G_EXT_CTRLS, &controls) < 0)
            goto ctrl_fail2;

        *value = control.value;

        return 0;

    }

ctrl_fail2:
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
        controls.count = 1;
        controls.controls = &control;

        control.id = attribute_num;

        if (ioctl (fd, VIDIOC_G_EXT_CTRLS, &controls) < 0)
            goto ctrl_fail3;

        *value = control.value;

        return 0;

    }

    /* ERRORS */
ctrl_fail3:
    {
        LOGE ("Failed to get control %d on device '%d'.",
                        attribute_num, fd);
        return -1;
    }
}

/******************************************************
 * atomisp_set_attribute():
 *   try to set the value of one specific attribute
 * return value: 0 for success
 * 				 others are errors
 ******************************************************/
int IntelCamera::atomisp_set_attribute (int fd, int attribute_num,
                                             const int value, const char *name)
{
    struct v4l2_control control;

    LOG1 ("setting attribute [%s] to %d\n", name, value);

    if (fd < 0)
        return -1;

    control.id = attribute_num;
    control.value = value;
    if (ioctl (fd, VIDIOC_S_CTRL, &control) < 0)
        goto ctrl_fail1;

    return 0;

ctrl_fail1:
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
        controls.count = 1;
        controls.controls = &control;

        control.id = attribute_num;
        control.value = value;

        if (ioctl (fd, VIDIOC_S_EXT_CTRLS, &controls) < 0)
            goto ctrl_fail2;

        return 0;
    }

ctrl_fail2:
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_USER;
        controls.count = 1;
        controls.controls = &control;

        control.id = attribute_num;
        control.value = value;

        if (ioctl (fd, VIDIOC_S_EXT_CTRLS, &controls) < 0)
            goto ctrl_fail3;

        return 0;
    }

    /* ERRORS */
ctrl_fail3:
    {
        LOGE("Failed to set value %d for control %d on device '%d', %s\n.",
             value, attribute_num, fd, strerror (errno));
        return -1;
    }
}

int IntelCamera::atomisp_get_de_config (int fd, struct atomisp_de_config *de_cfg)
{
    return _xioctl (fd, ATOMISP_IOC_G_ISP_FALSE_COLOR_CORRECTION, de_cfg);
}

int IntelCamera::atomisp_get_macc_tbl (int fd, struct atomisp_macc_config *macc_config)
{
    return _xioctl (fd, ATOMISP_IOC_G_ISP_MACC,macc_config);
}

int IntelCamera::atomisp_get_ctc_tbl (int fd, struct atomisp_ctc_table *ctc_tbl)
{
    return _xioctl (fd, ATOMISP_IOC_G_ISP_CTC, ctc_tbl);
}

int IntelCamera::atomisp_get_gdc_tbl (int fd, struct atomisp_morph_table *morph_tbl)
{
    return _xioctl (fd, ATOMISP_IOC_G_ISP_GDC_TAB, morph_tbl);
}

int IntelCamera::atomisp_get_tnr_config (int fd, struct atomisp_tnr_config *tnr_cfg)
{
    return _xioctl (fd, ATOMISP_IOC_G_TNR, tnr_cfg);
}


int IntelCamera::atomisp_get_ee_config (int fd, struct atomisp_ee_config *ee_cfg)
{
    return _xioctl (fd, ATOMISP_IOC_G_EE, ee_cfg);

}

int IntelCamera::atomisp_get_nr_config (int fd, struct atomisp_nr_config *nr_cfg) {
    return _xioctl (fd, ATOMISP_IOC_G_BAYER_NR, nr_cfg);

}

int IntelCamera::atomisp_get_dp_config (int fd, struct atomisp_dp_config *dp_cfg)
{
    return _xioctl (fd, ATOMISP_IOC_G_ISP_BAD_PIXEL_DETECTION, dp_cfg);
}

int IntelCamera::atomisp_get_wb_config (int fd, struct atomisp_wb_config *wb_cfg)
{
    return _xioctl (fd, ATOMISP_IOC_G_ISP_WHITE_BALANCE, wb_cfg);
}
int IntelCamera::atomisp_get_ob_config (int fd, struct atomisp_ob_config *ob_cfg)
{
    return _xioctl (fd, ATOMISP_IOC_G_BLACK_LEVEL_COMP, ob_cfg);
}

int IntelCamera::atomisp_get_fpn_tbl(int fd, struct atomisp_frame* fpn_tbl)
{
    return _xioctl (fd, ATOMISP_IOC_G_ISP_FPN_TABLE, fpn_tbl);
}

/*
  Make gamma table
*/
int IntelCamera::autoGmLut (unsigned short *pptDst, struct atomisp_gm_config *cfg_gm)
{
    /* cannot use this on cirrus because of missing powf implementation */
    const double adbToe = (double) (cfg_gm->GmToe) / 1024.;       // [u5.11] -> double
    const double adbKnee = (double) (cfg_gm->GmKne) / 1024.;      // [u5.11] -> double
    const double adbDRange = (double) (cfg_gm->GmDyr) / 256.;     // [u8.8] -> double
    const double adbReGammaVal = 1 / (double) (cfg_gm->GmVal);    // 1/GmVal : [u8.8] -> double
    const double adbTmpKnee =
        adbKnee / (adbDRange * adbKnee + adbDRange - adbKnee);
    const double adbTmpToe =
        ((1. + adbTmpKnee) * adbToe * adbKnee) / (adbDRange * (1. +
                adbKnee) * adbTmpKnee);
    const double adbDx = 1. / (double) 1024;      /* 1024 is the gamma table size */
    double adbX = (double) 0.;
    int asiCnt;

    for (asiCnt = 0; asiCnt < 1024; asiCnt++, adbX += adbDx) {
        const double adbDeno = (1. + adbTmpToe) * (1. + adbTmpKnee) * adbX * adbX;
        const double adbNume = (adbX + adbTmpToe) * (adbX + adbTmpKnee);
        const double adbY =
            (adbNume == 0.) ? 0. : pow (adbDeno / adbNume, adbReGammaVal);
        short auiTmp = (short) ((double) 255 * adbY + 0.5);

        if (auiTmp < cfg_gm->GmLevelMin) {
            auiTmp = cfg_gm->GmLevelMin;
        } else if (auiTmp > cfg_gm->GmLevelMax) {
            auiTmp = cfg_gm->GmLevelMax;
        }
        pptDst[asiCnt] = auiTmp;
    }
    return 0;
}

int IntelCamera::atomisp_set_fpn (int fd, int on)
{
    int ret;
    if (on) {
        if (atomisp_get_fpn_tbl(fd, &old_fpn_tbl) < 0)
            return -1;
        if (ci_adv_cfg_file_loaded())
            return ci_adv_load_fpn_table();
        else
            return 0;
    }
    else
        return _xioctl (fd, ATOMISP_IOC_S_ISP_FPN_TABLE, &old_fpn_tbl);
}

int IntelCamera::atomisp_set_macc (int fd, int on, int effect)
{
    int ret;
    if (on)
    {
        if (atomisp_get_macc_tbl(fd, &old_macc_config) < 0)
            return -1;
        if (ci_adv_cfg_file_loaded())
            return ci_adv_load_macc_table(effect);
        else
            return 0;
    }
    else
        return _xioctl (fd, ATOMISP_IOC_S_ISP_MACC,&old_macc_config);
}


int IntelCamera::atomisp_set_sc (int fd, int on)
{
    return atomisp_set_attribute (fd, V4L2_CID_ATOMISP_SHADING_CORRECTION, on,
                                     "Shading Correction");
}

/* Bad Pixel Detection*/
int IntelCamera::atomisp_set_bpd (int fd, int on)
{
    int ret;
    ret = atomisp_set_attribute (fd, V4L2_CID_ATOMISP_BAD_PIXEL_DETECTION,
	    on, "Bad Pixel Detection");
    if (ret == 0 && on)
    {
        if (ci_adv_cfg_file_loaded())
        {
            return ci_adv_load_dp_config();
        }
        else
            return 0;
    }
    else
        return ret;
}

int IntelCamera::atomisp_get_bpd (int fd, int *on)
{
    return atomisp_get_attribute (fd, V4L2_CID_ATOMISP_BAD_PIXEL_DETECTION,
                                     on, "Bad Pixel Detection");
}

int IntelCamera::atomisp_set_bnr (int fd, int on)
{
    struct atomisp_nr_config bnr;
    int ret;

    //Need update these magic number. Read it from configuration file
    if (on) {
        bnr.gain = 60000;
        bnr.direction = 3200;
        bnr.threshold_cb = 64;
        bnr.threshold_cr = 64;

        if (ci_adv_cfg_file_loaded())
            return ci_adv_load_nr_config();
        else
            return _xioctl (fd, ATOMISP_IOC_S_BAYER_NR, &bnr);
    } else {
        memset (&bnr, 0, sizeof(bnr));
        return _xioctl (fd, ATOMISP_IOC_S_BAYER_NR, &bnr);
    }
}

/* False Color Correction, Demosaicing */
int IntelCamera::atomisp_set_fcc (int fd, int on)
{
    int ret;
    ret = atomisp_set_attribute (fd, V4L2_CID_ATOMISP_FALSE_COLOR_CORRECTION,
            on, "False Color Correction");
    if(ret == 0 && on)
    {
        if (ci_adv_cfg_file_loaded())
            return ci_adv_load_dp_config();
        else
            return 0;
    }
    else
        return ret;

}

int IntelCamera::atomisp_set_ynr (int fd, int on)
{
    /* YCC NR use the same parameter as Bayer NR */
    return atomisp_set_bnr(fd, on);
}

int IntelCamera::atomisp_set_ee (int fd, int on)
{
    struct atomisp_ee_config ee;
    if (on) {
        ee.gain = 8192;
        ee.threshold = 128;
        ee.detail_gain = 2048;
        if (ci_adv_cfg_file_loaded())
            return ci_adv_load_ee_config();
        else
            return _xioctl (fd, ATOMISP_IOC_S_EE, &ee);
    } else {
        ee.gain = 0;
        ee.threshold = 0;
        ee.detail_gain = 0;
        return _xioctl (fd, ATOMISP_IOC_S_EE, &ee);
    }
}

/* Black Level Compensation */
int IntelCamera::atomisp_set_blc (int fd, int on)
{
    static struct atomisp_ob_config ob_off;
    struct atomisp_ob_config ob_on;
    static int current_status = 0;
    int ret;

    if (on && current_status) {
        LOG1("Black Level Compensation Already On\n");
        return 0;
    }

    if (!on && !current_status) {
        LOG1("Black Level Composition Already Off\n");
        return 0;
    }

    ob_on.mode = atomisp_ob_mode_fixed;
    ob_on.level_gr = 0;
    ob_on.level_r = 0;
    ob_on.level_b = 0;
    ob_on.level_gb = 0;
    ob_on.start_position = 0;
    ob_on.end_position = 63;

    if (on) {
        if (_xioctl (fd, ATOMISP_IOC_G_BLACK_LEVEL_COMP, &ob_off) < 0 ) {
            LOG1("Error Get black level composition\n");
            return -1;
        }
        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_ob_config();
            if(ret == 0)
            {
                current_status = 1;
                return 0;
            }
            else {
                current_status = 0;
                return -1;
            }
        }
        else {
            if (_xioctl (fd, ATOMISP_IOC_S_BLACK_LEVEL_COMP, &ob_on) < 0) {
                LOG1("Error Set black level composition\n");
                return -1;
            }
        }
    } else {
        if (_xioctl (fd, ATOMISP_IOC_S_BLACK_LEVEL_COMP, &ob_off) < 0) {
            LOG1("Error Set black level composition\n");
            return -1;
        }
    }
    current_status = on;
    return 0;
}

int IntelCamera::atomisp_set_tnr (int fd, int on)
{
    int ret;
    if (on)
    {
        if (ci_adv_cfg_file_loaded()) {
            if (atomisp_get_tnr_config(fd,&old_tnr_config) < 0)
                return -1;
            return ci_adv_load_tnr_config();
        }
        return -1;
    }
    else
        return _xioctl (fd, ATOMISP_IOC_S_TNR, &old_tnr_config);
}

int IntelCamera::atomisp_set_xnr (int fd, int on)
{
    return _xioctl (fd, ATOMISP_IOC_S_XNR, &on);
}

/* Configure the color effect Mode in the kernel
 */

int IntelCamera::atomisp_set_tone_mode (int fd, enum v4l2_colorfx colorfx)
{
    return atomisp_set_attribute (fd, V4L2_CID_COLORFX, colorfx, "Color Effect");
}

int IntelCamera::atomisp_get_tone_mode (int fd, int *colorfx)
{
    return atomisp_get_attribute (fd, V4L2_CID_COLORFX, colorfx, "Color Effect");
}

int IntelCamera::atomisp_set_gamma_tbl (int fd, struct atomisp_gamma_table *g_tbl)
{
    return _xioctl (fd, ATOMISP_IOC_S_ISP_GAMMA, g_tbl);
}

// apply gamma table from g_gamma_table_original to g_gamma_table according
// current brightness, contrast and inverse settings
int IntelCamera::atomisp_apply_to_runtime_gamma(int contrast,
                                                 int brightness, bool inv_gamma)
{
    int i, tmp;
    for (i = 0; i < atomisp_gamma_table_size; i++) {
        tmp = (g_gamma_table_original.data[i] * contrast >> 8) + brightness;

        if (tmp < g_cfg_gm.GmLevelMin) {
            tmp = g_cfg_gm.GmLevelMin;
        } else if (tmp > g_cfg_gm.GmLevelMax) {
            tmp = g_cfg_gm.GmLevelMax;
        }
        if (inv_gamma)
        {
            tmp = g_cfg_gm.GmLevelMin + g_cfg_gm.GmLevelMax - tmp;
        }
        g_gamma_table.data[i] = tmp;
    }
    return 0;
}

int IntelCamera::atomisp_init_gamma (int fd, int contrast, int brightness, bool inv_gamma)
{
    int ret = _xioctl (fd, ATOMISP_IOC_G_ISP_GAMMA, &g_gamma_table_original);
    if (ret < 0)
        return -1;
    else
        return atomisp_apply_to_runtime_gamma(contrast, brightness, inv_gamma);
}

int IntelCamera::atomisp_set_gamma_from_value (int fd, float gamma, int contrast,
                                           int brightness, bool inv_gamma)
{
    g_cfg_gm.GmVal = gamma;
    autoGmLut (g_gamma_table_original.data, &g_cfg_gm);

    if (atomisp_apply_to_runtime_gamma(contrast, brightness, inv_gamma) < 0)
        return -1;

    return atomisp_set_gamma_tbl (fd, &g_gamma_table);
}

int IntelCamera::atomisp_set_contrast_bright (int fd, int contrast, int brightness,
                                          bool inv_gamma)
{
    if (atomisp_apply_to_runtime_gamma(contrast, brightness, inv_gamma) < 0)
        return -1;

    return atomisp_set_gamma_tbl (fd, &g_gamma_table);
}

int IntelCamera::atomisp_set_gdc (int fd, int on)
{
    int ret2;
    int ret;
    ret2 = atomisp_set_attribute (fd, V4L2_CID_ATOMISP_POSTPROCESS_GDC_CAC,
            on, "GDC");
    if (on) {
        if (ci_adv_cfg_file_loaded()) {
            LOGD("%s: cfg file already loaded\n", __func__);
            ret = ci_adv_load_gdc_table();
            if (ret == 0)
                return 0;
            else {
                ret2 = atomisp_set_attribute (fd,
                            V4L2_CID_ATOMISP_POSTPROCESS_GDC_CAC, false, "GDC");
                return -1;
            }
        }
    }

    return ret2;
}

int IntelCamera::atomisp_set_dvs (int fd, int on)
{
    return atomisp_set_attribute(fd, V4L2_CID_ATOMISP_VIDEO_STABLIZATION,
                                    on, "Video Stabilization");
}

int IntelCamera::atomisp_set_exposure (int fd, int exposure)
{
    if (exposure == 0)
        return 0;
    return atomisp_set_attribute (fd, V4L2_CID_EXPOSURE_ABSOLUTE, exposure,
                                     "exposure");
}

int IntelCamera::atomisp_get_exposure (int fd, int *exposure)
{
    return atomisp_get_attribute (fd, V4L2_CID_EXPOSURE_ABSOLUTE, exposure, "Exposure");
}

int IntelCamera::atomisp_set_aperture (int fd, int aperture)
{
    if (aperture == 0)
        return 0;
    return atomisp_set_attribute (fd, V4L2_CID_APERTURE_ABSOLUTE, aperture, "aperture");
}

int IntelCamera::atomisp_get_aperture (int fd, int *aperture)
{
    return atomisp_get_attribute (fd, V4L2_CID_APERTURE_ABSOLUTE, aperture, "Aperture");
}

int IntelCamera::atomisp_set_iso_speed (int fd, int iso_speed)
{
    if (iso_speed == 0)
        return 0;
    return atomisp_set_attribute (fd, V4L2_CID_ISO_ABSOLUTE, iso_speed, "iso_speed");
}

int IntelCamera::atomisp_get_iso_speed (int fd, int *iso_speed)
{
    return atomisp_get_attribute (fd, V4L2_CID_ISO_ABSOLUTE, iso_speed, "ISO_SPEED");
}

int IntelCamera::atomisp_set_focus_posi (int fd, int focus)
{
    return atomisp_set_attribute (fd, V4L2_CID_FOCUS_ABSOLUTE, focus, "Focus");
}

int IntelCamera::atomisp_get_focus_posi (int fd, int *focus)
{
    return atomisp_get_attribute (fd, V4L2_CID_FOCUS_ABSOLUTE, focus, "Focus");
}

int IntelCamera::atomisp_set_zoom (int fd, int zoom)
{
    return atomisp_set_attribute (fd, V4L2_CID_ZOOM_ABSOLUTE, zoom, "zoom");
}

int IntelCamera::atomisp_get_zoom (int fd, int *zoom)
{
    return atomisp_get_attribute (fd, V4L2_CID_ZOOM_ABSOLUTE, zoom, "Zoom");
}

int IntelCamera::atomisp_led_flash_off (int fd)
{
    return atomisp_set_attribute(fd, V4L2_CID_FLASH_TRIGGER, 0,
                                 "led flash off");
}

int IntelCamera::atomisp_led_flash_trigger (int fd,
                                   int mode,
                                   int duration_ms,
                                   int percent_time_100)
{
    if (0 != atomisp_set_attribute(fd, V4L2_CID_FLASH_MODE,
                                                  mode, "flash mode")) {
        LOGE("Error to set flash strobe\n");
    }
#if 0
    if (0 != atomisp_set_attribute(fd,V4L2_CID_FLASH_DURATION,
                                              duration_ms, "flash duration")) {
        LOGE("Error to set flash duration\n");
    }
#endif
    if (0 != atomisp_set_attribute(fd, V4L2_CID_FLASH_INTENSITY,
                                      percent_time_100, "flash intesity")) {
        LOGE("Error to set flash intensity\n");
    }
    if (0 != atomisp_set_attribute(fd, V4L2_CID_FLASH_TRIGGER,
                                                  1, "flash trigger")) {
        LOGE("Error to trigger flash on\n");
    }
    return 0;
}

int IntelCamera::atomisp_led_indicator_trigger (int fd, int percent_time_100)
{
    return atomisp_set_attribute(fd,V4L2_CID_INDICATOR_INTENSITY,
                                 percent_time_100, "flash indicator intensity");
}

int IntelCamera::atomisp_led_assist_trigger (int fd, int percent_time_100)
{
    return atomisp_set_attribute(fd, V4L2_CID_TORCH_INTENSITY,
                                  percent_time_100, "flash torch intesity");
}

int IntelCamera::atomisp_set_cfg_from_file(int fd)
{
	return atomisp_set_cfg(fd);
}

int IntelCamera::find_cfg_index(char *in)
{
	int i;

	for(i = 0; i < NUM_OF_CFG; i++) {
		if(!memcmp(in, FunctionKey[i], strlen(FunctionKey[i])))
			return i;
	}

	return -1;
}

int IntelCamera::analyze_cfg_value(unsigned int index, char *value)
{
	int i;

	switch (index) {
		case MACC:
			for (i = 0; i < NUM_OF_MACC; i++) {
				if(!memcmp(value, FunctionOption_Macc[i],
					   strlen(FunctionOption_Macc[i]))) {
					default_function_value_list[index] = i;
					return 0;
				}
			}
			return -1;
		case IE:
			for (i = 0; i < NUM_OF_IE; i++) {
				if(!memcmp(value, FunctionOption_Ie[i],
					   strlen(FunctionOption_Ie[i]))) {
					default_function_value_list[index] = i;
					return 0;
				}
			}
			return -1;
		case ZOOM:
		case MF:
		case ME:
		case MWB:
			default_function_value_list[index] = atoi(value);
			return 0;
		default:
			for (i = 0; i < NUM_OF_GENERAL; i++) {
				if(!memcmp(value, FunctionOption_General[i],
					strlen(FunctionOption_General[i]))) {
					default_function_value_list[index] = i;
					return 0;
				}
			}
			return -1;
	}
}

int IntelCamera::atomisp_parse_cfg_file()
{
	char line[LINE_BUF_SIZE];
	char *line_name;
	char *line_value;
	int param_index;
	int res;
	int err = 0;

	FILE *fp;

	fp = fopen(CFG_PATH, "r");
	if (!fp) {
		LOGE("Error open file:%s\n", CFG_PATH);
		return -1;
	}
	/* anaylize file item */
	while(fgets(line, LINE_BUF_SIZE, fp)) {
		line_name = line;
		line_value = strchr(line, '=') + 1;
		param_index = find_cfg_index(line_name);
		if (param_index < 0) {
			LOGE("Error index in line: %s.\n", line);
			err = -1;
			continue;
		}

		res = analyze_cfg_value(param_index, line_value);
		if (res < 0) {
			LOGE("Error value in line: %s.\n", line);
			err = -1;
			continue;
		}
	}

	fclose(fp);

	return err;
}

int IntelCamera::atomisp_set_cfg(int fd)
{
	int err = 0;
	int i;
	unsigned int value;

	if (default_function_value_list[SWITCH] == FUNC_OFF) {
		LOGD("Does not using the configuration file.\n");
		return 0;
	}

	for (i = 1; i < NUM_OF_CFG; i++) {
		value = default_function_value_list[i];
		switch (i) {
			case MACC:
				switch (value) {
				case MACC_GRASSGREEN:
					//Fix Me! Added set macc table
					err |= atomisp_set_tone_mode(fd,
						V4L2_COLORFX_GRASS_GREEN);
				       break;
				case MACC_SKYBLUE:
					//Fix Me! Added set macc table
					err |= atomisp_set_tone_mode(fd,
						V4L2_COLORFX_SKY_BLUE);
					break;
			 	case MACC_SKIN:
					//Fix Me! Added set macc table
					err |= atomisp_set_tone_mode(fd,
					V4L2_COLORFX_SKIN_WHITEN);
					break;
				case MACC_NONE:
					err |= atomisp_set_tone_mode(fd,
						V4L2_COLORFX_NONE);
					break;
				}
				LOGD("macc:%s.\n", FunctionOption_Macc[value]);
				break;
			case SC:
				LOGD("sc:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= atomisp_set_sc(fd, value);
				break;
			case IE:
				LOGD("ie:%s.\n", FunctionOption_Ie[value]);
				switch (value) {
				case IE_MONO:
					err |= atomisp_set_tone_mode(fd,
						V4L2_COLORFX_BW);
				       break;
				case IE_SEPIA:
					err |= atomisp_set_tone_mode(fd,
						V4L2_COLORFX_SEPIA);
					break;
			 	case IE_NEGATIVE:
					err |= atomisp_set_tone_mode(fd,
						V4L2_COLORFX_NEGATIVE);
					break;
				}

				break;
			case GAMMA:
				//Fix Me! Add setting gamma table here
				LOGD("gamma:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= atomisp_set_gamma_from_value(fd, DEFAULT_GAMMA_VALUE,
                                                        DEFAULT_CONTRAST,
					                                    DEFAULT_BRIGHTNESS,
					                                    !!DEFAULT_INV_GAMMA);
				break;
			case BPC:
				LOGD("bpc:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= atomisp_set_bpd(fd, value);
				break;
			case FPN:
				LOGD("fpn:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= atomisp_set_fpn(fd, value);
				break;
			case BLC:
				LOGD("blc:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= atomisp_set_blc(fd, value);
				break;
			case EE:
				LOGD("ee:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= atomisp_set_ee(fd, value);
				break;
			case NR:
				LOGD("nr:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF) {
					err |= atomisp_set_bnr(fd, value);
					err |= atomisp_set_ynr(fd, value);
				}
				break;
			case XNR:
				LOGD("xnr:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= atomisp_set_xnr(fd, value);
				break;
			case BAYERDS:
				LOGD("bayer-ds:%s.\n", FunctionOption_General[value]);
				//Needed added new interface
				break;
			case ZOOM:
				LOGD("zoom:%d.\n", value);
				if(value != 0)
					err |= atomisp_set_zoom(fd, value);
				break;
			case MF:
				LOGD("mf:%d.\n", value);
				if(value != 0)
					err |= atomisp_set_focus_posi(fd, value);
				break;
			case ME:
				LOGD("me:%d.\n", value);
				if(value != 0)
					err |= atomisp_set_exposure(fd, value);

				break;
			case MWB:
				LOGD("mwb:%d.\n", value);
				//Fix Me! Add 3A Lib interface here
				break;
			case ISO:
				LOGD("iso:%d.\n", value);
				//Fix Me! Add implementatino here
				break;
			case DIS:
				LOGD("dis:%s.\n", FunctionOption_General[value]);
				//Fix Me! Add setting DIS Interface
				break;
			case DVS:
				LOGD("dvs:%s.\n", FunctionOption_General[value]);
				if(value != 0)
					err |= atomisp_set_dvs(fd, value);
				break;
			case REDEYE:
				LOGD("red-eye:%s.\n", FunctionOption_General[value]);
				//Fix Me! Add red-eye interface here
				break;
			default:
				err |= -1;
		}
	}

	return err;

}

}; // namespace android
