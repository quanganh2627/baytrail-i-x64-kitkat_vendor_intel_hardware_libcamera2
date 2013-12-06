/*
 * Copyright (C) 2013 The Android Open Source Project
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
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

#define LOG_TAG "Camera_MemoryUtils"
#include "MemoryUtils.h"
#include "PlatformData.h"

namespace android {
    namespace MemoryUtils {

    status_t allocateGraphicBuffer(AtomBuffer &aBuff, const AtomBuffer &formatDescriptor)
    {
        LOG1("@%s", __FUNCTION__);
        status_t status = OK;

        MapperPointer mapperPointer;
        mapperPointer.ptr = NULL;

        int lockMode = GRALLOC_USAGE_SW_READ_OFTEN |
                    GRALLOC_USAGE_SW_WRITE_NEVER |
                    GRALLOC_USAGE_HW_COMPOSER;

        LOG1("%s with these properties: (%dx%d)s:%d fourcc %s", __FUNCTION__,
                formatDescriptor.width, formatDescriptor.height,
                formatDescriptor.bpl, v4l2Fmt2Str(formatDescriptor.fourcc));

        GraphicBuffer *cameraGraphicBuffer = new GraphicBuffer(bytesToPixels(formatDescriptor.fourcc, formatDescriptor.bpl),
                        formatDescriptor.height, getGFXHALPixelFormatFromV4L2Format(formatDescriptor.fourcc),
                        GraphicBuffer::USAGE_HW_RENDER | GraphicBuffer::USAGE_SW_WRITE_OFTEN | GraphicBuffer::USAGE_HW_TEXTURE);

        if (!cameraGraphicBuffer) {
            LOGE("No memory to allocate graphic buffer");
            return NO_MEMORY;
        }

        ANativeWindowBuffer *cameraNativeWindowBuffer = cameraGraphicBuffer->getNativeBuffer();
        aBuff.buff = NULL;     // We do not allocate a normal camera_memory_t
        aBuff.width = formatDescriptor.width;
        aBuff.height = formatDescriptor.height;
        // ANativeWindowBuffer defines bpl in pixels
        if (bytesToPixels(formatDescriptor.fourcc, formatDescriptor.bpl) != cameraNativeWindowBuffer->stride) {
            LOGW("%s: potential bpl problem requested %d, Gfx requries %d",__FUNCTION__, formatDescriptor.bpl, cameraNativeWindowBuffer->stride);
        } else {
            LOG1("%s bpl from Gfx is %d", __FUNCTION__, formatDescriptor.bpl);
        }
        // Note: GraphicBuffer object will carry width as was our pixel stride
        // request basing bpl and resulting bpl may be bigger in the resulting AtomBuffer
        aBuff.bpl = pixelsToBytes(formatDescriptor.fourcc, cameraNativeWindowBuffer->stride);
        aBuff.fourcc = formatDescriptor.fourcc;
        aBuff.gfxInfo.scalerId = -1;
        aBuff.gfxInfo.gfxBufferHandle = &cameraGraphicBuffer->handle;
        aBuff.gfxInfo.gfxBuffer = cameraGraphicBuffer;
        cameraGraphicBuffer->incStrong(&aBuff);
        aBuff.size = frameSize(aBuff.fourcc, bytesToPixels(aBuff.fourcc, aBuff.bpl), aBuff.height);

        status = cameraGraphicBuffer->lock(lockMode, &mapperPointer.ptr);
        if (status != NO_ERROR) {
            LOGE("@%s: Failed to lock GraphicBuffer! status=%d", __FUNCTION__, status);
            return UNKNOWN_ERROR;
        }

        aBuff.gfxInfo.locked = true;
        aBuff.dataPtr = mapperPointer.ptr;
        aBuff.shared = false;
        LOG1("@%s allocated gfx buffer with pointer %p nativewindowbuf %p",
            __FUNCTION__, aBuff.dataPtr, cameraNativeWindowBuffer);

        // It is used specially for BYT with Gen GPU. The video encoder need NV12 tiled format graphic buffer.
        // Every recording buffer will be converted to this group of buffers which are really used for encoding.
        if (aBuff.type == ATOM_BUFFER_VIDEO && PlatformData::isGraphicGen()) {
            GraphicBuffer *gfxbuf = new GraphicBuffer(formatDescriptor.width, ALIGN32(formatDescriptor.height), HAL_PIXEL_FORMAT_NV12_TILED_INTEL,
                    GraphicBuffer::USAGE_HW_RENDER | GraphicBuffer::USAGE_HW_TEXTURE);

            if (!gfxbuf) {
                LOGE("No memory to allocate tiled graphic buffer");
                return NO_MEMORY;
            }

            cameraNativeWindowBuffer = gfxbuf->getNativeBuffer();
            aBuff.gfxInfo_rec.gfxBuffer = gfxbuf;
            aBuff.gfxInfo_rec.gfxBufferHandle = &gfxbuf->handle;
            gfxbuf->incStrong(&aBuff);
            LOG1("@%s allocated rec gfx buffer size(%dx%d) stride:%d",
                    __FUNCTION__, formatDescriptor.width, formatDescriptor.height, cameraNativeWindowBuffer->stride);
        }

        return status;
    }

    void freeGraphicBuffer(AtomBuffer &aBuff)
    {
        LOG1("@%s", __FUNCTION__);
        GraphicBuffer *graphicBuffer = aBuff.gfxInfo.gfxBuffer;
        if (graphicBuffer) { // if gfx buffers came through setGraphicPreviewBuffers, there is no graphic buffer stored..
            LOG1("@%s freeing gfx buffer with pointer %p (graphic win buf %p) refcount %d",
                __FUNCTION__, aBuff.dataPtr, graphicBuffer, graphicBuffer->getStrongCount());
            if (aBuff.gfxInfo.locked)
                graphicBuffer->unlock();

            graphicBuffer->decStrong(&aBuff);
        }
        aBuff.gfxInfo.gfxBuffer = NULL;
        aBuff.gfxInfo.gfxBufferHandle = NULL;
        aBuff.gfxInfo.scalerId = -1;
        aBuff.gfxInfo.locked = false;
        aBuff.dataPtr = NULL;

        graphicBuffer = aBuff.gfxInfo_rec.gfxBuffer;
        if (graphicBuffer) {
            LOG1("@%s freeing gfx buffer %p refcount %d", __FUNCTION__, graphicBuffer, graphicBuffer->getStrongCount());
            if (aBuff.gfxInfo_rec.locked)
                graphicBuffer->unlock();

            graphicBuffer->decStrong(&aBuff);
        }
        aBuff.gfxInfo_rec.gfxBuffer = NULL;
        aBuff.gfxInfo_rec.gfxBufferHandle = NULL;
        aBuff.gfxInfo_rec.scalerId = -1;
        aBuff.gfxInfo_rec.locked = false;
    }

    status_t allocateAtomBuffer(AtomBuffer &aBuff, const AtomBuffer &formatDescriptor, Callbacks *aCallbacks)
    {
        LOG1("%s with these properties: (%dx%d)s:%d fourcc %s", __FUNCTION__,
                formatDescriptor.width, formatDescriptor.height,
                formatDescriptor.bpl, v4l2Fmt2Str(formatDescriptor.fourcc));
        status_t status = OK;
        aBuff.dataPtr = NULL;

        aCallbacks->allocateMemory(&aBuff, formatDescriptor.size);
        if (aBuff.buff == NULL) {
            LOGE("Failed to allocate AtomBuffer");
            return NO_MEMORY;
        }

        aBuff.width = formatDescriptor.width;
        aBuff.height = formatDescriptor.height;
        aBuff.bpl = formatDescriptor.bpl;
        aBuff.fourcc = formatDescriptor.fourcc;
        aBuff.size = formatDescriptor.size;
        aBuff.dataPtr = aBuff.buff->data;
        aBuff.shared = false;

        LOG1("@%s allocated heap buffer with pointer %p", __FUNCTION__, aBuff.dataPtr);
        return status;
    }
    void freeAtomBuffer(AtomBuffer &aBuff)
    {
        LOG1("@%s: dataPtr %p", __FUNCTION__, aBuff.dataPtr);
        // free GFX memory, if any
        freeGraphicBuffer(aBuff);
        // free memory allocated through callbacks, if any
        if (aBuff.buff != NULL) {
            aBuff.buff->release(aBuff.buff);
            aBuff.buff = NULL;
        }
        // free metadata, if any
        if (aBuff.metadata_buff != NULL) {
            aBuff.metadata_buff->release(aBuff.metadata_buff);
            aBuff.metadata_buff = NULL;
        }
        aBuff.dataPtr = NULL;
    }

    } // namespace MemoryUtils
} // namespace android
