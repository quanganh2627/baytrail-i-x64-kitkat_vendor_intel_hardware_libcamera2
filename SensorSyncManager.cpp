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
#define LOG_TAG "Camera_SensorManager"

#include "LogHelper.h"
#include "AtomISP.h"
#include <sys/time.h>
#include <signal.h>
#include "AtomCommon.h"
#include "SensorSyncManager.h"
#include "PlatformData.h"

namespace android {

SensorSyncManager::SensorSyncManager(IHWSensorControl *sensorCI) :
     mImmediateIo(true)
    ,mImmediateIoSet(false)
    ,mUseFrameSync(false)
    ,mExposureLag(0)
    ,mGainDelayFilter(NULL)
    ,mExposureFifo(NULL)
    ,mSensorCI(sensorCI)
    ,mRecovery(false)
{
    LOG1("@%s", __FUNCTION__);
    memset(&mCurrentExposure, 0, sizeof(struct atomisp_exposure));
}

SensorSyncManager::~SensorSyncManager()
{
    LOG1("@%s: %p, %p", __FUNCTION__, mGainDelayFilter, mExposureFifo);

    if (mGainDelayFilter) {
        delete mGainDelayFilter;
        mGainDelayFilter = NULL;
    }
    if (mExposureFifo) {
        delete mExposureFifo;
        mExposureFifo = NULL;
    }
}

/**
 * Init SensorSyncManager based on PlatformData configuration
 *
 * Note: PlatformData::synchronizeExposure() returns true when
 *       frame synchronization is requested. SensorSyncManager is
 *       responsible of this synchronization. Secondly,
 *       SensorSyncManager is responcible of aligning sensor gain
 *       and exposure lags - so effectively SensorSyncManager may
 *       be instantiated and configured when synchronizeExposure is
 *       FALSE.
 */
status_t SensorSyncManager::init()
{
    LOG1("@%s", __FUNCTION__);
    int gainLag = PlatformData::getSensorGainLag();
    int exposureLag = PlatformData::getSensorExposureLag();
    bool useFrameSync = PlatformData::synchronizeExposure();
    unsigned int gainDelay = 0;

    LOG1("SensorSyncManager config read, gain lag %d, exposure lag %d, synchronize %s",
            gainLag, exposureLag, useFrameSync ? "true" : "false");

    if (useFrameSync &&
        gainLag == 0 && exposureLag == 0) {
        LOGW("Exposure synchronization enabled without sensor latencies information,"
             " exposure sync not enabled");
        return BAD_VALUE;
    }

    if (gainLag == exposureLag) {
        LOG1("Gain delaying not needed");
    } else if (gainLag > exposureLag) {
        LOGW("Check sensor latencies configuration, not supported");
        return BAD_VALUE;
    } else {
        gainDelay = exposureLag - gainLag;
    }

    // Note: 1. exposure delay is fixed when frame sync is not requested
    //       2. exposure delay is increased by one based on whether frame sync is used
    mExposureLag = (gainLag <= exposureLag) ? exposureLag : gainLag;
    mUseFrameSync = useFrameSync;
    if (mUseFrameSync)
        mExposureLag++;

    if (!mUseFrameSync && gainDelay == 0) {
        LOG1("Asynchronous direct applying, SensorSyncManager not needed");
        return NO_INIT;
    }

    LOG1("sensor delays: gain %d, exposure %d", gainLag, exposureLag);
    LOG1("using sw gain delay %d, %s", gainDelay, mUseFrameSync ? "frame synchronized":"direct");

    return config(DEFAULT_DEPTH_OF_EXPOSURE_FIFO, gainDelay);
}

status_t SensorSyncManager::config(unsigned int fifoDepth, unsigned int gainDelayFrames, unsigned int gainDefaultValue)
{
    if (fifoDepth < MIN_DEPTH_OF_EXPOSURE_FIFO)
        return BAD_VALUE;

    if (mGainDelayFilter)
        delete mGainDelayFilter;
    if (mExposureFifo)
        delete mExposureFifo;

    mGainDelayFilter = new AtomDelayFilter <unsigned int> (gainDefaultValue, gainDelayFrames);
    mExposureFifo = new AtomFifo <struct atomisp_exposure> (fifoDepth);
    return NO_ERROR;
}

int SensorSyncManager::_setExposure(struct atomisp_exposure *exposure)
{
    mCurrentExposure = *exposure;
    return mSensorCI->setExposure(exposure);
}

int SensorSyncManager::processGainDelay(struct atomisp_exposure *exposure)
{
    struct atomisp_exposure myExposure = *exposure;
    myExposure.gain[0] = mGainDelayFilter->enqueue(exposure->gain[0]);
    return _setExposure(&myExposure);
}

/**
 * Implements IHWSensorControl::setExposure()
 *
 * Consider new exposure based on whether to synchronize applying with
 * frames and whether to use gain delay filter. When immediate IO is
 * set pass parameters through for setting.
 */
int SensorSyncManager::setExposure(struct atomisp_exposure *exposure)
{
    int ret = 0;
    Mutex::Autolock lock(mLock);
    if (!exposure) {
        LOGE("%s: NULL exposure", __FUNCTION__);
        return 0;
    }
    if (mImmediateIo) {
        // set the sensor settings immediately without gain delay
        // we should get here only when stream is off.
        LOG1("@%s immediate.\t\tgain %d, citg %d, fitg %d", __FUNCTION__,
                exposure->gain[0],
                exposure->integration_time[0],
                exposure->integration_time[1]);
        mSensorCI->setExposure(exposure);
        // update filter with gain value
        mGainDelayFilter->enqueue(exposure->gain[0]);
    } else if (!mUseFrameSync) {
        ret = processGainDelay(exposure);
    } else {
        ret = mExposureFifo->enqueue(*exposure);
        LOG1("@%s enqueued exposure, gain %d, citg %d", __FUNCTION__, exposure->gain[0], exposure->integration_time[0]);
    }
    if (ret != 0) {
        LOGE("%s: Error!", __FUNCTION__);
    }
    return 0;
}

int SensorSyncManager::setImmediateIo(bool enable)
{
    LOG1("@%s(%d)", __FUNCTION__, enable);
    Mutex::Autolock lock(mLock);
    mImmediateIoSet = mImmediateIo = enable;
    if (enable)
        mExposureFifo->reset();
    return NO_ERROR;
}

/**
 * Implement IAtomIspObserver::atomIspNotify()
 *
 * - execute local frameSyncProc() on FrameSync events
 * - reset local exposure fifo on first successful even after error.
 * - switch to immediate mode (direct applying) based on observer state
 */
bool SensorSyncManager::atomIspNotify(Message *msg, const ObserverState state)
{
   LOG2("@%s: msg id %d, state %d", __FUNCTION__, (msg) ? msg->id : -1, state);
   if (msg == NULL) {
       setImmediateIo(state != OBSERVER_STATE_RUNNING);
   } else if (msg && msg->id == IAtomIspObserver::MESSAGE_ID_EVENT) {
       if (mRecovery) {
           LOG1("%s: reseting fifo for recovery", __FUNCTION__);
           mLock.lock();
           mExposureFifo->reset();
           mLock.unlock();
           mRecovery = false;
       } else {
           nsecs_t timestamp = TIMEVAL2USECS(&msg->data.event.timestamp);
           frameSyncProc(timestamp);
       }
   } else if (msg && msg->id == IAtomIspObserver::MESSAGE_ID_ERROR) {
       mRecovery = true;
   }
   return false;
}

/**
 * Process framesync event
 *
 * Consume exposure parameters from FiFo
 *
 * - process queued exposure through gain delay filter
 */
int SensorSyncManager::frameSyncProc(nsecs_t timestamp)
{
    LOG2("@%s:\t%lld us", __FUNCTION__, timestamp);
    Mutex::Autolock lock(mLock);
    if (!mUseFrameSync)
        return INVALID_OPERATION;

    if (!mImmediateIo) {
        int err;
        struct atomisp_exposure myExposure;
        if (mExposureFifo->getCount()) {
            err = mExposureFifo->dequeue(&myExposure);
            processGainDelay(&myExposure);
        } else {
            // no new parameters, keep pushing gain delay filter in order to
            // have last delayd value applied
            myExposure = mCurrentExposure;
            myExposure.gain[0] = mGainDelayFilter->enqueue(mCurrentExposure.gain[0]);
            if (myExposure.gain[0] != mCurrentExposure.gain[0]) {
                _setExposure(&myExposure);
            }
        }
    }

    return NO_ERROR;
}

} // namespace android
