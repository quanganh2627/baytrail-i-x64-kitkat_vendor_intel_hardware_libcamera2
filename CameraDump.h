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

#ifndef ANDROID_HARDWARE_CAMERA_DUMP_H
#define ANDROID_HARDWARE_CAMERA_DUMP_H

#include "AtomAAA.h"
#include "LogHelper.h"

namespace android {

    #define DUMPIMAGE_RECORD_PREVIEW_FILENAME    "record_v0.nv12"
    #define DUMPIMAGE_RECORD_STORE_FILENAME     "record_v1.nv12"
    #define DUMPIMAGE_PREVIEW_FILENAME          "preview.nv12"
    #define DUMPIMAGE_RAW_NONE_FILENAME         "raw.none"
    #define DUMPIMAGE_RAW_YUV_FILENAME          "raw.yuv"
    #define DUMPIMAGE_RAW_BAYER_FILENAME        "raw.bayer"

    #define DUMPIMAGE_RAWDPPATHSIZE    50
    #define DUMPIMAGE_SD_EXT_PATH      "/sdcard_ext/DCIM/100ANDRO/"
    #define DUMPIMAGE_SD_INT_PATH      "/sdcard/DCIM/100ANDRO/"
    #define DUMPIMAGE_MEM_INT_PATH     "/data/"

    enum err_wf_code{
        ERR_D2F_SUCESS = 0,
        ERR_D2F_NOPATH = 1,
        ERR_D2F_EVALUE = 2,
        ERR_D2F_NOMEM = 3,
        ERR_D2F_EOPEN = 4,
        ERR_D2F_EXIST = 5,
    };
    typedef enum {
        RAW_NONE = 0,
        RAW_YUV,
        RAW_BAYER,
        RAW_OVER,
    }raw_data_format_E;
    typedef struct camera_delay_dump {
        void * buffer_raw;
        int buffer_size;
        int width;
        int height;
    } camera_delay_dumpImage_T;

    class CameraDump {
    public:
        ~CameraDump();
        static CameraDump *getInstance() {
            if (sInstance == NULL) {
                sInstance = new CameraDump();
            }
            return sInstance;
        }
        static void setDumpDataFlag(void);
        static bool isDumpImageEnable(int dumpflag);
        static bool isDumpImageEnable(void) {
            bool ret = false;
            ret = (sRawDataFormat == RAW_BAYER) || (sRawDataFormat == RAW_YUV)
                   || sNeedDumpPreview || sNeedDumpVideo || sNeedDumpSnapshot;
            return ret;
        }
        static bool isDumpImage2FileFlush(void) {
            return sNeedDumpFlush;
        }
        int dumpImage2Buf(void *buffer, unsigned int size, unsigned int width, unsigned int height);
        int dumpImage2File(const void *data, const unsigned int size, unsigned int width,
                      unsigned int height, const char *filename);
        int dumpImage2FileFlush(bool bufflag = true);

    private:
        CameraDump();
        int getRawDataPath(char *ppath);
        void showMediaServerGroup(void);
        static CameraDump *sInstance;
        static raw_data_format_E sRawDataFormat;
        static bool sNeedDumpPreview;
        static bool sNeedDumpSnapshot;
        static bool sNeedDumpVideo;
        static bool sNeedDumpFlush;
        AtomAAA *mAAA;
        camera_delay_dumpImage_T mDelayDump;
    };// class CameraDump

}; // namespace android

#endif
