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

#ifndef CAMHEAPMEM_H_
#define CAMHEAPMEM_H_

#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>

namespace android {

/**
 * Have to copy this private class here because we want to access the MemoryBase object
 * which is hiding in the handle of camera_memory_t
 * It's for camera recording to share MemoryHeap buffer and zero-copy preview
 * callbacks.
 */
class CameraHeapMemory : public RefBase {
public:
    CameraHeapMemory(int fd, size_t buf_size, uint_t num_buffers) :
        mBufSize(buf_size),
        mNumBufs(num_buffers) {}

    CameraHeapMemory(size_t buf_size, uint_t num_buffers) :
        mBufSize(buf_size),
        mNumBufs(num_buffers) {}

    void commonInitialization() {
        handle.data = mHeap->base();
        handle.size = mBufSize * mNumBufs;
        handle.handle = this;

        mBuffers = new sp<MemoryBase>[mNumBufs];
        for (uint_t i = 0; i < mNumBufs; i++)
            mBuffers[i] = new MemoryBase(mHeap,
                                         i * mBufSize,
                                         mBufSize);
    }

    virtual ~CameraHeapMemory() {}

    size_t mBufSize;
    uint_t mNumBufs;
    sp<MemoryHeapBase> mHeap;
    sp<MemoryBase> *mBuffers;
    camera_memory_t handle;
};

} // namespace android

#endif /* CAMHEAPMEM_H_ */
