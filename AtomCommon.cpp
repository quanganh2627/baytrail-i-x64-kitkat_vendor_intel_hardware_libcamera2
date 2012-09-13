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
                                               void *gfxData,
                                               buffer_handle_t *mNativeBufPtr) {
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
    buf.shared = shared;
    buf.capture_timestamp = capture_timestamp;
    buf.gfxData = gfxData;
    buf.mNativeBufPtr = mNativeBufPtr;
    return buf;
}

}
