/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "IntelCamera"
#include <utils/Log.h>

#include "IntelCamera.h"


#ifndef TRUE
#define TRUE   1
#endif
#ifndef FALSE
#define FALSE  0
#endif

namespace android {

  static intel_fmt_list_t intel_fmt_list[] = {
    { INTEL_PIX_FMT_RGB565 ,16 },
    { INTEL_PIX_FMT_BGR32, 32 },
    { INTEL_PIX_FMT_YUYV, 16 },
    { INTEL_PIX_FMT_YUV422P, 16 },
    { INTEL_PIX_FMT_YUV420, 12 },
    { INTEL_PIX_FMT_YVU420, 12 },
    { INTEL_PIX_FMT_NV12, 12 },
    { INTEL_PIX_FMT_JPEG, 12 },
    { INTEL_PIX_FMT_RAW08, 8 },
    { INTEL_PIX_FMT_RAW10, 16 },
    { INTEL_PIX_FMT_RAW12, 16 }
  };

  static adv_param_t default_adv_params[] = {
    {
      {640,480},/* res */
      {{0, 0, 640, 480},CI_ISP_AWB_AUTO,CI_ISP_AWB_AUTO_ON},/* wb */
      {{((640/2)-(50/2)),((480/2)-(50/2)),50,50},{0, 0, 0, 0},{0, 0, 0, 0},CI_ISP_AFSS_OFF},/* af */
      {{3, 1, 516, 388},{0, 0, 640, 480}}/* ae */
    },
    {
      {1280,720},
      {{0, 0, 1280, 720},CI_ISP_AWB_AUTO,CI_ISP_AWB_AUTO_ON},
      {{((1280/2)-(50/2)),((720/2)-(50/2)),50,50},{0, 0, 0, 0},{0, 0, 0, 0},CI_ISP_AFSS_OFF},
      {{6, 2, 516, 388},{0, 0, 1280, 720}}
    },
    {
      {1280,960},
      {{0, 0, 1280, 960},CI_ISP_AWB_AUTO,CI_ISP_AWB_AUTO_ON},
      {{((1280/2)-(50/2)),((960/2)-(50/2)),50,50},{0, 0, 0, 0},{0, 0, 0, 0},CI_ISP_AFSS_OFF},
      {{6, 2, 516, 388},{0, 0, 1280, 960}}
    },
    {
      {1920,1080},
      {{0, 0, 1920, 1080},CI_ISP_AWB_AUTO,CI_ISP_AWB_AUTO_ON},
      {{((1920/2)-(50/2)),((1080/2)-(50/2)),50,50},{0, 0, 0, 0},{0, 0, 0, 0},CI_ISP_AFSS_OFF},
      {{6, 2, 516, 388},{0, 0, 1920, 1080}}
    },
    {
      {2592,1944},
      {{0, 0, 2592, 1944},CI_ISP_AWB_AUTO,CI_ISP_AWB_AUTO_ON},
      {{((2592/2)-(50/2)),((1944/2)-(50/2)),50,50},{0, 0, 0, 0},{0, 0, 0, 0},CI_ISP_AFSS_OFF},
      {{6, 2, 516, 388},{0, 0, 2592, 1944}}
    }
  };

  /*
typedef enum
  {
    CI_JPEG_HIGH_COMPRESSION,
    CI_JPEG_LOW_COMPRESSION,
    CI_JPEG_01_PERCENTAGE,
    CI_JPEG_20_PERCENTAGE,
    CI_JPEG_30_PERCENTAGE,
    CI_JPEG_40_PERCENTAGE,
    CI_JPEG_50_PERCENTAGE,
    CI_JPEG_60_PERCENTAGE,
    CI_JPEG_70_PERCENTAGE,
    CI_JPEG_80_PERCENTAGE,
    CI_JPEG_90_PERCENTAGE,
    CI_JPEG_99_PERCENTAGE
  }ci_jpeg_ratio;
  */
static pref_map_t pref_jpeg_quality_map[] = {
  { "01", CI_JPEG_01_PERCENTAGE },
  { "20", CI_JPEG_20_PERCENTAGE },
  { "30", CI_JPEG_30_PERCENTAGE },
  { "40", CI_JPEG_40_PERCENTAGE },
  { "50", CI_JPEG_50_PERCENTAGE },
  { "60", CI_JPEG_60_PERCENTAGE },
  { "70", CI_JPEG_70_PERCENTAGE },
  { "80", CI_JPEG_80_PERCENTAGE },
  { "90", CI_JPEG_90_PERCENTAGE },
  { "99", CI_JPEG_99_PERCENTAGE },
  { "100", CI_JPEG_HIGH_COMPRESSION },
  { NULL, 0 }
};
  /*
  <typedef for enum of image effect mode for context config>
  typedef enum {
    <no image effect (bypass)>
    CI_IE_MODE_OFF = 0,
    <Set a fixed chrominance of 128 (neutral grey)>
    CI_IE_MODE_GRAYSCALE,
    <Luminance and chrominance data is being inverted>
    CI_IE_MODE_NEGATIVE,
    <Chrominance is changed to produce a historical like brownish image color>
    CI_IE_MODE_SEPIA,
    <Converting picture to grayscale while maintaining one color component>
    CI_IE_MODE_COLORSEL,
    <Edge detection, will look like an relief made of metal>
    CI_IE_MODE_EMBOSS,
    <Edge detection, will look like a pencil drawing>
    CI_IE_MODE_SKETCH
  }ci_ie_mode;
  */
static pref_map_t pref_color_effect_map[] = {
  { "none", CI_IE_MODE_OFF },
  { "mono", CI_IE_MODE_GRAYSCALE },
  { "negative", CI_IE_MODE_NEGATIVE},
  { "sepia", CI_IE_MODE_SEPIA},
  { "aqua", CI_IE_MODE_COLORSEL},
  { "pastel", CI_IE_MODE_EMBOSS},
  { "whiteboard", CI_IE_MODE_SKETCH},
  { NULL, 0 }
};

