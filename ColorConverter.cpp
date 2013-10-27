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
#define LOG_TAG "Camera_ColorConverter"

#include <camera/CameraParameters.h>
#include <linux/atomisp.h>
#include <linux/videodev2.h>
#include "ColorConverter.h"
#include "LogHelper.h"
#include "AtomCommon.h"

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

void trimConvertNV12ToRGB565(int width, int height, int srcStride, void *src, void *dst)
{

    unsigned char *yuvs = (unsigned char *) src;
    unsigned char *rgbs = (unsigned char *) dst;

    //the end of the luminance data
    int lumEnd = srcStride * height;
    //points to the next luminance value pair
    int lumPtr = 0;
    //points to the next chromiance value pair
    int chrPtr = 0;

    int i = 0, j = 0;

    for( i=0; i < height; i++) {
        lumPtr = i * srcStride;
        chrPtr = i / 2 * srcStride + lumEnd;
        for ( j=0; j < width; j+=2 ) {
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
            *rgbs++ = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
            *rgbs++ = (unsigned char) ((R & 0xf8) | (G >> 5));

            //generate second RGB components
            B = Y2 + ((454 * Cb) >> 8);
            if(B < 0) B = 0; else if(B > 255) B = 255;
            G = Y2 - ((88 * Cb + 183 * Cr) >> 8);
            if(G < 0) G = 0; else if(G > 255) G = 255;
            R = Y2 + ((359 * Cr) >> 8);
            if(R < 0) R = 0; else if(R > 255) R = 255;
            //NOTE: this assume little-endian encoding
            *rgbs++ = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
            *rgbs++ = (unsigned char) ((R & 0xf8) | (G >> 5));
        }
    }
}

// covert YV12 (Y plane, V plane, U plane) to NV21 (Y plane, interlaced VU bytes)
void convertYV12ToNV21(int width, int height, int srcStride, int dstStride, void *src, void *dst)
{
    const int cStride = srcStride>>1;
    const int vuStride = dstStride;
    const int hhalf = height>>1;
    const int whalf = width>>1;

    // copy the entire Y plane
    unsigned char *srcPtr = (unsigned char *)src;
    unsigned char *dstPtr = (unsigned char *)dst;
    if (srcStride == dstStride) {
        memcpy(dstPtr, srcPtr, dstStride*height);
    } else {
        for (int i = 0; i < height; i++) {
            memcpy(dstPtr, srcPtr, width);
            srcPtr += srcStride;
            dstPtr += dstStride;
        }
    }

    // interlace the VU data
    unsigned char *srcPtrV = (unsigned char *)src + height*srcStride;
    unsigned char *srcPtrU = srcPtrV + cStride*hhalf;
    dstPtr = (unsigned char *)dst + dstStride*height;
    for (int i = 0; i < hhalf; ++i) {
        unsigned char *pDstVU = dstPtr;
        unsigned char *pSrcV = srcPtrV;
        unsigned char *pSrcU = srcPtrU;
        for (int j = 0; j < whalf; ++j) {
            *pDstVU ++ = *pSrcV ++;
            *pDstVU ++ = *pSrcU ++;
        }
        dstPtr += vuStride;
        srcPtrV += cStride;
        srcPtrU += cStride;
    }
}

// copy YV12 to YV12 (Y plane, V plan, U plan) in case of different stride length
void copyYV12ToYV12(int width, int height, int srcStride, int dstStride, void *src, void *dst)
{
    // copy the entire Y plane
    if (srcStride == dstStride) {
        memcpy(dst, src, dstStride * height);
    } else {
        unsigned char *srcPtrY = (unsigned char *)src;
        unsigned char *dstPtrY = (unsigned char *)dst;
        for (int i = 0; i < height; i ++) {
            memcpy(dstPtrY, srcPtrY, width);
            srcPtrY += srcStride;
            dstPtrY += dstStride;
        }
    }

    // copy VU plane
    const int scStride = srcStride >> 1;
    const int dcStride = ALIGN16(dstStride >> 1); // Android CTS required: U/V plane needs 16 bytes aligned!
    if (dcStride == scStride) {
        unsigned char *srcPtrVU = (unsigned char *)src + height * srcStride;
        unsigned char *dstPtrVU = (unsigned char *)dst + height * dstStride;
        memcpy(dstPtrVU, srcPtrVU, height * dcStride);
    } else {
        const int wHalf = width >> 1;
        const int hHalf = height >> 1;
        unsigned char *srcPtrV = (unsigned char *)src + height * srcStride;
        unsigned char *srcPtrU = srcPtrV + scStride * hHalf;
        unsigned char *dstPtrV = (unsigned char *)dst + height * dstStride;
        unsigned char *dstPtrU = dstPtrV + dcStride * hHalf;
        for (int i = 0; i < hHalf; i ++) {
            memcpy(dstPtrU, srcPtrU, wHalf);
            memcpy(dstPtrV, srcPtrV, wHalf);
            dstPtrU += dcStride, srcPtrU += scStride;
            dstPtrV += dcStride, srcPtrV += scStride;
        }
    }
}

