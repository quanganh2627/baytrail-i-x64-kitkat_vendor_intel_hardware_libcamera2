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

#ifndef ANDROID_LIBCAMERA_SENSOR_SYNC_MANAGER_H
#define ANDROID_LIBCAMERA_SENSOR_SYNC_MANAGER_H

#include "AtomCommon.h"
#include "I3AControls.h"
#include "AtomDelayFilter.h"
#include "AtomFifo.h"
#include "IAtomIspObserver.h"

namespace android {

/* FIFO depth of postponed exposure */
#define DEFAULT_DEPTH_OF_EXPOSURE_FIFO 4    // default
#define MIN_DEPTH_OF_EXPOSURE_FIFO 2        // minimum
#define CLEAR(x) memset (&(x), 0, sizeof (x))

/**
 * class SensorSyncManager
 *
 * TODO: AtomISP refactory (Bug 109307) is about to introduce Sensor-class, which will
 *       effectively take over the responsibility of synchronization and filtering.
 *       SensorSyncManager is used temporarily in between 3A and sensor controls
 */
class SensorSyncManager:
    public IHWSensorControl,
    public IAtomIspObserver,
    public RefBase {

// constructor destructor
public:
    SensorSyncManager(IHWSensorControl *sensorCI);
    virtual ~SensorSyncManager();

// public methods
public:
    status_t init();
    status_t config(unsigned int fifoDepth, unsigned int gainDelayFrames, unsigned int gainDefaultValue = 0);

    // IIQSensorControl overloads
    virtual const char * getSensorName(void) { return mSensorCI->getSensorName(); };
    virtual float getFrameRate() const { return mSensorCI->getFrameRate(); };
    virtual unsigned int getExposureDelay() { return mExposureLag; };
    virtual int setExposure(struct atomisp_exposure *);

    virtual status_t getSensorParams(SensorParams * sp) { return mSensorCI->getSensorParams(sp); };

    virtual void getSensorData(sensorPrivateData *sensor_data) { return mSensorCI->getSensorData(sensor_data); };
    virtual int  getModeInfo(struct atomisp_sensor_mode_data *mode_data) { return mSensorCI->getModeInfo(mode_data); };
    virtual int  getExposureTime(int *exposure_time) { return mSensorCI->getExposureTime(exposure_time); };
    virtual int  getAperture(int *aperture) { return mSensorCI->getAperture(aperture); };
    virtual int  getFNumber(unsigned short  *fnum_num, unsigned short *fnum_denom) { return mSensorCI->getFNumber(fnum_num, fnum_denom); };
    virtual int setExposureTime(int time) { return mSensorCI->setExposureTime(time); };
    virtual int setExposureMode(v4l2_exposure_auto_type type) { return mSensorCI->setExposureMode(type); };
    virtual int getExposureMode(v4l2_exposure_auto_type * type) { return mSensorCI->getExposureMode(type); };
    virtual int setExposureBias(int bias) { return mSensorCI->setExposureBias(bias); };
    virtual int getExposureBias(int * bias) { return mSensorCI->getExposureBias(bias); };
    virtual int setSceneMode(v4l2_scene_mode mode) { return mSensorCI->setSceneMode(mode); };
    virtual int getSceneMode(v4l2_scene_mode * mode) { return mSensorCI->getSceneMode(mode); };
    virtual int setWhiteBalance(v4l2_auto_n_preset_white_balance mode) { return mSensorCI->setWhiteBalance(mode); };
    virtual int getWhiteBalance(v4l2_auto_n_preset_white_balance * mode) { return mSensorCI->getWhiteBalance(mode); };
    virtual int setIso(int iso) { return mSensorCI->setIso(iso); };
    virtual int getIso(int * iso) { return mSensorCI->getIso(iso); };
    virtual int setAeMeteringMode(v4l2_exposure_metering mode) { return mSensorCI->setAeMeteringMode(mode); };
    virtual int getAeMeteringMode(v4l2_exposure_metering * mode) { return mSensorCI->getAeMeteringMode(mode); };
    virtual int setAeFlickerMode(v4l2_power_line_frequency mode) { return mSensorCI->setAeFlickerMode(mode); };
    virtual int setAfMode(v4l2_auto_focus_range mode) { return mSensorCI->setAfMode(mode); };
    virtual int getAfMode(v4l2_auto_focus_range * mode) { return mSensorCI->getAfMode(mode); };
    virtual int setAfEnabled(bool enable) { return mSensorCI->setAfEnabled(enable); };
    virtual int set3ALock(int aaaLock) { return mSensorCI->set3ALock(aaaLock); };
    virtual int get3ALock(int * aaaLock) { return mSensorCI->get3ALock(aaaLock); };
    virtual int setAeFlashMode(v4l2_flash_led_mode mode) { return mSensorCI->setAeFlashMode(mode); };
    virtual int getAeFlashMode(v4l2_flash_led_mode * mode) { return mSensorCI->getAeFlashMode(mode); };

    // IAtomIspObserver overloads
    virtual bool atomIspNotify(Message *msg, const ObserverState state);

protected:

// private methods
private:
    int setImmediateIo(bool enable);
    int frameSyncProc(nsecs_t timestamp);
    int _setExposure(struct atomisp_exposure *);
    int processGainDelay(struct atomisp_exposure *);

// private data
private:
    bool mImmediateIo;          /* set exposure immediately */
    bool mImmediateIoSet;       /* API setImmediateIo(true) has been explicitly called */
    bool mUseFrameSync;         /* use frameSyncProc() to synchronize exposure applying */
    unsigned int mExposureLag;  /* delay of exposure applying based on configuration */
    AtomDelayFilter <unsigned int>  *mGainDelayFilter;
    AtomFifo <struct atomisp_exposure> *mExposureFifo;
    struct atomisp_exposure          mCurrentExposure;
    IHWSensorControl                *mSensorCI;
    bool mRecovery;             /* frame sync was lost */
    Mutex mLock;
}; // class SensorSyncManager

}; // namespace android

#endif // ANDROID_LIBCAMERA_SENSOR_SYNC_MANAGER_H