  /*
     possible autofocus search strategy modes
     enum ci_isp_afss_mode {
     CI_ISP_AFSS_OFF,
     CI_ISP_AFSS_FULL_RANGE,
     CI_ISP_AFSS_HILLCLIMBING,
     CI_ISP_AFSS_ADAPTIVE_RANGE,
     CI_ISP_AFSS_HELIMORPH_OPT,
     CI_ISP_AFSS_OV2630_LPD4_OPT
     };
  */

static pref_map_t pref_af_map[] = {
  { "off", CI_ISP_AFSS_OFF },
  { "auto", CI_ISP_AFSS_ADAPTIVE_RANGE },
  { "infinity", CI_ISP_AFSS_FULL_RANGE },
  { "macro", CI_ISP_AFSS_OFF },
  { NULL, 0 }
};

  /*
     possible AEC modes
     enum ci_isp_aec_mode {
     CI_ISP_AEC_OFF,
     CI_ISP_AEC_INTEGRAL,
     CI_ISP_AEC_SPOT,
     CI_ISP_AEC_MFIELD5,
     CI_ISP_AEC_MFIELD9
     };
  */
 static pref_map_t pref_aec_map[] = {
  { "off", CI_ISP_AEC_OFF }, //off
  { "on", CI_ISP_AEC_INTEGRAL }, //on
  { "spot", CI_ISP_AEC_SPOT },
  { "mfield5", CI_ISP_AEC_MFIELD5 },
  { "mfield9", CI_ISP_AEC_MFIELD9 },
  { NULL, 0 }
};

  /*
  white balancing modes for the marvin hardware
  enum ci_isp_awb_mode {
    CI_ISP_AWB_COMPLETELY_OFF = 0,
    CI_ISP_AWB_AUTO,
    CI_ISP_AWB_MAN_MEAS,
    CI_ISP_AWB_MAN_NOMEAS,
    CI_ISP_AWB_MAN_PUSH_AUTO,
    CI_ISP_AWB_ONLY_MEAS
  };

  white balancing modes for the marvin hardware
  enum ci_isp_awb_sub_mode {
    CI_ISP_AWB_SUB_OFF = 0,
    CI_ISP_AWB_MAN_DAYLIGHT,
    CI_ISP_AWB_MAN_CLOUDY,
    CI_ISP_AWB_MAN_SHADE,
    CI_ISP_AWB_MAN_FLUORCNT,
    CI_ISP_AWB_MAN_FLUORCNTH,
    CI_ISP_AWB_MAN_TUNGSTEN,
    CI_ISP_AWB_MAN_TWILIGHT,
    CI_ISP_AWB_MAN_SUNSET,
    CI_ISP_AWB_MAN_FLASH,
    CI_ISP_AWB_MAN_CIE_D65,
    CI_ISP_AWB_MAN_CIE_D75,
    CI_ISP_AWB_MAN_CIE_F2,
    CI_ISP_AWB_MAN_CIE_F11,
    CI_ISP_AWB_MAN_CIE_F12,
    CI_ISP_AWB_MAN_CIE_A,
    CI_ISP_AWB_AUTO_ON
  };
  */
static pref_map_t pref_awb_map[] = {
  { "off", CI_ISP_AWB_COMPLETELY_OFF },
  { "auto", CI_ISP_AWB_AUTO },
  { "man-meas", CI_ISP_AWB_MAN_MEAS },
  { "man-nomeas", CI_ISP_AWB_MAN_NOMEAS },
  { "man-push-auto", CI_ISP_AWB_MAN_PUSH_AUTO },
  { "only-meas", CI_ISP_AWB_ONLY_MEAS},
  { NULL, 0 }
};
static pref_map_t pref_awb_sub_map[] = {
  { "auto", CI_ISP_AWB_AUTO_ON },
  { "incandescent", CI_ISP_AWB_MAN_CIE_D65 },
  { "daylight", CI_ISP_AWB_MAN_CIE_F2 },
  { "fluorescent", CI_ISP_AWB_MAN_CIE_F11 },
  { "cloudy", CI_ISP_AWB_MAN_CIE_F12 },
  { "twilight", CI_ISP_AWB_MAN_CIE_A },
  { NULL, 0 }
};

#define CHECK_RET(ret, cond, msg)					\
  if((ret) != (cond)) {							\
    LOGE("%s: %s failed error code = %d", __FUNCTION__, (msg), (ret));	\
  }									\
  else {								\
      LOGV("%s: %s success", __FUNCTION__, (msg));			\
  }
#define CHECK_CI_RET(ret, msg)			\
  CHECK_RET(ret, CI_STATUS_SUCCESS, msg)

#define TURN_ON  1
#define TURN_OFF 0

AdvanceProcess::AdvanceProcess(ci_struct_t *ci_struct, sensor_info_t *snr_info_struct)
  : fpImageProcessAF(NULL),
    fpImageProcessAE(NULL),
    fpImageProcessAWB(NULL),
    fpSetAF(NULL),
    fpSetAE(NULL),
    fpSetAWB(NULL),
    mCI(NULL),
    mSensorInfo(NULL),
    mParamAF(NULL),
    mParamAE(NULL),
    mParamAWB(NULL),
    mImageProcessFlags(0),
    mFinishedAE(FALSE),
    mFinishedAWB(FALSE),
    mFinishedAF(FALSE)
{
     mCI = ci_struct;
     mSensorInfo = snr_info_struct;
     if ( mSensorInfo->type == SENSOR_TYPE_2M ) {
	 fpSetAF = &AdvanceProcess::setAFforSOC;
         fpSetAE = &AdvanceProcess::setAEforSOC;
	 fpSetAWB = &AdvanceProcess::setAWBforSOC;
	 fpImageProcessAF = &AdvanceProcess::imageProcessAFforSOC;
	 fpImageProcessAE = &AdvanceProcess::imageProcessAEforSOC;
	 fpImageProcessAWB = &AdvanceProcess::imageProcessAWBforSOC;
     } else {
	 fpSetAF = &AdvanceProcess::setAFforRAW;
         fpSetAE = &AdvanceProcess::setAEforRAW;
	 fpSetAWB = &AdvanceProcess::setAWBforRAW;
	 fpImageProcessAF = &AdvanceProcess::imageProcessAFforRAW;
	 fpImageProcessAE = &AdvanceProcess::imageProcessAEforRAW;
	 fpImageProcessAWB = &AdvanceProcess::imageProcessAWBforRAW;
     }
}

AdvanceProcess::~AdvanceProcess()
{
    fpSetAF = NULL;
    fpSetAE = NULL;
    fpSetAWB = NULL;
    fpImageProcessAF = NULL;
    fpImageProcessAE = NULL;
    fpImageProcessAWB = NULL;
    mParamAF = NULL;
    mParamAE = NULL;
    mParamAWB = NULL;
}

void AdvanceProcess::setAdvanceParams(unsigned int w, unsigned int h)
{
  int default_adv_param_num =
    sizeof(default_adv_params)/sizeof(adv_param_t);

  if (mSensorInfo->type == SENSOR_TYPE_5M) {
    LOGV("%s: w=%u, h=%u",__func__,w,h);
    for (int i=0; i<default_adv_param_num; i++) {
      resolution_t *res = &default_adv_params[i].res;
      if(res->width == w && res->height == h) {
	mParamAF = &default_adv_params[i].af_param;
	mParamAE = &default_adv_params[i].ae_param;
	mParamAWB = &default_adv_params[i].wb_param;
      }
    }
  } else {
    mParamAF = NULL;
    mParamAE = NULL;
    mParamAWB = NULL;
  }

  mFinishedAE = FALSE;
  mFinishedAWB = FALSE;
  mFinishedAF = FALSE;
}

void AdvanceProcess::advImageProcessAF(void)
{
    mImageProcessLock.lock();
    if (fpImageProcessAF != NULL &&
	isFlagEnabled(IMAGE_PRCOESS_FLAGS_TYPE_AF)) {
        (this->*fpImageProcessAF)();
    }
    mImageProcessLock.unlock();
}

void AdvanceProcess::advImageProcessAE(void)
{
    mImageProcessLock.lock();
    if (fpImageProcessAE != NULL &&
	isFlagEnabled(IMAGE_PRCOESS_FLAGS_TYPE_AE)) {
        (this->*fpImageProcessAE)();
    }
    mImageProcessLock.unlock();
}

void AdvanceProcess::advImageProcessAWB(void)
{
    mImageProcessLock.lock();
    if (fpImageProcessAWB != NULL &&
	isFlagEnabled(IMAGE_PRCOESS_FLAGS_TYPE_AWB)) {
        (this->*fpImageProcessAWB)();
    }
    mImageProcessLock.unlock();
}

void AdvanceProcess::advSetAF(ci_isp_afss_mode mode)
{
    mImageProcessLock.lock();
    if (fpSetAF != NULL) {
        (this->*fpSetAF)(mode);
    }
    mImageProcessLock.unlock();
}

void AdvanceProcess::advSetAE(ci_isp_aec_mode mode)
{
    mImageProcessLock.lock();
    if (fpSetAE != NULL) {
        (this->*fpSetAE)(mode);
    }
    mImageProcessLock.unlock();
}

void AdvanceProcess::advSetAWB(ci_isp_awb_mode mode, ci_isp_awb_sub_mode sub_mode)
{
    mImageProcessLock.lock();
    if (fpSetAWB != NULL) {
        (this->*fpSetAWB)(mode, sub_mode);
    }
    mImageProcessLock.unlock();
}

void AdvanceProcess::imageProcessAFforSOC(void)
{
    disableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AF);
}

