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

#include <Camera.h>

#include "IntelCamera.h"
#include "CameraAAAProcess.h"
#include "LogHelper.h"

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

static char *resolution_tables[] = {
    RESOLUTION_VGA_TABLE,
    RESOLUTION_720P_TABLE,
    RESOLUTION_1080P_TABLE,
    RESOLUTION_5MP_TABLE,
    RESOLUTION_8MP_TABLE,
    RESOLUTION_14MP_TABLE
};
/* Debug Use Only */
static void write_image(const void *data, const int size, int width, int height,
                       const char *name)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    char filename[80];
    static unsigned int count = 0;
    size_t bytes;
    FILE *fp;

    snprintf(filename, 50, "/data/dump_%d_%d_00%u_%s", width,
             height, count, name);

    fp = fopen (filename, "w+");
    if (fp == NULL) {
        LOGE ("open file %s failed %s", filename, strerror (errno));
        return ;
    }

    LogDetail("Begin write image %s", filename);
    if ((bytes = fwrite (data, size, 1, fp)) < (size_t)size)
        LOGW ("Write less bytes to %s: %d, %d", filename, size, bytes);
    count++;

    fclose (fp);
}

static void
dump_v4l2_buffer(int fd, struct v4l2_buffer *buffer, char *name)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);

    m_camera_phy_id = DEFAULT_CAMERA_SENSOR;
    num_buffers = DEFAULT_NUM_BUFFERS;
    m_sensor_type = SENSOR_TYPE_RAW;

    video_fds[V4L2_FIRST_DEVICE] = -1;
    video_fds[V4L2_SECOND_DEVICE] = -1;
    video_fds[V4L2_THIRD_DEVICE] = -1;
    main_fd = -1;
    m_flag_camera_start[0] = 0;
    m_flag_camera_start[1] = 0;

    // init ISP settings
    mIspSettings.contrast = 256;			// 1.0
    mIspSettings.brightness = 0;
    mIspSettings.inv_gamma = false;		// no inverse

    num_snapshot = 1;
    num_postview = 1;
}

IntelCamera::~IntelCamera()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    // color converter
}

char* IntelCamera::getMaxSnapShotResolution()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int index = RESOLUTION_14MP;

    if (m_snapshot_max_width < RESOLUTION_14MP_WIDTH || m_snapshot_max_height < RESOLUTION_14MP_HEIGHT)
            index--;
    if (m_snapshot_max_width < RESOLUTION_8MP_WIDTH || m_snapshot_max_height < RESOLUTION_8MP_HEIGHT)
            index--;
    if (m_snapshot_max_width < RESOLUTION_5MP_WIDTH || m_snapshot_max_height < RESOLUTION_5MP_HEIGHT)
            index--;
    if (m_snapshot_max_width < RESOLUTION_1080P_WIDTH || m_snapshot_max_height < RESOLUTION_1080P_HEIGHT)
            index--;
    if (m_snapshot_max_width < RESOLUTION_720P_WIDTH || m_snapshot_max_height < RESOLUTION_720P_HEIGHT)
            index--;
    if (m_snapshot_max_width < RESOLUTION_VGA_WIDTH || m_snapshot_max_height < RESOLUTION_VGA_HEIGHT)
            index--;

    if (index < 0)
        index = 0;
    return resolution_tables[index];
}

int IntelCamera::initCamera(int camera_id, int camera_idx, int sensor_type, AAAProcess *tmpAAA)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;

    m_camera_phy_id = camera_id;
    mAAA = tmpAAA;
    m_sensor_type = sensor_type;

    // Open the main device first
    ret = openMainDevice(camera_idx);

    /* Detect Maximum still capture resolution */
    m_snapshot_max_width = 0xffff;
    m_snapshot_max_height = 0xffff;
    ret = detectDeviceResolution(&m_snapshot_max_width,
                                &m_snapshot_max_height,
                                STILL_IMAGE_MODE);
    if (ret) {
        LogError("Faied to detect camera %d, resolution! Use default settings",
             camera_id);
        switch (camera_id) {
        case CAMERA_FACING_FRONT:
            m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
            m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
            break;
        case CAMERA_FACING_BACK:
            m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
            m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
            break;
        default:
            LogError("Invalid camera id(%d)", camera_id);
            return -1;
        }
    }
    else
        LogDetail("Camera %d Max-resolution detect: %dx%d", camera_id,
            m_snapshot_max_width,
            m_snapshot_max_height);

    switch (camera_id) {
    case CAMERA_FACING_FRONT:
        m_preview_max_width   = MAX_FRONT_CAMERA_PREVIEW_WIDTH;
        m_preview_max_height  = MAX_FRONT_CAMERA_PREVIEW_HEIGHT;
        m_recorder_max_width = MAX_FRONT_CAMERA_VIDEO_WIDTH;
        m_recorder_max_height = MAX_FRONT_CAMERA_VIDEO_HEIGHT;
        m_snapshot_width = 1920;
        m_snapshot_height = 1080;
        break;
    case CAMERA_FACING_BACK:
        m_preview_max_width   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
        m_preview_max_height  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
        m_recorder_max_width = MAX_BACK_CAMERA_VIDEO_WIDTH;
        m_recorder_max_height = MAX_BACK_CAMERA_VIDEO_HEIGHT;
        m_snapshot_width = 2560;
        m_snapshot_height = 1920;
        break;
    default:
        LogError("Invalid camera id(%d)", camera_id);
        return -1;
    }

    m_preview_width = 640;
    m_preview_pad_width = 640;
    m_preview_height = 480;
    m_preview_v4lformat = V4L2_PIX_FMT_NV12;

    m_postview_width = 640;
    m_postview_height = 480;
    m_postview_v4lformat = V4L2_PIX_FMT_NV12;

    m_snapshot_pad_width = 2560;
    m_snapshot_v4lformat = V4L2_PIX_FMT_NV12;

    m_recorder_width = 1920;
    m_recorder_pad_width = 1920;
    m_recorder_height = 1080;
    m_recorder_v4lformat = V4L2_PIX_FMT_NV12;

    file_injection = false;
    g_isp_timeout = 0;

    //RAW sensor need this effect. Invalid for Soc sensor
    if (m_sensor_type == SENSOR_TYPE_RAW) {
        //Set some default values.
        mColorEffect = DEFAULT_COLOR_EFFECT;
        mShadingCorrection = DEFAULT_SHADING_CORRECTION;
        mXnrOn = DEFAULT_XNR;
        mTnrOn = DEFAULT_TNR;
        mMacc = DEFAULT_MACC;
        mNrEeOn = DEFAULT_NREE;
        mGDCOn = DEFAULT_GDC;
        mDVSOn = DEFAULT_DVS;

        // Do the basic init before open here for RAW sensor
        if (!m_flag_init) {
            //Parse the configure from file
            atomisp_parse_cfg_file();

            m_flag_init = 1;
        }

        //Gamma table initialization for RAW sensor
        g_cfg_gm.GmVal = 1.5;
        g_cfg_gm.GmToe = 123;
        g_cfg_gm.GmKne = 287;
        g_cfg_gm.GmDyr = 256;
        g_cfg_gm.GmLevelMin = 0;
        g_cfg_gm.GmLevelMax = 255;
    }

    return ret;
}

int IntelCamera::deinitCamera(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (m_flag_init) {
        m_flag_init = 0;
    }

    if (m_bcd_registered) {
        v4l2_release_bcd(main_fd);
        m_bcd_registered = false;
    }

    closeMainDevice();
    return 0;
}

//File Input
int IntelCamera::initFileInput()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    int device = V4L2_FIRST_DEVICE;
    v4l2_capture_s_input(video_fds[device], 2);

    // open the third device
    device = V4L2_THIRD_DEVICE;
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
    LogEntry(LOG_TAG, __FUNCTION__);
    int device = V4L2_THIRD_DEVICE;

    if (video_fds[device] < 0) {
        LogWarning("Already closed");
        return 0;
    }

    destroyBufferPool(device);

    v4l2_capture_close(video_fds[device]);

    video_fds[device] = -1;
    return 0;
}

int IntelCamera::configureFileInput(const struct file_input *image)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;
    int device = V4L2_THIRD_DEVICE;
    int fd = video_fds[device];
    int buffer_count = 1;

    if (NULL == image) {
        LogError("struct file_input NULL pointer");
        return -1;
    }

    if (NULL == image->name) {
        LogError("file_name NULL pointer");
        return -1;
    }

    if (v4l2_read_file(image->name,
                  image->width,
                  image->height,
                  image->format,
                  image->bayer_order) < 0)
        return -1;

    //Set the format
    ret = v4l2_capture_s_format(fd, device, image->width, image->height, image->format, false);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    int w = m_preview_pad_width;
    int h = m_preview_height;
    int fourcc = m_preview_v4lformat;
    int device = V4L2_FIRST_DEVICE;

    run_mode = PREVIEW_MODE;

    //Move the mAAA out after enable the open/close
    mAAA->SwitchMode(run_mode);
    mAAA->SetFrameRate (framerate);

    if (zoom_val != 0)
        set_zoom_val_real(zoom_val);

    ret = configureDevice(device, w, h, fourcc, false);
    if (ret < 0)
        return ret;
    ret = startCapture(device, PREVIEW_NUM_BUFFERS);
    if (ret < 0)
        return ret;

    return main_fd;
}