// covert NV12 (Y plane, interlaced UV bytes) to
// NV21 (Y plane, interlaced VU bytes) and trim stride width to real width
void trimConvertNV12ToNV21(int width, int height, int srcStride, void *src, void *dst)
{
    const int ysize = width * height;
    unsigned const char *pSrc = (unsigned char *)src;
    unsigned char *pDst = (unsigned char *)dst;

    // Copy Y component
    if (srcStride == width) {
        memcpy(pDst, pSrc, ysize);
    } else if (srcStride > width) {
        int j = height;
        while(j--) {
            memcpy(pDst, pSrc, width);
            pSrc += srcStride;
            pDst += width;
        }
    } else {
        LOGE("bad stride value");
        return;
    }

    // Convert UV to VU
    pSrc = (unsigned char *)src + srcStride * height;
    pDst = (unsigned char *)dst + width * height;
    for (int j = 0; j < height / 2; j++) {
        if (width >= 16) {
            const uint32_t *ptr0 = (const uint32_t *)(pSrc);
            uint32_t *ptr1 = (uint32_t *)(pDst);
            int bNotLastLine = ((j+1) == (height/2)) ? 0 : 1;
            int width_16 = (width + 15 * bNotLastLine) & ~0xf;
            if ((((uint32_t)(pSrc)) & 0xf) == 0 && (((uint32_t)(pDst)) & 0xf) == 0) { // 16 bytes aligned for both src and dest
                __asm__ volatile(\
                                 "movl       %0,  %%eax      \n\t"
                                 "movl       %1,  %%edx      \n\t"
                                 "movl       %2,  %%ecx      \n\t"
                                 "1:     \n\t"
                                 "movdqa (%%eax), %%xmm1     \n\t"
                                 "movdqa  %%xmm1, %%xmm0     \n\t"
                                 "psllw       $8, %%xmm1     \n\t"
                                 "psrlw       $8, %%xmm0     \n\t"
                                 "por     %%xmm0, %%xmm1     \n\t"
                                 "movdqa  %%xmm1, (%%edx)    \n\t"
                                 "add        $16, %%eax      \n\t"
                                 "add        $16, %%edx      \n\t"
                                 "sub        $16, %%ecx      \n\t"
                                 "jnz   1b \n\t"
                                 : "+m"(ptr0), "+m"(ptr1), "+m"(width_16)
                                 :
                                 : "eax", "ecx", "edx", "xmm0", "xmm1"
                                );
            }
            else { // either src or dest is not 16-bytes aligned
                __asm__ volatile(\
                                 "movl       %0,  %%eax      \n\t"
                                 "movl       %1,  %%edx      \n\t"
                                 "movl       %2,  %%ecx      \n\t"
                                 "1:     \n\t"
                                 "lddqu  (%%eax), %%xmm1     \n\t"
                                 "movdqa  %%xmm1, %%xmm0     \n\t"
                                 "psllw       $8, %%xmm1     \n\t"
                                 "psrlw       $8, %%xmm0     \n\t"
                                 "por     %%xmm0, %%xmm1     \n\t"
                                 "movdqu  %%xmm1, (%%edx)    \n\t"
                                 "add        $16, %%eax      \n\t"
                                 "add        $16, %%edx      \n\t"
                                 "sub        $16, %%ecx      \n\t"
                                 "jnz   1b \n\t"
                                 : "+m"(ptr0), "+m"(ptr1), "+m"(width_16)
                                 :
                                 : "eax", "ecx", "edx", "xmm0", "xmm1"
                                );
            }

            // process remaining data of less than 16 bytes of last row
            for (int i = width_16; i < width; i += 2) {
                pDst[i] = pSrc[i + 1];
                pDst[i + 1] = pSrc[i];
            }
        }
        else if ((((uint32_t)(pSrc)) & 0x3) == 0 && (((uint32_t)(pDst)) & 0x3) == 0){  // 4 bytes aligned for both src and dest
            const uint32_t *ptr0 = (const uint32_t *)(pSrc);
            uint32_t *ptr1 = (uint32_t *)(pDst);
            int width_4 = width & ~3;
            for (int i = 0; i < width_4; i += 4) {
                uint32_t data0 = *ptr0++;
                uint32_t data1 = (data0 >> 8) & 0x00ff00ff;
                uint32_t data2 = (data0 << 8) & 0xff00ff00;
                *ptr1++ = data1 | data2;
            }
            // process remaining data of less than 4 bytes at end of each row
            for (int i = width_4; i < width; i += 2) {
                pDst[i] = pSrc[i + 1];
                pDst[i + 1] = pSrc[i];
            }
        }
        else {
            unsigned const char *ptr0 = pSrc;
            unsigned char *ptr1 = pDst;
            for (int i = 0; i < width; i += 2) {
                *ptr1++ = ptr0[1];
                *ptr1++ = ptr0[0];
                ptr0 += 2;
            }
        }
        pDst += width;
        pSrc += srcStride;
    }
}

