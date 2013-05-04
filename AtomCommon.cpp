/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2012 Intel Corporation
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

#include "AtomCommon.h"
#include <ia_coordinate.h>

namespace android {

timeval AtomBufferFactory_AtomBufDefTS = {0, 0}; // default timestamp (see AtomCommon.h)
AtomBuffer AtomBufferFactory::createAtomBuffer(AtomBufferType type,
                                               int format,
                                               int width,
                                               int height,
                                               int stride,
                                               int size,
                                               IBufferOwner *owner,
                                               camera_memory_t *buff,
                                               camera_memory_t *metadata_buff,
                                               int id,
                                               int frameCounter,
                                               int ispPrivate,
                                               bool shared,
                                               struct timeval capture_timestamp,
                                               void *dataPtr) {
    AtomBuffer buf;
    buf.format = format;
    buf.type = type;
    buf.width = width;
    buf.height = height;
    buf.stride = stride;
    buf.size = size;
    buf.owner = owner;
    buf.buff = buff;
    buf.metadata_buff = metadata_buff;
    buf.id = id;
    buf.frameCounter = frameCounter;
    buf.ispPrivate = ispPrivate;
    buf.status = FRAME_STATUS_NA;
    buf.shared = shared;
    buf.capture_timestamp = capture_timestamp;
    buf.dataPtr = dataPtr;
    buf.frameSequenceNbr = 0;
    if (dataPtr == NULL && buff != NULL)
        buf.dataPtr = buff->data;
    return buf;
}

void convertFromAndroidToIaCoordinates(const CameraWindow &srcWindow, CameraWindow &toWindow)
{
    const ia_coordinate_system androidCoord = {-1000, -1000, 1000, 1000};
    const ia_coordinate_system iaCoord = {IA_COORDINATE_TOP, IA_COORDINATE_LEFT, IA_COORDINATE_BOTTOM, IA_COORDINATE_RIGHT};
    ia_coordinate topleft = {0, 0};
    ia_coordinate bottomright = {0, 0};

    topleft.x = srcWindow.x_left;
    topleft.y = srcWindow.y_top;
    bottomright.x = srcWindow.x_right;
    bottomright.y = srcWindow.y_bottom;

    topleft = ia_coordinate_convert(&androidCoord,
                                    &iaCoord, topleft);
    bottomright = ia_coordinate_convert(&androidCoord,
                                        &iaCoord, bottomright);

    toWindow.x_left = topleft.x;
    toWindow.y_top = topleft.y;
    toWindow.x_right = bottomright.x;
    toWindow.y_bottom = bottomright.y;
}

/**
 * Mirror the buffer contents by flipping the data horizontally or
 * vertically based on the camera sensor orientation and device
 * orientation.
 */
void mirrorBuffer(AtomBuffer *buffer, int currentOrientation, int cameraOrientation)
{
    LOG1("@%s", __FUNCTION__);

    int rotation = (cameraOrientation - currentOrientation + 360) % 360;
    if (rotation == 90 || rotation == 270) {
        flipBufferH(buffer);
    } else {
        flipBufferV(buffer);
    }
}

void flipBufferV(AtomBuffer *buffer) {
    LOG1("@%s", __FUNCTION__);
    int width, height, stride;
    unsigned char *data = NULL;

    void *ptr = NULL;
    if (buffer->shared)
        ptr = (void *) *((char **)buffer->dataPtr);
    else
        ptr = buffer->dataPtr;

    data = (unsigned char *) ptr;
    width = buffer->width;
    height = buffer->height;
    stride = buffer->stride;
    int w = width / 2;
    unsigned char temp = 0;

    // Y
    for (int j=0; j < height; j++) {
        for (int i=0; i < w; i++) {
            temp = data[i];
            data[i] = data[width-i-1];
            data[width-i-1] = temp;
        }
        data = data + stride;
    }

    int h = height / 2;
    unsigned char tempu = 0;
    unsigned char tempv = 0;

    // U+V
    for (int j=0; j < h; j++) {
        for (int i=0; i < w; i+=2) {
            tempu = data[i];
            tempv = data[i+1];
            data[i] = data[width-i-2];
            data[i+1] = data[width-i-1];
            data[width-i-2] = tempu;
            data[width-i-1] = tempv;
        }
        data = data + stride;
    }
}

void flipBufferH(AtomBuffer *buffer) {
    LOG1("@%s", __FUNCTION__);
    int width, height, stride;
    unsigned char *data = NULL;

    void *ptr = NULL;
    if (buffer->shared)
        ptr = (void *) *((char **)buffer->dataPtr);
    else
        ptr = buffer->dataPtr;

    data = (unsigned char *) ptr;
    width = buffer->width;
    height = buffer->height;
    stride = buffer->stride;
    int h = height / 2;
    unsigned char temp = 0;

    // Y
    for (int j=0; j < width; j++) {
        for (int i=0; i < h; i++) {
            temp = data[i*stride + j];
            data[i*stride + j] = data[(height-1-i)*stride + j];
            data[(height-1-i)*stride + j] = temp;
        }
    }

    // U+V
    data = data + stride * height;
    h = height / 4;
    int heightUV = height / 2;

    for (int j=0; j < width; j++) {
        for (int i=0; i < h; i++) {
            temp = data[i*stride + j];
            data[i*stride + j] = data[(heightUV-1-i)*stride + j];
            data[(heightUV-1-i)*stride + j] = temp;
        }
    }
}

}