void AdvanceProcess::imageProcessAEforSOC(void)
{
    disableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AE);
}

void AdvanceProcess::imageProcessAWBforSOC(void)
{
    disableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AWB);
}

void AdvanceProcess::imageProcessAFforRAW(void)
{
    int ret;
    LOGV("---- %s : AF PROCESS --",__func__);
    ret = ci_af_process(mCI->context);
    //CHECK_CI_RET(ret, "ci af process");
    //disableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AF);
    if (!ret)
	    mFinishedAF = TRUE;
}

void AdvanceProcess::imageProcessAEforRAW(void)
{
    int ret;
    LOGV("---- %s : AE PROCESS--",__func__);
    ret = ci_ae_process(mCI->context);
    //CHECK_CI_RET(ret, "ci ae process");
    //disableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AE);
    if (!ret)
	    mFinishedAE = TRUE;
}

void AdvanceProcess::imageProcessAWBforRAW(void)
{
    int ret;
    LOGV("---- %s : AWB PROCESS --",__func__);
    ret = ci_awb_process(mCI->context);
    //CHECK_CI_RET(ret, "ci awb process");
    //disableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AWB);
    if (!ret)
	    mFinishedAWB = TRUE;
}

void AdvanceProcess::setAFforSOC(ci_isp_afss_mode mode)
{
    int ret;
    // af only off
    if ( mode != CI_ISP_AFSS_OFF ) {
      mode = CI_ISP_AFSS_OFF;
    }

    if (mode == CI_ISP_AFSS_OFF ) {
      LOGV("%s: set AF OFF",__func__);
    } else {
      LOGV("%s: set AF ON",__func__);
    }

  /* setup AF */
  ret = ci_context_set_cfg(mCI->context, CI_CFG_AF, (void *)&(mode));
  CHECK_CI_RET(ret, "set config for AF");
  enableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AF);
}

void AdvanceProcess::setAEforSOC(ci_isp_aec_mode mode)
{
  int ret;
  // ae only on
  if ( mode != CI_ISP_AEC_INTEGRAL ) {
    mode = CI_ISP_AEC_INTEGRAL;
  }

  if ( mode == CI_ISP_AEC_OFF ) {
    LOGV("%s: set AE OFF",__func__);
  } else {
    LOGV("%s: set AE ON",__func__);
  }

  /* setup AE */
  ret = ci_context_set_cfg(mCI->context, CI_CFG_AE, (void*)&(mode));
  CHECK_CI_RET(ret, "set config for AE");
  enableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AE);
}

void AdvanceProcess::setAWBforSOC(ci_isp_awb_mode mode, ci_isp_awb_sub_mode sub_mode)
{
  int ret;
  unsigned int nmode = TURN_ON;
  if ( nmode == (unsigned int)TURN_ON ) {
    LOGV("%s: set AWB ON",__func__);
  } else {
    LOGV("%s: set AWB OFF",__func__);
  }
  /* setup AWB */
  ret = ci_context_set_cfg(mCI->context, CI_CFG_AWB, (void*)&(nmode));
  CHECK_CI_RET(ret, "set config for AWB");
  enableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AWB);
}