// covert NV12 (Y plane, interlaced UV bytes) to YV12 (Y plane, V plane, U plane)
void align16ConvertNV12ToYV12(int width, int height, int srcStride, void *src, void *dst)
{
    int yStride = ALIGN16(width);
    size_t ySize = yStride * height;
    int cStride = ALIGN16(yStride/2);
    size_t cSize = cStride * height/2;

    unsigned char *srcPtr = (unsigned char *) src;
    unsigned char *dstPtr = (unsigned char *) dst;
    unsigned char *dstPtrV = (unsigned char *) dst + ySize;
    unsigned char *dstPtrU = (unsigned char *) dst + ySize + cSize;

    // copy the entire Y plane
    if (srcStride == yStride) {
        memcpy(dstPtr, srcPtr, ySize);
        srcPtr += ySize;
    } else if (srcStride > width) {
        for (int i = 0; i < height; i++) {
            memcpy(dstPtr, srcPtr, width);
            srcPtr += srcStride;
            dstPtr += yStride;
        }
    } else {
        LOGE("bad src stride value");
        return;
    }

    // deinterlace the UV data
    for ( int i = 0; i < height / 2; ++i) {
        for ( int j = 0; j < width / 2; ++j) {
            dstPtrV[j] = srcPtr[j * 2 + 1];
            dstPtrU[j] = srcPtr[j * 2];
        }
        srcPtr += srcStride;
        dstPtrV += cStride;
        dstPtrU += cStride;
    }
}

// P411's Y, U, V are seperated. But the YUY2's Y, U and V are interleaved.
void YUY2ToP411(int width, int height, void *src, void *dst)
{
    int ySize = width * height;
    int cSize = width * height / 4;
    int wHalf = width >> 1;

    unsigned char *srcPtr = (unsigned char *) src;
    unsigned char *dstPtr = (unsigned char *) dst;
    unsigned char *dstPtrU = (unsigned char *) dst + ySize;
    unsigned char *dstPtrV = (unsigned char *) dst + ySize + cSize;

    for (int i = 0; i < height; i++) {
        //The first line of the source
        //Copy first Y Plane first
        for (int j=0; j < width; j++) {
            dstPtr[j] = srcPtr[j*2];
        }

        if (i & 1) {
            //Copy the V plane
            for (int k = 0; k < wHalf; k++) {
                dstPtrV[k] = srcPtr[k * 4 + 3];
            }
            dstPtrV = dstPtrV + wHalf;
        } else {
            //Copy the U plane
            for (int k = 0; k< wHalf; k++) {
                dstPtrU[k] = srcPtr[k * 4 + 1];
            }
            dstPtrU = dstPtrU + wHalf;
        }

        srcPtr = srcPtr + width * 2;
        dstPtr = dstPtr + width;
    }
}

