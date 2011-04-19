/* Intel ATOM ISP abstract Layer API
** Copyright (c) 2010 Intel Corporation
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

#ifndef _MFLD_DRIVER_H
#define _MFLD_DRIVER_H
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include "ci_adv_pub.h"

#ifndef ON
#define ON 1
#define OFF 0
#endif

typedef enum
{
    CAM_ERR_NONE,
    CAM_ERR_PARAM,
    CAM_ERR_UNSUPP,
    CAM_ERR_HW,
    CAM_ERR_NOT_OPEN,
    CAM_ERR_SYS,
    CAM_ERR_LEXIT,
    CAM_ERR_DEPRECATED,
    CAM_ERR_INVALID_STATE,
    CAM_ERR_INTERNAL,
    CAM_ERR_3A
} cam_err_t;

/* Color effect settings */
cam_err_t cam_driver_set_tone_mode (int fd, enum v4l2_colorfx colorfx);
cam_err_t cam_driver_get_tone_mode (int fd, int *colorfx);

/* **********************************************************
 * Noise Reduction Part
 * **********************************************************/

/* Fixed Pattern Noise Reduction */
cam_err_t cam_driver_set_fpn (int fd, int on);

/* Bayer Noise Reduction */
cam_err_t cam_driver_set_bnr (int fd, int on);

/* YNR (Y Noise Reduction), YEE (Y Edge Enhancement) */
cam_err_t cam_driver_set_ynr (int fd, int on);

/* Temporal Noise Reduction */
cam_err_t cam_driver_set_tnr (int fd, int on);

/* Extra Noise Reduction */
cam_err_t cam_driver_set_xnr (int fd, int on);

/* **********************************************************
 * Advanced Features Part
 * **********************************************************/

/* Shading Correction */
cam_err_t cam_driver_set_sc (int fd, int on);

/* Bad Pixel Detection */
cam_err_t cam_driver_set_bpd (int fd, int on);
cam_err_t cam_driver_get_bpd (int fd, int *on);

/* False Color Correction, Demosaicing */
cam_err_t cam_driver_set_fcc (int fd, int on);

/* White Balance */
cam_err_t cam_driver_set_wb (int fd, int on);

/* Edge Enhancement, Sharpness */
cam_err_t cam_driver_set_ee (int fd, int on);

/* Black Level Compensation */
cam_err_t cam_driver_set_blc (int fd, int on);

/* Chromatic Aberration Correction */
cam_err_t cam_driver_set_cac (int fd, int on);

/* GDC : Geometry Distortion Correction */
cam_err_t cam_driver_set_gdc (int fd, int on);

cam_err_t cam_driver_set_macc (int fd, int on, int effect);
/* Exposure Value setting */
cam_err_t cam_driver_set_exposure(int fd, int exposure);

/* aperture settings */
cam_err_t cam_driver_set_aperture(int fd, int aperture);
cam_err_t cam_driver_set_ev_compensation(int fd, int ev_comp);
cam_err_t cam_driver_set_iso_speed(int fd, int iso_speed);
cam_err_t cam_driver_set_focus_posi(int fd, int focus);

cam_err_t cam_driver_set_zoom(int fd, int zoom);
cam_err_t cam_driver_set_dvs(int fd, int on);
cam_err_t cam_driver_set_autoexposure(int fd, enum v4l2_exposure_auto_type expo);

cam_err_t cam_driver_set_gamma (int fd, float gamma);
cam_err_t cam_driver_init_gamma(int fd);
cam_err_t cam_driver_get_exposure(int fd, int *exposure);
cam_err_t cam_driver_get_iso_speed(int fd, int *iso_speed);
cam_err_t cam_driver_get_focus_posi(int fd, int *focus);

cam_err_t cam_driver_set_contrast (int fd, int contrast, int brightness);

void cam_driver_dbg(const char *format, ...);

cam_err_t cam_driver_get_makernote (int fd, unsigned char *buf, unsigned size);

void cam_driver_led_flash_trigger (int fd, int mode, int duration, int intensity);
void cam_driver_led_flash_off (int fd);
int cam_driver_set_capture_mode(int fd, int mode);
int atomisp_set_cfg_from_file(int fd);

#endif /* _MFLD_DRIVER_H */
