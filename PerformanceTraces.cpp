/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include <utils/Timers.h>

#define LOG_TAG "Atom_PerformanceTraces"
#include "LogHelper.h"

#include <time.h>
#include "PerformanceTraces.h"

namespace android {
namespace PerformanceTraces {

/**
 * \class PerformanceTimer
 *
 * Private class for managing R&D traces used for performance
 * analysis and testing.
 *
 * This code should be disabled in product builds.
 */
class PerformanceTimer {

public:
    nsecs_t mStartAt;
    nsecs_t mLastRead;
    bool mFilled;            //!< timestamp has been taken
    bool mRequested;         //!< trace is requested/enabled

    PerformanceTimer(void) :
        mStartAt(0),
        mLastRead(0),
        mFilled(false),
        mRequested(false) {
    }

    bool isRunning(void) { return mFilled && mRequested; }

    bool isRequested(void) { return mRequested; }

    int64_t timeUs(void) {
        uint64_t now = systemTime();
        mLastRead = now;
        return (now - mStartAt) / 1000;
    }

    int64_t lastTimeUs(void) {
        uint64_t now = systemTime();
        return (now - mLastRead) / 1000;
    }

   /**
     * Enforce a standard format on timestamp traces parsed
     * by offline PnP tools.
     *
     * \see system/core/include/android/log.h
     */
    void formattedTrace(const char* p, const char *f) {
        LOGD("%s:%s, Time: %lld us, Diff: %lld us",
             p, f, timeUs(), mFilled ? lastTimeUs() : -1);
    }

    void start(void) {
        mStartAt = mLastRead = systemTime();
        mFilled = true;
    }