void AdvanceProcess::setAFforRAW(ci_isp_afss_mode mode)
{
    if ( mParamAF != NULL ) {
        int ret;
	mParamAF->mode = mode;

	if(mParamAF->mode == CI_ISP_AFSS_OFF) {
	    ret = ci_af_setup(mCI->context, *mParamAF, TURN_OFF);
	    LOGV("%s: set AF OFF",__func__);
	} else {
	    ret = ci_af_setup(mCI->context, *mParamAF, TURN_ON);
	    LOGV("%s: set AF ON",__func__);
	    enableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AF);
	}
	CHECK_CI_RET(ret, "ci_af_setup");
    }
}

void AdvanceProcess::setAEforRAW(ci_isp_aec_mode mode)
{
    if ( mParamAE != NULL ) {
        int ret;
	LOGV("mParamAE->meas_wnd: hoffs = %d, voffs = %d, ", mParamAE->meas_wnd.hoffs, mParamAE->meas_wnd.voffs);
	LOGV("w = %d, h = %d", mParamAE->meas_wnd.hsize, mParamAE->meas_wnd.vsize);
	LOGV("mParamAE->hist_wnd: hoffs = %d, voffs = %d, ", mParamAE->hist_wnd.hoffs, mParamAE->hist_wnd.voffs);
	LOGV("w = %d, h = %d", mParamAE->hist_wnd.hsize, mParamAE->hist_wnd.vsize);

	if (mode == CI_ISP_AEC_OFF) {
	    ret = ci_ae_setup(mCI->context, *mParamAE, TURN_OFF);
	    LOGV("%s: set AE OFF",__func__);
	} else {
	    ret = ci_ae_setup(mCI->context, *mParamAE, TURN_ON);
	    LOGV("%s: set AE ON",__func__);
	    enableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AE);
	}
	CHECK_CI_RET(ret, "ci_ae_setup");
    }
}

void AdvanceProcess::setAWBforRAW(ci_isp_awb_mode mode, ci_isp_awb_sub_mode sub_mode)
{
    if ( mParamAWB != NULL ) {
        int ret;
	mParamAWB->mode = mode;
	mParamAWB->sub_mode = sub_mode;
	LOGV("mParamAWB->window: hoffs = %d, voffs = %d, ", mParamAWB->window.hoffs, mParamAWB->window.voffs);
	LOGV("w = %d, h = %d", mParamAWB->window.hsize, mParamAWB->window.vsize);

	if (mParamAWB->mode == CI_ISP_AWB_COMPLETELY_OFF) {
	    ret = ci_wb_setup(mCI->context, *mParamAWB, TURN_OFF);
	    LOGV("%s: set AWB OFF", __func__);
	} else {
	    ret = ci_wb_setup(mCI->context, *mParamAWB, TURN_ON);
	    LOGV("%s: set AWB ON", __func__);
	    enableFlag(IMAGE_PRCOESS_FLAGS_TYPE_AWB);
	}
	CHECK_CI_RET(ret, "ci_awb_setup");
    }
}

IntelCamera::IntelCamera()
  : mFrameInfos(0),
    mCurrentFrameFormat(0),
    mSensorInfo(0),
    mAdvanceProcess(NULL)
{
  int ret;
  mCI = new ci_struct_t;
  /* initialize ci */
  ret = ci_initialize(&mCI->major_version, &mCI->minor_version);
  CHECK_CI_RET(ret, "ci initialize");

  allocSensorInfos();

  if ( (mCI != NULL)  && (mSensorInfo != NULL)) {
    mAdvanceProcess = new AdvanceProcess(mCI, mSensorInfo);
  }
  mCI->frame_num = 0;
  mCI->cur_frame = 0;
}

IntelCamera::~IntelCamera()
{

  int ret;
  delete mAdvanceProcess;
  freeSensorInfos();
  ret = ci_terminate();
  CHECK_CI_RET(ret, "ci terminate");
  delete mCI;
}

void IntelCamera::captureInit(unsigned int width,
				unsigned int height,
				ci_frame_format frame_fmt,
				unsigned int frame_num)
{
  int ret;
  /* create context for view finding */
  ret = ci_create_context(&(mCI->context));
  CHECK_CI_RET(ret, "create context");

  /* config contexts */
  mCI->snr_id = mSensorInfo->snr_id;
  ret = ci_context_set_cfg(mCI->context, CI_CFG_SENSOR,
			   (void*)(&mCI->snr_id));
  CHECK_CI_RET(ret, "set sensor");

  /* set sensor output resolution */
  ci_resolution res;
  res.width = width;
  res.height = height;
  ret = ci_context_set_cfg(mCI->context, CI_CFG_SENSOR_RES,
			   (void *)(&res));
  CHECK_CI_RET(ret, "set sensor resolution");
  if ( ret == CI_STATUS_SUCCESS ) {
    mCI->snr_width = res.width;
    mCI->snr_height = res.height;
  }

  /* get isp and snr device */
  ret = ci_get_device(mCI->context, CI_DEVICE_SENSOR,
		&(mCI->snr_dev));
  CHECK_CI_RET(ret, "get sensor device");

  ret = ci_get_device(mCI->context, CI_DEVICE_ISP,
		&(mCI->isp_dev));
  CHECK_CI_RET(ret, "get isp device");

if (frame_fmt == INTEL_PIX_FMT_RGB565 || frame_fmt == INTEL_PIX_FMT_BGR32) {
  ret = ci_isp_open(LANGWELL_ISP_SELF, &(mCI->isp_dev_self));
  CHECK_CI_RET(ret, "ci isp open self");
}
  /*start context */
  ret = ci_start_context(mCI->context);
  CHECK_CI_RET(ret, "start context");

  /* get isp config to determine whether to use continous_af */
  ci_isp_config isp_cfg;
  ret = ci_isp_get_cfg(mCI->isp_dev, &isp_cfg);
  mCI->continous_af = isp_cfg.flags.continous_af;
  CHECK_CI_RET(ret, "get isp config");

  /* create frames */
  //  mCI->frames = (ci_isp_frame_id *)malloc(sizeof(ci_isp_frame_id)*frame_num);
  mCI->frames = new ci_isp_frame_id[frame_num];
if (frame_fmt == INTEL_PIX_FMT_RGB565 || frame_fmt == INTEL_PIX_FMT_BGR32) {
  mCI->frames_self = new ci_isp_frame_id[frame_num];
}

  unsigned int w, h;
  w = width;
  h = height;
  /* VIDIOC_S_FMT, VIDIOC_REQBUFS */
if (frame_fmt != INTEL_PIX_FMT_JPEG)
  ret = ci_isp_create_frames(mCI->isp_dev, &w, &h,
		       INTEL_PIX_FMT_NV12,
		       frame_num,
		       mCI->frames);
else
  ret = ci_isp_create_frames(mCI->isp_dev, &w, &h,
		       frame_fmt,
		       frame_num,
		       mCI->frames);
  CHECK_CI_RET(ret, "isp create frames");

  mCI->fm_width = w;
  mCI->fm_height = h;

  mCurrentFrameFormat = frame_fmt;

  mCI->frame_num = frame_num;

/*
  ret = ci_isp_close(mCI->isp_dev_self);
  CHECK_CI_RET(ret, "ci isp open");
  ret = ci_isp_open(LANGWELL_ISP_SELF, &(mCI->isp_dev_self));
  CHECK_CI_RET(ret, "ci isp open self");
*/

if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
  frame_fmt = mCurrentFrameFormat;
  ret = ci_isp_create_frames(mCI->isp_dev_self, &w, &h,
		       frame_fmt,
		       frame_num,
		       mCI->frames_self);
  CHECK_CI_RET(ret, "isp create frames self");
}
  if (mAdvanceProcess != NULL) {
       mAdvanceProcess->setAdvanceParams(w,h);
  }
}

