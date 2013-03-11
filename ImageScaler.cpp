/*
 * Copyright (c) 2012 Intel Corporation.
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
#define LOG_TAG "Camera_ImageScaler"
#include "AtomCommon.h"
#include "LogHelper.h"
#include "ImageScaler.h"

#define RESOLUTION_1080P_WIDTH  1920
#define RESOLUTION_1080P_HEIGHT 1080
#define RESOLUTION_VGA_WIDTH    640
#define RESOLUTION_VGA_HEIGHT   480
#define RESOLUTION_QVGA_WIDTH   320
#define RESOLUTION_QVGA_HEIGHT  240
#define RESOLUTION_QCIF_WIDTH   176
#define RESOLUTION_QCIF_HEIGHT  144
#define MIN(a,b) ((a)<(b)?(a):(b))

namespace android {

void ImageScaler::downScaleImage(AtomBuffer *src, AtomBuffer *dst,
        int src_skip_lines_top, int src_skip_lines_bottom)
{
    void *srcPtr, *dstPtr;

    if (src->type == ATOM_BUFFER_PREVIEW_GFX)
        srcPtr = src->gfxData;
    else {
        if (src->shared)
            srcPtr = (void *) *((char **)src->buff->data);
        else
            srcPtr = src->buff->data;
    }

    if (dst->type == ATOM_BUFFER_PREVIEW_GFX)
        dstPtr = dst->gfxData;
    else {
        if (dst->shared)
            dstPtr = (void *) *((char **)dst->buff->data);
        else
            dstPtr = dst->buff->data;
    }

    downScaleImage(srcPtr, dstPtr,
        dst->width, dst->height, dst->stride,
        src->width, src->height, src->stride,
        src->format, src_skip_lines_top, src_skip_lines_bottom);
}

void ImageScaler::downScaleImage(void *src, void *dest,
    int dest_w, int dest_h, int dest_stride,
    int src_w, int src_h, int src_stride,
    int format, int src_skip_lines_top, // number of lines that are skipped from src image start pointer
    int src_skip_lines_bottom) // number of lines that are skipped after reading src_h (should be set always to reach full image height)
{
    unsigned char *m_dest = (unsigned char *)dest;
    const unsigned char * m_src = (const unsigned char *)src;
    switch (format) {
        case V4L2_PIX_FMT_NV12: {
            if (dest_w == src_w && dest_h == src_h) {
                // trim only
                ImageScaler::trimNv12Image(m_dest, m_src,
                    dest_w, dest_h, dest_stride,
                    src_w, src_h, src_stride,
                    src_skip_lines_top, src_skip_lines_bottom);
            } else {
                // downscale & crop
                ImageScaler::downScaleAndCropNv12Image(m_dest, m_src,
                    dest_w, dest_h, dest_stride,
                    src_w, src_h, src_stride,
                    src_skip_lines_top, src_skip_lines_bottom);
            }
            break;
        }
        default: {
            LOGE("no downscale support for format = %d", format);
            break;
        }
    }
}

void ImageScaler::trimNv12Image(unsigned char *dst, const unsigned char *src,
    const int dest_w, const int dest_h, const int dest_stride,
    const int src_w, const int src_h, const int src_stride,
    const int src_skip_lines_top, // number of lines that are skipped from src image start pointer
    const int src_skip_lines_bottom) // number of lines that are skipped after reading src_h (should be set always to reach full image height)
{
    LOG1("@%s: dest_w: %d, dest_h: %d, dest_stride:%d, src_w: %d, src_h: %d, src_stride: %d, skip_top: %d, skip_bottom: %d", __FUNCTION__, dest_w,dest_h,dest_stride,src_w,src_h,src_stride,src_skip_lines_top,src_skip_lines_bottom);

    // Y
    for (int i = 0; i < dest_h; i++) {
        memcpy(dst,src,dest_stride);
        dst += dest_stride;
        src += src_stride;
    }
    //UV
    for (int i = 0; i < dest_h/2; i++) {
        memcpy(dst,src,dest_stride);
        dst += dest_stride;
        src += src_stride;
    }
}

// VGA-QCIF begin (Enzo specific)
void ImageScaler::downScaleAndCropNv12Image(unsigned char *dest, const unsigned char *src,
    const int dest_w, const int dest_h, const int dest_stride,
    const int src_w, const int src_h, const int src_stride,
    const int src_skip_lines_top, // number of lines that are skipped from src image start pointer
    const int src_skip_lines_bottom) // number of lines that are skipped after reading src_h (should be set always to reach full image height)
{
    if (src_w == 800 && src_h == 600 && src_skip_lines_top == 0 && src_skip_lines_bottom == 0
        && dest_w == RESOLUTION_QVGA_WIDTH && dest_h == RESOLUTION_QVGA_HEIGHT) {
        downScaleNv12ImageFrom800x600ToQvga(dest, src, dest_stride, src_stride);
        return;
    }
    if (src_w == RESOLUTION_VGA_WIDTH && src_h == RESOLUTION_VGA_HEIGHT
        && src_skip_lines_top == 0 && src_skip_lines_bottom == 0
        && dest_w == RESOLUTION_QVGA_WIDTH && dest_h == RESOLUTION_QVGA_HEIGHT) {
        downScaleAndCropNv12ImageQvga(dest, src, dest_stride, src_stride);
        return;
    }
    if (src_w == RESOLUTION_VGA_WIDTH && src_h == RESOLUTION_VGA_HEIGHT
        && src_skip_lines_top == 0 && src_skip_lines_bottom == 0
        && dest_w == RESOLUTION_QCIF_WIDTH && dest_h == RESOLUTION_QCIF_WIDTH) {
        downScaleAndCropNv12ImageQcif(dest, src, dest_stride, src_stride);
        return;
    }

    ////////////////// TODO remove this BIG scaler from HAL after ZSL postview starts to work
    if (src_w == RESOLUTION_1080P_WIDTH && src_h == RESOLUTION_1080P_HEIGHT
            && src_skip_lines_top == 0 && src_skip_lines_bottom == 0
            && dest_w == 1024 && dest_h == 576) {
            downScaleNv12ImageFrom1080PTo1024x576(dest, src, dest_stride, src_stride);
            return;
    }

    LOG2("@%s", __FUNCTION__);

    // skip lines from top
    if (src_skip_lines_top > 0)
        src += src_skip_lines_top * src_stride;

    // Correct aspect ratio is defined by destination buffer
    long int aspect_ratio = (dest_w << 16) / dest_h;
    // Then, we calculate what should be the width of source image
    // (should be multiple by four)
    int proper_source_width = (aspect_ratio * (long int)(src_h) + 0x8000L) >> 16;
    proper_source_width = (proper_source_width + 2) & ~0x3;
    // Now, the source image should have some surplus width
    if (src_w < proper_source_width) {
        LOGE("%s: source image too narrow", __func__);
        return;
    }
    // Let's divide the surplus to both sides
    int l_skip = (src_w - proper_source_width) >> 1;
    int r_skip = src_w - proper_source_width - l_skip;
    int skip = l_skip + r_skip;

    int i, j, x1, y1, x2, y2;
    unsigned int val_1, val_2;
    int dx, dy;
    int src_Y_data = src_stride * (src_h + src_skip_lines_bottom + (src_skip_lines_top >> 1));
    int dest_Y_data = dest_stride * dest_h;
    int flag, width, height;
    if (0 == dest_w || 0 == dest_h) {
        LOGE("%s,dest_w or dest_h should not be 0", __func__);
        return;
    }
    const int scaling_w = ((src_w - skip) << 8) / dest_w;
    const int scaling_h = (src_h << 8) / dest_h;
    dx = 0;
    dy = 0;
    // get Y data
    for (i = 0; i < dest_h; i++) {
        y1 = i * scaling_h;
        dy = y1 & 0xff;
        y2 = y1 >> 8;
        for (j = 0; j < dest_w; j++) {
            x1 = j * scaling_w;
            dx = x1 & 0xff;
            x2 = (x1 >> 8) + l_skip;
            val_1 = ((unsigned int)src[y2 * src_stride + x2] * (256 - dx)
                    + (unsigned int)src[y2 * src_stride + x2 + 1] * dx) >> 8;
            val_2 = ((unsigned int)src[(y2 + 1) * src_stride + x2] * (256 - dx)
                    + (unsigned int)src[(y2 + 1) * src_stride + x2 + 1] * dx) >> 8;
            dest[i * dest_stride + j] = MIN(((val_1 * (256 - dy) + val_2 * dy) >> 8), 0xff);
        }
    }
    flag = 0;
    i = 0;
    j = 0;
    width = dest_w >> 1;
    height = dest_h >> 1;
    //get UV data
    for (i = 0; i < height; i++) {
        y1 = i * scaling_h;
        dy = y1 & 0xff;
        y2 = y1 >> 8;
        for (j = 0; j < width; j++) {
            x1 = j * scaling_w;
            dx = x1 & 0xff;
            x2 = (x1 >> 8) + l_skip / 2;
            //fill U data
            val_1 = ((unsigned int)src[y2 * src_stride + (x2 << 1) + src_Y_data] * (256 - dx)
                     + (unsigned int)src[y2 * src_stride + ((x2 + 1) << 1) + src_Y_data] * dx) >> 8;
            val_2 = ((unsigned int)src[(y2 + 1) * src_stride + (x2 << 1) + src_Y_data] * (256 -dx)
                     + (unsigned int)src[(y2 +1) * src_stride + ((x2 + 1) << 1) + src_Y_data] * dx) >> 8;
            dest[i * dest_stride + (j << 1) + dest_Y_data] = MIN(((val_1 * (256 - dy) + val_2 * dy) >> 8), 0xff);
            //fill V data
            val_1 = ((unsigned int)src[y2 * src_stride + (x2 << 1) + 1 + src_Y_data] * (256 - dx)
                     + (unsigned int)src[y2 * src_stride + ((x2 + 1) << 1) + 1 + src_Y_data] * dx) >> 8;
            val_2 = ((unsigned int)src[(y2 + 1) * src_stride + (x2 << 1) + 1 + src_Y_data] * (256 -dx)
                     + (unsigned int)src[(y2 +1) * src_stride + ((x2 + 1) << 1) + 1 + src_Y_data] * dx) >> 8;
            dest[i * dest_stride + (j << 1) + 1 + dest_Y_data] = MIN(((val_1 * (256 - dy) + val_2 * dy) >> 8), 0xff);
        }
    }
}

void ImageScaler::downScaleAndCropNv12ImageQvga(unsigned char *dest, const unsigned char *src,
    const int dest_stride, const int src_stride)
{
    LOG2("@%s", __FUNCTION__);
    const int dest_w = RESOLUTION_QVGA_WIDTH;
    const int dest_h = RESOLUTION_QVGA_HEIGHT;
    const int src_h = RESOLUTION_VGA_HEIGHT;
    const int scale = 2;

    // Y component
    for (int i = 0; i < dest_h; i++) {
        u_int32_t *s1 = (u_int32_t *)(&src[(i * scale + 0) * src_stride]);
        u_int32_t *s2 = (u_int32_t *)(&src[(i * scale + 1) * src_stride]);
        u_int32_t  *d = (u_int32_t *)(&dest[i * dest_stride]);
        // This processes 4 dest pixels at a time
        for (int j = 0; j < dest_w; j+=4) {
            u_int32_t a1; // Input data upper row
            u_int32_t a2; // Input data lower row
            u_int32_t b;  // Output data
            a1 = *s1++;
            a2 = *s2++;
            b  = ((a1 & 0xff) + ((a1 >> 8) & 0xff) + (a2 & 0xff) + ((a2 >> 8) & 0xff) + 2) / 4;
            b |= ((((a1 >> 16) & 0xff) + ((a1 >> 24) & 0xff) + ((a2 >> 16) & 0xff) + ((a2 >> 24) & 0xff) + 2) / 4) << 8;
            a1 = *s1++;
            a2 = *s2++;
            b |= (((a1 & 0xff) + ((a1 >> 8) & 0xff) + (a2 & 0xff) + ((a2 >> 8) & 0xff) + 2) / 4) << 16;
            b |= ((((a1 >> 16) & 0xff) + ((a1 >> 24) & 0xff) + ((a2 >> 16) & 0xff) + ((a2 >> 24) & 0xff) + 2) / 4) << 24;
            *d++ = b;
        }
    }

    // UV components
    src = &src[src_stride * src_h];
    dest = &dest[dest_stride * dest_h];

    for (int i = 0; i < dest_h/2; i++) {
        u_int32_t *s1 = (u_int32_t *)(&src[(i * scale + 0) * src_stride]);
        u_int32_t *s2 = (u_int32_t *)(&src[(i * scale + 1) * src_stride]);
        u_int32_t  *d = (u_int32_t *)(&dest[i * dest_stride]);
        // This processes 2 dest UV pairs at a time
        for (int j = 0; j < dest_w/2; j+=2) {
            u_int32_t a1; // Input data upper row
            u_int32_t a2; // Input data lower row
            u_int32_t b;  // Output data
            a1 = *s1++;
            a2 = *s2++;
            b  = ((a1 & 0xff) + ((a1 >> 16) & 0xff) + (a2 & 0xff) + ((a2 >> 16) & 0xff) + 2) / 4;
            b |= ((((a1 >> 8) & 0xff) + ((a1 >> 24) & 0xff) + ((a2 >> 8) & 0xff) + ((a2 >> 24) & 0xff) + 2) / 4) << 8;
            a1 = *s1++;
            a2 = *s2++;
            b |= (((a1 & 0xff) + ((a1 >> 16) & 0xff) + (a2 & 0xff) + ((a2 >> 16) & 0xff) + 2) / 4) << 16;
            b |= ((((a1 >> 8) & 0xff) + ((a1 >> 24) & 0xff) + ((a2 >> 8) & 0xff) + ((a2 >> 24) & 0xff) + 2) / 4) << 24;
            *d++ = b;
        }
    }
}

void ImageScaler::downScaleAndCropNv12ImageQcif(unsigned char *dest, const unsigned char *src,
    const int dest_stride, const int src_stride)
{
    LOG2("@%s", __FUNCTION__);
    const int dest_w = RESOLUTION_QCIF_WIDTH;
    const int dest_h = RESOLUTION_QCIF_HEIGHT;
    const int src_w = RESOLUTION_VGA_WIDTH;
    const int src_h = RESOLUTION_VGA_HEIGHT;

    // Correct aspect ratio is defined by destination buffer
    long int aspect_ratio = (dest_w << 16) / dest_h;
    // Then, we calculate what should be the width of source image
    // (should be multiple by four)
    int proper_source_width = (aspect_ratio * (long int)(src_h) + 0x8000L) >> 16;
    proper_source_width = (proper_source_width + 2) & ~0x3;
    // Now, the source image should have some surplus width
    if (src_w < proper_source_width) {
        LOGE("%s: source image too narrow", __func__);
        return;
    }
    // Let's divide the surplus to both sides
    int l_skip = (src_w - proper_source_width) >> 1;
    int r_skip = src_w - proper_source_width - l_skip;
    int skip = l_skip + r_skip;

    int i, j, x1, y1, x2, y2;
    unsigned int val_1, val_2;
    int dx, dy;
    int src_Y_data = src_stride * src_h;
    int dest_Y_data = dest_stride * dest_h;
    int flag, width, height;
    const int scaling_w = ((src_w - skip) << 8) / dest_w;
    const int scaling_h = (src_h << 8) / dest_h;
    dx = 0;
    dy = 0;
    // get Y data
    for (i = 0; i < dest_h; i++) {
        y1 = i * scaling_h;
        dy = y1 & 0xff;
        y2 = y1 >> 8;
        for (j = 0; j < dest_w; j++) {
            x1 = j * scaling_w;
            dx = x1 & 0xff;
            x2 = (x1 >> 8) + l_skip;
            val_1 = ((unsigned int)src[y2 * src_stride + x2] * (256 - dx)
                    + (unsigned int)src[y2 * src_stride + x2 + 1] * dx) >> 8;
            val_2 = ((unsigned int)src[(y2 + 1) * src_stride + x2] * (256 - dx)
                    + (unsigned int)src[(y2 + 1) * src_stride + x2 + 1] * dx) >> 8;
            dest[i * dest_stride + j] = MIN(((val_1 * (256 - dy) + val_2 * dy) >> 8), 0xff);
        }
    }
    flag = 0;
    i = 0;
    j = 0;
    width = dest_w >> 1;
    height = dest_h >> 1;
    //get UV data
    for (i = 0; i < height; i++) {
        y1 = i * scaling_h;
        dy = y1 & 0xff;
        y2 = y1 >> 8;
        for (j = 0; j < width; j++) {
            x1 = j * scaling_w;
            dx = x1 & 0xff;
            x2 = (x1 >> 8) + l_skip / 2;
            //fill U data
            val_1 = ((unsigned int)src[y2 * src_stride + (x2 << 1) + src_Y_data] * (256 - dx)
                     + (unsigned int)src[y2 * src_stride + ((x2 + 1) << 1) + src_Y_data] * dx) >> 8;
            val_2 = ((unsigned int)src[(y2 + 1) * src_stride + (x2 << 1) + src_Y_data] * (256 -dx)
                     + (unsigned int)src[(y2 +1) * src_stride + ((x2 + 1) << 1) + src_Y_data] * dx) >> 8;
            dest[i * dest_stride + (j << 1) + dest_Y_data] = MIN(((val_1 * (256 - dy) + val_2 * dy) >> 8), 0xff);
            //fill V data
            val_1 = ((unsigned int)src[y2 * src_w + (x2 << 1) + 1 + src_Y_data] * (256 - dx)
                     + (unsigned int)src[y2 * src_w + ((x2 + 1) << 1) + 1 + src_Y_data] * dx) >> 8;
            val_2 = ((unsigned int)src[(y2 + 1) * src_w + (x2 << 1) + 1 + src_Y_data] * (256 -dx)
                     + (unsigned int)src[(y2 +1) * src_w + ((x2 + 1) << 1) + 1 + src_Y_data] * dx) >> 8;
            dest[i * dest_stride + (j << 1) + 1 + dest_Y_data] = MIN(((val_1 * (256 - dy) + val_2 * dy) >> 8), 0xff);
        }
    }
}

void ImageScaler::downScaleNv12ImageFrom800x600ToQvga(unsigned char *dest, const unsigned char *src,
    const int dest_stride, const int src_stride)
{
    LOG2("@%s", __FUNCTION__);
    const int dest_w = RESOLUTION_QVGA_WIDTH;
    const int dest_h = RESOLUTION_QVGA_HEIGHT;
    const int src_h = 600;

    // Y component

    // Processing 2 dest rows and 5 src rows at a time
    for (int i = 0; i < dest_h / 2; i++) {
        u_int32_t *s1 = (u_int32_t *)(&src[(i * 5 + 0) * src_stride]);
        u_int32_t *s2 = (u_int32_t *)(&src[(i * 5 + 1) * src_stride]);
        u_int32_t *s3 = (u_int32_t *)(&src[(i * 5 + 2) * src_stride]);
        u_int32_t *d = (u_int32_t *)(&dest[(i * 2 + 0) * dest_stride]);
        // This processes 8 dest pixels at a time
        for (int j = 0; j < dest_w; j+=8) {
            u_int32_t a1; // Input data upper row
            u_int32_t a2; // Input data middle row
            u_int32_t a3; // Input data lower row
            u_int32_t t;  // Temp data (for constructing the output)
            u_int32_t b;  // Output data
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (4 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 8) & 0xff) + 2 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 8) & 0xff) + 2 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 8) & 0xff) + 1 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff));
            t = (t + 12) / 25;
            b = t; // First pixel
            t = (0 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 2 * ((a1 >> 16) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 2 * ((a2 >> 16) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 1 * ((a3 >> 16) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (4 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ t;
            t = (t + 12) / 25;
            b |= t << 8; // Second pixel
            t = (0 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 16) & 0xff) + 2 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 16) & 0xff) + 2 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 16) & 0xff) + 1 * ((a3 >> 24) & 0xff));
            t = (t + 12) / 25;
            b |= t << 16; // Third pixel
            t = (2 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (4 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ t;
            t = (t + 12) / 25;
            b |= t << 24; // Fourth pixel
            *d++ = b;
            t = (0 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 16) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 16) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 16) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (2 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ t;
            t = (t + 12) / 25;
            b = t; // Fifth pixel
            t = (2 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff));
            t = (t + 12) / 25;
            b |= t << 8; // Sixth pixel
            t = (0 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (4 * ((a1 >> 0) & 0xff) + 2 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 2 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 1 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ t;
            t = (t + 12) / 25;
            b |= t << 16; // Seventh pixel
            t = (0 * ((a1 >> 0) & 0xff) + 2 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 16) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 2 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 16) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 1 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 16) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            t = (t + 12) / 25;
            b |= t << 24; // Eigth pixel
            *d++ = b;
        }
        s1 = (u_int32_t *)(&src[(i * 5 + 4) * src_stride]);
        s2 = (u_int32_t *)(&src[(i * 5 + 3) * src_stride]);
        s3 = (u_int32_t *)(&src[(i * 5 + 2) * src_stride]);
        d = (u_int32_t *)(&dest[(i * 2 + 1) * dest_stride]);
        // This processes 8 dest pixels at a time
        for (int j = 0; j < dest_w; j+=8) {
            u_int32_t a1; // Input data lower row
            u_int32_t a2; // Input data middle row
            u_int32_t a3; // Input data upper row
            u_int32_t t;  // Temp data (for constructing the output)
            u_int32_t b;  // Output data
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (4 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 8) & 0xff) + 2 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 8) & 0xff) + 2 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 8) & 0xff) + 1 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff));
            t = (t + 12) / 25;
            b = t; // First pixel
            t = (0 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 2 * ((a1 >> 16) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 2 * ((a2 >> 16) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 1 * ((a3 >> 16) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (4 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ t;
            t = (t + 12) / 25;
            b |= t << 8; // Second pixel
            t = (0 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 16) & 0xff) + 2 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 16) & 0xff) + 2 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 16) & 0xff) + 1 * ((a3 >> 24) & 0xff));
            t = (t + 12) / 25;
            b |= t << 16; // Third pixel
            t = (2 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (4 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ t;
            t = (t + 12) / 25;
            b |= t << 24; // Fourth pixel
            *d++ = b;
            t = (0 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 16) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 16) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 16) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (2 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ t;
            t = (t + 12) / 25;
            b = t; // Fifth pixel
            t = (2 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff));
            t = (t + 12) / 25;
            b |= t << 8; // Sixth pixel
            t = (0 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            t = (4 * ((a1 >> 0) & 0xff) + 2 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 16) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 2 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 16) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 1 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 16) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ t;
            t = (t + 12) / 25;
            b |= t << 16; // Seventh pixel
            t = (0 * ((a1 >> 0) & 0xff) + 2 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 16) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 2 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 16) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 1 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 16) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            t = (t + 12) / 25;
            b |= t << 24; // Eigth pixel
            *d++ = b;
        }
    }

    // UV components
    src = &src[src_stride * src_h];
    dest = &dest[dest_stride * dest_h];

    // Processing 2 dest rows and 5 src rows at a time
    for (int i = 0; i < (dest_h/2) / 2; i++) {
        u_int32_t *s1 = (u_int32_t *)(&src[(i * 5 + 0) * src_stride]);
        u_int32_t *s2 = (u_int32_t *)(&src[(i * 5 + 1) * src_stride]);
        u_int32_t *s3 = (u_int32_t *)(&src[(i * 5 + 2) * src_stride]);
        u_int16_t *d = (u_int16_t *)(&dest[(i * 2 + 0) * dest_stride]);
        // This processes 4 dest UV pairs at a time
        for (int j = 0; j < dest_w/2; j+=4) {
            u_int32_t a1; // Input data upper row
            u_int32_t a2; // Input data middle row
            u_int32_t a3; // Input data lower row
            u_int32_t u;  // Temp data (for constructing the output)
            u_int32_t v;  // Temp data (for constructing the output)
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (4 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 16) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 16) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 16) & 0xff));
            v = (4 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (2 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 16) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 16) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 16) & 0xff))+ u;
            v = (2 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ v;
            u = (u + 12) / 25;
            v = (v + 12) / 25;
            *d++ = u | (v << 8); // First uv pair;
            u = (2 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 16) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 16) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 16) & 0xff));
            v = (2 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (4 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 16) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 16) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 16) & 0xff))+ u;
            v = (4 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ v;
            u = (u + 12) / 25;
            v = (v + 12) / 25;
            *d++ = u | (v << 8); // Second uv pair;
            u = (0 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 16) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 16) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 16) & 0xff));
            v = (0 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (4 * ((a1 >> 0) & 0xff) + 2 * ((a1 >> 16) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 2 * ((a2 >> 16) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 1 * ((a3 >> 16) & 0xff))+ u;
            v = (4 * ((a1 >> 8) & 0xff) + 2 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 8) & 0xff) + 2 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 8) & 0xff) + 1 * ((a3 >> 24) & 0xff))+ v;
            u = (u + 12) / 25;
            v = (v + 12) / 25;
            *d++ = u | (v << 8); // Third uv pair;
            u = (0 * ((a1 >> 0) & 0xff) + 2 * ((a1 >> 16) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 2 * ((a2 >> 16) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 1 * ((a3 >> 16) & 0xff));
            v = (0 * ((a1 >> 8) & 0xff) + 2 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 8) & 0xff) + 2 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 8) & 0xff) + 1 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (4 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 16) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 16) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 16) & 0xff))+ u;
            v = (4 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 24) & 0xff))+ v;
            u = (u + 12) / 25;
            v = (v + 12) / 25;
            *d++ = u | (v << 8); // Fourth uv pair;
        }
        s1 = (u_int32_t *)(&src[(i * 5 + 4) * src_stride]);
        s2 = (u_int32_t *)(&src[(i * 5 + 3) * src_stride]);
        s3 = (u_int32_t *)(&src[(i * 5 + 2) * src_stride]);
        d = (u_int16_t *)(&dest[(i * 2 + 1) * dest_stride]);
        // This processes 4 dest UV pairs at a time
        for (int j = 0; j < dest_w/2; j+=4) {
            u_int32_t a1; // Input data lower row
            u_int32_t a2; // Input data middle row
            u_int32_t a3; // Input data upper row
            u_int32_t u;  // Temp data (for constructing the output)
            u_int32_t v;  // Temp data (for constructing the output)
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (4 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 16) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 16) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 16) & 0xff));
            v = (4 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (2 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 16) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 16) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 16) & 0xff))+ u;
            v = (2 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ v;
            u = (u + 12) / 25;
            v = (v + 12) / 25;
            *d++ = u | (v << 8); // First uv pair;
            u = (2 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 16) & 0xff) +
                 2 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 16) & 0xff) +
                 1 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 16) & 0xff));
            v = (2 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 2 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 1 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (4 * ((a1 >> 0) & 0xff) + 0 * ((a1 >> 16) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 0 * ((a2 >> 16) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 0 * ((a3 >> 16) & 0xff))+ u;
            v = (4 * ((a1 >> 8) & 0xff) + 0 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 8) & 0xff) + 0 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 8) & 0xff) + 0 * ((a3 >> 24) & 0xff))+ v;
            u = (u + 12) / 25;
            v = (v + 12) / 25;
            *d++ = u | (v << 8); // Second uv pair;
            u = (0 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 16) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 16) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 16) & 0xff));
            v = (0 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (4 * ((a1 >> 0) & 0xff) + 2 * ((a1 >> 16) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 2 * ((a2 >> 16) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 1 * ((a3 >> 16) & 0xff))+ u;
            v = (4 * ((a1 >> 8) & 0xff) + 2 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 8) & 0xff) + 2 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 8) & 0xff) + 1 * ((a3 >> 24) & 0xff))+ v;
            u = (u + 12) / 25;
            v = (v + 12) / 25;
            *d++ = u | (v << 8); // Third uv pair;
            u = (0 * ((a1 >> 0) & 0xff) + 2 * ((a1 >> 16) & 0xff) +
                 0 * ((a2 >> 0) & 0xff) + 2 * ((a2 >> 16) & 0xff) +
                 0 * ((a3 >> 0) & 0xff) + 1 * ((a3 >> 16) & 0xff));
            v = (0 * ((a1 >> 8) & 0xff) + 2 * ((a1 >> 24) & 0xff) +
                 0 * ((a2 >> 8) & 0xff) + 2 * ((a2 >> 24) & 0xff) +
                 0 * ((a3 >> 8) & 0xff) + 1 * ((a3 >> 24) & 0xff));
            a1 = *s1++;
            a2 = *s2++;
            a3 = *s3++;
            u = (4 * ((a1 >> 0) & 0xff) + 4 * ((a1 >> 16) & 0xff) +
                 4 * ((a2 >> 0) & 0xff) + 4 * ((a2 >> 16) & 0xff) +
                 2 * ((a3 >> 0) & 0xff) + 2 * ((a3 >> 16) & 0xff))+ u;
            v = (4 * ((a1 >> 8) & 0xff) + 4 * ((a1 >> 24) & 0xff) +
                 4 * ((a2 >> 8) & 0xff) + 4 * ((a2 >> 24) & 0xff) +
                 2 * ((a3 >> 8) & 0xff) + 2 * ((a3 >> 24) & 0xff))+ v;
            u = (u + 12) / 25;
            v = (v + 12) / 25;
            *d++ = u | (v << 8); // Fourth uv pair;
        }
    }

}
// VGA-QCIF end



};