void IntelCamera::stopCameraPreview(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int device = V4L2_FIRST_DEVICE;
    if (!m_flag_camera_start[device]) {
        LogDetail("doing nothing because m_flag_camera_start is zero");
        return ;
    }
    int fd = video_fds[device];

    if (fd <= 0) {
        LogDetail("Camera was already closed");
        return ;
    }

    stopCapture(device);
}

int IntelCamera::getPreview(void **data, enum atomisp_frame_status *status)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    int device = V4L2_FIRST_DEVICE;
    int index = grabFrame(device, status);
    if(index < 0)
    {
        LogError("Error in grabbing frame!");
        return -1;
    }
    *data = v4l2_buf_pool[device].bufs[index].data;
    return index;
}

int IntelCamera::putPreview(int index)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    int device = V4L2_FIRST_DEVICE;
    int fd = video_fds[device];
    return v4l2_capture_qbuf(fd, index, &v4l2_buf_pool[device].bufs[index]);
}

//Snapsshot Control
//Snapshot and video recorder has the same flow. We can combine them together
void IntelCamera::checkGDC(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mGDCOn) {
        LogDetail("GDC is enabled now");
        //Only enable GDC in still image capture mode and 14M
        if (atomisp_set_gdc(main_fd, true))
            LogError("Error setting gdc:%d, fd:%d", true, main_fd);
        else
            v4l2_set_isp_timeout(ATOMISP_FILEINPUT_POLL_TIMEOUT);
    }
}

int IntelCamera::startSnapshot(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    run_mode = STILL_IMAGE_MODE;
    ret = openSecondDevice();
    if (ret < 0)
        return ret;

    mAAA->SwitchMode(run_mode);
    mAAA->SetFrameRate (framerate);

    //0 is the default. I don't need zoom.
    if (zoom_val != 0)
        set_zoom_val_real(zoom_val);

    checkGDC();

    ret = configureDevice(V4L2_FIRST_DEVICE, m_snapshot_width,
                          m_snapshot_height, m_snapshot_v4lformat, raw_data_dump.format == RAW_BAYER);
    if (ret < 0)
        goto configure1_error;

    ret = configureDevice(V4L2_SECOND_DEVICE, m_postview_width,
                          m_postview_height, m_postview_v4lformat, false);
    if (ret < 0)
        goto configure2_error;

    ret = startCapture(V4L2_FIRST_DEVICE, num_snapshot);
    if (ret < 0)
        goto start1_error;

    ret = startCapture(V4L2_SECOND_DEVICE, num_snapshot);
    if (ret < 0)
        goto start2_error;
    return main_fd;

start2_error:
    stopCapture(V4L2_FIRST_DEVICE);
start1_error:
registerbcd_error:
configure2_error:
configure1_error:
    closeSecondDevice();
    return ret;
}

void IntelCamera::stopSnapshot(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    stopDualStreams();
    v4l2_set_isp_timeout(0);
}

void IntelCamera::setSnapshotNum(int num)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    num_snapshot = num;
}

void IntelCamera::setPostviewNum(int num)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    num_postview = num;
}

int IntelCamera::putDualStreams(int index)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("index = %d", index);
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
int IntelCamera::getSnapshot(void **main_out, void **postview,
                             void *postview_rgb565, enum atomisp_frame_status *status)
{
    LogEntry(LOG_TAG, __FUNCTION__);

    int index0 = grabFrame(V4L2_FIRST_DEVICE, status);
    if (index0 < 0) {
        LogError("Error in grabbing frame from 1'st device!");
        return -1;
    }

    int index1 = grabFrame(V4L2_SECOND_DEVICE, NULL);
    if (index1 < 0) {
        LogError("Error in grabbing frame from 2'nd device!");
        return -1;
    }
    if (index0 != index1) {
        LogError("Error, line:%d", __LINE__);
        return -1;
    }
    if (index0 >= MAX_V4L2_BUFFERS) {
        LogError("Error, line:%d", __LINE__);
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
    if(raw_data_dump.format == RAW_BAYER) {
        struct v4l2_buffer_info *buf =
            &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index0];
        unsigned int length = raw_data_dump.size;
        LogDetail("dumping raw data");
        void *start = mmap(NULL /* start anywhere */ ,
                                PAGE_ALIGN(length),
                                PROT_READ | PROT_WRITE /* required */ ,
                                MAP_SHARED /* recommended */ ,
                                video_fds[V4L2_FIRST_DEVICE], 0xfffff000);
        if (MAP_FAILED == start)
                LogError("mmap failed");
        else {
            printf("MMAP raw address from kerenl 0x%p", start);
        }
        write_image(start, length, buf->width, buf->height, "raw");
        if (-1 == munmap(start, PAGE_ALIGN(length)))
            LogError("munmap failed");
    }

    if(postview_rgb565) {
        toRGB565(m_postview_width, m_postview_height, m_postview_v4lformat, (unsigned char *)(*postview),
            (unsigned char *)postview_rgb565);
        LogDetail("postview w:%d, h:%d, dstaddr:0x%x",
            m_postview_width, m_postview_height, (int)postview_rgb565);
    }

    return index0;
}

int IntelCamera::putSnapshot(int index)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return putDualStreams(index);
}

//video recording Control
int IntelCamera::startCameraRecording(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;
    run_mode = VIDEO_RECORDING_MODE;
    ret = openSecondDevice();
    if (ret < 0)
        return ret;

    //Move mAAA out after enable open/close in CameraHardware
    mAAA->SwitchMode(run_mode);
    mAAA->SetFrameRate (framerate);
    mAAA->FlushManualSettings ();
    mAAA->SetDoneStatisticsState(false);

    if ((zoom_val != 0) && (m_recorder_width != 1920))
        set_zoom_val_real(zoom_val);

    //set DVS
    LogDetail("dvs,line:%d, set dvs val:%d to driver", __LINE__, mDVSOn);
    ret = atomisp_set_dvs(main_fd, mDVSOn);

    if (ret)
        LogError("dvs,line:%d, set dvs val:%d to driver fail", __LINE__, mDVSOn);

    ret = configureDevice(V4L2_FIRST_DEVICE, m_recorder_pad_width,
                          m_recorder_height, m_recorder_v4lformat, false);
    if (ret < 0)
        goto configure1_error;

    //176x144 using pad width
    ret = configureDevice(V4L2_SECOND_DEVICE, m_preview_pad_width,
                          m_preview_height, m_preview_v4lformat, false);
    if (ret < 0)
        goto configure2_error;

    //flush tnr
    if (mTnrOn != DEFAULT_TNR) {
        ret = atomisp_set_tnr(main_fd, mTnrOn);
        if (ret)
            LogError("Error setting xnr:%d, fd:%d", mTnrOn, main_fd);
    }

    ret = startCapture(V4L2_FIRST_DEVICE, VIDEO_NUM_BUFFERS);
    if (ret < 0)
        goto start1_error;

    ret = startCapture(V4L2_SECOND_DEVICE, VIDEO_NUM_BUFFERS);
    if (ret < 0)
        goto start2_error;

    return main_fd;

start2_error:
    stopCapture(V4L2_FIRST_DEVICE);
start1_error:
configure2_error:
configure1_error:
    closeSecondDevice();
    return ret;
}

void IntelCamera::stopCameraRecording(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    stopDualStreams();
}

void IntelCamera::stopDualStreams(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (m_flag_camera_start == 0) {
        LogDetail("doing nothing because m_flag_camera_start is 0");
        return ;
    }

    if (main_fd <= 0) {
        LogWarning("Camera was closed");
        return ;
    }

    stopCapture(V4L2_FIRST_DEVICE);
    stopCapture(V4L2_SECOND_DEVICE);
    closeSecondDevice();
}

