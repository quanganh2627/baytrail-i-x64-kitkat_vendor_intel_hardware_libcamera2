/*
 * Copyright (C) 2013 The Android Open Source Project
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
#define LOG_TAG "Camera_SensorHW"

#include "SensorHW.h"
#include "v4l2device.h"
#include "PerformanceTraces.h"

namespace android {

static sensorPrivateData gSensorDataCache[MAX_CAMERAS];

int SensorHW::getCurrentCameraId(void)
{
    return mCameraId;
}

size_t SensorHW::enumerateInputs(Vector<struct cameraInfo> &inputs)
{
    LOG1("@%s", __FUNCTION__);
    status_t ret;
    size_t numCameras = 0;
    struct v4l2_input input;
    struct cameraInfo sCamInfo;

    for (int i = 0; i < PlatformData::numberOfCameras(); i++) {
        memset(&input, 0, sizeof(input));
        memset(&sCamInfo, 0, sizeof(sCamInfo));
        input.index = i;
        ret = mDevice->enumerateInputs(&input);
        if (ret != NO_ERROR) {
            if (ret == INVALID_OPERATION || ret == BAD_INDEX)
                break;
            LOGE("Device input enumeration failed for sensor input %d", i);
        } else {
            sCamInfo.index = i;
            strncpy(sCamInfo.name, (const char *)input.name, sizeof(sCamInfo.name)-1);
            LOG1("Detected sensor \"%s\"", sCamInfo.name);
        }
        inputs.push(sCamInfo);
        numCameras++;
    }
    return numCameras;
}

status_t SensorHW::selectActiveSensor(sp<V4L2VideoNode> &device)
{
    LOG1("@%s", __FUNCTION__);
    mDevice = device;
    status_t ret = NO_ERROR;
    Vector<struct cameraInfo> camInfo;
    size_t numCameras = enumerateInputs(camInfo);

    if (numCameras < (size_t) PlatformData::numberOfCameras()) {
        LOGE("Number of detected sensors not matching static Platform data!");
    }

    if (numCameras < 1) {
        LOGE("No detected sensors!");
        return UNKNOWN_ERROR;
    }

    // Static mapping of v4l2_input.index to android camera id
    if (numCameras == 1) {
        mCameraInput = camInfo[0];
    } else if (PlatformData::cameraFacing(mCameraId) == CAMERA_FACING_BACK) {
        mCameraInput = camInfo[0];
    } else if (PlatformData::cameraFacing(mCameraId) == CAMERA_FACING_FRONT) {
        mCameraInput = camInfo[1];
    }

    // Choose the camera sensor
    LOG1("Selecting camera sensor: %s", mCameraInput.name);
    ret = mDevice->setInput(mCameraInput.index);
    if (ret != NO_ERROR) {
        ret = UNKNOWN_ERROR;
    } else {
        PERFORMANCE_TRACES_BREAKDOWN_STEP("capture_s_input");
        // Query now the supported pixel formats
        Vector<v4l2_fmtdesc> formats;
        ret = mDevice->queryCapturePixelFormats(formats);
        if (ret != NO_ERROR) {
            LOGW("Cold not query capture formats from sensor: %s", mCameraInput.name);
            ret = NO_ERROR;   // This is not critical
        }
        sensorStoreRawFormat(formats);
    }

    return ret;
}

/**
 * Helper method for the sensor to select the prefered BAYER format
 * the supported pixel formats are retrieved when the sensor is selected.
 *
 * This helper method finds the first Bayer format and saves it to mRawBayerFormat
 * so that if raw dump feature is enabled we know what is the sensor
 * preferred format.
 *
 * TODO: sanity check, who needs a preferred format
 */
status_t SensorHW::sensorStoreRawFormat(Vector<v4l2_fmtdesc> &formats)
{
    LOG1("@%s", __FUNCTION__);
    Vector<v4l2_fmtdesc>::iterator it = formats.begin();

    for (;it != formats.end(); ++it) {
        /* store it only if is one of the Bayer formats */
        if (isBayerFormat(it->pixelformat)) {
            mRawBayerFormat = it->pixelformat;
            break;  //we take the first one, sensors tend to support only one
        }
    }
    return NO_ERROR;
}

