/* Intel ATOM ISP Abstract layer API
** Copyright (c) 2011 Intel Corporation
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

/*
 * This file provide the IOCTL wrap to the ATOM ISP drivers
 * It provides the following features.
 * Image stablization
 * Video stablization
 * Skin tone detection/correction
 * Image effect (Color Sapce Convertion)
 * Noise Reduction (XNR, TNR, BNR, YNR FPN)
 * Color Enhancement
 * Edge Enhancement
 * False Color Correction
 * MACC(Sky blue, grass green, skin whiten)
 * Bad Pixel detection
 * Lens shading correction
 * black level compensation
 * digital zoom
 * gamma
 * tone control FIXME
 * CAC/GDC
 * super impose FIXME
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>
#include <utils/Log.h>
#include <linux/atomisp.h>
#include "atomisp_features.h"
#include "atomisp_config.h"

#undef LOG_TAG
#define LOG_TAG "ATOMISP abstract layer"
#define CAM_ISP_IS_OPEN(fd)	(fd > 0)
#define cam_driver_dbg LOGV
#define cam_driver_err LOGE
/*these configs are used to restore configs*/
struct atomisp_de_config old_de_config;
struct atomisp_ctc_table old_ctc_table;
struct atomisp_tnr_config old_tnr_config;
struct atomisp_nr_config old_nr_config;
struct atomisp_dp_config old_dp_config;
struct atomisp_wb_config old_wb_config;
struct atomisp_morph_table old_gdc_table;
struct atomisp_macc_config old_macc_config;
struct atomisp_frame old_fpn_tbl;

static int
xioctl (int fd, int request, void *arg, const char *name)
{
    int ret;

    cam_driver_dbg ("ioctl %s ", name);

    do {
        ret = ioctl (fd, request, arg);
    } while (-1 == ret && EINTR == errno);

    if (ret < 0)
        cam_driver_dbg ("failed: %s\n", strerror (errno));
    else
        cam_driver_dbg ("ok\n");

    return ret;
}

/* Utilities for debug message and error message output
 * */

static const char *cameralib_error_map[] = {
    "CAM_ERR_NONE",
    "CAM_ERR_PARAM",
    "CAM_ERR_UNSUPP",
    "CAM_ERR_HW",
    "CAM_ERR_NOT_OPEN",
    "CAM_ERR_SYS",
    "CAM_ERR_LEXIT",
    "CAM_ERR_DEPRECATED",
    "CAM_ERR_INVALID_STATE",
    "CAM_ERR_INTERNAL",
    "CAM_ERR_3A"
};

void
cam_err_print (cam_err_t err)
{
    if (err > CAM_ERR_3A) {
        LOGE (" %s Wrong error number in lib camera\n", __func__);
        return;
    }

    LOGE ("%s\n", cameralib_error_map[err]);
}

int cam_driver_set_capture_mode(int fd, int mode)
{
    int binary;
    struct v4l2_streamparm parm;

    switch (mode) {
    case PREVIEW_MODE:
        binary = CI_MODE_PREVIEW;
        break;;
    case STILL_IMAGE_MODE:
        binary = CI_MODE_STILL_CAPTURE;
        break;
    case VIDEO_RECORDING_MODE:
        binary = CI_MODE_VIDEO;
        break;
    default:
        binary = CI_MODE_STILL_CAPTURE;
        break;
    }

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.capturemode = binary;

    if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
        LOGE("ERR(%s): error %s\n", __func__, strerror(errno));
        return -1;
    }

    return 0;
}


/******************************************************
 * cam_driver_get_attribute():
 *   try to get the value of one specific attribute
 * return value: CAM_ERR_NONE for success
 *               others are errors
 ******************************************************/