int IntelCamera::trimRecordingBuffer(void *buf)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int size = m_frameSize(V4L2_PIX_FMT_NV12, m_recorder_width,
                           m_recorder_height);
    int padding_size = m_frameSize(V4L2_PIX_FMT_NV12, m_recorder_pad_width,
                                   m_recorder_height);
    void *tmp_buffer = malloc(padding_size);
    if (tmp_buffer == NULL) {
        LogError("Error allocating memory!");
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
    LogEntry(LOG_TAG, __FUNCTION__);
	enum atomisp_frame_status status;
    int index0 = grabFrame(V4L2_FIRST_DEVICE, &status);
    if (index0 < 0) {
        LogError("Error grabbing frame from 1'st device!");
        return -1;
    }

    int index1 = 0;
    if(!m_flag_camera_start[V4L2_SECOND_DEVICE])
        LogDetail("Camera is in preview mode!");
    else
        index1 = grabFrame(V4L2_SECOND_DEVICE, &status);
    if (index1 < 0) {
        LogError("Error grabbing frame from 2'nd device!");
        return -1;
    }

    if (index1 > 0 && index1 != index0) {
            LogError("Error,index1 = %d, index2 = %d", index0, index1);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    return putDualStreams(index);
}

int IntelCamera::openMainDevice(int camera_idx)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    int device = V4L2_FIRST_DEVICE;

    if (video_fds[device] > 0) {
        LogWarning("Device already opened!");
        return video_fds[device];
    }

    video_fds[device] = v4l2_capture_open(device);

    if (video_fds[device] < 0)
        return -1;

    // Query and check the capabilities
    if (v4l2_capture_querycap(video_fds[device], device, &cap) < 0)
        goto error0;

    main_fd = video_fds[device];

    // load init gamma table only once
    if (m_sensor_type == SENSOR_TYPE_RAW)
        atomisp_init_gamma (main_fd, mIspSettings.contrast,
                            mIspSettings.brightness, mIspSettings.inv_gamma);

    //Do some other intialization here after open
    //flushISPParameters();

    //Choose the camera sensor
    ret = v4l2_capture_s_input(video_fds[device], camera_idx);
    if (ret < 0)
        goto error0;
    return ret;

error0:
    v4l2_capture_close(video_fds[V4L2_FIRST_DEVICE]);
    video_fds[V4L2_FIRST_DEVICE] = -1;
    return -1;
}

int IntelCamera::openSecondDevice(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int device = V4L2_SECOND_DEVICE;
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
    video_fds[V4L2_SECOND_DEVICE] = -1;
    return -1;
}

void IntelCamera::closeMainDevice(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int device = V4L2_FIRST_DEVICE;

    if (video_fds[device] < 0) {
        LogWarning("Device already closed");
        return;
    }

    v4l2_capture_close(video_fds[device]);

    video_fds[device] = -1;
    main_fd = -1;
}

void IntelCamera::closeSecondDevice(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int device = V4L2_SECOND_DEVICE;

    //Close the second device
    if (video_fds[device] < 0)
        return ;
    v4l2_capture_close(video_fds[device]);
    video_fds[device] = -1;
}

int IntelCamera::detectDeviceResolution(int *w, int *h, int run_mode)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;
    int fourcc = V4L2_PIX_FMT_NV12;
    int device = V4L2_FIRST_DEVICE;

    if ((*w <= 0) || (*h <= 0)) {
        LogError("Wrong Width %d or Height %d", *w, *h);
        return -1;
    }

    //Switch the Mode before try the format.
    ret = set_capture_mode(run_mode);
    if (ret < 0)
        return ret;;

    //Set the format
    ret = v4l2_capture_try_format(video_fds[device], device, w, h, &fourcc);
    if (ret < 0)
        return ret;

    return 0;
}

int IntelCamera::configureDevice(int device, int w, int h, int fourcc, bool raw)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;
    LogDetail("device: %d, width:%d, height%d, mode%d format%d", device,
         w, h, run_mode, fourcc);

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LogError("Wrong device %d", device);
        return -1;
    }

    if ((w <= 0) || (h <= 0)) {
        LogError("Wrong Width %d or Height %d", w, h);
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
    ret = v4l2_capture_s_format(fd, device, w, h, fourcc, raw);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("device = %d", device);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("device = %d", device);

    int fd = video_fds[device];
    struct v4l2_buffer_pool *pool = &v4l2_buf_pool[device];

    for (int i = 0; i < pool->active_buffers; i++)
        v4l2_capture_free_buffer(fd, device, &pool->bufs[i]);
    v4l2_capture_release_buffers(fd, device);
}

int IntelCamera::activateBufferPool(int device)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("device = %d", device);

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
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("device = %d", device);

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LogError("Wrong device %d", device);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("device = %d", device);
    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LogError("Wrong device %d", device);
        return;
    }
    int fd = video_fds[device];

    //stream off
    v4l2_capture_streamoff(fd);

    destroyBufferPool(device);

    enableIndicator(0);
    enableTorch(0);

    m_flag_camera_start[device] = 0;
}


void IntelCamera::setRawFormat(enum raw_data_format format)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    raw_data_dump.format = format;
    return;
}


int IntelCamera::grabFrame(int device, enum atomisp_frame_status *status)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    int ret;
    struct v4l2_buffer buf;
    //Must start first
    if (!m_flag_camera_start[device])
        return -1;

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_SECOND_DEVICE)) {
        LogError("Wrong device %d", device);
        return -1;
    }

    ret = v4l2_capture_dqbuf(video_fds[device], &buf);

    if (ret < 0) {
        LogDetail("DQ error, reset the camera");
        ret = resetCamera();
        if (ret < 0) {
            LogError("Reset camera error");
            return ret;
        }
        ret = v4l2_capture_dqbuf(video_fds[device], &buf);
        if (ret < 0) {
            LogError("Reset camera error again");
            return ret;
        }
    }
    if (status)
        *status = (enum atomisp_frame_status)buf.reserved;
    return buf.index;
}

int IntelCamera::resetCamera(void)
{
    Mutex::Autolock lock(mIntelCameraAutoLock);
    LogEntry(LOG_TAG, __FUNCTION__);
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
        LogError("Wrong run_mode");
        break;
    }
    return ret;
}

//YUV420 to RGB565 for the postview output. The RGB565 from the preview stream
//is bad
void IntelCamera::yuv420_to_rgb565(int width, int height, unsigned char *src,
                                   unsigned short *dst)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    return framerate;
}

void IntelCamera::nv12_to_rgb565(int width, int height, unsigned char* yuvs, unsigned char* rgbs) {
    LogEntry2(LOG_TAG, __FUNCTION__);
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
    LogEntry2(LOG_TAG, __FUNCTION__);
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
    LogEntry2(LOG_TAG, __FUNCTION__);
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
    LogEntry2(LOG_TAG, __FUNCTION__);
    void *buffer;
    int size = width * height * 2;

    if (src == NULL || dst == NULL) {
        LogError("NULL data or source pointer");
        return;
    }

    if (src == dst) {
        buffer = calloc(1, size);
        if (buffer == NULL) {
            LogError("calloc error: %s", strerror(errno));
            return;
        }
    } else
        buffer = dst;

    switch(fourcc) {
    case V4L2_PIX_FMT_YUV420:
        LogDetail("yuv420 to rgb565 conversion");
        yuv420_to_rgb565(width, height, (unsigned char*)src, (unsigned short*)buffer);
        break;
    case V4L2_PIX_FMT_NV12:
        LogDetail("nv12 to rgb565 conversion");
        nv12_to_rgb565(width, height, (unsigned char*)src, (unsigned char*)buffer);
        break;
    case V4L2_PIX_FMT_RGB565:
        break;
    default:
        LogError("Unknown conversion format!");
        break;
    }

    if (src == dst) {
        memcpy(dst, buffer, size);
        free(buffer);
    }
}

void IntelCamera::toNV12(int width, int height, int fourcc, void *src, void *dst)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    void *buffer;
    int size = width * height * 3 / 2;

    if (src == NULL || dst == NULL) {
        LogError("NULL pointer");
        return;
    }

    if (src == dst) {
        buffer = calloc(1, size);
        if (buffer == NULL) {
            LogError("Alloc error: %s", strerror(errno));
            return;
        }
    } else
        buffer = dst;

    switch(fourcc) {
    case V4L2_PIX_FMT_YUYV:
        LogDetail2("yuyv422 to yuv420sp conversion");
        yuyv422_to_yuv420sp(width, height, (unsigned char*)src, (unsigned char*)buffer);
        break;
    case V4L2_PIX_FMT_YUV420:
        LogDetail2("yuv420 to yuv420sp conversion");
        yuv420_to_yuv420sp(width, height, (unsigned char*)src, (unsigned char*)buffer);
        break;
    default:
        LogError("Unknown conversion format");
        break;
    }

    if (src == dst) {
        memcpy(dst, buffer, size);
        free(buffer);
    }
}

int IntelCamera::getFd(void)
{
    return main_fd;
}

int IntelCamera::get_num_buffers(void)
{
    return num_buffers;
}

void IntelCamera::setPreviewUserptr(int index, void *addr)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    if (index > PREVIEW_NUM_BUFFERS) {
        LogError("index %d is out of range", index);
        return ;
    }
    v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index].data = addr;
}