void SensorHW::getMotorData(sensorPrivateData *sensor_data)
{
    LOG2("@%s", __FUNCTION__);
    int rc;
    struct v4l2_private_int_data motorPrivateData;

    motorPrivateData.size = 0;
    motorPrivateData.data = NULL;
    motorPrivateData.reserved[0] = 0;
    motorPrivateData.reserved[1] = 0;

    sensor_data->data = NULL;
    sensor_data->size = 0;
    // First call with size = 0 will return motor private data size.
    rc = mDevice->xioctl(ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA, &motorPrivateData);
    LOG2("%s IOCTL ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA to get motor private data size ret: %d\n", __FUNCTION__, rc);
    if (rc != 0 || motorPrivateData.size == 0) {
        LOGD("Failed to get motor private data size. Error: %d", rc);
        return;
    }

    motorPrivateData.data = malloc(motorPrivateData.size);
    if (motorPrivateData.data == NULL) {
        LOGD("Failed to allocate memory for motor private data.");
        return;
    }

    // Second call with correct size will return motor private data.
    rc = mDevice->xioctl(ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA, &motorPrivateData);
    LOG2("%s IOCTL ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA to get motor private data ret: %d\n", __FUNCTION__, rc);

    if (rc != 0 || motorPrivateData.size == 0) {
        LOGD("Failed to read motor private data. Error: %d", rc);
        free(motorPrivateData.data);
        return;
    }

    sensor_data->data = motorPrivateData.data;
    sensor_data->size = motorPrivateData.size;
    sensor_data->fetched = true;
}

void SensorHW::getSensorData(sensorPrivateData *sensor_data)
{
    LOG2("@%s", __FUNCTION__);
    int rc;
    struct v4l2_private_int_data otpdata;
    int cameraId = getCurrentCameraId();

    sensor_data->data = NULL;
    sensor_data->size = 0;

    if ((gControlLevel & CAMERA_DISABLE_FRONT_NVM) || (gControlLevel & CAMERA_DISABLE_BACK_NVM)) {
        LOG1("NVM data reading disabled");
        sensor_data->fetched = false;
    }
    else {
        otpdata.size = 0;
        otpdata.data = NULL;
        otpdata.reserved[0] = 0;
        otpdata.reserved[1] = 0;

        if (gSensorDataCache[cameraId].fetched) {
            sensor_data->data = gSensorDataCache[cameraId].data;
            sensor_data->size = gSensorDataCache[cameraId].size;
            return;
        }
        // First call with size = 0 will return OTP data size.
        rc = mDevice->xioctl(ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA, &otpdata);
        LOG2("%s IOCTL ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA to get OTP data size ret: %d\n", __FUNCTION__, rc);
        if (rc != 0 || otpdata.size == 0) {
            LOGD("Failed to get OTP size. Error: %d", rc);
            return;
        }

        otpdata.data = calloc(otpdata.size, 1);
        if (otpdata.data == NULL) {
            LOGD("Failed to allocate memory for OTP data.");
            return;
        }

        // Second call with correct size will return OTP data.
        rc = mDevice->xioctl(ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA, &otpdata);
        LOG2("%s IOCTL ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA to get OTP data ret: %d\n", __FUNCTION__, rc);

        if (rc != 0 || otpdata.size == 0) {
            LOGD("Failed to read OTP data. Error: %d", rc);
            free(otpdata.data);
            return;
        }

        sensor_data->data = otpdata.data;
        sensor_data->size = otpdata.size;
        sensor_data->fetched = true;
    }
    gSensorDataCache[cameraId] = *sensor_data;
}

int SensorHW::setExposureMode(v4l2_exposure_auto_type v4l2Mode)
{
    LOG2("@%s: %d", __FUNCTION__, v4l2Mode);
    return mDevice->setControl(V4L2_CID_EXPOSURE_AUTO, v4l2Mode, "AE mode");
}

int SensorHW::getExposureMode(v4l2_exposure_auto_type * type)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_EXPOSURE_AUTO, (int*)type);
}

int SensorHW::setExposureBias(int bias)
{
    LOG2("@%s: bias: %d", __FUNCTION__, bias);
    return mDevice->setControl(V4L2_CID_EXPOSURE, bias, "exposure");
}

int SensorHW::getExposureBias(int * bias)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_EXPOSURE, bias);
}

int SensorHW::setSceneMode(v4l2_scene_mode mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_SCENE_MODE, mode, "scene mode");
}