cam_err_t
cam_driver_get_attribute (int fd, int attribute_num, int *value, char *name)
{
    struct v4l2_control control;

    cam_driver_dbg ("getting value of attribute %d: %s\n", attribute_num, name);

    if (!CAM_ISP_IS_OPEN (fd))
        return CAM_ERR_NOT_OPEN;

    control.id = attribute_num;

    if (ioctl (fd, VIDIOC_G_CTRL, &control) < 0)
        goto ctrl_failed1;

    *value = control.value;

    return CAM_ERR_NONE;

ctrl_failed1:
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_USER;
        controls.count = 1;
        controls.controls = &control;

        control.id = attribute_num;

        if (ioctl (fd, VIDIOC_G_EXT_CTRLS, &controls) < 0)
            goto ctrl_failed2;

        *value = control.value;

        return CAM_ERR_NONE;

    }

ctrl_failed2:
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
        controls.count = 1;
        controls.controls = &control;

        control.id = attribute_num;

        if (ioctl (fd, VIDIOC_G_EXT_CTRLS, &controls) < 0)
            goto ctrl_failed3;

        *value = control.value;

        return CAM_ERR_NONE;

    }

    /* ERRORS */
ctrl_failed3:
    {
        cam_driver_dbg ("Failed to get value for control %d on device '%d'.",
                        attribute_num, fd);
        return CAM_ERR_SYS;
    }
}

/******************************************************
 * cam_driver_set_attribute():
 *   try to set the value of one specific attribute
 * return value: CAM_ERR_NONE for success
 * 				 others are errors
 ******************************************************/
cam_err_t
cam_driver_set_attribute (int fd, int attribute_num, const int value,
                          const char *name)
{
    struct v4l2_control control;

    cam_driver_dbg ("setting value of attribute [%s] to %d\n", name, value);

    if (!CAM_ISP_IS_OPEN (fd))
        return CAM_ERR_NOT_OPEN;

    control.id = attribute_num;
    control.value = value;
    if (ioctl (fd, VIDIOC_S_CTRL, &control) < 0)
        goto ctrl_failed1;

    return CAM_ERR_NONE;

ctrl_failed1:
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
        controls.count = 1;
        controls.controls = &control;

        control.id = attribute_num;
        control.value = value;

        if (ioctl (fd, VIDIOC_S_EXT_CTRLS, &controls) < 0)
            goto ctrl_failed2;

        return CAM_ERR_NONE;
    }

ctrl_failed2:
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_USER;
        controls.count = 1;
        controls.controls = &control;

        control.id = attribute_num;
        control.value = value;

        if (ioctl (fd, VIDIOC_S_EXT_CTRLS, &controls) < 0)
            goto ctrl_failed3;

        return CAM_ERR_NONE;
    }

    /* ERRORS */
ctrl_failed3:
    {
        cam_driver_dbg
        ("Failed to set value %d for control %d on device '%d', %s\n.", value,
         attribute_num, fd, strerror (errno));
        return CAM_ERR_SYS;
    }
}

static cam_err_t
cam_driver_get_de_config (int fd, struct atomisp_de_config *de_cfg)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_ISP_FALSE_COLOR_CORRECTION,
	    de_cfg, "Get_DE");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static cam_err_t
cam_driver_get_macc_tbl (int fd, struct atomisp_macc_config *macc_config)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_ISP_MACC,macc_config, "Get_Macc_TBL");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static cam_err_t
cam_driver_get_ctc_tbl (int fd, struct atomisp_ctc_table *ctc_tbl)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_ISP_CTC, ctc_tbl, "Get_CTC_TBL");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static cam_err_t
cam_driver_get_gdc_tbl (int fd, struct atomisp_morph_table *morph_tbl)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_ISP_GDC_TAB, morph_tbl, "Get_GDC_TBL");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}



static cam_err_t
cam_driver_get_tnr_config (int fd, struct atomisp_tnr_config *tnr_cfg)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_TNR, tnr_cfg,
	    "Get_TNR");

    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}