void IntelCamera::setRecorderUserptr(int index, void *preview, void *recorder)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    if (index > VIDEO_NUM_BUFFERS) {
        LogError("index %d is out of range", index);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    int i, index, ret, last_index = 0;
    if (num > VIDEO_NUM_BUFFERS) {
        LogError("buffer number %d is out of range", num);
        return -1;
    }
    //DQ all buffer out
    for (i = 0; i < num; i++) {
        ret = grabFrame(V4L2_FIRST_DEVICE, NULL);
        if (ret < 0) {
            LogError("Error grabbing frame from 1'st device!");
            goto error;
        }
        ret = grabFrame(V4L2_SECOND_DEVICE, NULL);
        if (ret < 0) {
            LogError("Error grabbing frame from 2'nd device!");
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
        LogDetail("Update new userptr %p", recorder[index]);
        ret = v4l2_capture_qbuf(video_fds[V4L2_FIRST_DEVICE], index,
                          &v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index]);
        LogDetail("Update new userptr %p finished", recorder[index]);

        ret = v4l2_capture_qbuf(video_fds[V4L2_SECOND_DEVICE], index,
                          &v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index]);
    }
    //Start the DQ thread
    v4l2_capture_control_dq(main_fd, 1);
    return 0;
error2:
    v4l2_capture_control_dq(main_fd, 1);
error:
    return -1;
}

void IntelCamera::enableIndicator(int intensity)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (CAMERA_FACING_FRONT == m_camera_phy_id)
        return;

    if (intensity) {
        atomisp_set_attribute(main_fd, V4L2_CID_FLASH_INDICATOR_INTENSITY, intensity, "torch intensity");
        atomisp_set_attribute(main_fd, V4L2_CID_FLASH_MODE, ATOMISP_FLASH_MODE_TORCH, "flash mode");
    } else {
        atomisp_set_attribute(main_fd, V4L2_CID_FLASH_MODE, ATOMISP_FLASH_MODE_OFF, "flash mode");
    }
}

void IntelCamera::enableTorch(int intensity)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (CAMERA_FACING_FRONT == m_camera_phy_id)
        return;

    if (intensity) {
        atomisp_set_attribute(main_fd, V4L2_CID_FLASH_TORCH_INTENSITY, intensity, "torch intensity");
        atomisp_set_attribute(main_fd, V4L2_CID_FLASH_MODE, ATOMISP_FLASH_MODE_TORCH, "flash mode");
    } else {
        atomisp_set_attribute(main_fd, V4L2_CID_FLASH_MODE, ATOMISP_FLASH_MODE_OFF, "flash mode");
    }
}

int IntelCamera::requestFlash(int numFrames)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (CAMERA_FACING_FRONT == m_camera_phy_id)
        return 0;

    /* We don't drive the flash directly, instead we ask the ISP driver
     * to drive the flash. That way the driver can annotate the output
     * frames with the flash status. */
    if (atomisp_set_attribute(main_fd, V4L2_CID_REQUEST_FLASH, numFrames, "request flash"))
        return 0;
    return 1;
}

void IntelCamera::setFlashMode(int mode)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    mFlashMode = mode;
}

int IntelCamera::getFlashMode()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return mFlashMode;
}

void IntelCamera::setSnapshotFlip(int mode, int mflip)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    atomisp_image_flip (main_fd, mode, mflip);
}

//Use flags to detern whether it is called from the snapshot
int IntelCamera::set_zoom_val_real(int zoom)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (main_fd < 0) {
        LogDetail("device not opened!");
        return 0;
    }

    //Map 8x to 56. The real effect is 64/(64 - zoom) in the driver.
    //Max zoom is 60 because we only support 16x not 64x
    if (zoom != 0)
        zoom = 64 - (64 / (((zoom * 16 + 59)/ 60 )));

    LogDetail("set zoom to %d", zoom);
    return atomisp_set_zoom (main_fd, zoom);
}

int IntelCamera::set_zoom_val(int zoom)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (zoom == zoom_val)
        return 0;
    zoom_val = zoom;
    if (run_mode == STILL_IMAGE_MODE)
        return 0;
    return set_zoom_val_real(zoom);
}

int IntelCamera::get_zoom_val(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return zoom_val;
}

int IntelCamera::set_capture_mode(int mode)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (main_fd < 0) {
        LogWarning("Camera not opened!");
        return -1;
    }
    return atomisp_set_capture_mode(main_fd, mode);
}

//These are for the frame size and format
int IntelCamera::setPreviewSize(int width, int height, int fourcc)
{
    LOG1("@%s:%d", __FUNCTION__, __LINE__);
    LogEntry(LOG_TAG, __FUNCTION__);
    LOG1("@%s:%d", __FUNCTION__, __LINE__);
    if (width > m_preview_max_width || width <= 0)
        width = m_preview_max_width;
    LOG1("@%s:%d", __FUNCTION__, __LINE__);
    if (height > m_preview_max_height || height <= 0)
        height = m_preview_max_height;
    LOG1("@%s:%d", __FUNCTION__, __LINE__);
    m_preview_width = width;
    LOG1("@%s:%d", __FUNCTION__, __LINE__);
    m_preview_height = height;
    LOG1("@%s:%d", __FUNCTION__, __LINE__);
    m_preview_v4lformat = fourcc;
    LOG1("@%s:%d", __FUNCTION__, __LINE__);
    m_preview_pad_width = m_paddingWidth(fourcc, width, height);
    LogDetail("width(%d), height(%d), pad_width(%d), format(%d)",
         width, height, m_preview_pad_width, fourcc);
    LOG1("@%s:%d", __FUNCTION__, __LINE__);
    return 0;
}

int IntelCamera::getPreviewSize(int *width, int *height, int *frame_size,
                                int *padded_size)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    *width = m_preview_width;
    *height = m_preview_height;
    *frame_size = m_frameSize(m_preview_v4lformat, m_preview_width,
                              m_preview_height);
    *padded_size = m_frameSize(m_preview_v4lformat, m_preview_pad_width,
                       m_preview_height);
    LogDetail("width(%d), height(%d), size(%d)", *width, *height,
         *frame_size);
    return 0;
}

int IntelCamera::getPreviewPixelFormat(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return m_preview_v4lformat;
}

//postview
int IntelCamera::setPostViewSize(int width, int height, int fourcc)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("width(%d), height(%d), format(%d)",
         width, height, fourcc);
    m_postview_width = width;
    m_postview_height = height;
    m_postview_v4lformat = fourcc;

    return 0;
}

int IntelCamera::getPostViewSize(int *width, int *height, int *frame_size)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    return m_postview_v4lformat;
}

int IntelCamera::setSnapshotSize(int width, int height, int fourcc)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (width > m_snapshot_max_width || width <= 0)
        width = m_snapshot_max_width;
    if (height > m_snapshot_max_height || height <= 0)
        height = m_snapshot_max_height;
    m_snapshot_width  = width;
    m_snapshot_height = height;
    m_snapshot_v4lformat = fourcc;
    m_snapshot_pad_width = m_paddingWidth(fourcc, width, height);
    LogDetail("width(%d), height(%d), pad_width(%d), format(%d)",
         width, height, m_snapshot_pad_width, fourcc);
    return 0;
}

int IntelCamera::getSnapshotSize(int *width, int *height, int *frame_size)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    *width  = m_snapshot_width;
    *height = m_snapshot_height;

    *frame_size = m_frameSize(m_snapshot_v4lformat, m_snapshot_width,
                              m_snapshot_height);
    if (*frame_size == 0)
        *frame_size = m_snapshot_width * m_snapshot_height * BPP;

    return 0;
}

int IntelCamera::getMaxSnapshotSize(int *width, int *height)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    *width	= m_snapshot_width;
    *height = m_snapshot_height;
    return 0;
}

int IntelCamera::getSnapshotPixelFormat(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return m_snapshot_v4lformat;
}

void IntelCamera::setSnapshotUserptr(int index, void *pic_addr, void *pv_addr)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (index >= num_snapshot) {
        LogError("index %d is out of range", index);
        return ;
    }

    v4l2_buf_pool[V4L2_FIRST_DEVICE].bufs[index].data = pic_addr;
    v4l2_buf_pool[V4L2_SECOND_DEVICE].bufs[index].data = pv_addr;
}

int IntelCamera::setRecorderSize(int width, int height, int fourcc)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    LogDetail("Max:W %d, MaxH: %d", m_recorder_max_width, m_recorder_max_height);
    if (width > m_recorder_max_width || width <= 0)
        width = m_recorder_max_width;
    if ((height > m_recorder_max_height) || (height <= 0))
        height = m_recorder_max_height;
    m_recorder_width  = width;
    m_recorder_height = height;
    m_recorder_v4lformat = fourcc;
    m_recorder_pad_width = m_paddingWidth(fourcc, width, height);
    LogDetail("width(%d), height(%d), pad_width(%d), format(%d)",
         width, height, m_recorder_pad_width, fourcc);
    return 0;
}

