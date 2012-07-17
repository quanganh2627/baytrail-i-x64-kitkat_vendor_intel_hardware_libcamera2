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

#ifndef HALPROXYOLA_H_
#define HALPROXYOLA_H_

#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <utils/threads.h>
#include "LogHelper.h"

namespace android {

#define FACE_ACCELERATION_FIRMWARE "system/etc/firmware/fa_extension.bin"

class ControlThread;
/**
 * \class HalProxyOla
 *
 * This class is used by the OlaBufferService to interact with
 * the camera HAL. Its main purpose is to isolate the camera HAL from
 * the OLABuffer service code.
 */
class HalProxyOla: public RefBase{
public:
    HalProxyOla(ControlThread *aControlThread);
    virtual ~HalProxyOla();

    void copyPreview(void* src, int width, int height);


      /*
       * Wrapper to standard acceleration API for face acceleration.
       * In case of face acceleration, Camera HAL maintains acceleration
       * firmware and handle to the firmware when loaded. This function
       * reads the internally maintained firmware file and calls the
       * standard "open_firmware" API for loading the firmware to ISP.
       * The returned handle is maintained inside HAL.
       */
       int configLoadFirmware(void);

      /*
       * Wrapper to standard acceleration API for face acceleration.
       * This function triggers unloading firmware from ISP and uses the
       * handle stored at the time loading the firmware. This function
       * calls the standard "closeFirmware" API for unloading the firmware.
       */
       void configUnloadFirmware(void);

      /*
       * Wrapper to standard acceleration API for face acceleration.
       * Sets the arguments for the acceleration firmware. Calls standard'
       * acceleration API setFirmwareArg with the face acceleration handle.
       */
       int configSetArgFirmware(const unsigned int arg_ID, const void *arg,
                              const size_t size);

      /*
       * Wrapper to standard acceleration API for face acceleration.
       * Flushes the argument for the acceleration firmware. Calls standard'
       * acceleration API destabilizeFirmwareArg with the face acceleration handle.
       */
       int configDestabilizeArgFirmware(const unsigned int arg_ID);
private:
       /*
        * Opens the face acceleration firmware file and loads the firmware
        * to the memory. Used only for the face acceleration support.
        * @param fw_name [IN]pointer to the name of the firmware file
        * @param size [OUT] reference to an int where to store the size of the firmware file
        * @return pointer to the malloc'ed memory where the firmware is store
        *         returns NULL if it could not allocate
        */
       void *host_load_firmware (const char *fw_name, unsigned int& size);

       int  configRegisterFirmware( const char *fw_name);

      /*
       * Loads the acceleration firmware to ISP.
       * Expects the fwData to be following the "atomisp_acc_fw" structure.
       * Driver fills "fw_handle" and HAL copies this fw_handle and
       * returns it. HAL does not store that value. Application needs to
       * maintain that handle in order to identify a firmware loaded until
       * the firmware is unloaded.
       */
       virtual status_t load_firmware(void *fwData, size_t size,
                                      unsigned int *fwHandle);

      /*
       * Triggers unloading the acceleration firmware from the ISP.
       * Acceleration firmware to be unloaded is identified by the fwHandle.
       * As Camera HAL do not maintain any handles, it do not check the
       * authenticity of the firmware handle.
       */
       status_t unload_firmware(unsigned int fwHandle);

private:
       ControlThread *mHAL;

       void * mFaAccFirmware;   /**< Pointer to the face acceleration firmware binary in host memory */

      /*
       * Handle to the face acceleration firmware loaded to the ISP.
       * Handle value ranges from 0 to Max value. 0 is a valid handle.
       */
       unsigned int mFaAccFirmwareHandle;

      /*
       * Size of the face acceleration firmware. This information is
       * needed for the firmware loading. This is the size of the firmware
       * file.
       */
       size_t mFaAccFirmwareSize;

};
} //namespace android
#endif /* HALPROXYOLA_H_ */