static cam_err_t
cam_driver_get_ee_config (int fd, struct atomisp_ee_config *ee_cfg)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_EE, ee_cfg,
	    "Get_EE");

    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static cam_err_t
cam_driver_get_nr_config (int fd, struct atomisp_nr_config *nr_cfg)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_BAYER_NR, nr_cfg,
	    "Get_BAYER_NR");

    if (ret < 0 )
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static cam_err_t
cam_driver_get_dp_config (int fd, struct atomisp_dp_config *dp_cfg)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_ISP_BAD_PIXEL_DETECTION, dp_cfg, "Get_PIXEL_DEFECT");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static cam_err_t
cam_driver_get_wb_config (int fd, struct atomisp_wb_config *wb_cfg)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_ISP_WHITE_BALANCE, wb_cfg, "Get_WHITE_BALANCE");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}
static cam_err_t
cam_driver_get_ob_config (int fd, struct atomisp_ob_config *ob_cfg)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_BLACK_LEVEL_COMP, ob_cfg, "Get_Optical_Black");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static cam_err_t
cam_driver_get_fpn_tbl(int fd, struct atomisp_frame* fpn_tbl)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_ISP_FPN_TABLE, fpn_tbl, "Get_Fpn_Tbl");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static struct atomisp_gamma_table g_gamma_table_original;	// original Gamma table for precise Gamma restore
static struct atomisp_gamma_table g_gamma_table;

/* Gamma configuration
 * Also used by extended dymanic range and tone control
 */
struct Camera_gm_config
{
    /* [gain]      1.0..2.4    Gamma value.  */
    float GmVal;
    int GmToe;                    /* [intensity]        Toe position of S-curve. */
    int GmKne;                    /* [intensity]        Knee position of S-curve */

    /* [gain]      100%..400%  Magnification factor of dynamic range
     * (1.0 for normal dynamic range) */
    int GmDyr;

    /* Minimum output levels: Set to   0 for 256 full 8it level output or
     * 16 for ITU.R601 16-235 output.*/
    unsigned char GmLevelMin;
    /* Maximum output levels: Set to 128 for 256 full 8it level output or
     * 235 for ITU.R601 16-235 output */
    unsigned char GmLevelMax;
};

static struct Camera_gm_config g_cfg_gm = {
    .GmVal = 1.5,
    .GmToe = 123,
    .GmKne = 287,
    .GmDyr = 256,
    .GmLevelMin = 0,
    .GmLevelMax = 255,
};

/*
  Make gamma table
*/
static void
AutoGmLut (unsigned short *pptDst, struct Camera_gm_config *cfg_gm)
{
    /* cannot use this on cirrus because of missing powf implementation */
    const double adbToe = (double) (cfg_gm->GmToe) / 1024.;       // [u5.11] -> double
    const double adbKnee = (double) (cfg_gm->GmKne) / 1024.;      // [u5.11] -> double
    const double adbDRange = (double) (cfg_gm->GmDyr) / 256.;     // [u8.8] -> double
    const double adbReGammaVal = 1 / (double) (cfg_gm->GmVal);    // 1/GmVal : [u8.8] -> double
    const double adbTmpKnee =
        adbKnee / (adbDRange * adbKnee + adbDRange - adbKnee);
    const double adbTmpToe =
        ((1. + adbTmpKnee) * adbToe * adbKnee) / (adbDRange * (1. +
                adbKnee) * adbTmpKnee);
    const double adbDx = 1. / (double) 1024;      /* 1024 is the gamma table size */
    double adbX = (double) 0.;
    int asiCnt;

    for (asiCnt = 0; asiCnt < 1024; asiCnt++, adbX += adbDx) {
        const double adbDeno = (1. + adbTmpToe) * (1. + adbTmpKnee) * adbX * adbX;
        const double adbNume = (adbX + adbTmpToe) * (adbX + adbTmpKnee);
        const double adbY =
            (adbNume == 0.) ? 0. : pow (adbDeno / adbNume, adbReGammaVal);
        short auiTmp = (short) ((double) 255 * adbY + 0.5);

        if (auiTmp < cfg_gm->GmLevelMin) {
            auiTmp = cfg_gm->GmLevelMin;
        } else if (auiTmp > cfg_gm->GmLevelMax) {
            auiTmp = cfg_gm->GmLevelMax;
        }
        pptDst[asiCnt] = auiTmp;
    }
}

