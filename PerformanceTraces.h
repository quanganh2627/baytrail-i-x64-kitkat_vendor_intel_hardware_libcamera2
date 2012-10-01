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

#ifndef ANDROID_LIBCAMERA_PERFORMANCE_TRACES
#define ANDROID_LIBCAMERA_PERFORMANCE_TRACES

namespace android {

/**
 * \class PerformanceTraces
 *
 * Interface for managing R&D traces used for performance
 * analysis and testing.
 *
 * This interface is designed to minimize call overhead and it can
 * be disabled altogether in product builds. Calling the functions
 * from different threads is safe (no crashes), but may lead to
 * at least transient incorrect results, so the output values need
 * to be postprocessed for analysis.
 *
 * This code should be disabled in product builds.
 */
namespace PerformanceTraces {

  // this is a bit ugly but this is a compact way to define a no-op
  // implementation in case LIBCAMERA_RD_FEATURES is not set
#undef STUB_BODY
#ifdef LIBCAMERA_RD_FEATURES
#define STUB_BODY ;
#else
#define STUB_BODY {}
#endif

  class Launch2Preview {
  public:
    static void enable(bool set) STUB_BODY
    static void start(void) STUB_BODY
    static void stop(void) STUB_BODY
  };

  class Shot2Shot {
  public:
    static void enable(bool set) STUB_BODY
    static void enableBreakdown(bool set) STUB_BODY
    static void start(int frameCounter = -1) STUB_BODY
    static void takePictureCalled(void) STUB_BODY
    static void autoFocusDone(void) STUB_BODY
    static void step(const char *func, const char* note = 0, int frameCounter = -1) STUB_BODY
    static void stop(int frameCounter = -1) STUB_BODY
  };

  class ShutterLag {
  public:
    static void enable(bool set) STUB_BODY
    static void takePictureCalled(void) STUB_BODY
    static void snapshotTaken(struct timeval *ts) STUB_BODY
  };

  class AAAProfiler {
  public:
    static void enable(bool set) STUB_BODY
    static void start(void) STUB_BODY
    static void stop(void) STUB_BODY
  };

  /**
   * Helper macro to call PerformanceTraces::Shot2Shot::step() with
   * the proper function name, and pass additional arguments.
   *
   * @param note textual description of the trace point
   * @param frameCounter frame id this trace relates to
   *
   * See also PERFORMANCE_TRACES_SHOT2SHOT_NOPARAM()
   */
  #define PERFORMANCE_TRACES_SHOT2SHOT_STEP(note, frameCounter) \
    PerformanceTraces::Shot2Shot::step(__FUNCTION__, note, frameCounter)

  /**
   * Helper macro to call PerformanceTraces::Shot2Shot::step() with
   * the proper function name.
   */
  #define PERFORMANCE_TRACES_SHOT2SHOT_STEP_NOPARAM() \
    PerformanceTraces::Shot2Shot::step(__FUNCTION__)

  /**
   * Helper macro to call PerformanceTraces::Shot2Shot::takePictureCalled() with
   * the proper function name.
   */
  #define PERFORMANCE_TRACES_SHOT2SHOT_TAKE_PICTURE_CALLED() \
      do { \
          PerformanceTraces::Shot2Shot::takePictureCalled(); \
          PerformanceTraces::Shot2Shot::step(__FUNCTION__);  \
      } while(0)

  /**
   * Helper macro to call PerformanceTraces::Shot2Shot::autoFocusDone() with
   * the proper function name.
   *
   * @param ok true if AF was succesful
   */
  #define PERFORMANCE_TRACES_SHOT2SHOT_AUTO_FOCUS_DONE(ok) \
      do { \
          if (ok) PerformanceTraces::Shot2Shot::autoFocusDone();   \
          PerformanceTraces::Shot2Shot::step(__FUNCTION__);  \
      } while(0)

  /**
   * Helper macro to call when preview frame has been sent
   * to display subsystem. This step is used in multiple metrics.
   *
   * @param x preview frame counter
   */
  #define PERFORMANCE_TRACES_PREVIEW_SHOWN(x) \
      if (x == 1) {  \
          PerformanceTraces::Launch2Preview::stop(); \
          PerformanceTraces::Shot2Shot::step(__FUNCTION__);  \
          PerformanceTraces::Shot2Shot::stop(x); \
      }

}; // ns PerformanceTraces
}; // ns android

#endif // ANDROID_LIBCAMERA_PERFORMANCE_TRACES
