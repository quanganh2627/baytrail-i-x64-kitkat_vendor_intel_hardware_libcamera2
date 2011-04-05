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


#ifndef TRUE
#define TRUE   1
#endif
#ifndef FALSE
#define FALSE  0
#endif

namespace android {

IntelCamera::IntelCamera()
    : mFrameInfos(0),
      mCurrentFrameFormat(0),
  /*mSensorInfo(0),*/
  /*mAdvanceProcess(NULL),*/
      ccRGBtoYUV(NULL),
      zoom_val(0)
{
    LOGV("%s() called!\n", __func__);

    mCI = new v4l2_struct_t;


    if (mCI != NULL) {
        mCI->frame_num = 0;
        mCI->cur_frame = 0;
    }

    //color converter
    trimBuffer = new unsigned char [1024 * 768 * 3 / 2];
}

IntelCamera::~IntelCamera()
{
    LOGV("%s() called!\n", __func__);

    // color converter
    if (ccRGBtoYUV) {
        delete ccRGBtoYUV;
        ccRGBtoYUV = NULL;
    };

    if (trimBuffer) {
        delete [] trimBuffer;
    };

    delete mCI;
}

int IntelCamera::captureOpen(int *fd)
{
    if (v4l2_capture_open(mCI) < 0)
        return -1;
    *fd = mCI->dev_fd;
    return 0;
}

void IntelCamera::captureInit(unsigned int width,
                              unsigned int height,
                              v4l2_frame_format frame_fmt,
                              unsigned int frame_num,
                              enum v4l2_memory mem_type,
                              int camera_id)
{
    unsigned int w, h;


    w = width;
    h = height;

    mCI->frame_ids = new unsigned int[frame_num];
    mCI->camera_id = camera_id;

    /* Open, VIDIOC_S_INPUT, VIDIOC_S_PARM */
    v4l2_capture_init(mCI);

    /* VIDIOC_S_FMT, VIDIOC_REQBUFS */
    v4l2_capture_create_frames(mCI, w, h,
                               frame_fmt,
                               frame_num,
                               mem_type,
                               mCI->frame_ids);

    mCI->fm_width = w;
    mCI->fm_height = h;
    mCurrentFrameFormat = frame_fmt;

    //color converter
    ccRGBtoYUV = CCRGB16toYUV420sp::New();
    ccRGBtoYUV->Init(mCI->fm_width, mCI->fm_height,
                     mCI->fm_width,
                     mCI->fm_width, mCI->fm_height,
                     ((mCI->fm_width + 15)>>4)<<4,
                     0);

}

void IntelCamera::captureFinalize(void)
{
    //color converter
    if (ccRGBtoYUV) {
        delete ccRGBtoYUV;
        ccRGBtoYUV = NULL;
    };

    mCI->fm_width = 0;
    mCI->fm_height = 0;

    v4l2_capture_destroy_frames(mCI);
    v4l2_capture_finalize(mCI);

    delete [] mCI->frame_ids;
}

void IntelCamera::captureStart(void)
{
    v4l2_capture_start(mCI);
}

void IntelCamera::captureStop(void)
{
    v4l2_capture_stop(mCI);
}

void IntelCamera::captureMapFrame(void)
{
    unsigned int i, ret;
    unsigned int frame_num = mCI->frame_num;

    mFrameInfos = new v4l2_frame_info[frame_num];

    for(i = 0; i < frame_num; i++) {
        v4l2_capture_map_frame(mCI, i, &(mFrameInfos[i]));
        LOGV("mFrameInfos[%u] -- \
		     mapped_addr = %p\n \
		     mapped_length = %d\n \
		     width = %d\n \
		     height = %d\n",
             i,
             mFrameInfos[i].mapped_addr,
             mFrameInfos[i].mapped_length,
             mFrameInfos[i].width,
             mFrameInfos[i].height);
    }

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
    if (mCurrentFrameFormat != V4L2_PIX_FMT_JPEG) {
        /* camera bcd stuff */
        ret = ci_isp_register_camera_bcd(mCI, mCI->frame_num, mCI->frame_ids, mFrameInfos);
        CHECK_V4L2_RET(ret, "register camera bcd");
        LOGD("main end of bcd");
    }
#endif

}

void IntelCamera::captureUnmapFrame(void)
{
    unsigned int i, frame_num = mCI->frame_num;

    for(i = 0; i < frame_num; i++) {
        v4l2_capture_unmap_frame(mCI, &(mFrameInfos[i]));
        LOGV("%s : mFrameInfos[%u].addr=%p",__func__, i, mFrameInfos[i].mapped_addr);
    }
    delete [] mFrameInfos;
}

void IntelCamera::captureSetPtr(unsigned int frame_size, void **ptrs)
{
    unsigned int i, ret;
    unsigned int frame_num = mCI->frame_num;
    unsigned int page_size = getpagesize();
    mFrameInfos = new v4l2_frame_info[frame_num];
    mCI->fm_infos = mFrameInfos;

    mCI->frame_size = frame_size;

    if (ptrs == NULL) {
	LOGE("pointer array is null!");
    } else {
        for(i = 0; i < frame_num; i++) {
            mFrameInfos[i].mapped_length = frame_size;
            mFrameInfos[i].mapped_addr = ptrs[i];
            mFrameInfos[i].width = mCI->fm_width;
            mFrameInfos[i].height = mCI->fm_height;
            mFrameInfos[i].stride = mCI->fm_width;
            mFrameInfos[i].fourcc = mCI->fm_fmt;
        }
    }

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
    if (mCurrentFrameFormat != V4L2_PIX_FMT_JPEG) {
        /* camera bcd stuff */
        ret = ci_isp_register_camera_bcd(mCI, mCI->frame_num, mCI->frame_ids, mFrameInfos);
        CHECK_V4L2_RET(ret, "register camera bcd");
        LOGD("main end of bcd");
}
#endif
}

void IntelCamera::captureUnsetPtr(void)
{
    unsigned int i, frame_num = mCI->frame_num;
#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
    ci_isp_unregister_camera_bcd(mCI);
#endif
    delete [] mFrameInfos;
}

unsigned int IntelCamera::captureGrabFrame(void)
{
    /* VIDIOC_DQBUF */
    if (v4l2_capture_grab_frame(mCI) < 0)
        return 0;

    LOGV("captureGrabFrame : frame = %d", frame);

    return mCI->frame_size;
}

unsigned int IntelCamera::captureGetFrame(void *buffer)
{
    unsigned int frame = mCI->cur_frame;

    if(buffer != NULL) {
        switch(mCurrentFrameFormat) {
        case V4L2_PIX_FMT_RGB565 :
            //LOGE("INTEL_PIX_FMT_RGB565");
            trimRGB565((unsigned char*)mFrameInfos[frame].mapped_addr, (unsigned char*)buffer,
                       mCI->fm_width * 2, mCI->fm_height,
                       mCI->fm_width, mCI->fm_height);
            break;
        case V4L2_PIX_FMT_JPEG :
            //LOGE("INTEL_PIX_FMT_JPEG");
            memcpy(buffer, mFrameInfos[0].mapped_addr, mCI->frame_size);
            break;
        case V4L2_PIX_FMT_YUYV :
            //LOGE("INTEL_PIX_FMT_YUYV");
            yuyv422_to_yuv420sp((unsigned char *)mFrameInfos[frame].mapped_addr, (unsigned char *)buffer,
                                mCI->fm_width, mCI->fm_height);
            break;
        case V4L2_PIX_FMT_NV12 :
            //LOGE("INTEL_PIX_FMT_NV12 - preview");
            trimNV12((unsigned char*)mFrameInfos[frame].mapped_addr, (unsigned char*)buffer,
                     mCI->frame_size/mCI->fm_height*2/3, mCI->fm_height,
                     mCI->fm_width, mCI->fm_height);

            break;
        default :
            LOGE("Unknown Format type");
            break;
        }
    }
    return frame;
}

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
unsigned int IntelCamera::captureGetFrameID(void)
{
    return mCI->cur_frame;
}
#endif

unsigned int IntelCamera::captureGetRecordingFrame(void *buffer, int buffer_share)
{
    unsigned int frame = mCI->cur_frame;

    if(buffer != NULL) {
        if (buffer_share) {
            memcpy(buffer, &frame, sizeof(unsigned int));
        } else {
            switch(mCurrentFrameFormat) {
            case V4L2_PIX_FMT_RGB565 :
                //LOGV("INTEL_PIX_FMT_RGB565");
                // nv12_to_nv21((unsigned char *)mFrameInfos[frame].addr, (unsigned char *)buffer,
                //mCI->fm_width, mCI->fm_height);
                trimRGB565((unsigned char*)mFrameInfos[frame].mapped_addr, (unsigned char*)trimBuffer,
                           mFrameInfos[frame].mapped_length/mCI->fm_height, mCI->fm_height,
                           mCI->fm_width, mCI->fm_height);
                ccRGBtoYUV->Convert(trimBuffer, (uint8*)buffer);
                break;
            case V4L2_PIX_FMT_YUYV :
                //LOGV("INTEL_PIX_FMT_YUYV");
                yuyv422_to_yuv420sp((unsigned char *)mFrameInfos[frame].mapped_addr, (unsigned char *)buffer,
                                    mCI->fm_width, mCI->fm_height);
                break;
            case V4L2_PIX_FMT_NV12 :
                //LOGV("INTEL_PIX_FMT_NV12");
                //memcpy(buffer, (void*)mFrameInfos[frame].addr, mCI->fm_width * mCI->fm_height * 3 / 2);
                trimNV12((unsigned char*)mFrameInfos[frame].mapped_addr, (unsigned char*)buffer,
                         mFrameInfos[frame].mapped_length/mCI->fm_height*2/3, mCI->fm_height,
                         mCI->fm_width, mCI->fm_height);
                break;
            default :
                LOGE("Unknown Format typedddd");
                break;
            }
        }
    }
    return frame;
}

void IntelCamera::captureRecycleFrame(void)
{
    if (!mCI || mCI->cur_frame >= mCI->frame_num) {
        LOGE("captureRecycleFrame: ERROR. Frame not ready!! cur_frame %d, mCI->frame_num %d", mCI->cur_frame, mCI->frame_num);
        return;
    }
    v4l2_capture_recycle_frame(mCI, mCI->cur_frame);
}

void IntelCamera::trimRGB565(unsigned char *src, unsigned char* dst,
                             int src_width, int src_height,
                             int dst_width, int dst_height)
{
    for (int i=0; i<dst_height; i++) {
        memcpy( (unsigned char *)dst+i*2*dst_width,
                (unsigned char *)src+i*src_width,
                2*dst_width);
    }

};

void IntelCamera::trimNV12(unsigned char *src, unsigned char* dst,
                           int src_width, int src_height,
                           int dst_width, int dst_height)
{
    unsigned char *dst_y, *dst_uv;
    unsigned char *src_y, *src_uv;

    dst_y = dst;
    src_y = src;

    LOGV("%s:%s:%d", __FILE__, __func__, __LINE__);
    LOGV("%d:%d:%d:%d", src_width, src_height, dst_width, dst_height);

    for (int i=0; i<dst_height; i++) {
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
};

void IntelCamera::nv12_to_nv21(unsigned char *nv12, unsigned char *nv21, int width, int height)
{
    int h,w;

#ifdef BOARD_USE_SOFTWARE_ENCODE
    memcpy(nv21, nv12, 1.5*width*height);
    return;
#endif

    for(h=0; h<height; h++) {
        memcpy(nv21 + h * width , nv12 + h * width, width);
    }

    nv21 += width * height;
    nv12 += width * height;

    /* Change uv to vu*/
    int height_div_2 = height/2;
    for(h=0; h<height_div_2; h++) {
        for(w=0; w<width; w+=2) {
            *(nv21 + w + 1) = *(nv12 + w);
            *(nv21 + w) = *(nv12 + w + 1);
        }
        nv21 += width;
        nv12 += width;
    }
}

void IntelCamera::yuv_to_rgb16(unsigned char y,unsigned char u, unsigned char v, unsigned char *rgb)
{
    register int r,g,b;
    int rgb16;

    r = (1192 * (y - 16) + 1634 * (v - 128) ) >> 10;
    g = (1192 * (y - 16) - 833 * (v - 128) - 400 * (u -128) ) >> 10;
    b = (1192 * (y - 16) + 2066 * (u - 128) ) >> 10;

    r = r > 255 ? 255 : r < 0 ? 0 : r;
    g = g > 255 ? 255 : g < 0 ? 0 : g;
    b = b > 255 ? 255 : b < 0 ? 0 : b;

    rgb16 = (int)(((r >> 3)<<11) | ((g >> 2) << 5)| ((b >> 3) << 0));

    *rgb = (unsigned char)(rgb16 & 0xFF);
    rgb++;
    *rgb = (unsigned char)((rgb16 & 0xFF00) >> 8);
}

void IntelCamera::yuyv422_to_rgb16(unsigned char *buf, unsigned char *rgb, int width, int height)
{
    int x,y,z=0;
    int blocks;

    blocks = (width * height) * 2;
    for (y = 0; y < blocks; y+=4) {
        unsigned char Y1, Y2, U, V;

        Y1 = buf[y + 0];
        U = buf[y + 1];
        Y2 = buf[y + 2];
        V = buf[y + 3];

        yuv_to_rgb16(Y1, U, V, &rgb[y]);
        yuv_to_rgb16(Y2, U, V, &rgb[y + 2]);
    }
}


void IntelCamera::yuyv422_to_yuv420sp(unsigned char *bufsrc, unsigned char *bufdest, int width, int height)
{
    LOGV("yuyv422_to_yuv420sp empty");
}

unsigned int IntelCamera::get_frame_num(void)
{
    return mCI->frame_num;
}

void IntelCamera::get_frame_id(unsigned int *frame_id, unsigned int frame_num)
{
    unsigned int frame_index;
    for (frame_index=0; frame_index<frame_num; frame_index++)
        frame_id[frame_index] = mCI->frame_ids[frame_index];
}

int IntelCamera::get_device_fd(void)
{
    if(mCI)
        return mCI->dev_fd;

    return -1;
}

void IntelCamera::captureFlashOff(void)
{
    cam_driver_led_flash_off (mCI->dev_fd);
}

void IntelCamera::captureFlashOnCertainDuration(int mode, int smode, int duration, int intensity)
{
    cam_driver_led_flash_trigger (mCI->dev_fd, mode, smode, duration, intensity);
}

#define MAX_ZOOM_LEVEL	64
#define MIN_ZOOM_LEVEL	1

int IntelCamera::set_zoom_val(int zoom)
{
    /* Zoom is 100,150,200,250,300,350,400 */
    /* AtomISP zoom range is 1 - 64 */
    int atomisp_zoom;
    zoom_val = zoom;
    int fd = get_device_fd();
    if (fd < 0) {
        LOGE("%s: device not opened\n", __func__);
        return -1;
    }

    if (zoom == 0)
        return 0;
    if (zoom < MIN_ZOOM_LEVEL)
        zoom = MIN_ZOOM_LEVEL;
    if (zoom > MAX_ZOOM_LEVEL)
        zoom = MAX_ZOOM_LEVEL;

    atomisp_zoom = ((zoom - MIN_ZOOM_LEVEL) * 63 /
                    (MAX_ZOOM_LEVEL - MIN_ZOOM_LEVEL)) + 1;
    return cam_driver_set_zoom (fd, atomisp_zoom);
}

int IntelCamera::get_zoom_val(void)
{
    return zoom_val;
}


int IntelCamera::set_capture_mode(int mode)
{
    return v4l2_capture_set_capture_mode(mCI->dev_fd, mode);
}



}; // namespace android