int IntelCamera::getRecorderSize(int *width, int *height, int *frame_size,
                                 int *padded_size)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    *width  = m_recorder_width;
    *height = m_recorder_height;

    *frame_size = m_frameSize(m_recorder_v4lformat, m_recorder_width,
                              m_recorder_height);
    if (*frame_size == 0)
        *frame_size = m_recorder_width * m_recorder_height * BPP;
    *padded_size = m_frameSize(m_recorder_v4lformat, m_recorder_pad_width,
                              m_recorder_height);

    LogDetail("width(%d), height(%d),size (%d)", *width, *height,
         *frame_size);
    return 0;
}

int IntelCamera::getRecorderPixelFormat(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return m_recorder_v4lformat;
}

int IntelCamera::m_frameSize(int format, int width, int height)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
        LogError("Invalid V4L2 pixel format(%d)", format);
    }

    return size;
}

int IntelCamera::m_paddingWidth(int format, int width, int height)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
        LogError("Invalid V4L2 pixel format(%d)", format);
    }

    return padding;
}

int IntelCamera::setShadingCorrection(bool on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    mShadingCorrection = on;
    if (main_fd < 0) {
        LogDetail("Set Shading Correction failed. "
                "will set after device is open.");
        return 0;
    }
    return atomisp_set_sc(main_fd, on);
}

int IntelCamera::setColorEffect(int effect)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    mColorEffect = effect;
    if (main_fd < 0){
        LogDetail("Set Color Effect failed. "
                "will set after device is open.");
        return 0;
    }
    ret = atomisp_set_tone_mode(main_fd,
                        (enum v4l2_colorfx)effect);
    if (ret) {
        LogError("Error setting color effect:%d, fd:%d", effect, main_fd);
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
            LogError("Error setting contrast and brightness in color effect:%d, fd:%d", effect, main_fd);
            return -1;
        }
    }

    return 0;
}

int IntelCamera::setXNR(bool on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    mXnrOn = on;
    if (main_fd < 0){
        LogDetail("Set XNR failed. "
                "will set after device is open.");
        return 0;
    }
    ret = atomisp_set_xnr(main_fd, on);
    if (ret) {
        LogError("Error setting xnr:%d, fd:%d", on, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::setGDC(bool on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    mGDCOn = on;
    //set the GDC when do the still image capture
    return 0;
}

int IntelCamera::setDVS(bool on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    mDVSOn = on;
    LogDetail("dvs,line:%d, set dvs val:%d to 3A", __LINE__, mDVSOn);
    mAAA->SetStillStabilizationEnabled(mDVSOn); // set 3A

    return 0;
}

bool IntelCamera::getDVS(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return mDVSOn;
}

int IntelCamera::setTNR(bool on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    mTnrOn= on;
    if (main_fd < 0){
        LogDetail("Set TNR failed."
                " will set after device is open.");
        return 0;
    }
    ret = atomisp_set_tnr(main_fd, on);
    if (ret) {
        LogError("Error setting tnr:%d, fd:%d", on, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::setNREE(bool on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret, ret2;
    mNrEeOn= on;
    if (main_fd < 0){
        LogDetail("Set NR/EE failed."
                " will set after device is open.");
        return 0;
    }
    ret = atomisp_set_ee(main_fd,on);
    ret2 = atomisp_set_bnr(main_fd,on);

    if (ret || ret2) {
        LogError("Error setting NR/EE:%d, fd:%d", on, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::setMACC(int macc)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    mMacc= macc;
    if (main_fd < 0){
        LogDetail("Set MACC failed. "
                "will set after device is open.");
        return 0;
    }
    ret = atomisp_set_macc(main_fd,1,macc);
    if (ret) {
        LogError("Error setting MACC:%d, fd:%d", macc, main_fd);
        return -1;
    }
    return 0;
}

int IntelCamera::flushISPParameters ()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret, ret2;

    if (main_fd < 0){
        LogDetail("flush Color Effect failed."
                " will set after device is open.");
        return 0;
    }

    //flush color effect
    if (mColorEffect != DEFAULT_COLOR_EFFECT){
        ret = atomisp_set_tone_mode(main_fd,
                (enum v4l2_colorfx)mColorEffect);
        if (ret) {
            LogError("Error setting color effect:%d, fd:%d",
                            mColorEffect, main_fd);
        }
        else {
            LogError("set color effect success to %d.", mColorEffect);
        }
    }
    else  LogDetail("ignore color effect setting");

    // do gamma inverse only if start status is negative effect
    if (mColorEffect == V4L2_COLORFX_NEGATIVE) {
        mIspSettings.inv_gamma = true;
        ret = atomisp_set_contrast_bright(main_fd, mIspSettings.contrast,
                              mIspSettings.brightness, mIspSettings.inv_gamma);
        if (ret != 0)
        {
            LogError("Error setting contrast and brightness in color effect "
                 "flush:%d, fd:%d", mColorEffect, main_fd);
            return -1;
        }
    }


    //flush xnr
    if (mXnrOn != DEFAULT_XNR) {
        ret = atomisp_set_xnr(main_fd, mXnrOn);
        if (ret) {
            LogError("Error setting xnr:%d, fd:%d",  mXnrOn, main_fd);
            return -1;
        }
        mColorEffect = DEFAULT_COLOR_EFFECT;
    }
    else
        LogDetail("ignore xnr setting");

    //flush nr/ee
    if (mNrEeOn != DEFAULT_NREE) {
        ret = atomisp_set_ee(main_fd,mNrEeOn);
        ret2 = atomisp_set_bnr(main_fd,mNrEeOn);

        if (ret || ret2) {
            LogError("Error setting NR/EE:%d, fd:%d", mNrEeOn, main_fd);
            return -1;
        }
    }

    //flush macc
    if (mMacc != DEFAULT_MACC) {
        ret = atomisp_set_macc(main_fd,1,mMacc);
        if (ret) {
            LogError("Error setting NR/EE:%d, fd:%d", mMacc, main_fd);
        }
    }

    //flush shading correction
    if (mShadingCorrection != DEFAULT_SHADING_CORRECTION) {
        ret = atomisp_set_sc(main_fd, mShadingCorrection);
        if (ret) {
            LogError("Error setting shading correction:%d, fd:%d", mShadingCorrection, main_fd);
        }
    }

    return 0;
}

//Remove padding for 176x144 resolution
void IntelCamera::trimRGB565(unsigned char *src, unsigned char* dst,
                             int src_width, int src_height,
                             int dst_width, int dst_height)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry2(LOG_TAG, __FUNCTION__);
    unsigned char *dst_y, *dst_uv;
    unsigned char *src_y, *src_uv;

    dst_y = dst;
    src_y = src;

    LogDetail("%d:%d:%d:%d", src_width, src_height, dst_width, dst_height);

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

int IntelCamera::getFocusLength(unsigned int *length)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mSomeEXIFAttibutes.valid)
        return *length = mSomeEXIFAttibutes.AtomispMakeNoteInfo.focal_length;
    else {
        LogInfo("WARNING: invalid EXIF focus length");
        return -1;
    }
}

int IntelCamera::getFnumber(unsigned int *fnumber)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mSomeEXIFAttibutes.valid)
        return *fnumber = mSomeEXIFAttibutes.AtomispMakeNoteInfo.f_number_curr;
    else {
        LogInfo("WARNING: invalid EXIF fnumber");
        return -1;
    }
}

int IntelCamera::getFnumberRange(unsigned int *fnumber_range)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mSomeEXIFAttibutes.valid)
        return *fnumber_range = mSomeEXIFAttibutes.AtomispMakeNoteInfo.f_number_range;
    else {
        LogInfo("WARNING: invalid EXIF fnumber range");
        return -1;
    }
}

int IntelCamera::acheiveEXIFAttributesFromDriver()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;
    int fd = video_fds[V4L2_FIRST_DEVICE];

    mSomeEXIFAttibutes.valid = false;

    if (fd > 0) {
        ret = atomisp_get_make_note_info(fd, &mSomeEXIFAttibutes.AtomispMakeNoteInfo);
        if (ret < 0) {
            LogInfo("WARNING: get make note from driver failed");
            return -1;
        }

        mSomeEXIFAttibutes.valid = true;
        return ret;
    }
    else {
        LogInfo("WARNING: invalid file descriptor");
        return -1;
    }
}


int IntelCamera::v4l2_capture_open(int device)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int fd;
    struct stat st;

    if ((device < V4L2_FIRST_DEVICE) || (device > V4L2_THIRD_DEVICE)) {
        LogError("Wrong device node %d", device);
        return -1;
    }

    char *dev_name = dev_name_array[device];
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

    if (device == V4L2_THIRD_DEVICE)
        file_injection = true;

    return fd;
}

void IntelCamera::v4l2_capture_close(int fd)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    /* close video device */
    LogDetail("----close device ---");
    if (fd < 0) {
        LogWarning("Device not opened!");
        return ;
    }

    if (close(fd) < 0) {
        LogError("Close video device failed!");
        return;
    }
    file_injection = false;
}