    void stop(void) { mFilled = false; }

};

// To allow disabling all tracing infrastructure for non-R&D builds,
// wrap everything in LIBCAMERA_RD_FEATURES (see Android.mk).
// -----------------------------------------------------------------

#ifdef LIBCAMERA_RD_FEATURES

static PerformanceTimer gLaunch2Preview;
static PerformanceTimer gLaunch2FocusLock;
static PerformanceTimer gShot2Shot;
static PerformanceTimer gShutterLag;
static PerformanceTimer gSwitchCameras;
static PerformanceTimer gAAAProfiler;

static bool gShot2ShotBreakdown = false;
static bool gLaunch2PreviewBreakdown = false;
static int gShot2ShotFrame = -1;
static bool gShot2ShotTakePictureCalled = false;
static bool gShot2ShotAutoFocusDone = false;
static bool gSwitchCamerasCalled = false;
static bool gSwitchCamerasOriginalVideoMode = false;
static bool gSwitchCamerasVideoMode = false;
static int gSwitchCamerasOriginalCameraId = 0;
/**
 * Reset the flags that enable the different performance traces
 * This is needed during HAL open so that we can turn off the performance
 * traces from the system property
 */
void reset(void)
{
    gShot2ShotBreakdown = false;
    gLaunch2PreviewBreakdown = false;
    gShot2ShotFrame = -1;
    gShot2ShotTakePictureCalled = false;
    gShot2ShotAutoFocusDone = false;
    gSwitchCamerasCalled = false;
    gSwitchCamerasVideoMode = false;

    gLaunch2Preview.mRequested = false;
    gShot2Shot.mRequested = false;
    gAAAProfiler.mRequested = false;
    gShutterLag.mRequested = false;
    gSwitchCameras.mRequested = false;
    gLaunch2FocusLock.mRequested = false;

}
/**
 * Controls trace state
 */
void Launch2Preview::enable(bool set)
{
    gLaunch2Preview.mRequested = set;
}

/**
 * Starts the launch2preview trace.
 */
void Launch2Preview::start(void)
{
    if (gLaunch2Preview.isRequested()) {
        gLaunch2Preview.formattedTrace("Launch2Preview", __FUNCTION__);
        gLaunch2Preview.start();
    }
}

/**
 * Enable more detailed breakdown analysis that shows how long
 * intermediate steps took time.
 */
void Launch2Preview::enableBreakdown(bool set)
{
    gLaunch2PreviewBreakdown = set;
}


/**
 * Mark an intermedia step in the Launch2Preview trace
 *
 * @arg note a string printed with the breakdown trace
 */
void Launch2Preview::step(const char* func, const char* note)
{
    if (gLaunch2Preview.isRunning() && gLaunch2PreviewBreakdown) {
        if (!note)
            note = "";
        LOGD("Launch2Preview step %s:%s, Time: %lld us, Diff: %lld us",
             func, note, gLaunch2Preview.timeUs(), gLaunch2Preview.lastTimeUs());
    }
}


/**
 * Stops the launch2preview trace and prints out results.
 */
void Launch2Preview::stop(void)
{
    if (gLaunch2Preview.isRunning()) {
        LOGD("LAUNCH time calculated from create instance to the 1st preview frame show:\t%lld ms\n",
             gLaunch2Preview.timeUs() / 1000);
        gLaunch2Preview.stop();
    }
}

/**
 * Controls trace state
 */
void Launch2FocusLock::enable(bool set)
{
    gLaunch2FocusLock.mRequested = set;
}

/**
 * Starts the launch2FocusLock trace.
 */
void Launch2FocusLock::start(void)
{
    if (gLaunch2FocusLock.isRequested()) {
        gLaunch2FocusLock.formattedTrace("Launch2FocusLock", __FUNCTION__);
        gLaunch2FocusLock.start();
    }
}

/**
 * Stops the launch2FocusLock trace and prints out results.
 */
void Launch2FocusLock::stop(void)
{
    if (gLaunch2FocusLock.isRunning()) {
        LOGD("LAUNCH time calculated from create instance to lock the focus frame:\t%lld ms\n",
             gLaunch2FocusLock.timeUs() / 1000);
        gLaunch2FocusLock.stop();
    }
}

/**
 * Controls trace state
 */
void ShutterLag::enable(bool set)
{
    gShutterLag.mRequested = set;
}

/**
 * Starts the ShutterLag trace.
 */
void ShutterLag::takePictureCalled(void)
{
    if (gShutterLag.isRequested())
        gShutterLag.start();
}

/**
 * Prints ShutterLag trace results.
 */
void ShutterLag::snapshotTaken(struct timeval *ts)
{
    if (gShutterLag.isRunning()) {
        LOGD("ShutterLag from takePicture() to shot taken:\t%lldms\n",
             (((nsecs_t(ts->tv_sec)*1000000LL
             +  nsecs_t(ts->tv_usec))
             - gShutterLag.mStartAt/1000)/1000));
    }
}

/**
 * Controls trace state
 */
void Shot2Shot::enable(bool set)
{
    gShot2Shot.mRequested = set;
}

/**
 * Enable more detailed breakdown analysis that shows how long
 * intermediate steps took time.
 */
void Shot2Shot::enableBreakdown(bool set)
{
    gShot2ShotBreakdown = set;
}

/**
 * Starts shot2shot trace
 */
void Shot2Shot::start(int frameCounter)
{
    // In JellyBean, autofocus may start right after start preview
    // and may occur before the first preview frame is displayed.
    // As two shot2shot measurements cannot overlap with current
    // definiton of shot2shot, we must stop the previous measurement here.
    if (gShot2Shot.isRunning()) {
        Shot2Shot::stop(gShot2ShotFrame);
    }

    if (gShot2Shot.isRequested()) {
        gShot2Shot.start();
        gShot2ShotFrame = frameCounter;
        gShot2ShotTakePictureCalled = false;
        gShot2ShotAutoFocusDone = false;
        gShot2Shot.formattedTrace("Shot2Shot", __FUNCTION__);
    }
}

/**
 * Marks takePicture HAL call has been issued.
 *
 * This is needed to reliably detect start and end of shot2shot
 * sequences.
 */
void Shot2Shot::takePictureCalled(void)
{
    if (gShot2Shot.isRunning() == false) {
        // application has skipped AF
        start(1);
    }
    gShot2ShotTakePictureCalled = true;
}

/**
 * Marks that AF has completed.
 *
 * This is needed to reliably filter out test sequences
 * where AF was not run, or where AF failed.
*/
void Shot2Shot::autoFocusDone(void)
{
    if (gShot2Shot.isRunning()) {
        gShot2ShotAutoFocusDone = true;
    }
}

/**
 * Mark an intermedia step in the shot2shot trace
 *
 * @arg note a string printed with the breakdown trace
 * @arg frameCounter if non-negative, a valid frame counter value
 *      that links the step to a specific frame
 */
void Shot2Shot::step(const char* func, const char* note, int frameCounter)
{
    if (gShot2Shot.isRunning() && gShot2ShotBreakdown) {
        if (!note)
            note = "";
        if (frameCounter < 0) {
            LOGD("Shot2Shot step %s:%s, Time: %lld us, Diff: %lld us",
                 func, note, gShot2Shot.timeUs(), gShot2Shot.lastTimeUs());
        }
        else {
            LOGD("Shot2Shot step %s:%s [%d], Time: %lld us, Diff: %lld us",
                 func, note, frameCounter, gShot2Shot.timeUs(), gShot2Shot.lastTimeUs());
        }
    }
}

void Shot2Shot::stop(int frameCounter)
{
    if (gShot2Shot.isRunning() &&
            frameCounter == gShot2ShotFrame &&
            gShot2ShotTakePictureCalled == true) {

        if (gShot2ShotAutoFocusDone) {
            // This trace is only printed for the strict
            // definition of shot2shot metric, which requires
            // that AF has run and has succeeded.
            LOGD("shot2shot latency: %lld us, frame %d",
                 gShot2Shot.timeUs(), frameCounter);
        }
        else {
            LOGW("shot2shot not calculated, AF failed or not in use");
        }

        gShot2Shot.formattedTrace("Shot2Shot", __FUNCTION__);
        gShot2Shot.stop();
    }
}

/**
 * Controls trace state
 */

void AAAProfiler::enable(bool set)
{
    gAAAProfiler.mRequested = set;
}

/**
 * Starts the AAAprofiler trace.
 */
void AAAProfiler::start(void)
{
    if (gAAAProfiler.isRequested()) {
        gAAAProfiler.formattedTrace("gAAAProfiler", __FUNCTION__);
        gAAAProfiler.start();
    }
}

/**
 * Stops the AAAprofiler trace and prints out results.
 */
void AAAProfiler::stop(void)
{
    if (gAAAProfiler.isRunning()) {
        LOGD("3A profiling time::\t%lldms\n",
             gAAAProfiler.timeUs() / 1000);
        gAAAProfiler.stop();
    }
}

/**
 * Controls trace state
 */
void SwitchCameras::enable(bool set)
{
    gSwitchCameras.mRequested = set;
}

/**
 * Starts the SwitchCameras trace.
 */
void SwitchCameras::start(int cameraid)
{
    if (gSwitchCameras.isRequested()) {
        gSwitchCameras.formattedTrace("SwitchCameras", __FUNCTION__);
        gSwitchCamerasCalled = false;
        gSwitchCamerasOriginalVideoMode = false;
        gSwitchCamerasVideoMode = false;
        gSwitchCamerasOriginalCameraId = cameraid;
        gSwitchCameras.start();
    }
}

/**
 * Get the original mode
 */
void SwitchCameras::getOriginalMode(bool videomode)
{
    if (gSwitchCameras.isRequested())
        gSwitchCamerasOriginalVideoMode = videomode;
}

/**
 * This function will be called at the time of start preview.
 */
void SwitchCameras::called(bool videomode)
{
    if (gSwitchCameras.isRequested()) {
        gSwitchCamerasCalled = true;
        gSwitchCamerasVideoMode = videomode;
    }
}

/**
 * Stops the SwitchCameras trace and prints out results.
 */
void SwitchCameras::stop(void)
{
    if (gSwitchCameras.isRunning() && gSwitchCamerasCalled == true) {
        if (gSwitchCamerasOriginalVideoMode == gSwitchCamerasVideoMode) {
            LOGD("Using %s mode, Switch from %s camera to %s camera, SWITCH time::\t%lldms\n",
                    (gSwitchCamerasVideoMode ? "video" : "camera"),
                    ((gSwitchCamerasOriginalCameraId == 0) ? "back" : "front"),
                    ((gSwitchCamerasOriginalCameraId == 1) ? "back" : "front"),
                    gSwitchCameras.timeUs() / 1000);
        } else {
            LOGD("Using %s camera, Switch from %s mode to %s mode, SWITCH time::\t%lldms\n",
                    ((gSwitchCamerasOriginalCameraId == 0) ? "back" : "front"),
                    (gSwitchCamerasOriginalVideoMode ? "video" : "camera"),
                    (gSwitchCamerasVideoMode ? "video" : "camera"),
                    gSwitchCameras.timeUs() / 1000);
        }
        gSwitchCamerasCalled = false;
        gSwitchCameras.stop();
    }
}

#else // LIBCAMERA_RD_FEATURES
void reset(void) {}

#endif // LIBCAMERA_RD_FEATURES

} // namespace PerformanceTraces
} // namespace android
