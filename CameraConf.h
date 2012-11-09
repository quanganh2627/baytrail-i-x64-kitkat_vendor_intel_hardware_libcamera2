/*
 * Copyright 2012 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_LIBCAMERA_CAMERA_CONFIGURATION_H
#define ANDROID_LIBCAMERA_CAMERA_CONFIGURATION_H

#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <sys/stat.h>

namespace android {

class CameraBlob : public RefBase
{
public:
    CameraBlob(const int size);
    CameraBlob(const sp<CameraBlob>& ref, const int offset, const int size);
    CameraBlob(const sp<CameraBlob>& ref, void * const ptr, const int size);
    virtual ~CameraBlob();
    inline int getSize() const { return this == 0 ? 0 : mSize; }
    inline void *getPtr() const { return this == 0 ? 0 : mPtr; }
private:
    void *mPtr;
    int mSize;
    sp<CameraBlob> mRef;
    // Disallow copy and assignment
    CameraBlob(const CameraBlob&);
    void operator=(const CameraBlob&);
};

namespace cpf {

    void setSensorName(const char *ptr);
    status_t init(sp<CameraBlob>& aiqConf, sp<CameraBlob>& drvConf, sp<CameraBlob>& halConf);

namespace internal {

    status_t loadAll(sp<CameraBlob>& allConf, struct stat& statCurrent);
    status_t initAiq(const sp<CameraBlob>& allConf, sp<CameraBlob>& aiqConf, bool skipChecksum);
    status_t initDrv(const sp<CameraBlob>& allConf, sp<CameraBlob>& drvConf, bool skipChecksum);
    status_t initHal(const sp<CameraBlob>& allConf, sp<CameraBlob>& halConf, bool skipChecksum);
    const char *constructFileName();

}; // namespace internal

}; // namespace cpf

}; // namespace android

#endif // ANDROID_LIBCAMERA_CAMERA_CONFIGURATION_H