void IntelCamera::captureFinalize(void)
{
  int ret;
  /* destroy frames */
  ret = ci_isp_destroy_frames(mCI->isp_dev, mCI->frames);
  CHECK_CI_RET(ret, "destory frames");
if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
  ret = ci_isp_destroy_frames(mCI->isp_dev_self, mCI->frames_self);
  CHECK_CI_RET(ret, "destory frames self");
  delete [] mCI->frames_self;
}
  //free(mCI->frames);
  delete [] mCI->frames;
  /* stop context */
  ret = ci_stop_context(mCI->context);
  CHECK_CI_RET(ret, "stop context");
  /* destroy context */
  ret = ci_destroy_context(mCI->context);
  CHECK_CI_RET(ret, "destory context");
  mCI->cur_frame = 0;
if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
  ret = ci_isp_off(mCI->isp_dev_self);
  CHECK_CI_RET(ret, "ci isp off");
  ret = ci_isp_close(mCI->isp_dev_self);
  CHECK_CI_RET(ret, "ci isp close");
}
}

void IntelCamera::captureStart(void)
{
  int ret;
  ret = ci_isp_max_num_lock_frames(mCI->isp_dev,
			     &mCI->max_lock_frame_num);
  CHECK_CI_RET(ret, "isp max num lock frames");

  /* VIDIOC_STREAMON , VIDIOC_QUERYBUF */
  ret = ci_isp_start_capture(mCI->isp_dev);
  CHECK_CI_RET(ret, "isp start capture");
if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
  ret = ci_isp_start_capture(mCI->isp_dev_self);
  CHECK_CI_RET(ret, "isp start capture self");
}

  unsigned int i, frame_num = mCI->frame_num;
  for (i = 0; i < frame_num; i++) {
    /* VIDIOC_QBUF */
    ret = ci_isp_set_frame_ext(mCI->isp_dev,
			       mCI->frames[i]);
    CHECK_CI_RET(ret, "isp set frame ext");
if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
    ret = ci_isp_set_frame_ext(mCI->isp_dev_self,
			       mCI->frames_self[i]);
    CHECK_CI_RET(ret, "isp set frame ext self");
}
  }

}

int IntelCamera::captureMapFrame(void)
{
    int ret, size;
    if (mCurrentFrameFormat == INTEL_PIX_FMT_NV12 || mCurrentFrameFormat == INTEL_PIX_FMT_RGB565) {
        unsigned int i, frame_num = mCI->frame_num;

	mFrameInfos = new ci_isp_frame_map_info[frame_num];

	for(i = 0; i < frame_num; i++) {
	    ret = ci_isp_map_frame(mCI->isp_dev, mCI->frames[i], &(mFrameInfos[i]));
	    CHECK_CI_RET(ret, "capture map frame");
	    LOGV("%s : mFrameInfos[%u].addr=%p, mFrameInfos[%u].size=%d",
		 __func__, i, mFrameInfos[i].addr, i, mFrameInfos[i].size);
	}
	if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565) {
	mFrameInfos_self = new ci_isp_frame_map_info[frame_num];
	for(i = 0; i < frame_num; i++) {
	    ret = ci_isp_map_frame(mCI->isp_dev_self, mCI->frames_self[i], &(mFrameInfos_self[i]));
	    CHECK_CI_RET(ret, "capture map frame self");
	    LOGV("%s self : mFrameInfos[%u].addr=%p, mFrameInfos[%u].size=%d",
		 __func__, i, mFrameInfos_self[i].addr, i, mFrameInfos_self[i].size);
	}
        }

	size =  mFrameInfos[0].size;

	/* camera bcd stuff */
#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
	ret = ci_isp_register_camera_bcd(mCI->isp_dev, mCI->frame_num, mCI->frames, mFrameInfos);
	CHECK_CI_RET(ret, "register camera bcd");
	LOGD("main end of bcd");
#endif

    } else if (mCurrentFrameFormat == INTEL_PIX_FMT_JPEG) {
        int ret;
	ret = ci_isp_map_frame(mCI->isp_dev, mCI->cur_frame, &mJpegFrameInfo);
	CHECK_CI_RET(ret, "capture jpeg map frame");
	size = mJpegFrameInfo.size;
    } else {
        size = 0;
    }
    return size;
}

void IntelCamera::captureUnmapFrame(void)
{
  int ret;
    if (mCurrentFrameFormat == INTEL_PIX_FMT_NV12 || mCurrentFrameFormat == INTEL_PIX_FMT_RGB565) {
      unsigned int i, frame_num = mCI->frame_num;

      for(i = 0; i < frame_num; i++) {
	  ret = ci_isp_unmap_frame(mCI->isp_dev, &(mFrameInfos[i]));
	  CHECK_CI_RET(ret, "capture unmap frame");
	  LOGV("%s : mFrameInfos[%u].addr=%p",__func__, i, mFrameInfos[i].addr);
      }
      delete [] mFrameInfos;
      if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565) {
      for(i = 0; i < frame_num; i++) {
	  ret = ci_isp_unmap_frame(mCI->isp_dev_self, &(mFrameInfos_self[i]));
	  CHECK_CI_RET(ret, "capture unmap frame self");
	  LOGV("%s self: mFrameInfos[%u].addr=%p",__func__, i, mFrameInfos_self[i].addr);
      }
      delete [] mFrameInfos_self;
      }
  } else if (mCurrentFrameFormat == INTEL_PIX_FMT_JPEG) {
      ret = ci_isp_unmap_frame(mCI->isp_dev, &mJpegFrameInfo);
      CHECK_CI_RET(ret, "capture jpeg unmap frame");
  }
}

