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

#define CHECK_RET(ret, cond, msg)					\
  if((ret) != (cond)) {							\
    LOGE("%s: %s failed error code = %d", __FUNCTION__, (msg), (ret));	\
  }									\
  else {								\
      LOGV("%s: %s success", __FUNCTION__, (msg));			\
  }
#define CHECK_V4L2_RET(ret, msg)			\
  CHECK_RET(ret, 0, msg)

#define V4L2_VM_FRAME_NUM	3
#define V4L2_IM_FRAME_NUM	1

#define V4L2_VM_FRAME_FORMAT	V4L2_PIX_FMT_NV12
#define V4L2_IM_FRAME_FORMAT	V4L2_PIX_FMT_JPEG

typedef unsigned long v4l2_frame_format;

typedef struct _v4l2_frame_info {
    void *mapped_addr;
    unsigned int mapped_length;
    unsigned short width;
    unsigned short height;
    unsigned int stride;
    unsigned long fourcc;
} v4l2_frame_info;

typedef struct _v4l2_struct {
    int dev_fd;
    char *dev_name;

    unsigned short fm_width;
    unsigned short fm_height;
    unsigned int fm_fmt;
    enum v4l2_memory mem_type;
    v4l2_frame_info *fm_infos;

    struct v4l2_input input;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_streamparm parm;
    struct v4l2_requestbuffers req_buf;
    unsigned int frame_num;
    unsigned int frame_size;
    unsigned int cur_frame;
    void * cur_userptr;
    unsigned int *frame_ids;
    struct v4l2_buffer *bufs;

    unsigned int *buf_status;
    int camera_id;
} v4l2_struct_t;

int v4l2_capture_open(v4l2_struct_t *v4l2_str);

void v4l2_capture_init(v4l2_struct_t *v4l2_str);

void v4l2_capture_create_frames(v4l2_struct_t *v4l2_str,
                                unsigned int frame_width,
                                unsigned int frame_height,
                                unsigned int frame_fmt,
                                unsigned int frame_num,
                                enum v4l2_memory mem_type,
                                unsigned int *frame_ids);

void v4l2_capture_start(v4l2_struct_t *v4l2_str);

int v4l2_capture_grab_frame(v4l2_struct_t *v4l2_str);

void v4l2_capture_map_frame(v4l2_struct_t *v4l2_str,
                            unsigned int frame_idx,
                            v4l2_frame_info *buf_info);

void v4l2_capture_unmap_frame(v4l2_struct_t *v4l2_str,
                              v4l2_frame_info *buf_info);

void v4l2_capture_recycle_frame(v4l2_struct_t *v4l2_str,
                                unsigned int frame_id);

void v4l2_capture_stop(v4l2_struct_t *v4l2_str);

void v4l2_capture_destroy_frames(v4l2_struct_t *v4l2_str);

void v4l2_capture_finalize(v4l2_struct_t *v4l2_str);

int v4l2_capture_set_capture_mode(int fd, int mode);

/* for camera texture streaming */
#if defined(ANDROID)
typedef struct bc_buf_ptr {
    unsigned int index;
    int size;
    unsigned long pa;
    unsigned long handle;
} bc_buf_ptr_t;

#define BC_Video_ioctl_fill_buffer	0
#define BC_Video_ioctl_get_buffer_count	1
#define BC_Video_ioctl_get_buffer_phyaddr	2
#define BC_Video_ioctl_get_buffer_index	3
#define BC_Video_ioctl_request_buffers	4
#define BC_Video_ioctl_set_buffer_phyaddr	5
#define BC_Video_ioctl_release_buffer_device	6

enum BC_memory {
    BC_MEMORY_MMAP		= 1,
    BC_MEMORY_USERPTR	= 2,
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

int ci_isp_register_camera_bcd(v4l2_struct_t *v4l2_str,
                               unsigned int num_frames,
                               unsigned int *frame_ids,
                               v4l2_frame_info *frame_info);

int ci_isp_unregister_camera_bcd(v4l2_struct_t *v4l2_str);
#endif /* ANDROID */

#endif /* _V4L2_H_ */
