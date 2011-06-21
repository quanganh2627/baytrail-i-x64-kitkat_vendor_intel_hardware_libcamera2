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

#ifndef ANDROID_HARDWARE_INTEL_CAMERA_H
#define ANDROID_HARDWARE_INTEL_CAMERA_H

#include <utils/threads.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <semaphore.h>
#include "atomisp_config.h"
#include <linux/atomisp.h>

    /* Define the default parameter for the camera */

#ifdef __cplusplus
}
#endif

namespace android {

/*Bayer order transfer on the MIPI lane*/
#define BAYER_ORDER_GRBG        0
#define BAYER_ORDER_RGGB        1
#define BAYER_ORDER_BGGR        2
#define BAYER_ORDER_GBRG        3
struct file_input {
    char *name;
    unsigned int width;
    unsigned int height;
    unsigned int size;
    int format;
    int bayer_order;
    char *mapped_addr;
};

//v4l2 buffer in pool
struct v4l2_buffer_info {
    void *data;
    size_t length;
    int width;
    int height;
    int fourcc;
    int flags; //You can use to to detern the buf status
    struct v4l2_buffer vbuffer;
};

struct v4l2_buffer_pool {
    int active_buffers;
    struct v4l2_buffer_info bufs [MAX_V4L2_BUFFERS];
};

/* Gamma configuration
 * Also used by extended dymanic range and tone control
 */
struct atomisp_gm_config
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
// for camera texture streaming
#define BC_Video_ioctl_fill_buffer           0
#define BC_Video_ioctl_get_buffer_count      1
#define BC_Video_ioctl_get_buffer_phyaddr    2
#define BC_Video_ioctl_get_buffer_index      3
#define BC_Video_ioctl_request_buffers       4
#define BC_Video_ioctl_set_buffer_phyaddr    5
#define BC_Video_ioctl_release_buffer_device 6

typedef struct bc_buf_ptr {
    unsigned int index;
    int size;
    unsigned long pa;
    unsigned long handle;
} bc_buf_ptr_t;

enum BC_memory {
    BC_MEMORY_MMAP      = 1,
    BC_MEMORY_USERPTR   = 2,
};

/*
 * the following types are tested for fourcc in struct bc_buf_params_t
 *   NV12
 *   UYVY
 *   RGB565 - not tested yet
 *   YUYV
 */
typedef struct bc_buf_params {
    int count;	/* number of buffers, [in/out] */
    int width;
    int height;
    int stride;
    unsigned int fourcc;	/* buffer pixel format */
    enum BC_memory type;
} bc_buf_params_t;
typedef struct __IspSettings
{
    int contrast;               // FIX Un.8
    int brightness;
    bool inv_gamma;          // inversed gamma flag, used in negative effect
} IspSettings;

class IntelCamera {
public:
    IntelCamera();
    ~IntelCamera();

    static IntelCamera* createInstance(void) {
        static IntelCamera singleton;
        return &singleton;
    }

    int initCamera(int camera_id);
    int deinitCamera(void);

    //File Input
    int initFileInput();
    int deInitFileInput();
    int configureFileInput(const struct file_input *image);

    //Preview
    int startCameraPreview();
    void stopCameraPreview();
    int getPreview(void **data);
    int putPreview(int index);
    int setPreviewSize(int width, int height, int fourcc);
    int getPreviewSize(int *width, int *height, int *frame_size, int *padded_size);
    int getPreviewPixelFormat(void);
    void setPreviewUserptr(int index, void *addr);

    //Postview
    int setPostViewSize(int width, int height, int fourcc);
    int getPostViewSize(int *width, int *height, int *frame_size);
    int getPostViewPixelFormat(void);

    //Snapshot
    int startSnapshot();
    void stopSnapshot();
    int getSnapshot(void **main_out, void **postview, void *postview_rgb565);
    int putSnapshot(int index);
    int setSnapshotSize(int width, int height, int fourcc);
    int getSnapshotSize(int *width, int *height, int *frame_size);
    int getSnapshotPixelFormat(void);
    void setSnapshotUserptr(int index, void *pic_addr, void *pv_addr);
    void releasePostviewBcd();
    int SnapshotPostProcessing(void *img_data);