unsigned int IntelCamera::captureGrabFrame(void)
{
  int ret;
  unsigned int frame;
  unsigned int frame_self;
  unsigned int frame_size;
  unsigned int frame_size_self;

  /* VIDIOC_DQBUF */
  ret = ci_isp_capture_frame_ext(mCI->isp_dev, (ci_isp_frame_id *)&frame,
		  &frame_size);

#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
  if(ret != CI_ISP_STATUS_SUCCESS) {
      return (unsigned int)-1;  	
  }
#endif

  if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
        ret = ci_isp_capture_frame_ext(mCI->isp_dev_self, (ci_isp_frame_id *)&frame_self,
		  &frame_size_self);
#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
        if(ret != CI_ISP_STATUS_SUCCESS) {
            return (unsigned int)-1;
        }
#endif
  }
  //  CHECK_CI_RET(ret, "capture frame ext");

  LOGV("captureGrabFrame : frame = %d", frame);
  mCI->cur_frame = frame;
  mCI->frame_size = frame_size;
  if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
      mCI->frame_size_self = frame_size_self;
  }
  return frame_size;
}

unsigned int IntelCamera::captureGetFrame(void *buffer)
{
  unsigned int frame = mCI->cur_frame;

  if(buffer != NULL) {
    switch(mCurrentFrameFormat) {
    case INTEL_PIX_FMT_RGB565 :
      //      LOGV("INTEL_PIX_FMT_RGB565");
      memcpy(buffer, (unsigned char *)mFrameInfos_self[frame].addr, mFrameInfos_self[frame].size);
	break;
    case INTEL_PIX_FMT_JPEG :
      //      LOGV("INTEL_PIX_FMT_JPEG");
      memcpy(buffer, mJpegFrameInfo.addr, mJpegFrameInfo.size);
      break;
    case INTEL_PIX_FMT_YUYV :
      //      LOGV("INTEL_PIX_FMT_YUYV");
      yuyv422_to_yuv420sp((unsigned char *)mFrameInfos[frame].addr, (unsigned char *)buffer,
                          mCI->fm_width, mCI->fm_height);
      break;
    case INTEL_PIX_FMT_NV12 :
      //      LOGV("INTEL_PIX_FMT_NV12");
      nv12_to_nv21((unsigned char *)mFrameInfos[frame].addr, (unsigned char *)buffer,
		   mCI->fm_width, mCI->fm_height);
//      LOGE("jinlu nv12 to pure 5650");
//      memcpy(buffer, (unsigned char *)mFrameInfos_self[frame].addr, mFrameInfos_self[frame].size);
      break;
    default :
      LOGE("Unknown Format type");
      break;
    }
  }
  return frame;
}

#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
unsigned int IntelCamera::captureGetFrameID(void)
{
	return mCI->cur_frame;
}
#endif

unsigned int IntelCamera::captureGetRecordingFrame(void *buffer, int buffer_share)
{
	unsigned int frame = mCI->cur_frame;

	if(buffer != NULL) {
		if (buffer_share) {
			memcpy(buffer, &frame, sizeof(unsigned int));
		} else {
			switch(mCurrentFrameFormat) {
			case INTEL_PIX_FMT_RGB565 :
				//      LOGV("INTEL_PIX_FMT_RGB565");
				nv12_to_nv21((unsigned char *)mFrameInfos[frame].addr, (unsigned char *)buffer,
					     mCI->fm_width, mCI->fm_height);
				break;
			case INTEL_PIX_FMT_YUYV :
				//      LOGV("INTEL_PIX_FMT_YUYV");
				yuyv422_to_yuv420sp((unsigned char *)mFrameInfos[frame].addr, (unsigned char *)buffer,
						    mCI->fm_width, mCI->fm_height);
				break;
			case INTEL_PIX_FMT_NV12 :
				//      LOGV("INTEL_PIX_FMT_NV12");
				nv12_to_nv21((unsigned char *)mFrameInfos[frame].addr, (unsigned char *)buffer,
					     mCI->fm_width, mCI->fm_height);
				break;
			default :
				LOGE("Unknown Format typedddd");
				break;
			}
		}
	}
	return frame;
}

#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
void IntelCamera::captureRecycleFrameWithFrameId(unsigned int id)
{
   int ret;

   LOGV("captureRecycleFrameWithFrameId :  id = 0x%x", id);

   ret = ci_isp_set_frame_ext(mCI->isp_dev, mCI->frames[id]);
   if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
       ret = ci_isp_set_frame_ext(mCI->isp_dev_self, mCI->frames_self[id]);
   }
}
#endif

void IntelCamera::captureRecycleFrame(void)
{
  int ret;

  if (!mCI || mCI->cur_frame >= mCI->frame_num) {
    LOGE("captureRecycleFrame: ERROR. Frame not ready!! cur_frame %d, mCI->frame_num %d", mCI->cur_frame, mCI->frame_num);
    return;
  }
  //  mCI->cur_frame = (mCI->cur_frame + 1) % mCI->frame_num;
  ret = ci_isp_set_frame_ext(mCI->isp_dev,mCI->frames[mCI->cur_frame]);
if (mCurrentFrameFormat == INTEL_PIX_FMT_RGB565 || mCurrentFrameFormat == INTEL_PIX_FMT_BGR32) {
  ret = ci_isp_set_frame_ext(mCI->isp_dev_self,mCI->frames_self[mCI->cur_frame]);
}
  //  CHECK_CI_RET(ret, "isp set frame ext");
}

