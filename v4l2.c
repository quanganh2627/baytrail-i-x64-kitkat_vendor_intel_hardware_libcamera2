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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include "v4l2.h"
#include <linux/atomisp.h>
#include <ci_adv_pub.h>

static volatile int32_t gLogLevel;

#define LOG1(...) LOGD_IF(gLogLevel >= 1, __VA_ARGS__);
#define LOG2(...) LOGD_IF(gLogLevel >= 2, __VA_ARGS__);


#define CLEAR(x) memset (&(x), 0, sizeof (x))

static void errno_print(v4l2_struct_t * v4l2_str, const char *s)
{
    fprintf(stderr, "%s error %d, %s\n",
            s, errno, strerror(errno));
    /* Do the resource cleanup here */
    v4l2_capture_stop(v4l2_str);
    v4l2_capture_finalize(v4l2_str);
}

static int xioctl(int fd, int request, void *arg)
{
    int r;

    do
        r = ioctl(fd, request, arg);
    while (-1 == r && EINTR == errno);

    return r;
}

static char *dev_name = "/dev/video0";

int v4l2_capture_open(v4l2_struct_t *v4l2_str)
{
    int fd;
    LOG1("---Open video device %s---", dev_name);
    fd = open(dev_name, O_RDWR);

    if (fd <= 0) {
        LOGE("Error opening video device %s", dev_name);
        return -1;
    }

    v4l2_str->dev_fd = fd;
    return 0;
}

void v4l2_capture_init(v4l2_struct_t *v4l2_str)
{
    int ret;

    assert(v4l2_str);

    v4l2_str->dev_name = dev_name;

    /* query capability */
    CLEAR(v4l2_str->cap);

    LOG2("VIDIOC_QUERYCAP");
    if (-1 == xioctl(v4l2_str->dev_fd, VIDIOC_QUERYCAP, &v4l2_str->cap)) {
        if (EINVAL == errno) {
            LOGE("%s is no V4L2 device", dev_name);
            return ;
        } else {
            errno_print(v4l2_str, "VIDIOC_QUERYCAP");
            return ;
        }
    }

    if (!(v4l2_str->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOGE("%s is no video capture device", dev_name);
        return ;
    }

    if (!(v4l2_str->cap.capabilities & V4L2_CAP_STREAMING)) {
        LOGE("%s is no video streaming device", dev_name);
        return;
    }

    struct v4l2_input input;

    CLEAR(input);

    LOG2("VIDIOC_S_INPUT");
    input.index = v4l2_str->camera_id;
    if (-1 == xioctl(v4l2_str->dev_fd, VIDIOC_S_INPUT, &input)) {
        if (EINVAL == errno)
            LOGE("input index %d is out of bounds!", input.index);
        else if (EBUSY == errno)
            LOGE("I/O is in progress, the input cannot be switched!");
        return ;
    } else
        LOG1("Set %s (index %d) as input", input.name, input.index);

    CLEAR(v4l2_str->parm);
}

void v4l2_capture_finalize(v4l2_struct_t *v4l2_str)
{
    /* close video device */
    LOGD("----close device %s---", dev_name);
    if (-1 == close(v4l2_str->dev_fd)) {
        LOGE("Close video device %s failed!",
             v4l2_str->dev_name);
        return;
    }
    v4l2_str->dev_fd = -1;
}

void v4l2_capture_create_frames(v4l2_struct_t *v4l2_str,
                                unsigned int frame_width,
                                unsigned int frame_height,
                                unsigned int frame_fmt,
                                unsigned int frame_num,
                                unsigned int *frame_ids)
{
    int ret;
    int fd;
    unsigned int i;

    assert(v4l2_str);

    fd = v4l2_str->dev_fd;

    v4l2_str->fm_width = frame_width;
    v4l2_str->fm_height = frame_height;
    v4l2_str->fm_fmt = frame_fmt;

    /* set format */
    CLEAR(v4l2_str->fmt);
    v4l2_str->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_str->fmt.fmt.pix.pixelformat = v4l2_str->fm_fmt;
    v4l2_str->fmt.fmt.pix.width = v4l2_str->fm_width;
    v4l2_str->fmt.fmt.pix.height = v4l2_str->fm_height;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &(v4l2_str->fmt)))
        return;

    LOG2("VIDIOC_S_FMT");

    /* Note VIDIOC_S_FMT may change width and height */

    /* request buffers */
    CLEAR(v4l2_str->req_buf);

    v4l2_str->req_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_str->req_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_str->req_buf.count = frame_num;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &v4l2_str->req_buf)) {
        if (EINVAL == errno) {
            LOGE("%s does not support "
                 "memory mapping", v4l2_str->dev_name);
            return;
        } else {
            errno_print(v4l2_str, "VIDIOC_REQBUFS");
            return ;
        }
    }

    if (v4l2_str->req_buf.count == 0) {
        LOGE("Insufficient buffer memory on %s", v4l2_str->dev_name);
        return;
    }

    LOG2("VIDIOC_REQBUFS, count=%d", v4l2_str->req_buf.count);

    v4l2_str->frame_num = v4l2_str->req_buf.count;

    for (i = 0; i < v4l2_str->frame_num; i++)
        *(frame_ids + i) = i;
}

