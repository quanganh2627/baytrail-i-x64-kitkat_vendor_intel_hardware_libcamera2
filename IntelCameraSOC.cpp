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

#define LOG_TAG "IntelCameraSOC"
#include <utils/Log.h>
#include <camera/CameraHardwareInterface.h>

#include "IntelCameraSOC.h"
#include "v4l2SOC.h"


#ifndef TRUE
#define TRUE   1
#endif
#ifndef FALSE
#define FALSE  0
#endif

#define	CAMLOGD(fmt, arg...)    LOGD("%s(line %d): " fmt, \
            __FUNCTION__, __LINE__, ##arg)

namespace android {

IntelCameraSOC::IntelCameraSOC(int camera_id)
  : mFrameInfos(0),
    mCurrentFrameFormat(0),
    /*mSensorInfo(0),*/
    /*mAdvanceProcess(NULL),*/
    mCameraId(camera_id),
    ccRGBtoYUV(NULL)
{
	int ret;
	mCI = new v4l2_struct_t;

    //TODO: This logic shouldn't be in constructor,
    //should be moved to other seperate function.
    if (mCI != NULL) {
        mCI->frame_num = 0;
        mCI->cur_frame = 0;
    }
    else
    {
        LOGE("%s::failed to new v4l2_struct_t", __FUNCTION__); 
    }

    //color converter
    trimBuffer = new unsigned char [1024 * 768 * 3 / 2];
}

IntelCameraSOC::~IntelCameraSOC()
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

int IntelCameraSOC::captureOpen(int *fd)
{
    if (v4l2_capture_open_SOC(mCI) < 0)
        return -1;
    *fd = mCI->dev_fd;
    return 0;
}

void IntelCameraSOC::captureInit(unsigned int width,
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
    mCI->camera_id = mCameraId;
    LOGD("%s:: camera id is %d", __FUNCTION__, mCI->camera_id);
    /* Open, VIDIOC_S_INPUT, VIDIOC_S_PARM */
    v4l2_capture_init_SOC(mCI);

    /* VIDIOC_S_FMT, VIDIOC_REQBUFS */
    v4l2_capture_create_frames_SOC(mCI, w, h,
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

void IntelCameraSOC::captureFinalize(void)
{
    //color converter
    if (ccRGBtoYUV) {
        delete ccRGBtoYUV;
        ccRGBtoYUV = NULL;
    };

    mCI->fm_width = 0;
    mCI->fm_height = 0;

    v4l2_capture_destroy_frames_SOC(mCI);
    v4l2_capture_finalize_SOC(mCI);

    delete [] mCI->frame_ids;
}

void IntelCameraSOC::captureStart(void)
{
    v4l2_capture_start_SOC(mCI);
}

void IntelCameraSOC::captureStop(void)
{
    v4l2_capture_stop_SOC(mCI);
}

void IntelCameraSOC::captureMapFrame(void)
{
    unsigned int i, ret;
    unsigned int frame_num = mCI->frame_num;

    mFrameInfos = new v4l2_frame_info[frame_num];

    for(i = 0; i < frame_num; i++) {
        v4l2_capture_map_frame_SOC(mCI, i, &(mFrameInfos[i]));
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
        ret = ci_isp_register_camera_bcd_SOC(mCI, mCI->frame_num, mCI->frame_ids, mFrameInfos);
        CHECK_V4L2_RET(ret, "register camera bcd");
        LOGD("main end of bcd");
    }
#endif

}

void IntelCameraSOC::captureUnmapFrame(void)
{
    unsigned int i, frame_num = mCI->frame_num;

    for(i = 0; i < frame_num; i++) {
        v4l2_capture_unmap_frame_SOC(mCI, &(mFrameInfos[i]));
        LOGV("%s : mFrameInfos[%u].addr=%p",__func__, i, mFrameInfos[i].mapped_addr);
    }
#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
if (mCurrentFrameFormat != V4L2_PIX_FMT_JPEG) {
    ci_isp_unregister_camera_bcd_SOC(mCI);
}
#endif
    delete [] mFrameInfos;
}

void IntelCameraSOC::captureSetPtr(unsigned int frame_size, void **ptrs)
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
        ret = ci_isp_register_camera_bcd_SOC(mCI, mCI->frame_num, mCI->frame_ids, mFrameInfos);
        CHECK_V4L2_RET(ret, "register camera bcd");
        LOGD("main end of bcd");
}
#endif
}

void IntelCameraSOC::captureUnsetPtr(void)
{
    unsigned int i, frame_num = mCI->frame_num;
#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
    if (mCurrentFrameFormat != V4L2_PIX_FMT_JPEG) {
        ci_isp_unregister_camera_bcd_SOC(mCI);
    }
#endif
    delete [] mFrameInfos;
}

unsigned int IntelCameraSOC::captureGrabFrame(void)
{
    /* VIDIOC_DQBUF */
    if (v4l2_capture_grab_frame_SOC(mCI) < 0)
        return 0;

    LOGV("captureGrabFrame : frame = %d", frame);

    return mCI->frame_size;
}

unsigned int IntelCameraSOC::captureGetFrame(void *buffer)
{
	unsigned int frame = mCI->cur_frame;

	if(buffer != NULL) {
		switch(mCurrentFrameFormat) {
		case V4L2_PIX_FMT_RGB565 :
			//LOGE("INTEL_PIX_FMT_RGB565");
			//trimRGB565((unsigned char*)mFrameInfos[frame].mapped_addr, (unsigned char*)buffer,
			//mFrameInfos[frame].mapped_length/mCI->fm_height, mCI->fm_height,
			//mCI->fm_width, mCI->fm_height);
			yuv422_to_RGB565((unsigned char*)mFrameInfos[frame].mapped_addr,
					mCI->fm_width, mCI->fm_height,(unsigned char*)buffer);
			break;
		case V4L2_PIX_FMT_JPEG :
			//LOGE("INTEL_PIX_FMT_JPEG");
			//memcpy(buffer, mFrameInfos[0].mapped_addr, mCI->frame_size);
			yuv422_to_RGB565((unsigned char*)mFrameInfos[frame].mapped_addr,
					mCI->fm_width, mCI->fm_height,(unsigned char*)buffer);
			break;
		case V4L2_PIX_FMT_YUYV :
			//LOGE("INTEL_PIX_FMT_YUYV");
			yuyv422_to_yuv420sp((unsigned char *)mFrameInfos[frame].mapped_addr, (unsigned char *)buffer,
					mCI->fm_width, mCI->fm_height);
			break;
		case V4L2_PIX_FMT_NV12 :
			//LOGE("INTEL_PIX_FMT_NV12 - preview");
			//trimNV12((unsigned char*)mFrameInfos[frame].mapped_addr, (unsigned char*)buffer,
			//mCI->frame_size/mCI->fm_height*2/3, mCI->fm_height,
			//mCI->fm_width, mCI->fm_height);
			yuv422_to_yuv420sp_convert((unsigned char*)mFrameInfos[frame].mapped_addr,
					mCI->fm_width, mCI->fm_height,(unsigned char*)buffer);

			break;
		default :
			LOGE("Unknown Format type");
			break;
		}
	}
	return frame;
}

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
unsigned int IntelCameraSOC::captureGetFrameID(void)
{
    return mCI->cur_frame;
}
#endif

unsigned int IntelCameraSOC::captureGetRecordingFrame(void *buffer, int buffer_share)
{
	unsigned int frame = mCI->cur_frame;

	if(buffer != NULL) {
		if (buffer_share) {
			memcpy(buffer, &frame, sizeof(unsigned int));
		} else {
			switch(mCurrentFrameFormat) {
			case V4L2_PIX_FMT_RGB565 :
				//LOGV("INTEL_PIX_FMT_RGB565");
				//nv12_to_nv21((unsigned char *)mFrameInfos[frame].addr, (unsigned char *)buffer,
					//mCI->fm_width, mCI->fm_height);
				//trimRGB565((unsigned char*)mFrameInfos[frame].mapped_addr, (unsigned char*)trimBuffer,
				//mFrameInfos[frame].mapped_length/mCI->fm_height, mCI->fm_height,
				//mCI->fm_width, mCI->fm_height);
				//ccRGBtoYUV->Convert(trimBuffer, (uint8*)buffer);

				//For intel hw encoder, input format is 420sp
				yuv422_to_yuv420sp_convert((unsigned char*)mFrameInfos[frame].mapped_addr, 
						mCI->fm_width, mCI->fm_height, (unsigned char*)buffer);
				break;
			case V4L2_PIX_FMT_YUYV :
				//LOGV("INTEL_PIX_FMT_YUYV");
				yuyv422_to_yuv420sp((unsigned char *)mFrameInfos[frame].mapped_addr, 
						(unsigned char *)buffer, mCI->fm_width, mCI->fm_height);
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

void IntelCameraSOC::captureRecycleFrame(void)
{
    if (!mCI || mCI->cur_frame >= mCI->frame_num) {
        LOGE("captureRecycleFrame: ERROR. Frame not ready!! cur_frame %d, mCI->frame_num %d", mCI->cur_frame, mCI->frame_num);
        return;
    }
    v4l2_capture_recycle_frame_SOC(mCI, mCI->cur_frame);
}

void IntelCameraSOC::trimRGB565(unsigned char *src, unsigned char* dst,
                             int src_width, int src_height,
                             int dst_width, int dst_height)
{
    for (int i=0; i<dst_height; i++) {
        memcpy( (unsigned char *)dst+i*2*dst_width,
                (unsigned char *)src+i*src_width,
                2*dst_width);
    }

};

void IntelCameraSOC::trimNV12(unsigned char *src, unsigned char* dst,
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

void IntelCameraSOC::nv12_to_nv21(unsigned char *nv12, unsigned char *nv21, int width, int height)
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

void IntelCameraSOC::yuv_to_rgb16(unsigned char y,unsigned char u, unsigned char v, unsigned char *rgb)
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

void IntelCameraSOC::yuyv422_to_rgb16(unsigned char *buf, unsigned char *rgb, int width, int height)
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


void IntelCameraSOC::yuyv422_to_yuv420sp(unsigned char *bufsrc, unsigned char *bufdest, int width, int height)
{
    LOGV("yuyv422_to_yuv420sp empty");
}

unsigned int IntelCameraSOC::get_frame_num(void)
{
    return mCI->frame_num;
}

void IntelCameraSOC::get_frame_id(unsigned int *frame_id, unsigned int frame_num)
{
    unsigned int frame_index;
    for (frame_index=0; frame_index<frame_num; frame_index++)
        frame_id[frame_index] = mCI->frame_ids[frame_index];
}

int IntelCameraSOC::setCtrl(const int CID, const int value, const char *key)
{
    int ret; 
    struct v4l2_control v4l2_ctrl;
    memset(&v4l2_ctrl, 0 ,sizeof(v4l2_ctrl));
    v4l2_ctrl.id = CID;
    v4l2_ctrl.value = value;
    
    CAMLOGD("CID:0x%x, Value:%d", v4l2_ctrl.id, v4l2_ctrl.value);
    ret = xioctl_SOC(mCI->dev_fd, VIDIOC_S_CTRL, &v4l2_ctrl);
    return ret;
}

int IntelCameraSOC::setExtCtrls(const int CID, const int value, const char *key)
{
    int ret; 
    struct v4l2_ext_controls v4l2_ext_ctrls;
    struct v4l2_ext_control v4l2_ext_ctrl;
    memset(&v4l2_ext_ctrls, 0 ,sizeof(v4l2_ext_ctrls));
    memset(&v4l2_ext_ctrl, 0 ,sizeof(v4l2_ext_ctrl));

    v4l2_ext_ctrl.id = CID;
    v4l2_ext_ctrl.value = value;
    v4l2_ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
    v4l2_ext_ctrls.count = 1;
    v4l2_ext_ctrls.controls = &v4l2_ext_ctrl;
    
    CAMLOGD("CID:0x%x, Value:%d", v4l2_ext_ctrl.id, v4l2_ext_ctrl.value);
    ret = xioctl_SOC(mCI->dev_fd, VIDIOC_S_EXT_CTRLS, &v4l2_ext_ctrls);
    return ret;
}
/* Convert yuv422 buffer to yuv420sp buffer */
void IntelCameraSOC::yuv422_to_yuv420sp_convert(unsigned char *yuv422, int width, int height, 
                                                      unsigned char *yuv420sp)
{
    int y_len = width * height;
    int u422_len = y_len/2;
    int u420sp_len = u422_len/2;
    unsigned char *u422_ptr = yuv422 + y_len;
    unsigned char *v422_ptr = yuv422 + y_len + u422_len;
    unsigned char *uv420sp_ptr = yuv420sp + y_len;
    int i = 0, j = 0;

    /* Copy Y part as it is */
    memcpy(yuv420sp, yuv422, y_len);

    /* Copy u and v parts */
    while (j < u420sp_len) {
        uv420sp_ptr[i++] = *u422_ptr++;
        uv420sp_ptr[i++] = *v422_ptr++;
        if (i%width == 0) {
            /* end of the row. skip next row */
            u422_ptr = u422_ptr + width/2;
            v422_ptr = v422_ptr + width/2;
        } 
        j++;
    }
}

/* Convert yuv422 buffer to yuv420p buffer */
void IntelCameraSOC::yuv422_to_yuv420p_convert(unsigned char *yuv422, int width, int height, 
                                                      unsigned char *yuv420p)
{
    int y_len = width * height;
    int u422_len = y_len/2;
    int u420p_len = u422_len/2;
    unsigned char *u422_ptr = yuv422 + y_len;
    unsigned char *v422_ptr = yuv422 + y_len + u422_len;
    unsigned char *u420p_ptr = yuv420p + y_len;
    unsigned char *v420p_ptr = yuv420p + y_len + u420p_len;
    int i = 0, j = 0;

    /* Copy Y part as it is */
    memcpy(yuv420p, yuv422, y_len);

    /* Copy u and v parts */
    while (j < u420p_len) {
        u420p_ptr[i++] = *u422_ptr++;
        v420p_ptr[i++] = *v422_ptr++;
        if (i%width == 0) {
            /* end of the row. skip next row */
            u422_ptr = u422_ptr + width/2;
            v422_ptr = v422_ptr + width/2;
        } 
        j++;
    }
}

//we tackle the conversion two pixels at a time for greater speed
void IntelCameraSOC::yuv422_to_RGB565 (unsigned char *yuvs, int width, int height, unsigned char *rgbs)
{
    //the end of the luminance data
    int lumEnd = width * height;
    //points to the next luminance value pair
    int lumPtr = 0;
    //points to the next chromiance value pair
    int chrPtr = lumEnd;
    int chrPtr1 = lumEnd + lumEnd/2;
    //points to the next byte output pair of RGB565 value
    int outPtr = 0;
    //the end of the current luminance scanline
    int lineEnd = width;
    while (true) {

        //skip back to the start of the chromiance values when necessary
        if (lumPtr == lineEnd) {
            if (lumPtr == lumEnd) break; //we've reached the end
            //division here is a bit expensive, but's only done once per scanline
            /* Cb or V */
            //chrPtr = lumEnd + (((lumPtr  >> 1) / width) * width)/2;
            chrPtr = lumEnd + (((lumPtr  >> 1) / width) * width);
            /* Cr or U */
            chrPtr1 = lumEnd + (lumEnd/2) + (((lumPtr  >> 1) / width) * width);
            lineEnd += width;
        }
	        //read the luminance and chromiance values
        int Y1 = (yuvs[lumPtr++] & 0xff);
        int Y2 = (yuvs[lumPtr++] & 0xff);
        int Cb = (yuvs[chrPtr++] & 0xff) - 128;
        int Cr = (yuvs[chrPtr1++] & 0xff) - 128;
        int R, G, B;

        //generate first RGB components
        B = Y1 + ((454 * Cb) >> 8);
        if(B < 0) B = 0; else if(B > 255) B = 255;
        G = Y1 - ((88 * Cb + 183 * Cr) >> 8);
        if(G < 0) G = 0; else if(G > 255) G = 255;
        R = Y1 + ((359 * Cr) >> 8);
        if(R < 0) R = 0; else if(R > 255) R = 255;
        //NOTE: this assume little-endian encoding
        rgbs[outPtr++]  = (char) (((G & 0x3c) << 3) | (B >> 3));
        rgbs[outPtr++]  = (char) ((R & 0xf8) | (G >> 5));

        //generate second RGB components
        B = Y2 + ((454 * Cb) >> 8);
        if(B < 0) B = 0; else if(B > 255) B = 255;
        G = Y2 - ((88 * Cb + 183 * Cr) >> 8);
        if(G < 0) G = 0; else if(G > 255) G = 255;
        R = Y2 + ((359 * Cr) >> 8);
        if(R < 0) R = 0; else if(R > 255) R = 255;
        //NOTE: this assume little-endian encoding
        rgbs[outPtr++]  = (char) (((G & 0x3c) << 3) | (B >> 3));
        rgbs[outPtr++]  = (char) ((R & 0xf8) | (G >> 5));
    }
}

int IntelCameraSOC::getSensorID(char *sensorID)
{
    /* enumerate input */
	struct v4l2_input input;
	int index;

#if 0
    //TODO: In future, we may get the real sensor product ID 
    //instead of name
	memset(&input, 0, sizeof (input));
	input.index = mCI->camera_id;

	if (0 == xioctl_SOC(mCI->dev_fd, VIDIOC_ENUMINPUT, &input)) {
        strcpy(sensorID, (const char*)input.name);
        return 0;
    }
    else
    {
        LOGE("%s:: enum %d camera failed", __FUNCTION__, input.index);
        return -1;
    }
#endif
    //TODO: This is a work-around right now.
    //Need find one reasonable solution to get sensor ID or name.
    if (mCameraId == 0)
    {
        strcpy(sensorID, "a5140soc");
        return 0;
    }
    else if(mCameraId == 1)
    {
        strcpy(sensorID, "a1040soc");
        return 0;
    }
    else
    {
        LOGE("%s:: enum %d camera failed", __FUNCTION__, mCameraId);
        return -1;
    }
}


}; // namespace android