// P411's Y, U, V are seperated. But the NV12's U and V are interleaved.
void NV12ToP411(int width, int height, void *src, void *dst)
{
    int i, j, p, q;
    unsigned char *pdstU, *pdstV;
    unsigned char *psrcUV;

    // copy Y data
    memcpy(dst, src, width * height);
    // copy U data and V data
    psrcUV = (unsigned char *)src + width * height;
    pdstU = (unsigned char *)dst + width * height;
    pdstV = pdstU + width * height / 4;
    p = q = 0;
    for (i = 0; i < height / 2; i++) {
        for (j = 0; j < width; j++) {
            if (j % 2 == 0) {
                pdstU[p]= (psrcUV[i * width + j] & 0xFF) ;
                p++;
           } else {
                pdstV[q]= (psrcUV[i * width + j] & 0xFF);
                q++;
            }
        }
    }
}

// Re-pad YUV420 format image, the format can be YV12, YU12 or YUV420 planar.
// If buffer size: (height*dstStride*1.5) > (height*srcStride*1.5), src and dst
// buffer start addresses are same, the re-padding can be done inplace.
void repadYUV420(int width, int height, int srcStride, int dstStride, void *src, void *dst)
{
    unsigned char *dptr;
    unsigned char *sptr;
    void * (*myCopy)(void *dst, const void *src, size_t n);

    const int whalf = width >> 1;
    const int hhalf = height >> 1;
    const int scStride = srcStride >> 1;
    const int dcStride = dstStride >> 1;
    const int sySize = height * srcStride;
    const int dySize = height * dstStride;
    const int scSize = hhalf * scStride;
    const int dcSize = hhalf * dcStride;

    // directly copy, if (srcStride == dstStride)
    if (srcStride == dstStride) {
        memcpy(dst, src, dySize + 2*dcSize);
        return;
    }

    // copy V(YV12 case) or U(YU12 case) plane line by line
    sptr = (unsigned char *)src + sySize + 2*scSize - scStride;
    dptr = (unsigned char *)dst + dySize + 2*dcSize - dcStride;

    // try to avoid overlapped memcpy()
    myCopy = (abs(sptr -dptr) > dstStride) ? memcpy : memmove;

    for (int i = 0; i < hhalf; i ++) {
        myCopy(dptr, sptr, whalf);
        sptr -= scStride;
        dptr -= dcStride;
    }

    // copy  V(YV12 case) or U(YU12 case) U/V plane line by line
    sptr = (unsigned char *)src + sySize + scSize - scStride;
    dptr = (unsigned char *)dst + dySize + dcSize - dcStride;
    for (int i = 0; i < hhalf; i ++) {
        myCopy(dptr, sptr, whalf);
        sptr -= scStride;
        dptr -= dcStride;
    }

    // copy Y plane line by line
    sptr = (unsigned char *)src + sySize - srcStride;
    dptr = (unsigned char *)dst + dySize - dstStride;
    for (int i = 0; i < height; i ++) {
        myCopy(dptr, sptr, width);
        sptr -= srcStride;
        dptr -= dstStride;
    }
}

// covert YUYV(YUY2, YUV422 format) to YV12 (Y plane, V plane, U plane)
void convertYUYVToYV12(int width, int height, int srcStride, int dstStride, void *src, void *dst)
{

    int ySize, cSize, wHalf;

    ySize = dstStride * height;
    cSize = ALIGN16(dstStride/2) * height / 2;
    wHalf = dstStride >> 1;

    unsigned char *srcPtr = (unsigned char *) src;
    unsigned char *dstPtr = (unsigned char *) dst;
    unsigned char *dstPtrV = (unsigned char *) dst + ySize;
    unsigned char *dstPtrU = (unsigned char *) dst + ySize + cSize;

    for (int i = 0; i < height; i++) {
        //The first line of the source
        //Copy first Y Plane first
        for (int j=0; j < dstStride; j++) {
            dstPtr[j] = srcPtr[j*2];
        }

        if (i & 1) {
            //Copy the V plane
            for (int k = 0; k< wHalf; k++) {
                dstPtrV[k] = srcPtr[k * 4 + 3];
            }
            dstPtrV = dstPtrV + ALIGN16(dstStride>>1);
        } else {
            //Copy the U plane
            for (int k = 0; k< wHalf; k++) {
                dstPtrU[k] = srcPtr[k * 4 + 1];
            }
            dstPtrU = dstPtrU + ALIGN16(dstStride>>1);
        }

        srcPtr = srcPtr + srcStride * 2;
        dstPtr = dstPtr + dstStride;
    }
}