void v4l2_capture_destroy_frames(v4l2_struct_t *v4l2_str)
{
    assert(v4l2_str);

    int fd = v4l2_str->dev_fd;

    CLEAR(v4l2_str->req_buf);

    v4l2_str->req_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_str->req_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_str->req_buf.count = 0;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &v4l2_str->req_buf)) {
        errno_print(v4l2_str, "VIDIOC_REQBUFS");
        return ;
    }

    LOG2("VIDIOC_REQBUFS, count=%d", v4l2_str->req_buf.count);

    v4l2_str->frame_num = 0;
}

void v4l2_capture_start(v4l2_struct_t *v4l2_str)
{
    int ret;
    unsigned int i;
    enum v4l2_buf_type type;

    assert(v4l2_str);

    for (i = 0; i < v4l2_str->frame_num; i++) {
        struct v4l2_buffer buf;

        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl(v4l2_str->dev_fd, VIDIOC_QBUF, &buf)) {
            errno_print(v4l2_str, "VIDIOC_QBUF");
            return ;
        }

        LOG2("VIDIOC_QBUF");
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl(v4l2_str->dev_fd, VIDIOC_STREAMON, &type))
        errno_print(v4l2_str, "VIDIOC_STREAMON");

    LOG2("VIDIOC_STREAMON");
}

void v4l2_capture_stop(v4l2_struct_t *v4l2_str)
{
    assert(v4l2_str);

    int fd = v4l2_str->dev_fd;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
        errno_print(v4l2_str, "VIDIOC_STREAMOFF");

    LOG2("VIDIOC_STREAMOFF");
}

/* 20 seconds */
#define LIBCAMERA_POLL_TIMEOUT (20 * 1000)
int v4l2_capture_grab_frame(v4l2_struct_t *v4l2_str)
{
    assert(v4l2_str);

    int fd = v4l2_str->dev_fd;
    int ret;
    struct v4l2_buffer buf;
    struct pollfd pfd[1];

    pfd[0].fd = fd;
    pfd[0].events = POLLIN;

    ret = poll(pfd, 1, LIBCAMERA_POLL_TIMEOUT);
    if (ret < 0 ) {
        LOGE("select error in DQ\n");
        return -1;
    }
    if (ret == 0) {
        LOGE("select timeout in DQ\n");
        return -1;
    }

    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    ret = xioctl(fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        LOGE("DQ error -- ret is %d\n", ret);
        switch (errno) {
        case EAGAIN:
            return -1;
        case EIO:
            /* Could ignore EIO, see spec. */

            /* fail through */
        default:
            return -1;
        }
    }

    assert(buf.index < v4l2_str->frame_num);

    LOG2("VIDIOC_DQBUF");

    v4l2_str->frame_size = buf.bytesused;
    v4l2_str->cur_frame = buf.index;

    return 0;
}

void v4l2_capture_recycle_frame(v4l2_struct_t *v4l2_str,
                                unsigned int frame_id)
{
    assert(v4l2_str);

    int ret;
    int fd = v4l2_str->dev_fd;
    struct v4l2_buffer buf;

    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = frame_id;

    /* enqueue the buffer */
    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
        errno_print(v4l2_str, "VIDIOC_QBUF");

    LOG2("VIDIOC_QBUF");
}