cam_err_t
cam_driver_set_fpn (int fd, int on)
{
    int ret;
    if (on)
    {
        cam_driver_get_fpn_tbl(fd, &old_fpn_tbl);
        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_fpn_table();
            if(ret == 0)
            {
                return CAM_ERR_NONE;
            }
            else return CAM_ERR_SYS;
        }
        else return CAM_ERR_NONE;
    }
    else
    {
        return xioctl (fd, ATOMISP_IOC_S_ISP_FPN_TABLE, &old_fpn_tbl, "Set FPN Table");
    }
}

cam_err_t
cam_driver_set_macc (int fd, int on, int effect)
{
    int ret;
    if (on)
    {
        cam_driver_get_macc_tbl(fd, &old_macc_config);
        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_macc_table(effect);
            if (ret == 0)
            {
                return CAM_ERR_NONE;
            }
            else return CAM_ERR_SYS;
        }
        else return CAM_ERR_NONE;
    }
    else
        return xioctl (fd, ATOMISP_IOC_S_ISP_MACC,&old_macc_config, "Set_Macc_TBL");
}


cam_err_t
cam_driver_set_sc (int fd, int on)
{
    return cam_driver_set_attribute (fd, V4L2_CID_ATOMISP_SHADING_CORRECTION, on,
                                     "Shading Correction");
}

/* Bad Pixel Detection*/
cam_err_t
cam_driver_set_bpd (int fd, int on)
{
    cam_err_t ret2;
    int ret;
    ret2 = cam_driver_set_attribute (fd, V4L2_CID_ATOMISP_BAD_PIXEL_DETECTION,
	    on, "Bad Pixel Detection");
    if (ret2 == CAM_ERR_NONE && on)
    {
        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_dp_config();
            if (ret == 0)
            {
                return CAM_ERR_NONE;
            }
            else return CAM_ERR_SYS;
        }
        else return CAM_ERR_NONE;
    }
    else return ret2;
}

cam_err_t
cam_driver_get_bpd (int fd, int *on)
{
    return cam_driver_get_attribute (fd, V4L2_CID_ATOMISP_BAD_PIXEL_DETECTION,
                                     on, "Bad Pixel Detection");
}
cam_err_t
cam_driver_set_wb (int fd, int on)
{
    struct atomisp_wb_config wb_cfg;
    cam_err_t ret;

    if (on) {
        if (ci_adv_cfg_file_loaded())
        {
            cam_driver_get_wb_config(fd, &old_wb_config);
            ret = ci_adv_load_wb_config();
            if (ret == 0)
            {
                return CAM_ERR_NONE;
            }
            else return CAM_ERR_SYS;
        }
    }
    else
    {
        return xioctl (fd, ATOMISP_IOC_S_ISP_WHITE_BALANCE, &old_wb_config, "Set_WHITE_BALANCE");
    }
    return CAM_ERR_NONE;
}
cam_err_t
cam_driver_set_bnr (int fd, int on)
{
    struct atomisp_nr_config bnr;
    cam_err_t ret;

    if (on) {
        bnr.gain = 60000;
        bnr.direction = 3200;
        bnr.threshold_cb = 64;
        bnr.threshold_cr = 64;

        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_nr_config();
            if (ret == 0)
            {
                return CAM_ERR_NONE;
            }
            else return CAM_ERR_SYS;
        }
        else
            return xioctl (fd, ATOMISP_IOC_S_BAYER_NR, &bnr, "Bayer NR");
    } else {
        memset (&bnr, 0, sizeof(bnr));
        return xioctl (fd, ATOMISP_IOC_S_BAYER_NR, &bnr, "Bayer NR");
    }
}