int SensorHW::getSceneMode(v4l2_scene_mode * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_SCENE_MODE, (int*)mode);
}

int SensorHW::setWhiteBalance(v4l2_auto_n_preset_white_balance mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE, mode, "white balance");
}

int SensorHW::getWhiteBalance(v4l2_auto_n_preset_white_balance * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE, (int*)mode);
}

int SensorHW::setIso(int iso)
{
    LOG2("@%s: ISO: %d", __FUNCTION__, iso);
    return mDevice->setControl(V4L2_CID_ISO_SENSITIVITY, iso, "iso");
}

int SensorHW::getIso(int * iso)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_ISO_SENSITIVITY, iso);
}

int SensorHW::setAeMeteringMode(v4l2_exposure_metering mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_EXPOSURE_METERING, mode, "AE metering mode");
}

int SensorHW::getAeMeteringMode(v4l2_exposure_metering * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_EXPOSURE_METERING, (int*)mode);
}

int SensorHW::setAeFlickerMode(v4l2_power_line_frequency mode)
{
    LOG2("@%s: %d", __FUNCTION__, (int) mode);
    return mDevice->setControl(V4L2_CID_POWER_LINE_FREQUENCY,
                                    mode, "light frequency");
}

int SensorHW::setAfMode(v4l2_auto_focus_range mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_AUTO_FOCUS_RANGE , mode, "AF mode");
}

int SensorHW::getAfMode(v4l2_auto_focus_range * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_AUTO_FOCUS_RANGE, (int*)mode);
}

int SensorHW::setAfEnabled(bool enable)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->setControl(V4L2_CID_FOCUS_AUTO, enable, "Auto Focus");
}

int SensorHW::set3ALock(int aaaLock)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->setControl(V4L2_CID_3A_LOCK, aaaLock, "AE Lock");
}

int SensorHW::get3ALock(int * aaaLock)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_3A_LOCK, aaaLock);
}


int SensorHW::setAeFlashMode(v4l2_flash_led_mode mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_FLASH_LED_MODE, mode, "Flash mode");
}

int SensorHW::getAeFlashMode(v4l2_flash_led_mode * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_FLASH_LED_MODE, (int*)mode);
}

int SensorHW::getModeInfo(struct atomisp_sensor_mode_data *mode_data)
{
    LOG2("@%s", __FUNCTION__);
    int ret;
    ret = mDevice->xioctl(ATOMISP_IOC_G_SENSOR_MODE_DATA, mode_data);
    LOG2("%s IOCTL ATOMISP_IOC_G_SENSOR_MODE_DATA ret: %d\n", __FUNCTION__, ret);
    return ret;
}

int SensorHW::setExposure(struct atomisp_exposure *exposure)
{
    int ret;
    ret = mDevice->xioctl(ATOMISP_IOC_S_EXPOSURE, exposure);
    LOG2("%s IOCTL ATOMISP_IOC_S_EXPOSURE ret: %d, gain A:%d D:%d, itg C:%d F:%d\n", __FUNCTION__, ret, exposure->gain[0], exposure->gain[1], exposure->integration_time[0], exposure->integration_time[1]);
    return ret;
}

int SensorHW::setExposureTime(int time)
{
    LOG2("@%s", __FUNCTION__);

    return mDevice->setControl(V4L2_CID_EXPOSURE_ABSOLUTE, time, "Exposure time");
}

int SensorHW::getExposureTime(int *time)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_EXPOSURE_ABSOLUTE, time);
}

int SensorHW::getAperture(int *aperture)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_IRIS_ABSOLUTE, aperture);
}

int SensorHW::getFNumber(unsigned short *fnum_num, unsigned short *fnum_denom)
{
    LOG2("@%s", __FUNCTION__);
    int fnum = 0, ret;

    ret = mDevice->getControl(V4L2_CID_FNUMBER_ABSOLUTE, &fnum);

    *fnum_num = (unsigned short)(fnum >> 16);
    *fnum_denom = (unsigned short)(fnum & 0xFFFF);
    return ret;
}

/**
 * returns the V4L2 Bayer format preferred by the sensor
 */
int SensorHW::getRawFormat()
{
    return mRawBayerFormat;
}

const char * SensorHW::getSensorName(void)
{
    return mCameraInput.name;
}

float SensorHW::getFrameRate() const
{
    // TODO:
    return 24.0;
}


}