    //recorder
    int startCameraRecording();
    void stopCameraRecording();
    int getRecording(void **main_out, void **preview_out);
    int putRecording(int index);
    int setRecorderSize(int width, int height, int fourcc);
    int getRecorderSize(int *width, int *height, int *frame_size, int *padded_size);
    int getRecorderPixelFormat(void);
    void setRecorderUserptr(int index, void *preview, void *recorder);
    int updateRecorderUserptr(int num, unsigned char *recorder[]);

    int get_num_buffers(void);
    float getFramerate(void);

    // Flash
    void setIndicatorIntensity(int percent_time_100);
    void setAssistIntensity(int percent_time_100);
    void captureFlashOnCertainDuration(int mode, int duration, int percent_time_100);
    void setFlashMode(int mode);
    int getFlashMode();
    int calculateLightLevel();
    // ISP related settings
    int setColorEffect(int effect);
    int setXNR(bool on);
    int setGDC(bool on);
    void checkGDC(void);
    int setTNR(bool on);
    int setNREE(bool on);
    int setMACC(int macc);

    // Parameter settings
    int get_zoom_val(void);
    int set_zoom_val(int zoom);

    // RGB565 color space conversion
    void toRGB565(int width, int height, int fourcc, void *src, void *dst);
    // NV12 color space conversion
    void toNV12(int width, int height, int fourcc, void *src, void *dst);
    int      m_frameSize(int format, int width, int height);
private:
    int     createBufferPool(int device, int buffer_count);
    void    destroyBufferPool(int device);
    int     activateBufferPool(int device);
    int     putDualStreams(int index);
    void    update3Aresults(void);
    void    yuv_to_rgb16(unsigned char y,unsigned char u, unsigned char v, unsigned char *rgb);
    void    yuv420_to_rgb565(int width, int height, unsigned char *yuv420, unsigned short *rgb565);
    void    nv12_to_rgb565(int width, int height, unsigned char *nv12, unsigned char *rgb565);
    void    yuyv422_to_yuv420sp(int width, int height, unsigned char *src, unsigned char *dst);
    void    yuv420_to_yuv420sp(int width, int height, unsigned char *src, unsigned char *dst);

    // Device control
    int openDevice(int mode);
    void closeDevice(void);
    int configureDevice(int device, int w, int h, int fourcc);
    int startCapture(int device, int buffer_count);
    void stopCapture(int device);
    void stopDualStreams(void);
    int grabFrame(int device);
    int resetCamera(void);
    int set_capture_mode(int mode);
    int trimRecordingBuffer(void *main);
    void trimNV12(unsigned char *src, unsigned char* dst, int src_width, int src_height,
                           int dst_width, int dst_height);
    void trimRGB565(unsigned char *src, unsigned char* dst,
                             int src_width, int src_height,
                             int dst_width, int dst_height);

    int      m_paddingWidth(int format, int width, int height);

    int             m_flag_camera_start[V4L2_DEVICE_NUM];
    int             m_flag_init;
    int             m_camera_id;

    // Frame width, hight and size
    int             m_preview_v4lformat;
    int             m_preview_width;
    int             m_preview_height;
    int             m_preview_max_width;
    int             m_preview_max_height;
    int             m_preview_pad_width;

    int             m_postview_v4lformat;
    int             m_postview_width;
    int             m_postview_height;

    int             m_snapshot_v4lformat;
    int             m_snapshot_width;
    int             m_snapshot_height;
    int             m_snapshot_max_width;
    int             m_snapshot_max_height;
    int             m_snapshot_pad_width;

    int             m_recorder_v4lformat;
    int             m_recorder_width;
    int             m_recorder_height;
    int             m_recorder_max_width;
    int             m_recorder_max_height;
    int             m_recorder_pad_width;

    int             current_w[V4L2_DEVICE_NUM];
    int             current_h[V4L2_DEVICE_NUM];
    int             current_v4l2format[V4L2_DEVICE_NUM];

    //For V4l2
    int             num_buffers; //How many buffer request
    int             run_mode; //ISP run mode
    struct v4l2_buffer_pool v4l2_buf_pool[V4L2_DEVICE_NUM]; //pool[0] for device0 pool[1] for device1
    struct v4l2_buffer_pool v4l2_buf_pool_reserve[V4L2_DEVICE_NUM];
    struct v4l2_capability cap;
    struct v4l2_streamparm parm;
    int             video_fds[V4L2_DEVICE_NUM];
    int             main_fd;