/* False Color Correction, Demosaicing */
cam_err_t
cam_driver_set_fcc (int fd, int on)
{
    cam_err_t ret2;
    int ret;
    ret2 = cam_driver_set_attribute (fd, V4L2_CID_ATOMISP_FALSE_COLOR_CORRECTION,
            on, "False Color Correction");
    if(ret2 == CAM_ERR_NONE && on)
    {
        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_dp_config();
            if(ret == 0)
            {
                return CAM_ERR_NONE;
            }
            else return CAM_ERR_SYS;
        }
        else return CAM_ERR_NONE;
    }
    else return ret2;

}

cam_err_t
cam_driver_set_ynr (int fd, int on)
{
    /* YCC NR use the same parameter as Bayer NR */
    return cam_driver_set_bnr(fd, on);
}

cam_err_t
cam_driver_set_ee (int fd, int on)
{
    struct atomisp_ee_config ee;
    int ret;
    if (on) {
        ee.gain = 8192;
        ee.threshold = 128;
        ee.detail_gain = 2048;
        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_ee_config();
            if(ret == 0)
            {
                return CAM_ERR_NONE;
            }
            else return CAM_ERR_SYS;
        }
        else{
            return xioctl (fd, ATOMISP_IOC_S_EE, &ee, "Edege Ehancement");
        }
    } else {
        ee.gain = 0;
        ee.threshold = 0;
        ee.detail_gain = 0;
        return xioctl (fd, ATOMISP_IOC_S_EE, &ee, "Edege Ehancement");
    }
}

/*Black Level Compensation */
cam_err_t
cam_driver_set_blc (int fd, int on)
{
    static struct atomisp_ob_config ob_off;
    struct atomisp_ob_config ob_on;
    static int current_status = 0;
    int ret;

    if (on && current_status) {
        cam_driver_dbg("Black Level Compensation Already On\n");
        return CAM_ERR_NONE;
    }

    if (!on && !current_status) {
        cam_driver_dbg("Black Level Composition Already Off\n");
        return CAM_ERR_NONE;
    }

    ob_on.mode = atomisp_ob_mode_fixed;
    ob_on.level_gr = 0;
    ob_on.level_r = 0;
    ob_on.level_b = 0;
    ob_on.level_gb = 0;
    ob_on.start_position = 0;
    ob_on.end_position = 63;

    if (on) {
        if (xioctl (fd, ATOMISP_IOC_G_BLACK_LEVEL_COMP, &ob_off, "blc") < 0 ) {
            cam_driver_dbg("Error Get black level composition\n");
            return CAM_ERR_SYS;
        }
        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_ob_config();
            if(ret == 0)
            {
                current_status = 1;
                return CAM_ERR_NONE;
            }
            else {
                current_status = 0;
                return CAM_ERR_SYS;
            }
        }
        else {
            if (xioctl (fd, ATOMISP_IOC_S_BLACK_LEVEL_COMP, &ob_on, "blc") < 0) {
                cam_driver_dbg("Error Set black level composition\n");
                return CAM_ERR_SYS;
            }
        }
    } else {
        if (xioctl (fd, ATOMISP_IOC_S_BLACK_LEVEL_COMP, &ob_off, "blc") < 0) {
            cam_driver_dbg("Error Set black level composition\n");
            return CAM_ERR_SYS;
        }
    }
    current_status = on;
    return CAM_ERR_NONE;
}