int IntelCamera::v4l2_capture_querycap(int fd, int device, struct v4l2_capability *cap)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;

    ret = ioctl(fd, VIDIOC_QUERYCAP, cap);

    if (ret < 0) {
        LogError("VIDIOC_QUERYCAP failed");
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

int IntelCamera::v4l2_capture_s_input(int fd, int index)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    struct v4l2_input input;
    int ret;

    LogDetail("VIDIOC_S_INPUT");
    input.index = index;

    ret = ioctl(fd, VIDIOC_S_INPUT, &input);

    if (ret < 0) {
        LogError("VIDIOC_S_INPUT index %d failed",
             input.index);
        return ret;
    }
    return ret;
}


int IntelCamera::v4l2_capture_s_format(int fd, int device, int w, int h, int fourcc, bool raw)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    int ret;
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);
    LogDetail("VIDIOC_S_FMT");

    if (device == V4L2_THIRD_DEVICE) {
        g_isp_timeout = ATOMISP_FILEINPUT_POLL_TIMEOUT;
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        v4l2_fmt.fmt.pix.width = file_image.width;
        v4l2_fmt.fmt.pix.height = file_image.height;
        v4l2_fmt.fmt.pix.pixelformat = file_image.format;
        v4l2_fmt.fmt.pix.sizeimage = file_image.size;
        v4l2_fmt.fmt.pix.priv = file_image.bayer_order;

        LogDetail("width: %d, height: %d, format: %x, size: %d, bayer_order: %d",
             file_image.width,
             file_image.height,
             file_image.format,
             file_image.size,
             file_image.bayer_order);

        ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
        if (ret < 0) {
            LogError("VIDIOC_S_FMT failed %s", strerror(errno));
            return -1;
        }
        return 0;
    }

    g_isp_timeout = ATOMISP_POLL_TIMEOUT;
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl (fd,  VIDIOC_G_FMT, &v4l2_fmt);
    if (ret < 0) {
        LogError("VIDIOC_G_FMT failed %s", strerror(errno));
        return -1;
    }
    if (raw) {
        LogDetail("Choose raw dump path");
        v4l2_fmt.type = V4L2_BUF_TYPE_PRIVATE;
    }
    else
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    v4l2_fmt.fmt.pix.width = w;
    v4l2_fmt.fmt.pix.height = h;
    v4l2_fmt.fmt.pix.pixelformat = fourcc;
    v4l2_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        LogError("VIDIOC_S_FMT failed %s", strerror(errno));
        return -1;
    }

    if (raw) {
        raw_data_dump.size = v4l2_fmt.fmt.pix.priv;
        LogDetail("raw data size from kernel %d", raw_data_dump.size);
    }

    return 0;
}

int IntelCamera::v4l2_capture_try_format(int fd, int device, int *w, int *h,
                                         int *fourcc)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    int ret;
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);
    LogDetail("VIDIOC_TRY_FMT");

    if (device == V4L2_THIRD_DEVICE) {
        *w = file_image.width;
        *h = file_image.height;
        *fourcc = file_image.format;

        LogDetail("width: %d, height: %d, format: %x, size: %d, bayer_order: %d",
             file_image.width,
             file_image.height,
             file_image.format,
             file_image.size,
             file_image.bayer_order);

        return 0;
    }

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    v4l2_fmt.fmt.pix.width = *w;
    v4l2_fmt.fmt.pix.height = *h;
    v4l2_fmt.fmt.pix.pixelformat = *fourcc;
    v4l2_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    ret = ioctl(fd, VIDIOC_TRY_FMT, &v4l2_fmt);
    if (ret < 0) {
        LogError("VIDIOC_S_FMT failed %s", strerror(errno));
        return -1;
    }

    *w = v4l2_fmt.fmt.pix.width;
    *h = v4l2_fmt.fmt.pix.height;
    *fourcc = v4l2_fmt.fmt.pix.pixelformat;
    return 0;
}

int IntelCamera::v4l2_capture_g_framerate(int fd, float * framerate, int width,
                                         int height, int pix_fmt)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
        LogWarning("ioctl failed: %s", strerror(errno));
        return ret;
    }

    assert(0 !=frm_interval.discrete.numerator);

    *framerate = frm_interval.discrete.denominator;
    *framerate /= frm_interval.discrete.numerator;

    return 0;
}

int IntelCamera::v4l2_capture_request_buffers(int fd, int device, uint num_buffers)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    struct v4l2_requestbuffers req_buf;
    int ret;
    CLEAR(req_buf);

    if (fd < 0)
        return 0;
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

    LogDetail("VIDIOC_REQBUFS, count=%d", req_buf.count);
    ret = ioctl(fd, VIDIOC_REQBUFS, &req_buf);

    if (ret < 0) {
        LogError("VIDIOC_REQBUFS %d failed %s",
             num_buffers, strerror(errno));
        return ret;
    }

    if (req_buf.count < num_buffers)
        LogWarning("Got less buffers than requested!");

    return req_buf.count;
}

/* MMAP the buffer or allocate the userptr */
int IntelCamera::v4l2_capture_new_buffer(int fd, int device, int index, struct v4l2_buffer_info *buf)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    void *data;
    int ret;
    struct v4l2_buffer *vbuf = &buf->vbuffer;
    vbuf->flags = 0x0;

    if (device == V4L2_THIRD_DEVICE) {
        vbuf->index = index;
        vbuf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        vbuf->memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fd, VIDIOC_QUERYBUF, vbuf);
        if (ret < 0) {
            LogError("VIDIOC_QUERYBUF failed %s", strerror(errno));
            return -1;
        }

        data = mmap(NULL, vbuf->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    vbuf->m.offset);

        if (MAP_FAILED == data) {
            LogError("mmap failed: %sn", strerror(errno));
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
        LogError("VIDIOC_QUERYBUF failed %s", strerror(errno));
        return ret;
    }

    if (memory_userptr) {
         vbuf->m.userptr = (unsigned int)(buf->data);
    } else {
        data = mmap(NULL, vbuf->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    vbuf->m.offset);

        if (MAP_FAILED == data) {
            LogError("mmap failed %s", strerror(errno));
            return -1;
        }
        buf->data = data;
    }

    buf->length = vbuf->length;
    LogDetail("index %u", vbuf->index);
    LogDetail("type %d", vbuf->type);
    LogDetail("bytesused %u", vbuf->bytesused);
    LogDetail("flags %08x", vbuf->flags);
    LogDetail("memory %u", vbuf->memory);
    if (memory_userptr) {
        LogDetail("userptr:  %lu", vbuf->m.userptr);
    }
    else {
        LogDetail("MMAP offset:  %u", vbuf->m.offset);
    }

    LogDetail("length %u", vbuf->length);
    LogDetail("input %u", vbuf->input);

    return ret;
}

/* Unmap the buffer or free the userptr */
int IntelCamera::v4l2_capture_free_buffer(int fd, int device, struct v4l2_buffer_info *buf_info)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;
    void *addr = buf_info->data;
    size_t length = buf_info->length;

    if (device == V4L2_THIRD_DEVICE)
        if ((ret = munmap(addr, length)) < 0) {
            LogError("munmap failed: %s", strerror(errno));
            return ret;
        }

    if (!memory_userptr) {
        if ((ret = munmap(addr, length)) < 0) {
            LogError("munmap failed %s", strerror(errno));
            return ret;
        }
    }
    return ret;
}

int IntelCamera::v4l2_capture_streamon(int fd)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LogError("VIDIOC_STREAMON failed: %s", strerror(errno));
        return ret;
    }

    return ret;
}

int IntelCamera::v4l2_capture_streamoff(int fd)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (fd < 0) //Device is closed
        return 0;
    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LogError("VIDIOC_STREAMOFF failed %s", strerror(errno));
        return ret;
    }

    return ret;
}

int IntelCamera::v4l2_capture_qbuf(int fd, int index, struct v4l2_buffer_info *buf)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    struct v4l2_buffer *v4l2_buf = &buf->vbuffer;
    int ret;

    if (fd < 0) //Device is closed
        return 0;
    ret = ioctl(fd, VIDIOC_QBUF, v4l2_buf);
    if (ret < 0) {
        LogError("VIDIOC_QBUF index %d failed %s",
             index, strerror(errno));
        return ret;
    }

    return ret;
}

int IntelCamera::v4l2_capture_control_dq(int fd, int start)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
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
        LogError("VIDIOC_QBUF index %d failed %s",
             vbuf.index, strerror(errno));
        return ret;
    }
    return 0;
}


int IntelCamera::v4l2_capture_g_parm(int fd, struct v4l2_streamparm *parm)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;

    parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_G_PARM, parm);
    if (ret < 0) {
        LogError("VIDIOC_G_PARM, failed %s", strerror(errno));
        return ret;
    }

    LogDetail("timeperframe: numerator %d, denominator %d",
         parm->parm.capture.timeperframe.numerator,
         parm->parm.capture.timeperframe.denominator);

    return ret;
}