    void captureFlashOff(void);
    int mFlashMode;

    //this function will compare saved parameters
    //to default value. Set to isp if they diff since
    //isp is reset.
    int flushISPParameters();

    float framerate;
    //Save parameters here
    int	zoom_val;
    int mMacc;
    bool mNrEeOn;
    bool mXnrOn;
    bool mGDCOn;
    bool mTnrOn;
    int mColorEffect;

    bool mInitGamma;    // true if Gamma table in user space has been initialized

    int set_zoom_val_real(int zoom);
    IspSettings mIspSettings;	// ISP related settings

    //V4L2 related ioctl wrapper
    int v4l2_capture_open(int device);
    void v4l2_capture_close(int fd);

    int v4l2_capture_querycap(int fd, int device, struct v4l2_capability *cap);
    int v4l2_capture_s_input(int fd, int index);
    int v4l2_capture_s_format(int fd, int device, int w, int h, int fourcc);

    int v4l2_capture_g_framerate(int fd, float * framerate, int width,
                            int height, int pix_fmt);

    int v4l2_capture_request_buffers(int fd, int device, uint num_buffers);
    int v4l2_capture_new_buffer(int fd, int device, int frame_idx,
                                struct v4l2_buffer_info *buf_info);
    int v4l2_capture_free_buffer(int fd, int device,
                                 struct v4l2_buffer_info *buf_info);
    int v4l2_capture_release_buffers(int fd, int device);

    int v4l2_capture_streamon(int fd);
    int v4l2_capture_streamoff(int fd);

    int v4l2_capture_g_parm(int fd, struct v4l2_streamparm *parm);
    int v4l2_capture_s_parm(int fd, int device, struct v4l2_streamparm *parm);


    int v4l2_capture_qbuf(int fd, int index, struct v4l2_buffer_info *buf);
    int v4l2_capture_dqbuf(int fd, struct v4l2_buffer *buf);
    //To start/stop the kernel DQ thread
    int v4l2_capture_control_dq(int fd, int start);

    //For file input
    int v4l2_read_file(char *file_name, int width, int height, int format, int bayer_order);

    //Need increate ISP timeout when using GDC
    void v4l2_set_isp_timeout(int timeout);

    // Color effect settings
    int atomisp_set_tone_mode (int fd, enum v4l2_colorfx colorfx);
    int atomisp_get_tone_mode (int fd, int *colorfx);

    /* **********************************************************
     * Noise Reduction Part
     * **********************************************************/

    /* Fixed Pattern Noise Reduction */
    int atomisp_set_fpn (int fd, int on);

    /* Bayer Noise Reduction */
    int atomisp_set_bnr (int fd, int on);

    /* YNR (Y Noise Reduction), YEE (Y Edge Enhancement) */
    int atomisp_set_ynr (int fd, int on);

    /* Temporal Noise Reduction */
    int atomisp_set_tnr (int fd, int on);

    /* Extra Noise Reduction */
    int atomisp_set_xnr (int fd, int on);

    /* **********************************************************
     * Advanced Features Part
     * **********************************************************/

    /* Shading Correction */
    int atomisp_set_sc (int fd, int on);

    /* Bad Pixel Detection */
    int atomisp_set_bpd (int fd, int on);
    int atomisp_get_bpd (int fd, int *on);

    /* False Color Correction, Demosaicing */
    int atomisp_set_fcc (int fd, int on);

    /* Edge Enhancement, Sharpness */
    int atomisp_set_ee (int fd, int on);

    /* Black Level Compensation */
    int atomisp_set_blc (int fd, int on);

    /* Chromatic Aberration Correction */
    int atomisp_set_cac (int fd, int on);

    /* GDC : Geometry Distortion Correction */
    int atomisp_set_gdc (int fd, int on);

    int atomisp_set_macc (int fd, int on, int effect);
    /* Exposure Value setting */

    // exposure/focus/aperture settings
    int atomisp_set_aperture(int fd, int aperture);
    int atomisp_get_aperture (int fd, int *aperture);
    int atomisp_set_ev_compensation(int fd, int ev_comp);
    int atomisp_set_iso_speed(int fd, int iso_speed);
    int atomisp_set_focus_posi(int fd, int focus);
    int atomisp_set_exposure(int fd, int exposure);