cam_err_t
cam_driver_set_tnr (int fd, int on)
{
    int ret;
    if (on)
    {
        if (ci_adv_cfg_file_loaded())
        {
            cam_driver_get_tnr_config(fd,&old_tnr_config);
            ret = ci_adv_load_tnr_config();
            if (ret == 0)
            {
                return CAM_ERR_NONE;
            }
            else return CAM_ERR_SYS;
        }
        return CAM_ERR_SYS;
    }
    else
        return xioctl (fd, ATOMISP_IOC_S_TNR, &old_tnr_config, "ATOMISP_IOC_S_TNR");
}

cam_err_t
cam_driver_set_xnr (int fd, int on)
{
    return xioctl (fd, ATOMISP_IOC_S_XNR, &on, "ATOMISP_IOC_S_XNR");
}

cam_err_t
cam_driver_set_cac (int fd, int on)
{
    int ret;
    if(on)
    {
        cam_driver_get_gdc_tbl(fd, &old_gdc_table);
        if (ci_adv_cfg_file_loaded())
        {
            ret = ci_adv_load_gdc_table();
            if (ret != 0)
                return CAM_ERR_SYS;
        }
        else return CAM_ERR_NONE;
    }
    else {
        xioctl (fd, ATOMISP_IOC_S_ISP_GDC_TAB, &old_gdc_table, "Set_GDC_TBL");
    }

    return cam_driver_set_attribute (fd, V4L2_CID_ATOMISP_POSTPROCESS_GDC_CAC,
            on, "CAC");
}

/* Configure the color effect Mode in the kernel
 */

cam_err_t
cam_driver_set_tone_mode (int fd, enum v4l2_colorfx colorfx)
{
    return cam_driver_set_attribute (fd, V4L2_CID_COLORFX, colorfx, "Color Effect");
}

cam_err_t
cam_driver_get_tone_mode (int fd, int *colorfx)
{
    return cam_driver_get_attribute (fd, V4L2_CID_COLORFX, colorfx, "Color Effect");
}

