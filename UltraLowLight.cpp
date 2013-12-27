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
#include "CameraDump.h"
#include "LogHelper.h"
#include "PlatformData.h"

#include "morpho_image_stabilizer3.h"

#include "ia_cp_types.h"
#include "ia_types.h"
#include "ia_cp.h"


namespace android {

#ifdef ENABLE_INTEL_EXTRAS
const char UltraLowLight::MORPHO_INPUT_FORMAT[] = "YUV420_SEMIPLANAR";  // This should be equivalent to NV12, our default

// ULL bright threshold: from Normal to ULL
float UltraLowLight::ULL_ACTIVATION_GAIN_THRESHOLD = 4.0;
static status_t ia_error_to_status_t(ia_err status);

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

UltraLowLight::UltraLowLight(Callbacks *callbacks) : mMorphoCtrl(NULL),
                                 mCallbacks(callbacks),
                                 mState(ULL_STATE_NULL),
                                 mULLCounter(0),
                                 mWidth(0),
                                 mHeight(0),
                                 mCurrentPreset(0),
                                 mUserMode(ULL_OFF),
                                 mTrigger(false),
                                 mUseIntelULL(PlatformData::useIntelULL())
{
    if (mUseIntelULL) {
        mState = ULL_STATE_UNINIT;
    } else {
        mMorphoCtrl = new MorphoULL();
        if (mMorphoCtrl != NULL)
            mState = ULL_STATE_UNINIT;

        mPresets[0] = MorphoULLConfig(100, 10, 0, 3, 1, 1, MORPHO_IMAGE_STABILIZER3_NR_SUPERFINE, MORPHO_IMAGE_STABILIZER3_NR_SUPERFINE);
        mPresets[1] = MorphoULLConfig(100, 10, 0, 3, 3, 3, 0, 0);
    }
}

UltraLowLight::~UltraLowLight()
{
    LOG1("@%s :state=%d", __FUNCTION__, mState);

    deinit();

    if (mMorphoCtrl != NULL) {
        delete mMorphoCtrl;
        mMorphoCtrl = NULL;
    }
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

    State aState = getState();

    switch (aState) {
    case ULL_STATE_UNINIT:
    case ULL_STATE_INIT:
    case ULL_STATE_DONE:
        startTime= systemTime();
        if (mUseIntelULL)
            ret = initIntelULL(w, h);
        else
            ret = initMorphoLib(w, h, aPreset);

        LOG1("ULL init completed (ret=%d) in %u ms", ret, (unsigned)((systemTime() - startTime) / 1000000))
        break;

    case ULL_STATE_READY:
        if (mUseIntelULL) {
            deinitIntelULL();
            mInputBuffers.clear();
            ret = initIntelULL(w, h);
        } else {
            deinitMorphoLib();
            mInputBuffers.clear();
            ret = initMorphoLib(w, h, aPreset);
        }
        break;

    case ULL_STATE_NULL:
        LOGE("Object creation failed. Could not allocate algorithm control block");
        ret = NO_MEMORY;
        break;

    case ULL_STATE_PROCESSING:
    default:
        ret = INVALID_OPERATION;
        LOGE("Trying to initialize ULL on an invalid state %d",aState);
        break;
    }

    if (ret ==  NO_ERROR)
        setState(ULL_STATE_INIT);
    else
        LOGE("Error initializing ULL");
    return ret;

}

status_t UltraLowLight::deinit()
{
    LOG1("@%s ", __FUNCTION__);
    status_t ret = NO_ERROR;

    switch (getState()) {
    case ULL_STATE_UNINIT:
        // do nothing
        break;

    case ULL_STATE_CANCELING:
    case ULL_STATE_DONE:
    case ULL_STATE_INIT:
        if (mUseIntelULL)
            deinitIntelULL();
        else
            deinitMorphoLib();
        setState(ULL_STATE_UNINIT);
        break;

    case ULL_STATE_READY:
        mInputBuffers.clear();
        if (mUseIntelULL)
            deinitIntelULL();
        else
            deinitMorphoLib();
        setState(ULL_STATE_UNINIT);
        break;

    case ULL_STATE_NULL:
        LOGE("Object creation failed. Could not allocate algorithm control block");
        ret = NO_MEMORY;
        break;

    case ULL_STATE_PROCESSING:
    default:
        LOGW("De-initializing library in an invalid state: %d", getState());
        ret = INVALID_OPERATION;
        break;
    }
    return ret;
}

int UltraLowLight::getULLBurstLength()
{
    return MAX_INPUT_BUFFERS;
}

status_t UltraLowLight::addInputFrame(AtomBuffer *snap, AtomBuffer *pv)
{
    LOG1("@%s number of buffers currently stored %d ", __FUNCTION__, mInputBuffers.size());
    status_t ret = NO_ERROR;
    unsigned int maxBufs = MAX_INPUT_BUFFERS;

    if (snap == NULL || pv == NULL)
        return BAD_VALUE;

    if (getState() != ULL_STATE_INIT)
        return INVALID_OPERATION;

    if (mInputBuffers.size() >= maxBufs)
        return INVALID_OPERATION;

    if ((snap->width != mWidth) || (snap->height != mHeight)) {
        LOGE("Buffer dimension is not the same the library is configured for");
        return INVALID_OPERATION;
    }

    mInputBuffers.push(*snap);

    // Store the postview here, although no processing done with it yet.
    // This is to make more uniform buffer flow with the snapshot buffs.
    mPostviewBuffs.push(*pv);

    if (mInputBuffers.size() == maxBufs) {
        if (mUseIntelULL) {
            ret = NO_ERROR; // Intel ULL is configured when initialized
        } else {
            ret = configureMorphoLib();
        }

        if (ret == NO_ERROR)
            setState(ULL_STATE_READY);
    }

    return ret;
}

status_t UltraLowLight::addSnapshotMetadata(PictureThread::MetaData &metadata)
{
    mSnapMetadata = metadata;

    if (mUseIntelULL && mSnapMetadata.aeConfig != NULL) {
        LOG1("Passing snapshot metadata to Intel ULL");

        /* TODO: pass the tuning parameters here, all are set to default currently */
        mIntelUllCfg->deghost        = 110;
        mIntelUllCfg->luma_denoise   = 127;
        mIntelUllCfg->chroma_denoise = 127;
        mIntelUllCfg->apex_av        = mSnapMetadata.aeConfig->aecApexAv;
        mIntelUllCfg->apex_sv        = mSnapMetadata.aeConfig->aecApexSv;
        mIntelUllCfg->apex_tv        = mSnapMetadata.aeConfig->aecApexTv;
        mIntelUllCfg->exposure       = mSnapMetadata.aeConfig->expTime;
        mIntelUllCfg->ev_bias        = mSnapMetadata.aeConfig->evBias;
        mIntelUllCfg->digital_gain   = mSnapMetadata.aeConfig->digitalGain;
        mIntelUllCfg->total_gain     = mSnapMetadata.aeConfig->totalGain;
        mIntelUllCfg->aperture_num   = mSnapMetadata.aeConfig->aperture_num;
        mIntelUllCfg->aperture_denum = mSnapMetadata.aeConfig->aperture_denum;
    }

    return NO_ERROR;
}

/**
 * Return the AtomBuffer that contains the result of the ULL process.
 * The snapshot buffer is actually the first input buffer and it needs
 * to be returned together with the others.
 *
 * Please note that the ULL id is incremented after this call
 *
 * \param snap [out] pointer to store the ULL outcome
 * \param pv [out] pointer to store the postview image
 * \param metadata [out] pointer to store the metadata needed to encode JPEG
 * \param ULLid [out] pointer to store the id that this ULL is assigned.
 *                   The id is queried before processing start and notified to
 *                   application.
 */
status_t UltraLowLight::getOuputResult(AtomBuffer *snap, AtomBuffer * pv,
                                       PictureThread::MetaData *metadata, int *ULLid)
{
    LOG1("@%s", __FUNCTION__);

    if ( (snap == NULL) || (pv == NULL) || (metadata == NULL))
        return BAD_VALUE;

    if (getState() != ULL_STATE_DONE)
        return INVALID_OPERATION;

    *snap = mOutputBuffer;
    *pv = mOutputPostView;
    *metadata = mSnapMetadata;
    *ULLid = mULLCounter;
    mULLCounter++;

    return NO_ERROR;
}

/**
 * retrieves the input buffers after processing has completed
 * The input buffers are then returned to the pool of buffers
 */
status_t UltraLowLight::getInputBuffers(Vector<AtomBuffer> *inputs)
{
    LOG1("@%s: size = %d", __FUNCTION__, mInputBuffers.size());

    if (inputs == NULL) {
        LOGE("Invalid parameter");
        return BAD_VALUE;
    }

    Vector<AtomBuffer>::iterator it = mInputBuffers.begin();

    for (;it != mInputBuffers.end(); ++it) {
        it->status = FRAME_STATUS_OK;
        inputs->push(*it);
    }

    mInputBuffers.clear();
    // Reset the state back to INIT
    setState(ULL_STATE_INIT);
    return NO_ERROR;
}

/**
 * returns the postview buffers given as input with addInputFrame()
 * NOTE: no processing done for the postviews at the moment
 */
status_t UltraLowLight::getPostviewBuffers(Vector<AtomBuffer> *postviews)
{
    if (postviews == NULL) {
        LOGE("@%s: Null vector given. Invalid.", __FUNCTION__);
        return BAD_VALUE;
    }

    // No need to iterate, like in getInputBuffers(), as
    // we don't need to alter buffer status (at the moment).
    *postviews = mPostviewBuffs;
    mPostviewBuffs.clear();

    return NO_ERROR;
}

bool UltraLowLight::isActive()
{
    LOG1("@%s:%s",__FUNCTION__, mUserMode==ULL_OFF? "false":"true");
    return mUserMode==ULL_OFF? false: true;
}

bool UltraLowLight::isProcessing()
{
    Mutex::Autolock lock(mStateMutex);
    LOG1("@%s:%s",__FUNCTION__, mState==ULL_STATE_PROCESSING? "true":"false");
    return mState==ULL_STATE_PROCESSING? true: false;
}
/**
 * Cancels an ongoing ULL processing
 * The cancellation is not synchronous. The processing cannot be stopped
 * this method just notifies the ULL object that there is no need to compress
 * the resulting image once the processing completes.
 *
 * If this method is called in any other state than ULL_STATE_PROCESSING
 * the method is ignored
 *
 */
status_t UltraLowLight::cancelProcess()
{
   LOG1("@%s", __FUNCTION__);
   Mutex::Autolock lock(mStateMutex);

   if (mState != ULL_STATE_PROCESSING) {
       LOGW("trying to cancel ULL when ULL is not in progress, ignoring request");
       return INVALID_OPERATION;
   }

   mState = ULL_STATE_CANCELING;
   return NO_ERROR;
}

bool UltraLowLight::trigger()
{
    Mutex::Autolock lock(mStateMutex);

    // ULL is ready to start a capture in one of these 3 states
    if ( (mState != ULL_STATE_INIT) &&
         (mState != ULL_STATE_UNINIT) &&
         (mState != ULL_STATE_DONE))
        return false;

    if (mUserMode == ULL_ON)
        return true;

    return mTrigger;
}

status_t UltraLowLight::process()
{
    LOG1("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    if (getState() != ULL_STATE_READY)
        return INVALID_OPERATION;

    nsecs_t startTime = systemTime();
    setState(ULL_STATE_PROCESSING);

    if (mUseIntelULL) {
        ret = processIntelULL();
    } else {
        ret = processMorphoULL();
    }

    LOG1("ULL Processing completed (ret=%d) in %u ms", ret, (unsigned)((systemTime() - startTime) / 1000000))

    return ret;
}


/****************************************************************************
 *  PRIVATE PARTS
 ****************************************************************************/

static status_t ia_error_to_status_t(ia_err status)
{
    switch (status) {
    case ia_err_none:
        return NO_ERROR;
    case ia_err_general:
        return UNKNOWN_ERROR;
    case ia_err_nomemory:
        return NO_MEMORY;
    case ia_err_data:
        return BAD_VALUE;
    case ia_err_internal:
        return UNKNOWN_ERROR;
    case ia_err_argument:
        return BAD_VALUE;
    default:
        return UNKNOWN_ERROR;
    }
}

status_t UltraLowLight::initIntelULL(int w, int h)
{
    LOG1("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    mIntelUllCfg = new ia_cp_ull_cfg;
    if (!mIntelUllCfg)
        return NO_MEMORY;

    mCurrentPreset = 0;
    mWidth = w;
    mHeight = h;
    mInputBuffers.clear();

    return ret;
}

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
        if (mMorphoCtrl->workingBuffer != NULL) {
            delete[] mMorphoCtrl->workingBuffer;
            mMorphoCtrl->workingBuffer = NULL;
        }
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
    mInputBuffers.clear();

bail:
    return ret;

bailFree:
    deinitMorphoLib();
    return ret;
}

void UltraLowLight::deinitIntelULL()
{
    LOG1("@%s", __FUNCTION__);

    if (mIntelUllCfg) {
        delete mIntelUllCfg;
        mIntelUllCfg = NULL;
    }

    mWidth = 0;
    mHeight = 0;
    mCurrentPreset = 0;
}

void UltraLowLight::deinitMorphoLib()
{
    LOGE("@%s ", __FUNCTION__);
    status_t ret;

    ret = morpho_ImageStabilizer3_finalize( &mMorphoCtrl->stab );
    if (ret != MORPHO_OK)
       LOGW("Error closing the ImageSolid library");

    mWidth = 0;
    mHeight = 0;
    mCurrentPreset = 0;
    freeWorkingBuffer();

    // Blank the Morpho control Block
    memset(mMorphoCtrl,0,sizeof(UltraLowLight::MorphoULL));
}

status_t UltraLowLight::processIntelULL()
{
    LOG1("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    ia_frame out, out_pv;
    ia_frame * input = new ia_frame[mInputBuffers.size()];
    if (input == NULL) {
        LOGE("Input ia_frame sequence allocation failed.");
        return NO_MEMORY;
    }

    int i = 0;
    Vector<AtomBuffer>::const_iterator end = mInputBuffers.end();
    for (Vector<AtomBuffer>::const_iterator iter = mInputBuffers.begin(); iter != end; ++iter) {
        AtomToIaFrameBuffer(iter, &input[i++]);
        if (gLogLevel & CAMERA_DEBUG_ULL_DUMP) {
            String8 yuvName("/data/ull_yuv_dump_");
            yuvName.appendFormat("id_%d_%d.yuv",mULLCounter,i);
            CameraDump::dumpAtom2File(iter, yuvName.string());
        }
    }

    mOutputBuffer = mInputBuffers[0];
    mOutputPostView = mPostviewBuffs[0];
    AtomToIaFrameBuffer(&mOutputBuffer, &out);
    AtomToIaFrameBuffer(&mOutputPostView, &out_pv);

    LOG1("Intel ULL processing...");
    ia_err error = ia_cp_ull_compose(&out, &out_pv, input, input, mInputBuffers.size(), mIntelUllCfg);
    if (error != ia_err_none) {
        LOGE("Intel ULL failed with error status %d", error);
        ret = ia_error_to_status_t(error);
    }

    if (getState() == ULL_STATE_PROCESSING) {
        setState(ULL_STATE_DONE);
        mInputBuffers.removeAt(0);
        mPostviewBuffs.removeAt(0);
        if (gLogLevel & CAMERA_DEBUG_ULL_DUMP) {
           String8 yuvName("/data/ull_yuv_processed_");
           yuvName.appendFormat("id_%d.yuv",mULLCounter);
           CameraDump::dumpAtom2File(&mOutputBuffer, yuvName.string());
        }
    } else {
        LOGW("ULL was canceled during processing state = %d",getState());
    }

    if (input != NULL)
        delete[] input;

    return ret;
}

status_t UltraLowLight::processMorphoULL()
{
    LOG1("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;
    int i;

    /* Initialize the morpho specific input buffer structures */
    for (i = 0; i < MAX_INPUT_BUFFERS; i++) {
        if (gLogLevel & CAMERA_DEBUG_ULL_DUMP) {
            String8 yuvName("/data/ull_yuv_dump_");
            yuvName.appendFormat("id_%d_%d.yuv",mULLCounter,i);
            CameraDump::dumpAtom2File(&mInputBuffers[i], yuvName.string());
        }
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

processComplete:
    /* Render final image */
    ret = morpho_ImageStabilizer3_finalize( &mMorphoCtrl->stab );
    if (ret != MORPHO_OK)
       LOGW("Error closing the library");

    if (getState() == ULL_STATE_PROCESSING) {
        setState(ULL_STATE_DONE);
        mOutputBuffer = mInputBuffers[0];
        mInputBuffers.removeAt(0);
        mPostviewBuffs.removeAt(0);
        if (gLogLevel & CAMERA_DEBUG_ULL_DUMP) {
           String8 yuvName("/data/ull_yuv_processed_");
           yuvName.appendFormat("id_%d.yuv",mULLCounter);
           CameraDump::dumpAtom2File(&mOutputBuffer, yuvName.string());
        }
    } else {
        LOGW("ULL was canceled during processing state = %d",getState());
    }

    return ret;
}

void UltraLowLight::freeWorkingBuffer()
{
    if (mMorphoCtrl->workingBuffer != NULL) {
        delete[] mMorphoCtrl->workingBuffer;
        mMorphoCtrl->workingBuffer = NULL;
    }

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

void UltraLowLight::AtomToIaFrameBuffer(const AtomBuffer * atom, ia_frame * frame)
{
    LOG1("@%s (%dx%d)", __FUNCTION__, atom->width, atom->height);

    frame->format = ia_frame_format_nv12;
    frame->data = atom->dataPtr;
    frame->width = atom->width;
    frame->height = atom->height;
    frame->stride = atom->bpl;
    frame->size = atom->size;
    frame->rotation = 0;
}

void UltraLowLight::AtomToMorphoBuffer(const AtomBuffer *atom, void* m)
{
    unsigned int Ysize = atom->width * atom->height;
    morpho_ImageData* morpho = (morpho_ImageData*)m;
    void* p = atom->dataPtr;

    morpho->width = atom->width;
    morpho->height = atom->height;
    morpho->dat.semi_planar.y = p;
    morpho->dat.semi_planar.uv = (void*)((unsigned int) p + Ysize);
}

/**
 *  Update the status of the trigger for ULL capture.
 *  This method is used to update the ULL decision from AE.
 *
 *  This method is called from the 3A Thread for each 3A iteration.
 *  The status of the trigger can be queried using the method trigger()
 *
 *  \param trigger [in] AE decision whether to trigger ULL
 *  \return true if the state of the trigger changed
 *  \return false if the trigger state remained the same.
 */
bool UltraLowLight::updateTrigger(bool trigger)
{
    LOG2("%s", __FUNCTION__);
    Mutex::Autolock lock(mStateMutex);
    bool change = false;

    change = mTrigger == trigger ? false:true;
    mTrigger = trigger;

    if (change)
        LOG1("New trigger: %s", mTrigger?"true":"false");

    return change;
}

/**
 * Changes the state of the ULL algorithm in a thread safe way
 * This is an internal method to be used by other methods that do not need to
 * take the lock all the time.
 */
void UltraLowLight::setState(enum State aState)
{
    Mutex::Autolock lock(mStateMutex);
    mState = aState;
}

/**
 * Queries the state of the ULL algorithm in a thread safe way
 * This is an internal method to be used by other methods that do not need to
 * take the lock all the time.
 */
UltraLowLight::State UltraLowLight::getState()
{
    Mutex::Autolock lock(mStateMutex);
    return mState;
}

#endif // ENABLE_INTEL_EXTRAS
} //namespace android