    int atomisp_set_zoom(int fd, int zoom);
    int atomisp_get_zoom(int fd, int *zoom);
    int atomisp_set_dvs(int fd, int on);

    int atomisp_init_gamma(int fd, int contrast, int brightness,
                                        bool inv_gamma);
    int atomisp_set_gamma_from_value (int fd, float gamma,
                               int contrast, int brightness, bool inv_gamma);
    int atomisp_get_exposure(int fd, int *exposure);
    int atomisp_get_iso_speed(int fd, int *iso_speed);
    int atomisp_get_focus_posi(int fd, int *focus);

    int atomisp_set_contrast_bright (int fd, int contrast,
                                              int brightness, bool inv_effect);

    //Flash operation
    int atomisp_led_flash_trigger (int fd, int mode, int duration_ms,
                                    int percent_time_100);
    int atomisp_led_flash_off (int fd);
    int atomisp_led_indicator_trigger (int fd, int percent_time_100);
    int atomisp_led_assist_trigger (int fd, int percent_time_100);

    //Set the Capture Mode in driver
    int atomisp_set_capture_mode(int fd, int mode);

    int atomisp_get_de_config (int fd, struct atomisp_de_config *de_cfg);

    //BCD driver for text streaming
    int v4l2_register_bcd(int fd, int num_frames, void **ptrs, int w, int h,
                          int fourcc, int size);
    int v4l2_release_bcd(int fd);

    //configure file handling
    int atomisp_set_cfg_from_file(int fd);
    int atomisp_parse_cfg_file();
    //Internal function
    int xioctl(int fd, int request, void *arg, const char *name);
    int atomisp_get_attribute (int fd, int attribute_num, int *value,
                                         char *name);
    int atomisp_set_attribute (int fd, int attribute_num,
                                         const int value, const char *name);

    int atomisp_get_ctc_tbl (int fd, struct atomisp_ctc_table *ctc_tbl);
    int atomisp_get_macc_tbl (int fd, struct atomisp_macc_config *macc_config);
    int atomisp_get_gdc_tbl (int fd, struct atomisp_morph_table *morph_tbl);
    int atomisp_get_tnr_config (int fd, struct atomisp_tnr_config *tnr_cfg);
    int atomisp_get_ee_config (int fd, struct atomisp_ee_config *ee_cfg);
    int atomisp_get_nr_config (int fd, struct atomisp_nr_config *nr_cfg);
    int atomisp_get_dp_config (int fd, struct atomisp_dp_config *dp_cfg);
    int atomisp_get_wb_config (int fd, struct atomisp_wb_config *wb_cfg);
    int atomisp_get_ob_config (int fd, struct atomisp_ob_config *ob_cfg);
    int atomisp_get_fpn_tbl(int fd, struct atomisp_frame* fpn_tbl);
    int atomisp_set_gamma_tbl (int fd, struct atomisp_gamma_table *g_tbl);
    int atomisp_apply_to_runtime_gamma(int contrast, int brightness, bool inv_gamma);

    bool file_injection; /* file input node, used to distinguish DQ poll timeout */
    int g_isp_timeout;
    int autoGmLut (unsigned short *pptDst, struct atomisp_gm_config *cfg_gm);

    //these configs are used to restore configs
    struct atomisp_de_config old_de_config;
    struct atomisp_ctc_table old_ctc_table;
    struct atomisp_tnr_config old_tnr_config;
    struct atomisp_nr_config old_nr_config;
    struct atomisp_dp_config old_dp_config;
    struct atomisp_wb_config old_wb_config;
    struct atomisp_morph_table old_gdc_table;
    struct atomisp_macc_config old_macc_config;
    struct atomisp_frame old_fpn_tbl;
    struct atomisp_gamma_table g_gamma_table_original;	// original Gamma table for precise Gamma restore
    struct atomisp_gamma_table g_gamma_table;
    struct atomisp_gm_config g_cfg_gm;
    struct file_input file_image;

    int analyze_cfg_value(unsigned int index, char *value);
    int find_cfg_index(char *in);
    int atomisp_set_cfg(int fd);

};

}; // namespace android

#endif // ANDROID_HARDWARE_INTEL_CAMERA_H
