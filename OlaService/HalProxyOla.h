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
       * Wrapper to standard accerletaion API for face acceleration.
       * In case of face acceleration, Camera HAL maintains acceleration
       * firmware and handle to the firmware when loaded. This function
       * reads the internally maintained firmware file and calls the
       * standard "open_firmware" API for loading the firmware to ISP.
       * The returned handle is maintained inside HAL.
       */
       int configLoadFirmware(void);

      /*
       * Wrapper to standard accerletaion API for face acceleration.
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
       ControlThread *mHAL;

};
} //namespace android
#endif /* HALPROXYOLA_H_ */