int IntelCamera::v4l2_capture_s_parm(int fd, int device, struct v4l2_streamparm *parm)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;

    if (device == V4L2_THIRD_DEVICE) {
        parm->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        parm->parm.output.outputmode = OUTPUT_MODE_FILE;
    } else
        parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_S_PARM, parm);
    if (ret < 0) {
        LogError("VIDIOC_S_PARM, failed %s", strerror(errno));
        return ret;
    }

    return ret;
}

int IntelCamera::v4l2_capture_release_buffers(int fd, int device)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return v4l2_capture_request_buffers(fd, device, 0);
}

int IntelCamera::v4l2_capture_dqbuf(int fd, struct v4l2_buffer *buf)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
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
        LogError("DQ error -- ret is %d", ret);
        switch (errno) {
        case EINVAL:
            LogError("Failed to get frames from device. %s",
                 strerror(errno));
            return -1;
        case EINTR:
            LogWarning("Could not sync the buffer %s",
                 strerror(errno));
            break;
        case EAGAIN:
            LogWarning("No buffer in the queue %s",
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


int IntelCamera::v4l2_register_bcd(int fd, int num_frames,
                      void **ptrs, int w, int h, int fourcc, int size)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;
    int i;
    struct atomisp_bc_video_package ioctl_package;
    bc_buf_params_t buf_param;

    //release it if it is registered
    if (m_bcd_registered) {
        v4l2_release_bcd(fd);
        m_bcd_registered = false;
    }

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
        LogError("Failed to request buffers from buffer class"
             " camera driver (ret=%d).", ret);
        return -1;
    }
    LogDetail("fd:%d, request bcd buffers count=%d, width:%d, stride:%d,"
         " height:%d, fourcc:%x", fd, buf_param.count, buf_param.width,
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
            LogError("Failed to set buffer phyaddr from buffer class"
                 " camera driver (ret=%d).", ret);
            return -1;
        }
    }

    ioctl_package.ioctl_cmd = BC_Video_ioctl_get_buffer_count;
    ret = ioctl(fd, ATOMISP_IOC_CAMERA_BRIDGE, &ioctl_package);
    if (ret < 0 || ioctl_package.outputparam != num_frames)
        LogError("Check bcd buffer count error");
    LogDetail("Check bcd buffer count = %d",
         ioctl_package.outputparam);

    m_bcd_registered = true;

    return ret;
}

int IntelCamera::v4l2_release_bcd(int fd)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;
    struct atomisp_bc_video_package ioctl_package;
    bc_buf_params_t buf_param;

    ioctl_package.ioctl_cmd = BC_Video_ioctl_release_buffer_device;
    ret = ioctl(fd, ATOMISP_IOC_CAMERA_BRIDGE, &ioctl_package);
    if (ret < 0) {
        LogError("Failed to release buffers from buffer class camera"
             " driver (ret=%d).fd:%d", ret, fd);
        return -1;
    }

    return 0;
}

int IntelCamera::v4l2_read_file(char *file_name, int file_width, int file_height,
              int format, int bayer_order)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int file_fd = -1;
    int file_size = 0;
    void *file_buf = NULL;
    struct stat st;

    /* Open the file we will transfer to kernel */
    if ((file_fd = open(file_name, O_RDONLY)) == -1) {
        LogError("Failed to open %s", file_name);
        return -1;
    }

    CLEAR(st);
    if (fstat(file_fd, &st) < 0) {
        LogError("fstat %s failed", file_name);
        return -1;
    }

    file_size = st.st_size;
    if (file_size == 0) {
        LogError("empty file %s", file_name);
        return -1;
    }

    file_buf = mmap(NULL, PAGE_ALIGN(file_size),
                    MAP_SHARED, PROT_READ, file_fd, 0);
    if (file_buf == MAP_FAILED) {
        LogError("mmap failed %s", file_name);
        return -1;
    }

    file_image.name = file_name;
    file_image.size = PAGE_ALIGN(file_size);
    file_image.mapped_addr = (char *)file_buf;
    file_image.width = file_width;
    file_image.height = file_height;

    LogDetail("mapped_addr=%p, width=%d, height=%d, size=%d",
        (char *)file_buf, file_width, file_height, file_image.size);

    file_image.format = format;
    file_image.bayer_order = bayer_order;

    return 0;
}

void IntelCamera::v4l2_set_isp_timeout(int timeout)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    g_isp_timeout = timeout;
}

int IntelCamera::xioctl (int fd, int request, void *arg, const char *name)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;

    LogDetail ("ioctl %s ", name);

    do {
        ret = ioctl (fd, request, arg);
    } while (-1 == ret && EINTR == errno);

    if (ret < 0)
        LOGW ("failed: %s", strerror (errno));
    else
        LogDetail ("ok");

    return ret;
}

int IntelCamera::atomisp_set_capture_mode(int fd, int mode)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
        LogError("error %s", strerror(errno));
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
    LogEntry(LOG_TAG, __FUNCTION__);
    struct v4l2_control control;
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control ext_control;

    LogDetail ("getting value of attribute %d: %s", attribute_num, name);

    if (fd < 0)
        return -1;

    control.id = attribute_num;
    controls.ctrl_class = V4L2_CTRL_CLASS_USER;
    controls.count = 1;
    controls.controls = &ext_control;
    ext_control.id = attribute_num;

    if (ioctl(fd, VIDIOC_G_CTRL, &control) == 0) {
        *value = control.value;
	return 0;
    }

    if (ioctl(fd, VIDIOC_G_EXT_CTRLS, &controls) == 0) {
        *value = ext_control.value;
	return 0;
    }

    controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;

    if (ioctl(fd, VIDIOC_G_EXT_CTRLS, &controls) == 0) {
        *value = ext_control.value;
	return 0;
    }

    LogError("Failed to get value for control %s (%d) on device '%d', %s\n.",
         name, attribute_num, fd, strerror(errno));
    return -1;
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
    LogEntry(LOG_TAG, __FUNCTION__);
    struct v4l2_control control;
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control ext_control;

    LogDetail ("setting attribute [%s] to %d", name, value);

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

int IntelCamera::atomisp_get_de_config (int fd, struct atomisp_de_config *de_cfg)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_ISP_FALSE_COLOR_CORRECTION, de_cfg);
}

int IntelCamera::atomisp_get_macc_tbl (int fd, struct atomisp_macc_config *macc_config)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_ISP_MACC,macc_config);
}

int IntelCamera::atomisp_get_ctc_tbl (int fd, struct atomisp_ctc_table *ctc_tbl)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_ISP_CTC, ctc_tbl);
}

int IntelCamera::atomisp_get_gdc_tbl (int fd, struct atomisp_morph_table *morph_tbl)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_ISP_GDC_TAB, morph_tbl);
}

int IntelCamera::atomisp_get_tnr_config (int fd, struct atomisp_tnr_config *tnr_cfg)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_TNR, tnr_cfg);
}


int IntelCamera::atomisp_get_ee_config (int fd, struct atomisp_ee_config *ee_cfg)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_EE, ee_cfg);

}

int IntelCamera::atomisp_get_nr_config (int fd, struct atomisp_nr_config *nr_cfg)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_BAYER_NR, nr_cfg);

}

int IntelCamera::atomisp_get_dp_config (int fd, struct atomisp_dp_config *dp_cfg)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_ISP_BAD_PIXEL_DETECTION, dp_cfg);
}

int IntelCamera::atomisp_get_wb_config (int fd, struct atomisp_wb_config *wb_cfg)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_ISP_WHITE_BALANCE, wb_cfg);
}
int IntelCamera::atomisp_get_ob_config (int fd, struct atomisp_ob_config *ob_cfg)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_BLACK_LEVEL_COMP, ob_cfg);
}

int IntelCamera::atomisp_get_fpn_tbl(int fd, struct atomisp_frame* fpn_tbl)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_G_ISP_FPN_TABLE, fpn_tbl);
}

/*
  Make gamma table
*/
int IntelCamera::autoGmLut (unsigned short *pptDst, struct atomisp_gm_config *cfg_gm)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    return setColorEffect(effect);
}


int IntelCamera::atomisp_set_sc (int fd, int on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_set_attribute (fd, V4L2_CID_ATOMISP_SHADING_CORRECTION, on,
                                     "Shading Correction");
}

/* Bad Pixel Detection*/
int IntelCamera::atomisp_set_bpd (int fd, int on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_get_attribute (fd, V4L2_CID_ATOMISP_BAD_PIXEL_DETECTION,
                                     on, "Bad Pixel Detection");
}

int IntelCamera::atomisp_set_bnr (int fd, int on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    /* YCC NR use the same parameter as Bayer NR */
    return atomisp_set_bnr(fd, on);
}