void IntelCamera::allocSensorInfos(void)
{
  ci_context_id ctx;
  int snr_id;
  int ret;

  ret = ci_create_context(&ctx);
  CHECK_CI_RET(ret, "ci_create_context"); 

  for(snr_id = CI_SENSOR_SOC_0; snr_id <= CI_SENSOR_RAW_1; snr_id++) {

    ret = ci_context_set_cfg(ctx, CI_CFG_SENSOR, (void *)&snr_id);

    if(ret != CI_STATUS_SUCCESS) {
      mSensorInfo = NULL;
      continue;
    } else {
      ci_device_id snr_dev;
      ci_sensor_caps snr_cap;
      ci_resolution ress[CI_MAX_RES_NUM];
      int res_num, k;
      
      mSensorInfo = new sensor_info_t;
      ret = ci_get_device(ctx, CI_DEVICE_SENSOR, &snr_dev);
      CHECK_CI_RET(ret, "ci get sensor device");
      
      ret = ci_sensor_get_caps(snr_dev, &snr_cap);
      CHECK_CI_RET(ret, "ci get isp device");
      
      ret = ci_get_resolution((ci_sensor_num)snr_id, ress, &res_num, INTEL_PIX_FMT_JPEG);
      CHECK_CI_RET(ret, "ci_get_resolution");
      mSensorInfo->res_num = res_num;
      
      strncpy(mSensorInfo->name, snr_cap.name, SNR_NAME_LEN-1);
      LOGV("Found sensor: %s\n", snr_cap.name);
      
      if ((ret = strcmp(snr_cap.name, "s5k4e1")) == 0) {
	mSensorInfo->input = SENSOR_INPUT_MIPI;
	LOGV("It is a MIPI sensor, auto-review not supported");
      } else {
	mSensorInfo->input = SENSOR_INPUT_PARALLEL;
      }
      
      strncpy(mSensorInfo->name, snr_cap.name, SNR_NAME_LEN-1);
      mSensorInfo->snr_id = (ci_sensor_num)snr_id;
      
      if(snr_id == CI_SENSOR_SOC_0 || snr_id == CI_SENSOR_SOC_1)
	mSensorInfo->type = SENSOR_TYPE_2M;
      else
	mSensorInfo->type = SENSOR_TYPE_5M;
      
      mSensorInfo->resolutions = new sensor_res_t*[res_num];
      
      for(k = 0; k < res_num; k++) {
	mSensorInfo->resolutions[k] = new sensor_res_t;
	mSensorInfo->resolutions[k]->res.width = ress[k].width;
	mSensorInfo->resolutions[k]->res.height = ress[k].height;
	mSensorInfo->resolutions[k]->res.fps = ress[k].fps;
      }
      break;
    }

  }

  ret = ci_destroy_context(ctx);
  CHECK_CI_RET(ret, "ci_destroy_context");
}

void IntelCamera::freeSensorInfos(void)
{
   if (mSensorInfo != NULL) {
     for(unsigned int k = 0; k < mSensorInfo->res_num; k++) {
       delete mSensorInfo->resolutions[k];
     }
     delete [] mSensorInfo->resolutions;
     delete mSensorInfo;
  }
}

int IntelCamera::isResolutionSupported(int w, int h)
{
  sensor_res_t *snr_res;

  if (mSensorInfo != NULL) {
      for(unsigned int i = 0; i < mSensorInfo->res_num; i++) {
	snr_res = mSensorInfo->resolutions[i];
	if((unsigned int)w == snr_res->res.width &&
	   (unsigned int)h == snr_res->res.height) {
	  return 1;
	}
      }
  }
  return 0;
}

void IntelCamera::getMaxResolution(int *w, int *h)
{
    if (mSensorInfo != NULL) {
      sensor_res_t *snr_res =
	mSensorInfo->resolutions[mSensorInfo->res_num - 1];
      *w = snr_res->res.width;
      *h = snr_res->res.height;
    }
}

sensor_info_t * IntelCamera::getSensorInfos(void)
{
  return mSensorInfo;
}

void IntelCamera::printSensorInfos(void)
{
    if (mSensorInfo != NULL) {
      LOGV("Current Sensor Name: %s", mSensorInfo->name);
      LOGV("Type: %s", mSensorInfo->type == SENSOR_TYPE_2M ?  "SOC(2M)" : "RAW(5M)");
      LOGV("Supported Jpeg Resolutions: ");
      for(unsigned int i = 0; i < mSensorInfo->res_num; i++) {
	sensor_res_t *snr_res = mSensorInfo->resolutions[i];
	LOGV("\t %dx%d", snr_res->res.width, snr_res->res.height);
      }
    }
}

int IntelCamera::isImageProcessEnabled(void)
{
  if (mAdvanceProcess != NULL) {
    return mAdvanceProcess->isFlagDirty();
  }
  return -1;
}

int IntelCamera::getPrefMapValue(pref_map_t *map, const char *android_value)
{
  pref_map_t *p = map;

  while(p->android_value != NULL) {
    if (strcmp(p->android_value, android_value) == 0) {
      LOGV("catch map : p->android_value = %s, p->intel_value = %d",
	   p->android_value,p->intel_value);
      return p->intel_value;
    }
    p++;
  }
  return -1;
}

int IntelCamera::getFrameSize(int w, int h)
{
  int depth = getDepth();
  return calQBufferFrameSize(w,h,depth);
}

int IntelCamera::getRealFrameSize(int w, int h)
{
  int depth = getDepth();
  return calRealFrameSize(w,h,depth);
}

int IntelCamera::getDepth(void)
{
  int intel_fmt_list_num =
    sizeof(intel_fmt_list)/sizeof(intel_fmt_list_t);

  intel_fmt_list_t *p = intel_fmt_list;

  for(int i=0; i<intel_fmt_list_num; i++) {
    if (p->fourcc == mCurrentFrameFormat) {
      LOGV("catch depth : p->depth=%d",p->depth);
      return p->depth;
    }
    p++;
  }
  return 0;
}

int IntelCamera::calQBufferFrameSize(int w, int h, int depth)
{
  unsigned int size = (unsigned int)(0x01 << 12);
  unsigned int mask = (unsigned int)(~(size-1));
  return ((w*h*depth/8) + size - 1) & mask;
}

int IntelCamera::calRealFrameSize(int w, int h, int depth)
{
  return (w*h*depth/8);
}