static cam_err_t
cam_driver_set_gamma_tbl (int fd, struct atomisp_gamma_table *g_tbl)
{
    int ret;

    ret = xioctl (fd, ATOMISP_IOC_S_ISP_GAMMA, g_tbl, "S_GAMMA_TBL");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

// apply gamma table from g_gamma_table_original to g_gamma_table according current brightness, contrast and inverse settings
static void
cam_driver_apply_to_runtime_gamma(int contrast, int brightness, bool inv_gamma)
{
    int i, tmp;
    for (i = 0; i < atomisp_gamma_table_size; i++) {
        tmp = (g_gamma_table_original.data[i] * contrast >> 8) + brightness;

        if (tmp < g_cfg_gm.GmLevelMin) {
            tmp = g_cfg_gm.GmLevelMin;
        } else if (tmp > g_cfg_gm.GmLevelMax) {
            tmp = g_cfg_gm.GmLevelMax;
        }
        if (inv_gamma)
        {
            tmp = g_cfg_gm.GmLevelMin + g_cfg_gm.GmLevelMax - tmp;
        }
        g_gamma_table.data[i] = tmp;
    }
}

cam_err_t
cam_driver_init_gamma (int fd, int contrast, int brightness, bool inv_gamma)
{
    int ret;
    ret = xioctl (fd, ATOMISP_IOC_G_ISP_GAMMA, &g_gamma_table_original, "G_GAMMA_TBL");
    if (ret < 0)
    {
        return CAM_ERR_SYS;
    }
    else
    {
        cam_driver_apply_to_runtime_gamma(contrast, brightness, inv_gamma);
        return CAM_ERR_NONE;
    }
}

cam_err_t
cam_driver_set_gamma_from_value (int fd, float gamma, int contrast, int brightness, bool inv_gamma)
{
    g_cfg_gm.GmVal = gamma;
    AutoGmLut (g_gamma_table_original.data, &g_cfg_gm);

    cam_driver_apply_to_runtime_gamma(contrast, brightness, inv_gamma);

    return cam_driver_set_gamma_tbl (fd, &g_gamma_table);
}

cam_err_t
cam_driver_set_contrast_bright (int fd, int contrast, int brightness, bool inv_gamma)
{
    cam_driver_apply_to_runtime_gamma(contrast, brightness, inv_gamma);

    return cam_driver_set_gamma_tbl (fd, &g_gamma_table);
}

/* Description
 * VF Scaling for View Finder
 * Parameters:
 * factor : scaling factor, 0..2. Power of 1/2
 * TBD
 * Waiting for SH's implementation for this feature
 */
cam_err_t
cam_driver_set_vf (int fd, int factor, int updatek)
{
    cam_driver_dbg ("%s\n", __func__);
    return CAM_ERR_NONE;
}


/* SuperImpose
 * Waiting for SH provide the more useful API to do the image/vide overlay.
 */
cam_err_t
cam_driver_set_si (int fd, int on)
{
    cam_driver_dbg ("%s\n", __func__);
    /*
     * 1. convert the overlay file to Y file, U file and V file
     * 2. Store the Y U V file name to sh_si_config
     * 3. superimpose_file_read((sh_si_config *) arg);
     * 4. Call the kernel to store the pattern to xmem.
    */
    return CAM_ERR_NONE;
}

cam_err_t
cam_driver_set_gdc (int fd, int on)
{
    cam_err_t ret2;
    int ret;
    ret2 = cam_driver_set_attribute (fd, V4L2_CID_ATOMISP_POSTPROCESS_GDC_CAC,
            on, "GDC");
    if (on) {
        if (ci_adv_cfg_file_loaded()) {
            LOGD("%s: cfg file already loaded\n", __func__);
            ret = ci_adv_load_gdc_table();
            if (ret == 0)
                return CAM_ERR_NONE;
            else {
                ret2 = cam_driver_set_attribute (fd,
                            V4L2_CID_ATOMISP_POSTPROCESS_GDC_CAC, false, "GDC");
                return CAM_ERR_SYS;
            }
        }
    }

    return ret2;
}

cam_err_t
cam_driver_set_dvs (int fd, int on)
{
    return cam_driver_set_attribute(fd, V4L2_CID_ATOMISP_VIDEO_STABLIZATION,
                                    on, "Video Stabilization");
}

cam_err_t
cam_driver_set_exposure (int fd, int exposure)
{
    if (exposure == 0)
        return CAM_ERR_NONE;
    return cam_driver_set_attribute (fd, V4L2_CID_EXPOSURE_ABSOLUTE, exposure,
                                     "exposure");
}

cam_err_t
cam_driver_get_exposure (int fd, int *exposure)
{
    return cam_driver_get_attribute (fd, V4L2_CID_EXPOSURE_ABSOLUTE, exposure, "Exposure");
}

cam_err_t
cam_driver_set_aperture (int fd, int aperture)
{
    if (aperture == 0)
        return CAM_ERR_NONE;
    return cam_driver_set_attribute (fd, V4L2_CID_APERTURE_ABSOLUTE, aperture, "aperture");
}

cam_err_t
cam_driver_get_aperture (int fd, int *aperture)
{
    return cam_driver_get_attribute (fd, V4L2_CID_APERTURE_ABSOLUTE, aperture, "Aperture");
}

cam_err_t
cam_driver_set_iso_speed (int fd, int iso_speed)
{
    if (iso_speed == 0)
        return CAM_ERR_NONE;
    return cam_driver_set_attribute (fd, V4L2_CID_ISO_ABSOLUTE, iso_speed, "iso_speed");
}

cam_err_t
cam_driver_get_iso_speed (int fd, int *iso_speed)
{
    return cam_driver_get_attribute (fd, V4L2_CID_ISO_ABSOLUTE, iso_speed, "ISO_SPEED");
}

cam_err_t
cam_driver_set_focus_posi (int fd, int focus)
{
    return cam_driver_set_attribute (fd, V4L2_CID_FOCUS_ABSOLUTE, focus, "Focus");
}

cam_err_t
cam_driver_get_focus_posi (int fd, int *focus)
{
    return cam_driver_get_attribute (fd, V4L2_CID_FOCUS_ABSOLUTE, focus, "Focus");
}

cam_err_t
cam_driver_set_zoom (int fd, int zoom)
{
    return cam_driver_set_attribute (fd, V4L2_CID_ZOOM_ABSOLUTE, zoom, "zoom");
}

cam_err_t
cam_driver_get_zoom (int fd, int *zoom)
{
    return cam_driver_get_attribute (fd, V4L2_CID_ZOOM_ABSOLUTE, zoom, "Zoom");
}

cam_err_t
cam_driver_set_autoexposure (int fd, enum v4l2_exposure_auto_type expo)
{
    return cam_driver_set_attribute (fd, V4L2_CID_EXPOSURE_AUTO, expo, "auto exposure");
}

cam_err_t
cam_driver_get_makernote (int fd, unsigned char *buf, unsigned size)
{
    int ret;
    struct atomisp_makernote arg;

    arg.buf = buf;
    arg.size = size;

    ret = xioctl (fd, ATOMISP_IOC_ISP_MAKERNOTE, &arg, "G_MAKERNOTE");
    if (ret < 0)
        return CAM_ERR_SYS;
    else
        return CAM_ERR_NONE;
}

static cam_err_t cam_driver_set_led_flash (int fd, int id, int value)
{
    cam_err_t ret = CAM_ERR_NONE;
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control;

    controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
    controls.count  = 1;
    controls.controls = &control;

    control.id = id;
    control.value = value;

    if (-1 == xioctl(fd, VIDIOC_S_EXT_CTRLS, &controls, "led flash control")) {
        return CAM_ERR_SYS;
    }

    return ret;
}

void cam_driver_led_flash_off (int fd)
{
    if (CAM_ERR_NONE != cam_driver_set_led_flash(fd, V4L2_CID_FLASH_TRIGGER, 0)) {
        cam_driver_dbg("Error to trigger flash off\n");
    }
}

void cam_driver_led_flash_trigger (int fd,
                                   int mode,
                                   int duration_ms,
                                   int percent_time_100)
{
    if (CAM_ERR_NONE != cam_driver_set_led_flash(fd, V4L2_CID_FLASH_MODE, mode)) {
        cam_driver_dbg("Error to set flash strobe\n");
    }

    if (CAM_ERR_NONE != cam_driver_set_led_flash(fd, V4L2_CID_FLASH_DURATION, duration_ms)) {
        cam_driver_dbg("Error to set flash duration\n");
    }
    if (CAM_ERR_NONE != cam_driver_set_led_flash(fd, V4L2_CID_FLASH_INTENSITY, percent_time_100)) {
        cam_driver_dbg("Error to set flash intensity\n");
    }
    if (CAM_ERR_NONE != cam_driver_set_led_flash(fd, V4L2_CID_FLASH_TRIGGER, 1)) {
        cam_driver_dbg("Error to trigger flash on\n");
    }
}

void cam_driver_led_indicator_trigger (int fd, int percent_time_100)
{
    if (CAM_ERR_NONE != cam_driver_set_led_flash(fd, V4L2_CID_INDICATOR_INTENSITY, percent_time_100)) {
        cam_driver_dbg("Error to set indicator intensity\n");
    }
}

void cam_driver_led_assist_trigger (int fd, int percent_time_100)
{
    if (CAM_ERR_NONE != cam_driver_set_led_flash(fd, V4L2_CID_TORCH_INTENSITY, percent_time_100)) {
        cam_driver_dbg("Error to set assist intensity\n");
    }
}

int atomisp_set_cfg_from_file(int fd)
{
	return atomisp_set_cfg(fd);
}
