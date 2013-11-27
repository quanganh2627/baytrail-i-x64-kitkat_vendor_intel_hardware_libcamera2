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

#ifndef ANDROID_LIBCAMERA_SENSOR_CLASS
#define ANDROID_LIBCAMERA_SENSOR_CLASS

#include "ICameraHwControls.h"
#include "PlatformData.h"

namespace android {

class V4L2VideoNode;

class SensorHW:public IHWSensorControl {

public:
    SensorHW(int cameraId):mCameraId(cameraId) { }
    ~SensorHW() { mDevice.clear(); };
    status_t selectActiveSensor(sp<V4L2VideoNode> &device);

    /* IHWSensorControl overloads, */
    virtual const char * getSensorName(void);
    virtual int getCurrentCameraId(void);
    virtual void getMotorData(sensorPrivateData *sensor_data);
    virtual void getSensorData(sensorPrivateData *sensor_data);
    virtual int getModeInfo(struct atomisp_sensor_mode_data *mode_data);
    virtual int setExposureTime(int time);
    virtual int getExposureTime(int *exposure_time);
    virtual int getAperture(int *aperture);
    virtual int getFNumber(unsigned short  *fnum_num, unsigned short *fnum_denom);
    virtual int setExposureMode(v4l2_exposure_auto_type type);
    virtual int getExposureMode(v4l2_exposure_auto_type * type);
    virtual int setExposureBias(int bias);
    virtual int getExposureBias(int * bias);
    virtual int setSceneMode(v4l2_scene_mode mode);
    virtual int getSceneMode(v4l2_scene_mode * mode);
    virtual int setWhiteBalance(v4l2_auto_n_preset_white_balance mode);
    virtual int getWhiteBalance(v4l2_auto_n_preset_white_balance * mode);
    virtual int setIso(int iso);
    virtual int getIso(int * iso);
    virtual int setAeMeteringMode(v4l2_exposure_metering mode);
    virtual int getAeMeteringMode(v4l2_exposure_metering * mode);
    virtual int setAeFlickerMode(v4l2_power_line_frequency mode);
    virtual int setAfMode(v4l2_auto_focus_range mode);
    virtual int getAfMode(v4l2_auto_focus_range * mode);
    virtual int setAfEnabled(bool enable);
    virtual int set3ALock(int aaaLock);
    virtual int get3ALock(int * aaaLock);
    virtual int setAeFlashMode(v4l2_flash_led_mode mode);
    virtual int getAeFlashMode(v4l2_flash_led_mode * mode);
    virtual int getRawFormat();

    virtual unsigned int getExposureDelay() { return PlatformData::getSensorExposureLag(); };
    virtual int setExposure(struct atomisp_exposure *exposure);

    virtual float getFrameRate() const;

private:
    static const int MAX_SENSOR_NAME_LENGTH = 32;

    struct cameraInfo {
        uint32_t index;      //!< V4L2 index
        char name[MAX_SENSOR_NAME_LENGTH];
    };

    size_t enumerateInputs(Vector<struct cameraInfo> &);
    status_t sensorStoreRawFormat(Vector<v4l2_fmtdesc> &formats);


private:
    sp<V4L2VideoNode> mDevice;
    SensorType        mSensorType;
    int mCameraId;
    struct cameraInfo mCameraInput;
    int mRawBayerFormat;
}; // class SensorHW

}; // namespace android

#endif
