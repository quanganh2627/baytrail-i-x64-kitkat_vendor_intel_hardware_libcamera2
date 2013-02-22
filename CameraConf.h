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

#include <libtbd.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/String8.h>
#include <utils/Vector.h>
#include <sys/stat.h>

namespace android {

class CameraConf;

class CameraBlob : public RefBase
{
public:
    CameraBlob(const int size);
    CameraBlob(const sp<CameraBlob>& ref, const int offset, const int size);
    CameraBlob(const sp<CameraBlob>& ref, void * const ptr, const int size);
    virtual ~CameraBlob();
    inline int size() const { return this == 0 ? 0 : mSize; }
    inline void *ptr() const { return this == 0 ? 0 : mPtr; }
private:
    void *mPtr;
    int mSize;
    sp<CameraBlob> mRef;
    // Disallow copy and assignment
    CameraBlob(const CameraBlob&);
    void operator=(const CameraBlob&);
};

class CpfStore
{
struct SensorDriver
{
    String8 mSensorName;
    String8 mSysfsName;
};

public:
    explicit CpfStore(const int cameraId);
    virtual ~CpfStore();
    const sp<CameraConf> createCameraConf();
private:
    status_t initNames(String8& cpfName, String8& sysfsName);
    status_t initNamesHelper(const String8& filename, String8& refName, int& index);
    status_t initDriverList();
    status_t initDriverListHelper(int major, int minor, SensorDriver& drvInfo);
    status_t initConf(sp<CameraBlob>& aiqConf, sp<CameraBlob>& drvConf, sp<CameraBlob>& halConf);
    status_t loadConf(sp<CameraBlob>& allConf);
    status_t validateConf(const sp<CameraBlob>& allConf, const struct stat& statCurrent);
    status_t fetchConf(const sp<CameraBlob>& allConf, sp<CameraBlob>& recConf, tbd_class_t recordClass, const char *blockDebugName);
    status_t processDrvConf();
    status_t processHalConf();
private:
    int mCameraId;
    bool mIsOldConfig;
    String8 mCpfPathName;
    String8 mSysfsPathName;
    sp<CameraBlob> mAiqConf, mDrvConf, mHalConf;
    static Vector<struct SensorDriver> registeredDrivers;
    static Vector<struct stat> validatedCpfFiles;
    // Disallow copy and assignment
    CpfStore(const CpfStore&);
    void operator=(const CpfStore&);
};

class CameraConf : public RefBase
{
public:
    friend class CpfStore;
    virtual ~CameraConf() {}
    inline int cameraId() { return mCameraId; }
    inline int cameraFacing() { return mCameraFacing; }
    inline int cameraOrientation() { return mCameraOrientation; }
public:
    sp<CameraBlob> aiqConf;
protected:
    CameraConf() {}
private:
    int mCameraId;
    int mCameraFacing;
    int mCameraOrientation;
    // Disallow copy and assignment
    CameraConf(const CameraConf&);
    void operator=(const CameraConf&);
};

}; // namespace android

#endif // ANDROID_LIBCAMERA_CAMERA_CONFIGURATION_H
