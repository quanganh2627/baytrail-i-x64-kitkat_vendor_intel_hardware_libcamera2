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
#define LOG_TAG "Camera_Conf"

#include "CameraConf.h"
#include "LogHelper.h"
#include "libtbd.h"
#include <utils/Errors.h>
#include <stdio.h>
#include <errno.h>

namespace android {

CameraBlob::CameraBlob(const int size)
{
    mSize = 0;
    if (size == 0) {
        mPtr = 0;
        LOGE("ERROR zero memory allocation!");
        return;
    }
    mPtr = malloc(size);
    if (!mPtr) {
        LOGE("ERROR memory allocation failure!");
        return;
    }
    mSize = size;
}

CameraBlob::CameraBlob(const sp<CameraBlob>& ref, const int offset, const int size)
{
    mSize = 0;
    mPtr = 0;
    if (ref == 0) {
        LOGE("ERROR referring to null object!");
        return;
    }
    // Must refer only to memory allocated by reference object
    if (ref->getSize() < offset + size) {
        LOGE("ERROR illegal allocation!");
        return;
    }
    mRef = ref;
    mSize = size;
    mPtr = (char *)(ref->getPtr()) + offset;
}

CameraBlob::CameraBlob(const sp<CameraBlob>& ref, void * const ptr, const int size)
{
    mSize = 0;
    mPtr = 0;
    if (ref == 0) {
        LOGE("ERROR referring to null object!");
        return;
    }
    // Must refer only to memory allocated by reference object
    int offset = (char *)(ptr) - (char *)(ref->getPtr());
    if ((offset < 0) || (offset + size > ref->getSize())) {
        LOGE("ERROR illegal allocation!");
        return;
    }
    mRef = ref;
    mSize = size;
    mPtr = ptr;
}

CameraBlob::~CameraBlob()
{
    if ((mRef == 0) && (mPtr)) {
        free(mPtr);
    }
}

status_t cpf::init(sp<CameraBlob>& aiqConf, sp<CameraBlob>& drvConf, sp<CameraBlob>& halConf)
{
    status_t ret = 0;
    sp<CameraBlob> allConf;

    // First, we load the correct configuration file.
    // It will be behind reference counted MemoryHeapBase
    // object "allConf", meaning that the memory will be
    // automatically freed when it is no longer being pointed at
    if ((ret = internal::loadAll(allConf)))
        return ret;

    // Then, we will dig out component specific configuration
    // data from within "allConf". That will be placed behind
    // reference counting MemoryBase memory descriptors
    if ((ret = internal::initAiq(allConf, aiqConf)))
        return ret;

    return ret;
}

status_t cpf::internal::loadAll(sp<CameraBlob>& allConf)
{
    // FIXME: Get the file name e.g. from PlatformData
    const char *filename = "/system/lib/file.cpf";
    FILE *file;
    status_t ret = 0;

    do {
        file = fopen(filename, "rb");
        if (!file) {
            LOGE("ERROR in opening CPF file: %s", strerror(errno));
            ret = NAME_NOT_FOUND;
            break;
        }

        do {
            int fileSize;
            if ((fseek(file, 0, SEEK_END) < 0) ||
                ((fileSize = ftell(file)) < 0) ||
                (fseek(file, 0, SEEK_SET) < 0)) {
                LOGE("ERROR querying properties of CPF file: %s", strerror(errno));
                ret = UNKNOWN_ERROR;
                break;
            }

            allConf = new CameraBlob(fileSize);
            if ((allConf == 0) || (allConf->getSize() == 0)) {
                LOGE("ERROR no memory in %s",__func__);
                ret = NO_MEMORY;
                break;
            }

            if (fread(allConf->getPtr(), fileSize, 1, file) < 1) {
                LOGE("ERROR reading CPF file: %s", strerror(errno));
                ret = UNKNOWN_ERROR;
                break;
            }

        } while (0);
        fclose(file);

    } while (0);
    return ret;

}

status_t cpf::internal::initAiq(const sp<CameraBlob>& allConf, sp<CameraBlob>& aiqConf)
{
    status_t ret = 0;

    if (allConf == 0) {
        // This should never happen; CPF file has not been loaded properly
        LOGE("ERROR using null pointer in CPF");
        return NO_MEMORY;
    }

    if (tbd_err_none == tbd_validate(allConf->getPtr(), tbd_tag_cpff, allConf->getSize())) {
        // Looks like we have valid CPF file,
        // let's look for AIQ record
        void *data;
        int size;
        if (tbd_err_none == tbd_get_record(allConf->getPtr(), tbd_class_aiq, tbd_format_any, &data, &size)) {
            aiqConf = new CameraBlob(allConf, data, size);
            if (aiqConf == 0) {
                LOGE("ERROR no memory in %s",__func__);
                ret = NO_MEMORY;
            }
        } else  {
            // Error, looks like we didn't have AIQ record in CPF file
            LOGE("ERROR incomplete CPF file");
            ret = BAD_VALUE;
        }
    } else if (tbd_err_none == tbd_validate(allConf->getPtr(), tbd_tag_aiqb, allConf->getSize())) {
        // Looks like we have valid AIQ file
        // (FIXME: Enabled for R&D, but should lead to an error below)
        aiqConf = new CameraBlob(allConf, 0, allConf->getSize());
        if (aiqConf == 0) {
            LOGE("ERROR no memory in %s",__func__);
            ret = NO_MEMORY;
        }
    } else {
        // Error, looks like we had unknown file
        LOGE("ERROR corrupted CPF file");
        ret = BAD_VALUE;
    }

    return ret;
}

} // namespace android
