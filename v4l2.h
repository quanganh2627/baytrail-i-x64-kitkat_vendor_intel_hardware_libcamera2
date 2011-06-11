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

#ifndef _V4L2_H_
#define _V4L2_H_

#include <utils/Log.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <ci_adv_pub.h>
#include <atomisp_config.h>

struct v4l2_buffer_info {
    void *data;
    size_t length;
    int width;
    int height;
    int fourcc;
    int flags; //You can use to to detern the buf status
    struct v4l2_buffer vbuffer;
};

struct v4l2_buffer_pool {
    int active_buffers;
    struct v4l2_buffer_info bufs [MAX_V4L2_BUFFERS];
};

/*Bayer order transfer on the MIPI lane*/
#define BAYER_ORDER_GRBG        0
#define BAYER_ORDER_RGGB        1
#define BAYER_ORDER_BGGR        2
#define BAYER_ORDER_GBRG        3
struct file_input {
    char *name;
    unsigned int width;
    unsigned int height;
    unsigned int size;
    int format;
    int bayer_order;
    char *mapped_addr;
};

int v4l2_capture_open(int device);

void v4l2_capture_close(int fd);

int v4l2_capture_querycap(int fd, int device, struct v4l2_capability *cap);

int v4l2_capture_s_input(int fd, int index);

int v4l2_capture_s_format(int fd, int device, int w, int h,
                          int fourcc);
int v4l2_capture_g_framerate(int fd, int * framerate);

int v4l2_capture_request_buffers(int fd, int device, uint num_buffers);


int v4l2_capture_new_buffer(int fd, int device, int frame_idx,
                            struct v4l2_buffer_info *buf_info);
int v4l2_capture_free_buffer(int fd, int device,
                             struct v4l2_buffer_info *buf_info);

int v4l2_capture_streamon(int fd);
int v4l2_capture_streamoff(int fd);

int v4l2_capture_g_parm(int fd, struct v4l2_streamparm *parm);
int v4l2_capture_s_parm(int fd, int device, struct v4l2_streamparm *parm);

int v4l2_capture_release_buffers(int fd, int device);

int v4l2_capture_qbuf(int fd, int index, struct v4l2_buffer_info *buf);
int v4l2_capture_dqbuf(int fd, struct v4l2_buffer *buf);
int v4l2_capture_control_dq(int fd, int start);

int v4l2_read_file(char *file_name, int width, int height, int format, int bayer_order);

void v4l2_set_isp_timeout(int timeout);

/* for camera texture streaming */
typedef struct bc_buf_ptr {
    unsigned int index;
    int size;
    unsigned long pa;
    unsigned long handle;
} bc_buf_ptr_t;

#define BC_Video_ioctl_fill_buffer           0
#define BC_Video_ioctl_get_buffer_count      1
#define BC_Video_ioctl_get_buffer_phyaddr    2
#define BC_Video_ioctl_get_buffer_index      3
#define BC_Video_ioctl_request_buffers       4
#define BC_Video_ioctl_set_buffer_phyaddr    5
#define BC_Video_ioctl_release_buffer_device 6

enum BC_memory {
    BC_MEMORY_MMAP      = 1,
    BC_MEMORY_USERPTR   = 2,
};

/*
 * the following types are tested for fourcc in struct bc_buf_params_t
 *   NV12
 *   UYVY
 *   RGB565 - not tested yet
 *   YUYV
 */
typedef struct bc_buf_params {
    int count;	/* number of buffers, [in/out] */
    int width;
    int height;
    int stride;
    unsigned int fourcc;	/* buffer pixel format */
    enum BC_memory type;
} bc_buf_params_t;

int v4l2_register_bcd(int fd, int num_frames, void **ptrs, int w, int h, int fourcc, int size);
int v4l2_release_bcd(int fd);
#endif /* _V4L2_H_ */
