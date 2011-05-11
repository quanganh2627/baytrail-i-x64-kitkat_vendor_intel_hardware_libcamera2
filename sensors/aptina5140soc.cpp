
#include <camera/CameraHardwareInterface.h>

#include "IntelCameraSOC.h"

namespace android{

const static struct setting_map framerate_map[] = {
        {FPS30,          -1},
        {FPS15,          -1},
        {NULL,           -1}

};

const static struct setting_map videoformat_map[] = {
        {PIX_FMT_NV12,   -1},
        {NULL,            1}
};
const static struct setting_map previewformat_map[] = {
        {PIX_FMT_NV12,   -1},
        {PIX_FMT_YUYV,   -1},
        {PIX_FMT_RGB565, -1},
        {NULL,           -1}
};
const static struct setting_map previewsize_map[] = {
        {VGA,            -1},
        {QVGA,           -1},
        {NULL,           -1}

};
const static struct setting_map pictureformat_map[] = {
        {PIX_FMT_JPEG,  -1},
        {NULL,          -1}
};
const static struct setting_map picturesize_map[] = {
        {VGA,           -1},
        {NULL,          -1}

};

const static struct setting_map focusmode_map[] = {
        {CameraParameters::FOCUS_MODE_AUTO,    1},
        {FOCUS_MODE_TOUCHED,                   0},
        {NULL,                                -1}
};
const static struct setting_map flashmode_map[] = {
        {CameraParameters::FLASH_MODE_OFF,     0},
        {CameraParameters::FLASH_MODE_ON,      2},
        {CameraParameters::FLASH_MODE_AUTO,    1},
        {NULL,                                -1}
};

const static struct setting_map jpegquality_map[] = {
        {SUPERFINE,     90},
        {FINE,          80},
        {NORMAL,        70},
        {NULL,         -1}
};
const static struct setting_map rotation_map[] = {
        {DEGREE_0,      0},
        {DEGREE_90,     1},
        {DEGREE_180,    2},
        {NULL,         -1}
};

const static struct setting_map effect_map[] = {
        { CameraParameters::EFFECT_NONE,        	       V4L2_COLORFX_NONE          }, // "none"
        { CameraParameters::EFFECT_MONO,        	       V4L2_COLORFX_MONO          }, // "mono"
        { CameraParameters::EFFECT_NEGATIVE,    	       V4L2_COLORFX_NEGATIVE      }, // "negative"
        { CameraParameters::EFFECT_SOLARIZE,    	       V4L2_COLORFX_SOLARIZE      }, // "solarize"
        { CameraParameters::EFFECT_SEPIA,       	       V4L2_COLORFX_SEPIA         }, // "sepia"
        { NULL, -1 }
};
const static struct setting_map wb_map[] = {
        { CameraParameters::WHITE_BALANCE_AUTO,        	       SENSOR_AWB_AUTO            }, // "auto"
        { CameraParameters::WHITE_BALANCE_INCANDESCENT,	       SENSOR_AWB_INCANDESCENT    }, // "incandescent"
        { CameraParameters::WHITE_BALANCE_FLUORESCENT,         SENSOR_AWB_FLUORESCENT     }, // "flouorescent"
        { CameraParameters::WHITE_BALANCE_DAYLIGHT,            SENSOR_AWB_DAYLIGHT        }, // "daylight"
        { CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT,     SENSOR_AWB_CLOUDY_DAYLIGHT }, // "cloudy_daylight"
        { NULL, -1 }
};
const static struct setting_map exposure_map[] = {
        { CameraParameters::KEY_EXPOSURE_COMPENSATION,         EXPOSURE_COMPENSATION      }, // "default"
        { CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION,     MAX_EXPOSURE_COMPENSATION  }, // "max"
        { CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION,     MIN_EXPOSURE_COMPENSATION  }, // "min"
        { CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP,    EXPOSURE_COMPENSATION_STEP }, // "step"
        { NULL, -1 }
};

struct parameters aptina5140soc = { 
        "a5140soc"   ,
        framerate_map    ,
        videoformat_map  ,
        previewformat_map,
        previewsize_map  , 
        pictureformat_map,
        picturesize_map  , 
        focusmode_map    ,
        flashmode_map    ,
        //jpegquality_map  ,
        NULL,
        rotation_map     ,	
        effect_map       , 
        wb_map           , 
        exposure_map 
};





}



















