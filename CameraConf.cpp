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

const char *cpfPath = "/etc/atomisp/";  // Where CPF files are located
const int statCacheSize = 2;  // CPF files to be "cached" (can be zero)
static const char *sensorName = 0;  // Pointer to sensor name

const char *cpf::internal::constructFileName()
{
    static String8 cpfName;
    cpfName = cpfPath;

    // If there are spaces in driver name, only take the beginning
    String8 tmp(sensorName);
    for(int i = 0; (i = tmp.find(" ")) > 0; tmp.setTo(tmp, i));
    tmp = tmp + ".cpf";

    cpfName.appendPath(tmp);
    return cpfName;
}

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

void cpf::setSensorName(const char *ptr)
{
    sensorName = ptr;
}

status_t cpf::init(sp<CameraBlob>& aiqConf, sp<CameraBlob>& drvConf, sp<CameraBlob>& halConf)
{
    // In case the very same CPF configuration file has been verified
    // already earlier, checksum calculation will be skipped this time.
    // Files are identified by their stat structure. We need cache size
    // to be at least 2 in order to prevent checksum calculation
    // everytime the user switches between the front and back camera
    static struct stat statPrevious[statCacheSize];
    struct stat statCurrent;
    bool canSkipChecksum = false;

    sp<CameraBlob> allConf;
    status_t ret = 0;

    // First, we load the correct configuration file.
    // It will be behind reference counted MemoryHeapBase
    // object "allConf", meaning that the memory will be
    // automatically freed when it is no longer being pointed at
    if ((ret = internal::loadAll(allConf, statCurrent)))
        return ret;

    // See if we know the file already
    for (int i = statCacheSize - 1; i >= 0; i--) {
        if (!memcmp(&statPrevious[i], &statCurrent, sizeof(struct stat))) {
            canSkipChecksum = true;
            break;
        }
    }

    // Then, we will dig out component specific configuration
    // data from within "allConf". That will be placed behind
    // reference counting MemoryBase memory descriptors.
    // We only need to verify checksum once
    if ((ret = internal::initAiq(allConf, aiqConf, canSkipChecksum)))
        return ret;
    if ((ret = internal::initDrv(allConf, drvConf, true)))
        return ret;
    if ((ret = internal::initHal(allConf, halConf, true)))
        return ret;

    // If we are here, the file was ok. If it wasn't cached already,
    // then do so now (adding to end of cache, removing from beginning)
    if (!canSkipChecksum) {
        for (int i = statCacheSize - 1; i > 0; i--) {
            statPrevious[i-1] = statPrevious[i];
        }
        if (statCacheSize > 0) {
            statPrevious[statCacheSize-1] = statCurrent;
        }
    }

    return ret;
}

status_t cpf::internal::loadAll(sp<CameraBlob>& allConf, struct stat& statCurrent)
{
    FILE *file;
    const char *fileName = internal::constructFileName();
    status_t ret = 0;

    do {
        file = fopen(fileName, "rb");
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

            // We use file statistics for file identification purposes.
            // The access time was just changed (because of us!),
            // so let's nullify the access time info
            if (stat(fileName, &statCurrent) < 0) {
                LOGE("ERROR querying filestat of CPF file: %s", strerror(errno));
                ret = UNKNOWN_ERROR;
                break;
            }
            statCurrent.st_atime = 0;
            statCurrent.st_atime_nsec = 0;

        } while (0);
        fclose(file);

    } while (0);
    return ret;

}

status_t cpf::internal::initAiq(const sp<CameraBlob>& allConf, sp<CameraBlob>& aiqConf, bool skipChecksum)
{
    status_t ret = 0;

    if (allConf == 0) {
        // This should never happen; CPF file has not been loaded properly
        LOGE("ERROR using null pointer in CPF");
        return NO_MEMORY;
    }

    if ((skipChecksum) || (tbd_err_none == tbd_validate(allConf->getPtr(), tbd_tag_cpff, allConf->getSize()))) {
        // FIXME: Once we only accept one kind of CPF files, the content
        // is either ok or not - no need for kludge checks and jumps like this
        if (skipChecksum && (*(uint32_t *)(allConf->getPtr()) == tbd_tag_aiqb)) goto fixme;
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
    // FIXME: The tbd_tag_aiqb is enabled for R&D, but should lead to an error below
    } else if (tbd_err_none == tbd_validate(allConf->getPtr(), tbd_tag_aiqb, allConf->getSize())) {
fixme:  // Looks like we have valid AIQ file
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

status_t cpf::internal::initDrv(const sp<CameraBlob>& allConf, sp<CameraBlob>& drvConf, bool skipChecksum)
{
    return 0;
}

status_t cpf::internal::initHal(const sp<CameraBlob>& allConf, sp<CameraBlob>& halConf, bool skipChecksum)
{
    return 0;
}

} // namespace android
