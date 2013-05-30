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
    virtual float getFrameRate() const { return mSensorCI->getFrameRate(); };
    virtual unsigned int getExposureDelay() { return mExposureLag; };
    virtual int setExposure(struct atomisp_exposure *);

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
