/*
 * Copyright (C) 2013 The Android Open Source Project
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
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
#ifndef ANDROID_LIBCAMERA_ULTRALOWLIGHT_H_
#define ANDROID_LIBCAMERA_ULTRALOWLIGHT_H_

#include "PostCaptureThread.h"
#include "Callbacks.h"  // For memory allocation only
#include "PictureThread.h" // For Image metadata declaration
#include "AtomCommon.h"

namespace android {

#undef STUB_BODY
#undef STUB_BODY_STAT
#undef STUB_BODY_BOOL

#ifdef ENABLE_INTEL_EXTRAS
#define STUB_BODY ;
#define STUB_BODY_STAT ;
#define STUB_BODY_BOOL ;
#else
#define STUB_BODY {};
#define STUB_BODY_STAT {return NO_ERROR;};
#define STUB_BODY_BOOL {return false;};
#endif

/**
 * \class UltraLowLight
 *
 * Wrapper class for the Morpho Photo Solid  algorithm
 * This class handles the algorithm usage and also the triggering logic.
 *
 */
class UltraLowLight : public IPostCaptureProcessItem
{
public:
    /**
     * \enum
     * User modes for ULL.
     * This controls whether the ULL algorithm is in use or not.
     */
    enum ULLMode {
        ULL_AUTO,   /* ULL active, 3A thresholds will trigger the use of it */
        ULL_ON,     /* ULL active always, forced for all captures */
        ULL_OFF,    /* ULL disabled */
    };

    /**
     *  Algorithm hard coded values
     *  These values have been decided based on the current HAL implementation
     *
     */
    static const char MORPHO_INPUT_FORMAT[];
    static const int MORPHO_INPUT_FORMAT_V4L2;
    static const int  MAX_INPUT_BUFFERS;

    /**
     *  Activation/De-activation Thresholds
     *  used to trigger ULL based on 3A parameters
     *  There are 2 thresholds:
     *  - one to activate ULL when scene gets darker (bright threshold)
     *  - one to deactivate ULL if scene gets too dark (dark threshold)
     *
     *  TODO: They should eventually come from CPF
     **/
    static int ULL_BRIGHT_ISO_THRESHOLD;
    static int ULL_BRIGHT_EXPTIME_THRESHOLD;
    static int ULL_DARK_ISO_THRESHOLD;
    static int ULL_DARK_EXPTIME_THRESHOLD;

    /**
     * \enum ULLPreset
     * Different configurations for the ULL algorithm
     */
    enum ULLPreset {
        ULL_PRESET_1 = 0,
        ULL_PRESET_2,
        ULL_PRESET_MAX
    };

public:
    UltraLowLight() STUB_BODY
    virtual ~UltraLowLight() STUB_BODY

    void setMode(ULLMode m) STUB_BODY
    bool isActive() STUB_BODY_BOOL
    bool trigger() STUB_BODY_BOOL

    status_t init( int w, int h, int aPreset) STUB_BODY_STAT
    status_t deinit() STUB_BODY_STAT

    status_t addInputFrame(AtomBuffer *snapshot, AtomBuffer *postview) STUB_BODY_STAT
    status_t addSnapshotMetadata(PictureThread::MetaData &metadata) STUB_BODY_STAT
    status_t getOuputResult(AtomBuffer *snap, AtomBuffer * pv,
                            PictureThread::MetaData *metadata, int *ULLid) STUB_BODY_STAT
    status_t getInputBuffers(Vector<AtomBuffer> *inputs) STUB_BODY_STAT

    int getCurrentULLid() { return mULLCounter; };
    int getULLBurstLength() STUB_BODY_STAT

    // implementation of IPostCaptureProcessItem
    status_t process() STUB_BODY_STAT

    bool updateTrigger(SensorAeConfig &expInfo, int gain) STUB_BODY_BOOL;

private:
    status_t initMorphoLib(int w, int h, int aPreset) STUB_BODY_STAT
    status_t configureMorphoLib(void) STUB_BODY_STAT
    void deinitMorphoLib() STUB_BODY
    void AtomToMorphoBuffer(const   AtomBuffer *atom, void* morpho) STUB_BODY

private:
    enum State {
        ULL_STATE_NULL,
        ULL_STATE_UNINIT,
        ULL_STATE_INIT,
        ULL_STATE_READY,
        ULL_STATE_PROCESSING,
        ULL_STATE_DONE
    };

    /**
     * \struct MorphoULLConfig
     *
     * This struct contains the parameters that can be tuned in the algorithm
     * The ULL presets are a a list of elements of this type.
     */
    struct MorphoULLConfig {
        int gain;
        int margin;
        int block_size;
        int obc_level;
        int y_nr_level;
        int c_nr_level;
        int y_nr_type;
        int c_nr_type;
        MorphoULLConfig(){};
        MorphoULLConfig(int gain,int margin, int block_size, int obc_level,
                        int y_nr_level, int c_nr_level, int y_nr_type, int c_nr_type):
                        gain(gain),
                        margin(margin),
                        block_size(block_size),
                        obc_level(obc_level),
                        y_nr_level(y_nr_level),
                        c_nr_level(c_nr_level),
                        y_nr_type(y_nr_type),
                        c_nr_type(c_nr_type)
        {}
    };

    struct MorphoULL;       /*!> Forward declaration of the opaque struct for Morpho's algo configuration */
    MorphoULL   *mMorphoCtrl;
    Callbacks   *mCallbacks;
    AtomBuffer  mOutputBuffer;  /*!> Output of the ULL processing. this is actually the first input buffer passed */
    AtomBuffer  mOutputPostView;  /*!> post view image for the first snapshot, used as output one */
    State       mState;
    int mULLCounter;        /*!> Running counter of ULL shots. Used as frame id towards application */
    int mWidth;
    int mHeight;
    int mCurrentPreset;
    MorphoULLConfig mPresets[ULL_PRESET_MAX];

    Vector<AtomBuffer> mInputBuffers;      /*!> snapshots */

    PictureThread::MetaData mSnapMetadata;  /*!> metadata of the first snapshot taken */

    /**
     * algorithm external status
     */
    ULLMode        mUserMode; /*!> User selected mode of operation of the ULL feature */
    Mutex          mTrigerMutex; /*!> Protects the trigger variable that is modified in AAAThread but read from ControlThread*/
    bool           mTrigger;  /*!> Only valid if in auto mode. It signal that ULL shoudl be used. */
};
}  //namespace android
#endif /* ANDROID_LIBCAMERA_ULTRALOWLIGHT_H_ */
