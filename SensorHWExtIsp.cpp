/*
 * Copyright (c) 2014 Intel Corporation.
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

#define LOG_TAG "Camera_SensorHWExtIsp"

#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <assert.h>
#include "AtomCommon.h"
#include "v4l2device.h"
#include "PerformanceTraces.h"
#include "SensorHWExtIsp.h"

namespace android {

SensorHWExtIsp::SensorHWExtIsp(int cameraId)
    :SensorHW(cameraId)
{

}

SensorHWExtIsp::~SensorHWExtIsp()
{
    // Base class handles destruction
}

int SensorHWExtIsp::setAfMode(int mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    // Fixed focus camera -> no effect
    if (PlatformData::isFixedFocusCamera(mCameraId))
        return -1;

    int ret = -1;
    // For external ISP, use the extended ioctl() framework
    struct atomisp_ext_isp_ctrl cmd;
    cmd.id = EXT_ISP_FOCUS_MODE_CTRL;
    cmd.data = mode;

    ret = mDevice->xioctl(ATOMISP_IOC_EXT_ISP_CTRL, &cmd);

    return ret;

}

int SensorHWExtIsp::getAfMode(int *mode)
{
    LOG2("@%s", __FUNCTION__);

    if (PlatformData::isFixedFocusCamera(mCameraId))
        return -1;

    struct atomisp_ext_isp_ctrl cmd;
    cmd.id = EXT_ISP_GET_AF_MODE_CTRL;

    int retval = mDevice->xioctl(ATOMISP_IOC_EXT_ISP_CTRL, &cmd);
    *mode = (int)cmd.data;

    return retval;
}

int SensorHWExtIsp::setAfEnabled(bool enable)
{
    LOG2("@%s: en: %d ", __FUNCTION__, enable);

    if (PlatformData::isFixedFocusCamera(mCameraId))
        return -1;

    // start running the AF
    struct atomisp_ext_isp_ctrl cmd;
    cmd.id = EXT_ISP_FOCUS_EXECUTION_CTRL;
    cmd.data = enable ? EXT_ISP_FOCUS_SEARCH : EXT_ISP_FOCUS_STOP;

    int retval = mDevice->xioctl(ATOMISP_IOC_EXT_ISP_CTRL, &cmd);
    return retval;
}

int SensorHWExtIsp::setAfWindows(const CameraWindow *windows, int numWindows)
{
    LOG2("@%s", __FUNCTION__);

    status_t retX = -1, retY = -1;

    if (PlatformData::isFixedFocusCamera(mCameraId))
        return -1;

    if (windows == NULL || numWindows <= 0) {
        return NO_ERROR;
    }

    // TODO: Support multiple windows?

    struct atomisp_ext_isp_ctrl cmd;
    cmd.id = EXT_ISP_TOUCH_POSX_CTRL;
    cmd.data = windows[0].x_left;

    retX = mDevice->xioctl(ATOMISP_IOC_EXT_ISP_CTRL, &cmd);

    cmd.id = EXT_ISP_TOUCH_POSY_CTRL;
    cmd.data = windows[0].y_top;

    retY = mDevice->xioctl(ATOMISP_IOC_EXT_ISP_CTRL, &cmd);

    if (retX != NO_ERROR || retY != NO_ERROR) {
        LOGW("Failed setting AF windows, retvals %d, %d", retX, retY);
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

} // namespace android