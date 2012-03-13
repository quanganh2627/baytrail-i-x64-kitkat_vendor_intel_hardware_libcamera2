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
#define LOG_TAG "Atom_ColorConverter"

#include <camera/CameraParameters.h>
#include <linux/atomisp.h>
#include <linux/videodev2.h>
#include "ColorConverter.h"
#include "LogHelper.h"

namespace android {

void YUV420ToRGB565(int width, int height, void *src, void *dst)
{
    int line, col, linewidth;
    int y, u, v, yy, vr, ug, vg, ub;
    int r, g, b;
    const unsigned char *py, *pu, *pv;
    unsigned short *rgbs = (unsigned short *) dst;

    linewidth = width >> 1;
    py = (unsigned char *) src;
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
            *rgbs++ = (((unsigned short)r>>3)<<11) | (((unsigned short)g>>2)<<5)
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

void NV12ToRGB565(int width, int height, void *src, void *dst)
{

    unsigned char *yuvs = (unsigned char *) src;
    unsigned char *rgbs = (unsigned char *) dst;

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

void NV12ToNV21(int width, int height, void *src, void *dst)
{
    int planeSizeY = width * height;
    int planeSizeUV = planeSizeY / 2;
    int i = 0;
    unsigned char *srcPtr = (unsigned char *) src;
    unsigned char *dstPtr = (unsigned char *) dst;

    // copy the entire Y plane
    memcpy(dstPtr, src, planeSizeY);

    // byte swap the UV data
    for(i=planeSizeY; i<(planeSizeY+planeSizeUV); i=i+2)
    {
        dstPtr[i] = srcPtr[i + 1];
        dstPtr[i + 1] = srcPtr[i];
    }
}

const char *cameraParametersFormat(int v4l2Format)
{
    switch (v4l2Format) {
    case V4L2_PIX_FMT_YUV420:
        return CameraParameters::PIXEL_FORMAT_YUV420P;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
        return CameraParameters::PIXEL_FORMAT_YUV420SP;
    case V4L2_PIX_FMT_YUYV:
        return CameraParameters::PIXEL_FORMAT_YUV422I;
    case V4L2_PIX_FMT_JPEG:
        return CameraParameters::PIXEL_FORMAT_JPEG;
    default:
        return NULL;
    };
}

int V4L2Format(const char *cameraParamsFormat)
{
    LOG1("@%s", __FUNCTION__);
    if (!cameraParamsFormat) {
        LOGE("null cameraParamsFormat");
        return -1;
    }

    int len = strlen(CameraParameters::PIXEL_FORMAT_YUV420SP);
    if (strncmp(cameraParamsFormat, CameraParameters::PIXEL_FORMAT_YUV420SP, len) == 0)
        return V4L2_PIX_FMT_NV12;

    len = strlen(CameraParameters::PIXEL_FORMAT_YUV420P);
    if (strncmp(cameraParamsFormat, CameraParameters::PIXEL_FORMAT_YUV420P, len) == 0)
        return V4L2_PIX_FMT_YUV420;

    len = strlen(CameraParameters::PIXEL_FORMAT_JPEG);
    if (strncmp(cameraParamsFormat, CameraParameters::PIXEL_FORMAT_JPEG, len) == 0)
        return V4L2_PIX_FMT_JPEG;

    LOGE("invalid format %s", cameraParamsFormat);
    return -1;
}

} // namespace android
