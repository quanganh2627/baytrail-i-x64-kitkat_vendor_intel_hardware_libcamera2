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

#include "OlaService/HalProxyOla.h"
#include "OlaService/OlaBufferService.h"
#include "ControlThread.h"

#define LOG_TAG "Camera_HalProxyOla"

#define OLABUFFER_STATUS_BEFORECOPY (0)
#define OLABUFFER_STATUS_DIDCOPY    (1)
#define OLABUFFER_STATUS_DIDPROCESS (2)

typedef struct {
    int progressStatus;
    int previewWidth;
    int previewHeight;
} OlaBufferInfo;

namespace android {
static char* g_imageBuffer;

HalProxyOla::HalProxyOla(ControlThread *aControlThread):
    mFaAccFirmware(NULL),
    mFaAccFirmwareHandle(0),
    mFaAccFirmwareSize(0) {

    LOG1("@%s: ", __func__);
    mHAL = aControlThread;
    Ola_BufferService_Initiate();
    g_imageBuffer = Ola_BufferService_GetBufferMemPointer();
    gHAL = this;
    LOG1("@%s: got share mem %p", __func__, g_imageBuffer);

}

HalProxyOla::~HalProxyOla() {

    LOG1("@%s: ", __func__);
    //Clear the Face acceleration firmware
    if (mFaAccFirmware) {
       configUnloadFirmware();
       free(mFaAccFirmware);
    }

    Ola_BufferService_DeInitialize();
    gHAL = NULL;
}

void HalProxyOla::copyPreview(void* src, int width, int height) {
    LOG2("%s: g_imageBuffer = %p", __FUNCTION__, g_imageBuffer);
    if( g_imageBuffer ) {
        LOG2("%s: frame ptr %p dimension (%dx%d)", __func__, src, width, height);
        OlaBufferInfo * info = (OlaBufferInfo*) g_imageBuffer;
        char* pData = (char*)g_imageBuffer + sizeof(OlaBufferInfo);
        if( info->progressStatus == OLABUFFER_STATUS_DIDPROCESS || info->progressStatus == 0 ) {
            int previewSize = width * height * 3 /2;
            info->previewWidth = width;
            info->previewHeight = height;
            memcpy( pData , src, previewSize);
            info->progressStatus = OLABUFFER_STATUS_DIDCOPY;
        }
    } else {
        LOGD("[%s] getting OlaBuffer", __FUNCTION__);
        g_imageBuffer = Ola_BufferService_GetBufferMemPointer();
        LOG1("%s: got ptr %p ", __FUNCTION__, g_imageBuffer);
    }
}

int HalProxyOla::configLoadFirmware(void)
{
    LOG1("%s \n", __FUNCTION__);
    int ret = 0;

    //If the face acceleration is loaded already, return the client
    //with an error.
    if ( mFaAccFirmware ){
        LOGD("%s Firmware already Loaded.. 0x%p",
                    __func__ ,mFaAccFirmware);
        return -EINPROGRESS;
    }

    // first read the firmware file from to memory.
    ret = configRegisterFirmware(FACE_ACCELERATION_FIRMWARE);
    LOG1("%s configRegisterFirmware ret: %d\n", __func__, ret);

    //After reading the file, load it to ISP
    if ( !ret ){
        ret = load_firmware(mFaAccFirmware,
                            mFaAccFirmwareSize,
                            &mFaAccFirmwareHandle);
        LOG1("%s configRegisterFirmware ret: %d handle: %d\n", __func__, ret, mFaAccFirmwareHandle);
    }

    return ret;
}

void HalProxyOla::configUnloadFirmware(void)
{
    LOG1("%s \n", __FUNCTION__);
    int ret = unload_firmware(mFaAccFirmwareHandle);
    LOG1("%s ret : %d\n", __func__, ret);
    mFaAccFirmwareHandle = 0;
    if (mFaAccFirmware)
        free(mFaAccFirmware);
    mFaAccFirmware = NULL;
    mFaAccFirmwareSize = 0;
}

int HalProxyOla::configSetArgFirmware(const unsigned int arg_ID, const void *arg,
                                      const size_t size)
{
    LOG1("%s arg ID: %d arg: 0x%x size: %d",__FUNCTION__, arg_ID,
                           (unsigned int)arg, size );

    ControlThread::Message msg;
    msg.id = ControlThread::MESSAGE_ID_SET_FIRMWARE_ARGUMENT;
    msg.data.setFwArg.argIndex = arg_ID;
    msg.data.setFwArg.value = const_cast<void*> (arg);
    msg.data.setFwArg.size = size;
    msg.data.setFwArg.fwHandle = mFaAccFirmwareHandle;

    return mHAL->mMessageQueue.send(&msg, ControlThread::MESSAGE_ID_SET_FIRMWARE_ARGUMENT);
}

int HalProxyOla::configDestabilizeArgFirmware(const unsigned int arg_ID)
{
   LOG1("%s arg ID: %d",__FUNCTION__, arg_ID);
   ControlThread::Message msg;
   msg.id = ControlThread::MESSAGE_ID_UNSET_FIRMWARE_ARGUMENT;
   msg.data.setFwArg.argIndex = arg_ID;
   msg.data.setFwArg.fwHandle = mFaAccFirmwareHandle;

   return mHAL->mMessageQueue.send(&msg);
}
/*============================ Helper methods ===============================*/

status_t HalProxyOla::load_firmware(void *fwData, size_t size,
                                    unsigned int *fwHandle)
{
    LOG1("%s \n", __func__);

    ControlThread::Message msg;
    msg.id = ControlThread::MESSAGE_ID_LOAD_FIRMWARE;
    msg.data.loadFW.fwData = fwData;
    msg.data.loadFW.size = size;
    msg.data.loadFW.fwHandle = fwHandle;

    return mHAL->mMessageQueue.send(&msg, ControlThread::MESSAGE_ID_LOAD_FIRMWARE);
}

status_t HalProxyOla::unload_firmware(unsigned int fwHandle)
{
    LOG1("%s \n", __func__);

    ControlThread::Message msg;
    msg.id = ControlThread::MESSAGE_ID_UNLOAD_FIRMWARE;
    msg.data.unloadFW.fwHandle = fwHandle;

    return mHAL->mMessageQueue.send(&msg);
}
/*
 * Reads the firmware file and loads it to the memory
 */
int HalProxyOla::configRegisterFirmware( const char *fw_name)
{
    //Read the firmware file to mFaAccFirmware
    LOG1("%s fw_name: %s\n", __func__, fw_name);
    mFaAccFirmware = host_load_firmware( fw_name, mFaAccFirmwareSize);
    LOG2("%s mFaAccFirmware: 0x%x maFirmwareSize :%d \n",
            __func__, (unsigned int)mFaAccFirmware, mFaAccFirmwareSize);
    return (mFaAccFirmware ? 0 : -1);
}


void * HalProxyOla::host_load_firmware (const char *fw_name,
                                        unsigned int& size) {
    char *filename = (char*)fw_name;
    FILE *file;
    unsigned len = 0;
    unsigned err;
    void *fw;

    if (!fw_name) return NULL;

    file = fopen(filename, "rb");
    if (!file)
        return NULL;

    err = fseek (file, 0, SEEK_END);
    if (err)
        return NULL;
    len = ftell(file);

    err = fseek (file, 0, SEEK_SET);
    if (err)
        return NULL;

    fw = malloc (len);
    if (!fw)
        return NULL;

    err = fread (fw, 1, len, file);
    fclose(file);
    LOG2("%s err : %d len: %d\n", __func__, err, len );
    if (err != len)
        return NULL;

    size = len;
    return fw;
}
}// namespace android