// covert YUYV(YUY2, YUV422 format) to NV21 (Y plane, interlaced VU bytes)
void convertYUYVToNV21(int width, int height, int srcStride, void *src, void *dst)
{
    int ySize = width * height;
    int u_counter=1, v_counter=0;

    unsigned char *srcPtr = (unsigned char *) src;
    unsigned char *dstPtr = (unsigned char *) dst;
    unsigned char *dstPtrUV = (unsigned char *) dst + ySize;

    for (int i=0; i < height; i++) {
        //The first line of the source
        //Copy first Y Plane first
        for (int j=0; j < width * 2; j++) {
            if (j % 2 == 0)
                dstPtr[j/2] = srcPtr[j];
            if (i%2) {
                if (( j % 4 ) == 3) {
                    dstPtrUV[v_counter] = srcPtr[j]; //V plane
                    v_counter += 2;
                }
                if (( j % 4 ) == 1) {
                    dstPtrUV[u_counter] = srcPtr[j]; //U plane
                    u_counter += 2;
                }
            }
        }

        srcPtr = srcPtr + srcStride * 2;
        dstPtr = dstPtr + width;
    }
}

void convertBuftoYV12(int format, int width, int height, int srcStride, int
                      dstStride, void *src, void *dst)
{
    switch (format) {
    case V4L2_PIX_FMT_NV12:
        align16ConvertNV12ToYV12(width, height, srcStride, src, dst);
        break;
    case V4L2_PIX_FMT_YVU420:
        copyYV12ToYV12(width, height, srcStride, dstStride, src, dst);
        break;
    case V4L2_PIX_FMT_YUYV:
        convertYUYVToYV12(width, height, srcStride, dstStride, src, dst);
        break;
    default:
        LOGE("%s: unsupported format %d", __func__, format);
        break;
    }
}

void convertBuftoNV21(int format, int width, int height, int srcStride, int
                      dstStride, void *src, void *dst)
{
    switch (format) {
    case V4L2_PIX_FMT_NV12:
        trimConvertNV12ToNV21(width, height, srcStride, src, dst);
        break;
    case V4L2_PIX_FMT_YVU420:
        convertYV12ToNV21(width, height, srcStride, dstStride, src, dst);
        break;
    case V4L2_PIX_FMT_YUYV:
        convertYUYVToNV21(width, height, srcStride, src, dst);
        break;
    default:
        LOGE("%s: unsupported format %d", __func__, format);
        break;
    }
}

const char *cameraParametersFormat(int v4l2Format)
{
    switch (v4l2Format) {
    case V4L2_PIX_FMT_YVU420:
        return CameraParameters::PIXEL_FORMAT_YUV420P;
    case V4L2_PIX_FMT_NV21:
        return CameraParameters::PIXEL_FORMAT_YUV420SP;
    case V4L2_PIX_FMT_YUYV:
        return CameraParameters::PIXEL_FORMAT_YUV422I;
    case V4L2_PIX_FMT_JPEG:
        return CameraParameters::PIXEL_FORMAT_JPEG;
    default:
        LOGE("failed to map format %x to a PIXEL_FORMAT\n", v4l2Format);
        return NULL;
    };
}

int V4L2Format(const char *cameraParamsFormat)
{
    LOG1("@%s cameraParamsFormat=%s", __FUNCTION__, cameraParamsFormat);
    if (!cameraParamsFormat) {
        LOGE("null cameraParamsFormat");
        return -1;
    }

    int len = strlen(CameraParameters::PIXEL_FORMAT_YUV420SP);
    if (strncmp(cameraParamsFormat, CameraParameters::PIXEL_FORMAT_YUV420SP, len) == 0)
        return V4L2_PIX_FMT_NV21;

    len = strlen(CameraParameters::PIXEL_FORMAT_YUV420P);
    if (strncmp(cameraParamsFormat, CameraParameters::PIXEL_FORMAT_YUV420P, len) == 0)
        return V4L2_PIX_FMT_YVU420;

    len = strlen(CameraParameters::PIXEL_FORMAT_RGB565);
    if (strncmp(cameraParamsFormat, CameraParameters::PIXEL_FORMAT_RGB565, len) == 0)
        return V4L2_PIX_FMT_RGB565;

    len = strlen(CameraParameters::PIXEL_FORMAT_JPEG);
    if (strncmp(cameraParamsFormat, CameraParameters::PIXEL_FORMAT_JPEG, len) == 0)
        return V4L2_PIX_FMT_JPEG;

    LOGE("invalid format %s", cameraParamsFormat);
    return -1;
}

} // namespace android