int IntelCamera::atomisp_set_ee (int fd, int on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    static struct atomisp_ob_config ob_off;
    struct atomisp_ob_config ob_on;
    static int current_status = 0;
    int ret;

    if (on && current_status) {
        LogDetail("Black Level Compensation Already On");
        return 0;
    }

    if (!on && !current_status) {
        LogDetail("Black Level Composition Already Off");
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
            LogDetail("Error Get black level composition");
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
                LogDetail("Error Set black level composition");
                return -1;
            }
        }
    } else {
        if (_xioctl (fd, ATOMISP_IOC_S_BLACK_LEVEL_COMP, &ob_off) < 0) {
            LogDetail("Error Set black level composition");
            return -1;
        }
    }
    current_status = on;
    return 0;
}

int IntelCamera::atomisp_set_tnr (int fd, int on)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_S_XNR, &on);
}

/* Configure the color effect Mode in the kernel
 */

int IntelCamera::atomisp_set_tone_mode (int fd, enum v4l2_colorfx colorfx)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_set_attribute (fd, V4L2_CID_COLORFX, colorfx, "Color Effect");
}

int IntelCamera::atomisp_get_tone_mode (int fd, int *colorfx)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_get_attribute (fd, V4L2_CID_COLORFX, colorfx, "Color Effect");
}

int IntelCamera::atomisp_set_gamma_tbl (int fd, struct atomisp_gamma_table *g_tbl)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return _xioctl (fd, ATOMISP_IOC_S_ISP_GAMMA, g_tbl);
}

// apply gamma table from g_gamma_table_original to g_gamma_table according
// current brightness, contrast and inverse settings
int IntelCamera::atomisp_apply_to_runtime_gamma(int contrast,
                                                 int brightness, bool inv_gamma)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = _xioctl (fd, ATOMISP_IOC_G_ISP_GAMMA, &g_gamma_table_original);
    if (ret < 0)
        return -1;
    else
        return atomisp_apply_to_runtime_gamma(contrast, brightness, inv_gamma);
}

int IntelCamera::atomisp_set_gamma_from_value (int fd, float gamma, int contrast,
                                           int brightness, bool inv_gamma)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret2;
    int ret;
    ret2 = atomisp_set_attribute (fd, V4L2_CID_ATOMISP_POSTPROCESS_GDC_CAC,
            on, "GDC");
    if (on) {
        if (ci_adv_cfg_file_loaded()) {
            LogDetail("cfg file already loaded");
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
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_set_attribute(fd, V4L2_CID_ATOMISP_VIDEO_STABLIZATION,
                                    on, "Video Stabilization");
}

int IntelCamera::atomisp_get_exposure (int fd, int *exposure)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_get_attribute (fd, V4L2_CID_EXPOSURE_ABSOLUTE, exposure, "Exposure");
}

int IntelCamera::atomisp_get_aperture (int fd, int *aperture)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_get_attribute (fd, V4L2_CID_IRIS_ABSOLUTE, aperture, "Aperture");
}

int IntelCamera::atomisp_set_focus_posi (int fd, int focus)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_set_attribute (fd, V4L2_CID_FOCUS_ABSOLUTE, focus, "Focus");
}

int IntelCamera::atomisp_get_focus_posi (int fd, int *focus)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_get_attribute (fd, V4L2_CID_FOCUS_ABSOLUTE, focus, "Focus");
}

int IntelCamera::atomisp_get_make_note_info(int fd, atomisp_makernote_info*nt)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = 0;

    ret = xioctl (fd, ATOMISP_IOC_ISP_MAKERNOTE, nt, "make_note");
    return ret;
}

int IntelCamera::atomisp_set_zoom (int fd, int zoom)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_set_attribute (fd, V4L2_CID_ZOOM_ABSOLUTE, zoom, "zoom");
}

int IntelCamera::atomisp_get_zoom (int fd, int *zoom)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return atomisp_get_attribute (fd, V4L2_CID_ZOOM_ABSOLUTE, zoom, "Zoom");
}


int IntelCamera::atomisp_image_flip (int fd, int mode, int mflip)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int mflip_attribute_num = (mflip == FLIP_H) ? V4L2_CID_HFLIP : V4L2_CID_VFLIP;
    return atomisp_set_attribute(fd, mflip_attribute_num,
                                 mode, "image flip");
}

int IntelCamera::atomisp_set_cfg_from_file(int fd)
{
    LogEntry(LOG_TAG, __FUNCTION__);
   return atomisp_set_cfg(fd);
}

int IntelCamera::find_cfg_index(char *in)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int i;

    for(i = 0; i < NUM_OF_CFG; i++) {
        if(!memcmp(in, FunctionKey[i], strlen(FunctionKey[i])))
            return i;
    }

    return -1;
}

int IntelCamera::analyze_cfg_value(unsigned int index, char *value)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
        case ISO:
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
    LogEntry(LOG_TAG, __FUNCTION__);
    char line[LINE_BUF_SIZE];
    char *line_name;
    char *line_value;
    int param_index;
    int res;
    int err = 0;

    FILE *fp;

    fp = fopen(CFG_PATH, "r");
    if (!fp) {
        LogError("Error open file:%s", CFG_PATH);
        return -1;
    }
    /* anaylize file item */
    while(fgets(line, LINE_BUF_SIZE, fp)) {
        line_name = line;
        line_value = strchr(line, '=') + 1;
        param_index = find_cfg_index(line_name);
        if (param_index < 0) {
            LogError("Error index in line: %s", line);
            err = -1;
            continue;
        }

        res = analyze_cfg_value(param_index, line_value);
        if (res < 0) {
            LogError("Error value in line: %s", line);
            err = -1;
            continue;
        }
    }

    fclose(fp);

    return err;
}

int IntelCamera::atomisp_set_cfg(int fd)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int err = 0;
    int i;
    unsigned int value;

    if (default_function_value_list[SWITCH] == FUNC_OFF) {
        LogDetail("Does not using the configuration file");
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
                LogDetail("macc:%s", FunctionOption_Macc[value]);
                break;
            case SC:
                LogDetail("sc:%s", FunctionOption_General[value]);
                if(value != FUNC_OFF)
                    err |= atomisp_set_sc(fd, value);
                break;
            case IE:
                LogDetail("ie:%s", FunctionOption_Ie[value]);
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
                LogDetail("gamma:%s", FunctionOption_General[value]);
                if(value != FUNC_OFF)
                    err |= atomisp_set_gamma_from_value(fd, DEFAULT_GAMMA_VALUE,
                                                        DEFAULT_CONTRAST,
                                                        DEFAULT_BRIGHTNESS,
                                                        !!DEFAULT_INV_GAMMA);
                break;
            case BPC:
                LogDetail("bpc:%s", FunctionOption_General[value]);
                if(value != FUNC_OFF)
                    err |= atomisp_set_bpd(fd, value);
                break;
            case FPN:
                LogDetail("fpn:%s", FunctionOption_General[value]);
                if(value != FUNC_OFF)
                    err |= atomisp_set_fpn(fd, value);
                break;
            case BLC:
                LogDetail("blc:%s", FunctionOption_General[value]);
                if(value != FUNC_OFF)
                    err |= atomisp_set_blc(fd, value);
                break;
            case EE:
                LogDetail("ee:%s", FunctionOption_General[value]);
                if(value != FUNC_OFF)
                    err |= atomisp_set_ee(fd, value);
                break;
            case NR:
                LogDetail("nr:%s", FunctionOption_General[value]);
                if(value != FUNC_OFF) {
                    err |= atomisp_set_bnr(fd, value);
                    err |= atomisp_set_ynr(fd, value);
                }
                break;
            case XNR:
                LogDetail("xnr:%s", FunctionOption_General[value]);
                if(value != FUNC_OFF)
                    err |= atomisp_set_xnr(fd, value);
                break;
            case BAYERDS:
                LogDetail("bayer-ds:%s", FunctionOption_General[value]);
                //Needed added new interface
                break;
            case ZOOM:
                LogDetail("zoom:%d", value);
                if(value != 0)
                    err |= atomisp_set_zoom(fd, value);
                break;
            case MF:
                LogDetail("mf:%d", value);
                if(value != 0)
                    err |= atomisp_set_focus_posi(fd, value);
                break;
            case MWB:
                LogDetail("mwb:%d", value);
                //Fix Me! Add 3A Lib interface here
                break;
            case ISO:
                LogDetail("iso:%d", value);
                //Fix Me! Add implementatino here
                break;
            case DIS:
                LogDetail("dis:%s", FunctionOption_General[value]);
                //Fix Me! Add setting DIS Interface
                break;
            case DVS:
                LogDetail("dvs:%s", FunctionOption_General[value]);
                if(value != 0)
                    err |= atomisp_set_dvs(fd, value);
                break;
            case REDEYE:
                LogDetail("red-eye:%s", FunctionOption_General[value]);
                //Fix Me! Add red-eye interface here
                break;
            default:
                err |= -1;
        }
    }

    return err;

}

}; // namespace android