void v4l2_capture_map_frame(v4l2_struct_t *v4l2_str,
                            unsigned int frame_idx,
                            v4l2_frame_info *buf_info)
{
    int fd = v4l2_str->dev_fd;

    struct v4l2_buffer buf;
    void *mapped_addr;
    unsigned int mapped_length;

    int ret;

    if (frame_idx >= v4l2_str->frame_num) {
        LOGE("Invalid frame idx %d", frame_idx);
        return ;
    }

    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = frame_idx;

    if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
        errno_print(v4l2_str, "VIDIOC_QUERYBUF");

    LOG2("VIDIOC_QUERYBUF");

    mapped_addr = mmap(NULL,
                       buf.length,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       fd,
                       buf.m.offset);

    if (MAP_FAILED == mapped_addr)
        errno_print(v4l2_str, "mmap");

    LOG2("mmap");

    mapped_length = buf.length;

    buf_info->mapped_addr = mapped_addr;
    buf_info->mapped_length = mapped_length;
    buf_info->width = v4l2_str->fm_width;
    buf_info->height = v4l2_str->fm_height;
    buf_info->stride = v4l2_str->fm_width;
    buf_info->fourcc = v4l2_str->fm_fmt;
}

void v4l2_capture_unmap_frame(v4l2_struct_t *v4l2_str,
                              v4l2_frame_info *buf_info)
{
    int ret;

    void *mapped_addr = buf_info->mapped_addr;

    unsigned int mapped_length = buf_info->mapped_length;

    if (-1 == munmap(mapped_addr, mapped_length))
        errno_print(v4l2_str, "munmap");

    LOG2("munmap");

    buf_info->mapped_addr = NULL;
    buf_info->mapped_length = 0;
    buf_info->width = 0;
    buf_info->height = 0;
    buf_info->stride = 0;
    buf_info->fourcc = 0;
}

int v4l2_capture_set_capture_mode(int fd, int mode)
{
    int binary;
    struct v4l2_streamparm parm;
    switch (mode) {
    case CI_ISP_MODE_PREVIEW:
        binary = CI_MODE_PREVIEW;
        break;;
    case CI_ISP_MODE_CAPTURE:
        binary = CI_MODE_STILL_CAPTURE;
        break;
    case CI_ISP_MODE_VIDEO:
        binary = CI_MODE_VIDEO;
        break;
    default:
        binary = CI_MODE_STILL_CAPTURE;
        break;
    }

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.capturemode = binary;

    if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
        LOGE("ERR(%s): error:%s \n", __func__, strerror(errno));
        return -1;
    }

    return 0;
}


#if defined(ANDROID)
#define BASE BASE_VIDIOC_PRIVATE
#define VIDIOC_BC_CAMERA_BRIDGE _IOWR('V', BASE + 8, BC_Video_ioctl_package)
static BC_Video_ioctl_package ioctl_package;
static bc_buf_params_t buf_param;
int ci_isp_register_camera_bcd (v4l2_struct_t *v4l2_str,
                                unsigned int num_frames,
                                unsigned int *frame_ids,
                                v4l2_frame_info *frame_info)
{
    int ret = 0;
    unsigned int frame_id = 0;
    int fd = v4l2_str->dev_fd;

    buf_param.count = num_frames;
    buf_param.width = frame_info[0].width;
    buf_param.stride = frame_info[0].stride;
    buf_param.height = frame_info[0].height;
    buf_param.fourcc = frame_info[0].fourcc;
    buf_param.type = BC_MEMORY_MMAP;

    ioctl_package.ioctl_cmd = BC_Video_ioctl_request_buffers;
    ioctl_package.inputparam = (int)(&buf_param);
    if (-1 == xioctl(fd, VIDIOC_BC_CAMERA_BRIDGE, &ioctl_package)) {
        LOGE("Failed to request buffers from buffer class camera driver (errno=%d).", errno);
        return -1;
    }

    bc_buf_ptr_t buf_pa;

    unsigned int i;
    for (i = 0; i < num_frames; i++)
    {
        buf_pa.index = frame_ids[i];
        ioctl_package.ioctl_cmd = BC_Video_ioctl_set_buffer_phyaddr;
        ioctl_package.inputparam = (int) (&buf_pa);
        if (-1 == xioctl(fd, VIDIOC_BC_CAMERA_BRIDGE, &ioctl_package)) {
            LOGE("Failed to set buffer phyaddr from buffer class camera driver (errno=%d).", errno);
            return -1;
        }

    }
    return ret;
}
#endif /* ANDROID */
