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
#define LOG_TAG "Camera_ULL"
//#define LOG_NDEBUG 0

#include "UltraLowLight.h"
#include "AtomCommon.h"
#include "LogHelper.h"

#include "morpho_image_stabilizer3.h"


namespace android {


const char UltraLowLight::MORPHO_INPUT_FORMAT[] = "YUV420_SEMIPLANAR";  // This should be equivalent to NV12, our default
const int UltraLowLight::MORPHO_INPUT_FORMAT_V4L2 = V4L2_PIX_FMT_NV12; // Keep these two constants in sync
const int UltraLowLight::MAX_INPUT_BUFFERS = 3;

int UltraLowLight::ULL_ACTIVATE_ISO_THRESHOLD = 600;
int UltraLowLight::ULL_DEACTIVATE_ISO_THRESHOLD = 400;
int UltraLowLight::ULL_ACTIVATE_EXPTIME_THRESHOLD = 6000;
int UltraLowLight::ULL_DEACTIVATE_EXPTIME_THRESHOLD = 4000;

/**
 * \struct MorphoULL
 * Morpho ULL control block. Contains the handle to the algorithm context
 * and the custom morpho types.
 */
struct UltraLowLight::MorphoULL {
    unsigned char           *workingBuffer;
    morpho_ImageStabilizer3 stab;
    morpho_ImageData input_image[UltraLowLight::MAX_INPUT_BUFFERS];
    morpho_ImageData output_image;
    MorphoULL(): workingBuffer(NULL) {};
};


UltraLowLight::UltraLowLight() : mMorphoCtrl(NULL),
                                 mCallbacks(Callbacks::getInstance()),
                                 mState(ULL_STATE_NULL),
                                 mWidth(0),
                                 mHeight(0),
                                 mUserMode(ULL_OFF),
                                 mTrigger(false)
{

    mMorphoCtrl = new MorphoULL();
    if (mMorphoCtrl != NULL)
        mState = ULL_STATE_UNINIT;

    mPresets[0] = MorphoULLConfig( 100, 10, 0, 3, 1, 1, MORPHO_IMAGE_STABILIZER3_NR_SUPERFINE, MORPHO_IMAGE_STABILIZER3_NR_SUPERFINE );
    mPresets[1] = MorphoULLConfig( 100, 10, 0, 3, 3, 3, 0, 0 );
}

UltraLowLight::~UltraLowLight()
{
    LOG1("@%s :state=%d", __FUNCTION__, mState);
    if (mState > ULL_STATE_UNINIT)
        deinitMorphoLib();

    if (mMorphoCtrl != NULL)
        delete mMorphoCtrl;
}

void UltraLowLight::setMode(ULLMode aMode) {

    mUserMode = aMode;
    if (mUserMode == ULL_ON)
        mTrigger = true;
}

/**
 * init()
 * Initialize the ULL library
 *
 * \param w width of the images to process
 * \param h height of the images to process
 * \param aPreset One of the ULL algorithm presets
 */
status_t UltraLowLight::init( int w, int h, int aPreset)
{
    LOG1("@%s : w=%d h=%d preset=%d", __FUNCTION__, w, h, aPreset);
    status_t ret = NO_ERROR;
    nsecs_t startTime;

    if (aPreset >= ULL_PRESET_MAX)
        return BAD_VALUE;

    if (mUserMode == ULL_OFF)
        return INVALID_OPERATION;

    switch (mState) {
    case ULL_STATE_UNINIT:
    case ULL_STATE_INIT:
        startTime= systemTime();
        ret = initMorphoLib(w, h, aPreset);
        LOG1("ULL init completed (ret=%d) in %u ms", ret, (unsigned)((systemTime() - startTime) / 1000000))
        break;

    case ULL_STATE_READY:
        mInputBuffers.clear();
        ret = initMorphoLib(w, h, aPreset);
        break;

    case ULL_STATE_NULL:
        LOGE("Object creation failed. Could not allocate algorithm control block");
        ret = NO_MEMORY;
        break;

    case ULL_STATE_PROCESSING:
    default:
        ret = INVALID_OPERATION;
        break;
    }

    if (ret ==  NO_ERROR)
        mState = ULL_STATE_INIT;
    return ret;

}

status_t UltraLowLight::deinit()
{
    LOG1("@%s ", __FUNCTION__);
    status_t ret = NO_ERROR;

    switch (mState) {
    case ULL_STATE_UNINIT:
        // do nothing
        break;

    case ULL_STATE_INIT:
         deinitMorphoLib();
        break;

    case ULL_STATE_READY:
        mInputBuffers.clear();
        deinitMorphoLib();
        break;

    case ULL_STATE_NULL:
        LOGE("Object creation failed. Could not allocate algorithm control block");
        ret = NO_MEMORY;
        break;

    case ULL_STATE_PROCESSING:
    default:
        ret = INVALID_OPERATION;
        break;
    }
    return ret;
}

status_t UltraLowLight::addInputFrame(AtomBuffer *snap, AtomBuffer *pv)
{
    LOG1("@%s number of buffers currently stored %d ", __FUNCTION__, mInputBuffers.size());
    status_t ret = NO_ERROR;
    unsigned int maxBufs = MAX_INPUT_BUFFERS;

    if (mState != ULL_STATE_INIT)
        return INVALID_OPERATION;

    if (mInputBuffers.size() >= maxBufs)
        return INVALID_OPERATION;

    if ((snap->width != mWidth) || (snap->height != mHeight)) {
        LOGE("Buffer dimension is not the same the library is configured for");
        return INVALID_OPERATION;
    }

    mInputBuffers.push(*snap);

    /**
     * We use the first postview as the final one
     */
    if (mInputBuffers.size() == 1)
        mOutputPostView = *pv;


    if (mInputBuffers.size() == maxBufs) {
        ret = configureMorphoLib();
        if (ret == NO_ERROR)
            mState = ULL_STATE_READY;
    }

    return ret;
}

status_t UltraLowLight::addSnapshotMetadata(PictureThread::MetaData &metadata)
{
    mSnapMetadata = metadata;
    return NO_ERROR;
}

status_t UltraLowLight::getOuputResult(AtomBuffer *snap, AtomBuffer * pv, PictureThread::MetaData *metadata)
{
    LOG1("@%s", __FUNCTION__);

    if ( (snap == NULL) || (pv == NULL) || (metadata == NULL))
        return BAD_VALUE;

    if (mState != ULL_STATE_DONE)
        return INVALID_OPERATION;

    *snap = mOutputBuffer;
    *pv = mOutputPostView;
    *metadata = mSnapMetadata;
    mState = ULL_STATE_INIT;

    return NO_ERROR;
}

bool UltraLowLight::isActive()
{
    LOG1("@%s:%s",__FUNCTION__, mUserMode==ULL_OFF? "false":"true");
    return mUserMode==ULL_OFF? false: true;
}

bool UltraLowLight::trigger()
{
    Mutex::Autolock lock(mTrigerMutex);

    if (mUserMode == ULL_ON)
        return true;

    return mTrigger;
}

status_t UltraLowLight::process()
{
    LOG1("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    if (mState != ULL_STATE_READY)
        return INVALID_OPERATION;

    nsecs_t startTime = systemTime();
    mState = ULL_STATE_PROCESSING;
    int i;

    /* Initialize the morpho specific input buffer structures */
    for (i = 0; i < MAX_INPUT_BUFFERS; i++) {
        AtomToMorphoBuffer(&mInputBuffers[i], &mMorphoCtrl->input_image[i]);
    }

    /**
     * We use the first input buffer as output target. This is done to save
     * an extra buffer.
     */
    mMorphoCtrl->output_image = mMorphoCtrl->input_image[0];


    ret = morpho_ImageStabilizer3_start( &mMorphoCtrl->stab,
                                         &mMorphoCtrl->output_image,
                                         MAX_INPUT_BUFFERS );
    if (ret != MORPHO_OK) {
        LOGE("Processing start failed %d", ret);
        ret = UNKNOWN_ERROR;
        goto processComplete;
    }


    /* Motion detection and alpha blending */
    for (i = 0; i < MAX_INPUT_BUFFERS; i++) {

       morpho_MotionData motion;

       ret = morpho_ImageStabilizer3_detectMotion( &mMorphoCtrl->stab,
                                                   &mMorphoCtrl->input_image[i],
                                                   &motion );

       if (ret != MORPHO_OK) {
           LOGE("Processing detect Motion for buffer %d failed %d", i, ret);
           ret = UNKNOWN_ERROR;
           goto processComplete;
       }

       ret = morpho_ImageStabilizer3_render( &mMorphoCtrl->stab,
                                             &mMorphoCtrl->input_image[i],
                                             &motion );
       if (ret != MORPHO_OK) {
           LOGE("Processing render for buffer %d failed %d", i, ret);
           ret = UNKNOWN_ERROR;
           goto processComplete;
       }
    }

    /* Noise Reduction */
    ret = morpho_ImageStabilizer3_reduceNoise( &mMorphoCtrl->stab );
    if (ret != MORPHO_OK) {
        LOGE("Processing reduce noise failed %d", ret);
        ret = UNKNOWN_ERROR;
    } else
        ret = NO_ERROR;

    /* Render final image */
    ret = morpho_ImageStabilizer3_finalize( &mMorphoCtrl->stab );
    if (ret != MORPHO_OK)
       LOGW("Error closing the library");

processComplete:
    mOutputBuffer = mInputBuffers[0];
    mState = ULL_STATE_DONE;
    mInputBuffers.clear();
    LOG1("ULL Processing completed (ret=%d) in %u ms", ret, (unsigned)((systemTime() - startTime) / 1000000))
    return ret;
}


/****************************************************************************
 *  PRIVATE PARTS
 ****************************************************************************/

status_t UltraLowLight::initMorphoLib(int w, int h, int idx)
{
    LOG1("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;
    int workingBufferSize;

    workingBufferSize = morpho_ImageStabilizer3_getBufferSize( w, h,
                                                               MAX_INPUT_BUFFERS,
                                                               MORPHO_INPUT_FORMAT);
    LOG1("ULL working buf size %d", workingBufferSize);
    if (w != mWidth || h != mHeight) {
        if (mMorphoCtrl->workingBuffer != NULL)
            delete[] mMorphoCtrl->workingBuffer;
        mMorphoCtrl->workingBuffer = new unsigned char[workingBufferSize];
    }

    if (mMorphoCtrl->workingBuffer == NULL) {
        ret = NO_MEMORY;
        goto bail;
    }

    memset(&mMorphoCtrl->stab,0,sizeof(morpho_ImageStabilizer3));

    ret = morpho_ImageStabilizer3_initialize( &mMorphoCtrl->stab,
                                              mMorphoCtrl->workingBuffer,
                                              workingBufferSize );
    if (ret != MORPHO_OK) {
        LOGE("Error initializing working buffer to library");
        ret = NO_INIT;
        goto bailFree;
    }

    mCurrentPreset = idx;
    mWidth = w;
    mHeight = h;

    mState = ULL_STATE_INIT;

bail:
    return ret;

bailFree:
    delete[] mMorphoCtrl->workingBuffer;
    return ret;
}

void UltraLowLight::deinitMorphoLib()
{
    LOG1("@%s ", __FUNCTION__);

    mState = ULL_STATE_UNINIT;
    mWidth = 0;
    mHeight = 0;
    mCurrentPreset = 0;
    if (mMorphoCtrl->workingBuffer != NULL)
        delete[] mMorphoCtrl->workingBuffer;

    memset(mMorphoCtrl,0,sizeof(UltraLowLight::MorphoULL));

}


#define PRINT_ERROR_AND_BAIL(x)     if (ret != MORPHO_OK) {\
                                        LOGE(x);\
                                        return NO_INIT;\
                                      }

/**
 * Apply the current preset settings to the initialized library
 */
status_t UltraLowLight::configureMorphoLib()
{
    LOG1("@%s preset = %d", __FUNCTION__, mCurrentPreset);
    int ret;
    unsigned int pix;
    morpho_RectInt detection_rect;
    morpho_RectInt margin_rect;
    MorphoULLConfig cfg = mPresets[mCurrentPreset];

    /* Image format setting */
    ret = morpho_ImageStabilizer3_setImageFormat( &mMorphoCtrl->stab, MORPHO_INPUT_FORMAT );
    PRINT_ERROR_AND_BAIL("Failed to configure image format");

    /* Motion detection range setting */
    detection_rect.sx = 0;
    detection_rect.sy = 0;
    detection_rect.ex = mWidth;
    detection_rect.ey = mHeight;

    ret = morpho_ImageStabilizer3_setDetectionRect( &mMorphoCtrl->stab, &detection_rect );
    PRINT_ERROR_AND_BAIL("Failed to configure detection rectangle");

    /* Error threshold */
    pix = mWidth > mHeight ? mWidth : mHeight;
    pix = (pix *cfg.margin / 100) >> 1;
    pix &= 0xFFFFFFFE;

    margin_rect.sx = pix;
    margin_rect.sy = pix;
    margin_rect.ex = mWidth - pix;
    margin_rect.ey = mHeight - pix;

    ret = morpho_ImageStabilizer3_setMarginOfMotion( &mMorphoCtrl->stab, &margin_rect );
    PRINT_ERROR_AND_BAIL(" Failed to configure setMarginOfMotion");

    /* Image quality adjustment parameter */
    ret = morpho_ImageStabilizer3_setGain(&mMorphoCtrl->stab, cfg.gain );
    PRINT_ERROR_AND_BAIL(" Failed to configure setGain");

    if (cfg.block_size == 4 || cfg.block_size == 8 || cfg.block_size == 16) {
        ret = morpho_ImageStabilizer3_setObjBlurBlockSize(&mMorphoCtrl->stab, cfg.block_size );
        PRINT_ERROR_AND_BAIL(" Failed to configure  setObjBlurBlockSize");
    }

    ret = morpho_ImageStabilizer3_setObjBlurCorrectionLevel(&mMorphoCtrl->stab, cfg.obc_level );
    PRINT_ERROR_AND_BAIL(" Failed to configure setObjBlurCorrectionLevel");

    ret = morpho_ImageStabilizer3_setLumaNoiseReductionLevel(&mMorphoCtrl->stab, cfg.y_nr_level );
    PRINT_ERROR_AND_BAIL(" Failed to configure setNoiseReductionLevelLuma");

    ret = morpho_ImageStabilizer3_setChromaNoiseReductionLevel(&mMorphoCtrl->stab, cfg.c_nr_level );
    PRINT_ERROR_AND_BAIL(" Failed to configure setNoiseReductionLevelChroma");

    ret = morpho_ImageStabilizer3_setLumaNoiseReductionType(&mMorphoCtrl->stab, cfg.y_nr_type );
    PRINT_ERROR_AND_BAIL(" Failed to configure setNoiseReductionLevelLuma");

    ret = morpho_ImageStabilizer3_setChromaNoiseReductionType(&mMorphoCtrl->stab, cfg.c_nr_type );
    PRINT_ERROR_AND_BAIL(" Failed to configure setNoiseReductionLevelChroma");

    return NO_ERROR;
}

void UltraLowLight::AtomToMorphoBuffer(const AtomBuffer *atom, void* m)
{
    unsigned int Ysize = atom->width * atom->height;
    morpho_ImageData* morpho = (morpho_ImageData*)m;
    void* p;
    if (atom->shared)
        p = (void *) *((char **)atom->buff->data);
    else
        p = atom->buff->data;

    morpho->width = atom->width;
    morpho->height = atom->height;
    morpho->dat.semi_planar.y = p;
    morpho->dat.semi_planar.uv = (void*)((unsigned int) p + Ysize);
}

/**
 *  update the status of the trigger for ULL capture.
 *  This method is used to update the current exposure values and let the ULL
 *  class to decide whether ULL capture should be used or not.
 *
 *  This method is called from the 3A Thread for each 3A iteration.
 *  The status of the trigger can be queried using the method trigger()
 *  \sa trigger()
 *
 *  \param expInfo [in] current exposure settings
 *  \param iso [in] current iso value in use by the sensor
 *  \return true if the state of the trigger changed
 *  \return false if the trigger state remained the same.
 */
bool UltraLowLight::updateTrigger(SensorAeConfig &expInfo, int iso)
{
    LOG2("%s", __FUNCTION__);
    Mutex::Autolock lock(mTrigerMutex);
    int expTime = expInfo.expTime;
    bool change = false;

    if ((iso >= ULL_ACTIVATE_ISO_THRESHOLD) &&
        (expTime >= ULL_ACTIVATE_EXPTIME_THRESHOLD)) {
        change = (mTrigger? false:true);
        mTrigger = true;
    }

    if ((iso <= ULL_DEACTIVATE_ISO_THRESHOLD) &&
        (expTime <= ULL_DEACTIVATE_EXPTIME_THRESHOLD)) {
        change = (mTrigger? true:false);
        mTrigger = false;
    }
    if (change)
        LOG1("trigger %s, iso %d, expTime %d",mTrigger?"true":"false", iso, expTime);

    return change;
}

} //namespace android