void IntelCamera::setAF(const char *value)
{
    if (mAdvanceProcess != NULL ) {
      ci_isp_afss_mode mode =
	(ci_isp_afss_mode)getPrefMapValue(pref_af_map, value);
        mAdvanceProcess->advSetAF(mode);
    }
}

void IntelCamera::setAE(const char *value)
{
    if (mAdvanceProcess != NULL ) {
      ci_isp_aec_mode mode =
	(ci_isp_aec_mode)getPrefMapValue(pref_aec_map, value);
        mAdvanceProcess->advSetAE(mode);
    }
}

void IntelCamera::setAWB(const char *value)
{
  if (mAdvanceProcess != NULL) {
    ci_isp_awb_mode mode =
      (ci_isp_awb_mode)getPrefMapValue(pref_awb_map, "auto");

    ci_isp_awb_sub_mode sub_mode =
      (ci_isp_awb_sub_mode)getPrefMapValue(pref_awb_sub_map, value);

      mAdvanceProcess->advSetAWB(mode, sub_mode);
  }
}

void IntelCamera::setJPEGRatio(const char *value)
{
  int ret;
  ci_jpeg_ratio ratio = (ci_jpeg_ratio)getPrefMapValue(pref_jpeg_quality_map, value);
  ret = ci_context_set_cfg(mCI->context, CI_CFG_JPEG, (void*)&(ratio));
  CHECK_CI_RET(ret, "set jpeg ratio");
}

void IntelCamera::setColorEffect(const char *value)
{
  int ret;
  ci_ie_mode effect = (ci_ie_mode)getPrefMapValue(pref_color_effect_map, value);
  /* set color effect */
  ret = ci_context_set_cfg(mCI->context, CI_CFG_IE, (void*)&(effect));
  CHECK_CI_RET(ret, "set image effect");
}

void IntelCamera::imageProcessBP(void)
{
    int ret;
    ret = ci_bp_correct(mCI->context);
    //CHECK_CI_RET(ret, "bp correct");
}

void IntelCamera::imageProcessBL(void)
{
    int ret;
    ret = ci_bl_compensate(mCI->context);
    //CHECK_CI_RET(ret, "bl compensate");
}

void IntelCamera::imageProcessAF(void)
{
   if (mAdvanceProcess != NULL) {
	mAdvanceProcess->advImageProcessAF();
  }
}

void IntelCamera::imageProcessAE(void)
{
  if (mAdvanceProcess != NULL) {
	mAdvanceProcess->advImageProcessAE();
  }
}

void IntelCamera::imageProcessAWB(void)
{
  if (mAdvanceProcess != NULL) {
	mAdvanceProcess->advImageProcessAWB();
  }
}

int IntelCamera::isImageProcessFinishedAE(void)
{
	return mAdvanceProcess->isFinishedAE();
}

int IntelCamera::isImageProcessFinishedAWB(void)
{
	return mAdvanceProcess->isFinishedAWB();
}

int IntelCamera::isImageProcessFinishedAF(void)
{
	return mAdvanceProcess->isFinishedAF();
}

void IntelCamera::nv12_to_nv21(unsigned char *nv12, unsigned char *nv21, int width, int height)
{
  int h,w;
  /* copy y */
  /*
  for(h=0; h<height; h++) {
    for(w=0; w<width; w+=4) {
      *(nv21 + w + 0) = *(nv12 + w + 0);
      *(nv21 + w + 1) = *(nv12 + w + 1);
      *(nv21 + w + 2) = *(nv12 + w + 2);
      *(nv21 + w + 3) = *(nv12 + w + 3);
    }
    nv21 += width;
    nv12 += width;
  }
  */

#ifdef BOARD_USE_SOFTWARE_ENCODE
  memcpy(nv21, nv12, 1.5*width*height);
  return;
#endif

  for(h=0; h<height; h++) {
    memcpy(nv21 + h * width , nv12 + h * width, width);
  }

  nv21 += width * height;
  nv12 += width * height;

  /* Change uv to vu*/
  int height_div_2 = height/2;
  for(h=0; h<height_div_2; h++) {
    for(w=0; w<width; w+=2) {
      *(nv21 + w + 1) = *(nv12 + w);
      *(nv21 + w) = *(nv12 + w + 1);
    }
    nv21 += width;
    nv12 += width;
  }
}

void IntelCamera::yuv_to_rgb16(unsigned char y,unsigned char u, unsigned char v, unsigned char *rgb)
{
	register int r,g,b;
	int rgb16;

	r = (1192 * (y - 16) + 1634 * (v - 128) ) >> 10;
	g = (1192 * (y - 16) - 833 * (v - 128) - 400 * (u -128) ) >> 10;
	b = (1192 * (y - 16) + 2066 * (u - 128) ) >> 10;

	r = r > 255 ? 255 : r < 0 ? 0 : r;
	g = g > 255 ? 255 : g < 0 ? 0 : g;
	b = b > 255 ? 255 : b < 0 ? 0 : b;

	rgb16 = (int)(((r >> 3)<<11) | ((g >> 2) << 5)| ((b >> 3) << 0));

	*rgb = (unsigned char)(rgb16 & 0xFF);
	rgb++;
	*rgb = (unsigned char)((rgb16 & 0xFF00) >> 8);
}

void IntelCamera::yuyv422_to_rgb16(unsigned char *buf, unsigned char *rgb, int width, int height)
{
	int x,y,z=0;
	int blocks;

	blocks = (width * height) * 2;
	for (y = 0; y < blocks; y+=4) {
		unsigned char Y1, Y2, U, V;

		Y1 = buf[y + 0];
		U = buf[y + 1];
		Y2 = buf[y + 2];
		V = buf[y + 3];

		yuv_to_rgb16(Y1, U, V, &rgb[y]);
		yuv_to_rgb16(Y2, U, V, &rgb[y + 2]);
	}
}


void IntelCamera::yuyv422_to_yuv420sp(unsigned char *bufsrc, unsigned char *bufdest, int width, int height)
{
	LOGV("yuyv422_to_yuv420sp empty");
}

unsigned int IntelCamera::get_frame_num(void)
{
        return mCI->frame_num;
}

void IntelCamera::get_frame_id(unsigned int *frame_id, unsigned int frame_num)
{
        int frame_index;
        for (frame_index=0; frame_index<frame_num; frame_index++)
                frame_id[frame_index] = mCI->frames[frame_index];
}

}; // namespace android
