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

#define LOG_TAG "Atom_EXIFMaker"

#include "EXIFMaker.h"
#include "LogHelper.h"
#include "AtomISP.h"
#include <camera.h>


namespace android {

#define DEFAULT_ISO_SPEED 100

EXIFMaker::EXIFMaker() :
    mAAA(AtomAAA::getInstance())
    ,initialized(false)
{
    LOG1("@%s", __FUNCTION__);
}

EXIFMaker::~EXIFMaker()
{
    LOG1("@%s", __FUNCTION__);
}

void EXIFMaker::initialize(const CameraParameters &params, const atomisp_makernote_info &makerNote)
{
    LOG1("@%s: params = %p", __FUNCTION__, &params);

    /* We clear the exif attributes, so we won't be using some old values
     * from a previous EXIF generation.
     */
    clear();

    // Initialize the exifAttributes with specific values
    // time information
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime((char *)exifAttributes.date_time, sizeof(exifAttributes.date_time), "%Y:%m:%d %H:%M:%S", timeinfo);

    // conponents configuration. 0 means does not exist
    // 1 = Y; 2 = Cb; 3 = Cr; 4 = R; 5 = G; 6 = B; other = reserved
    memset(exifAttributes.components_configuration, 0, sizeof(exifAttributes.components_configuration));

    // max aperture. the smallest F number of the lens. unit is APEX value.
    // TBD, should get from driver
    exifAttributes.max_aperture.num = exifAttributes.aperture.num;
    exifAttributes.max_aperture.den = exifAttributes.aperture.den;

    // subject distance,    0 means distance unknown; (~0) means infinity.
    exifAttributes.subject_distance.num = EXIF_DEF_SUBJECT_DISTANCE_UNKNOWN;
    exifAttributes.subject_distance.den = 1;

    // light source, 0 means light source unknown
    exifAttributes.light_source = 0;

    // gain control, 0 = none;
    // 1 = low gain up; 2 = high gain up; 3 = low gain down; 4 = high gain down
    exifAttributes.gain_control = 0;

    // sharpness, 0 = normal; 1 = soft; 2 = hard; other = reserved
    exifAttributes.sharpness = 0;

    // the picture's width and height
    params.getPictureSize((int*)&exifAttributes.width, (int*)&exifAttributes.height);

    thumbWidth = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    thumbHeight = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);

    int rotation = params.getInt(CameraParameters::KEY_ROTATION);
    exifAttributes.orientation = 1;
    if (0 == rotation)
        exifAttributes.orientation = 1;
    else if (90 == rotation)
        exifAttributes.orientation = 6;
    else if (180 == rotation)
        exifAttributes.orientation = 3;
    else if (270 == rotation)
        exifAttributes.orientation = 8;
    LOG1("EXIF: rotation value:%d degrees, orientation value:%d",
            rotation, exifAttributes.orientation);

    initializeHWSpecific(makerNote);
    initializeLocation(params);

    initialized = true;
}

void EXIFMaker::initializeLocation(const CameraParameters &params)
{
    LOG1("@%s", __FUNCTION__);
    // GIS information
    bool gpsEnabled = true;
    const char *platitude = params.get(CameraParameters::KEY_GPS_LATITUDE);
    const char *plongitude = params.get(CameraParameters::KEY_GPS_LONGITUDE);
    const char *paltitude = params.get(CameraParameters::KEY_GPS_ALTITUDE);
    const char *ptimestamp = params.get(CameraParameters::KEY_GPS_TIMESTAMP);
    const char *pprocmethod = params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);

    // check whether the GIS Information is valid
    if((NULL == platitude) || (NULL == plongitude))
        gpsEnabled = false;

    exifAttributes.enableGps = gpsEnabled;
    LOG1("EXIF: gpsEnabled: %d", gpsEnabled);

    if(gpsEnabled) {
        float latitude, longitude, altitude;
        long timestamp;
        unsigned len;
        struct tm time;

        // the version is given as 2.2.0.0, it is mandatory when GPSInfo tag is present
        const unsigned char gpsversion[4] = {0x02, 0x02, 0x00, 0x00};
        memcpy(exifAttributes.gps_version_id, gpsversion, sizeof(gpsversion));

        // latitude, for example, 39.904214 degrees, N
        latitude = atof(platitude);
        if(latitude > 0)
            memcpy(exifAttributes.gps_latitude_ref, "N", sizeof(exifAttributes.gps_latitude_ref));
        else
            memcpy(exifAttributes.gps_latitude_ref, "S", sizeof(exifAttributes.gps_latitude_ref));
        latitude = fabs(latitude);
        exifAttributes.gps_latitude[0].num = (uint32_t)latitude;
        exifAttributes.gps_latitude[0].den = 1;
        exifAttributes.gps_latitude[1].num = (uint32_t)((latitude - exifAttributes.gps_latitude[0].num) * 60);
        exifAttributes.gps_latitude[1].den = 1;
        exifAttributes.gps_latitude[2].num = (uint32_t)(((latitude - exifAttributes.gps_latitude[0].num) * 60 - exifAttributes.gps_latitude[1].num) * 60 * 100);
        exifAttributes.gps_latitude[2].den = 100;
        LOG1("EXIF: latitude, ref:%s, dd:%d, mm:%d, ss:%d",
                exifAttributes.gps_latitude_ref, exifAttributes.gps_latitude[0].num,
                exifAttributes.gps_latitude[1].num, exifAttributes.gps_latitude[2].num);

        // longitude, for example, 116.407413 degrees, E
        longitude = atof(plongitude);
        if(longitude > 0)
            memcpy(exifAttributes.gps_longitude_ref, "E", sizeof(exifAttributes.gps_longitude_ref));
        else
            memcpy(exifAttributes.gps_longitude_ref, "W", sizeof(exifAttributes.gps_longitude_ref));
        longitude = fabs(longitude);
        exifAttributes.gps_longitude[0].num = (uint32_t)longitude;
        exifAttributes.gps_longitude[0].den = 1;
        exifAttributes.gps_longitude[1].num = (uint32_t)((longitude - exifAttributes.gps_longitude[0].num) * 60);
        exifAttributes.gps_longitude[1].den = 1;
        exifAttributes.gps_longitude[2].num = (uint32_t)(((longitude - exifAttributes.gps_longitude[0].num) * 60 - exifAttributes.gps_longitude[1].num) * 60 * 100);
        exifAttributes.gps_longitude[2].den = 100;
        LOG1("EXIF: longitude, ref:%s, dd:%d, mm:%d, ss:%d",
                exifAttributes.gps_longitude_ref, exifAttributes.gps_longitude[0].num,
                exifAttributes.gps_longitude[1].num, exifAttributes.gps_longitude[2].num);

        if (paltitude != NULL) {
            // altitude, sea level or above sea level, set it to 0; below sea level, set it to 1
            altitude = atof(paltitude);
            exifAttributes.gps_altitude_ref = ((altitude > 0) ? 0 : 1);
            altitude = fabs(altitude);
            exifAttributes.gps_altitude.num = (uint32_t)altitude;
            exifAttributes.gps_altitude.den = 1;
            LOG1("EXIF: altitude, ref:%d, height:%d",
                    exifAttributes.gps_altitude_ref, exifAttributes.gps_altitude.num);
        }

        if (ptimestamp != NULL) {
            // timestamp
            timestamp = atol(ptimestamp);
            gmtime_r(&timestamp, &time);
            time.tm_year += 1900;
            time.tm_mon += 1;
            exifAttributes.gps_timestamp[0].num = time.tm_hour;
            exifAttributes.gps_timestamp[0].den = 1;
            exifAttributes.gps_timestamp[1].num = time.tm_min;
            exifAttributes.gps_timestamp[1].den = 1;
            exifAttributes.gps_timestamp[2].num = time.tm_sec;
            exifAttributes.gps_timestamp[2].den = 1;
            snprintf((char *)exifAttributes.gps_datestamp, sizeof(exifAttributes.gps_datestamp), "%04d:%02d:%02d",
                    time.tm_year, time.tm_mon, time.tm_mday);
            LOG1("EXIF: timestamp, year:%d,mon:%d,day:%d,hour:%d,min:%d,sec:%d",
                    time.tm_year, time.tm_mon, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
        }

        if (pprocmethod != NULL) {
            // processing method
            if(strlen(pprocmethod) + 1 >= sizeof(exifAttributes.gps_processing_method))
                len = sizeof(exifAttributes.gps_processing_method);
            else
                len = strlen(pprocmethod) + 1;
            memcpy(exifAttributes.gps_processing_method, pprocmethod, len);
            LOG1("EXIF: GPS processing method:%s", exifAttributes.gps_processing_method);
        }
    }
}

void EXIFMaker::setSensorParams(const SensorParams& sensorParams)
{
    LOG1("@%s", __FUNCTION__);

    // exposure time
    exifAttributes.exposure_time.num = sensorParams.expTime;
    exifAttributes.exposure_time.den = 10000;
    LOG1("EXIF: exposure time=%u", sensorParams.expTime);

    // shutter speed, = -log2(exposure time)
    float exp_t = (float)(sensorParams.expTime / 10000.0);
    float shutter = -1.0 * (log10(exp_t) / log10(2.0));
    exifAttributes.shutter_speed.num = (shutter * 10000);
    exifAttributes.shutter_speed.den = 10000;
    LOG1("EXIF: shutter speed=%.2f", shutter);

    // aperture
    exifAttributes.aperture.num = 100*(int)((1.0*exifAttributes.fnumber.num/exifAttributes.fnumber.den) * sqrt(100.0/sensorParams.aperture));
    exifAttributes.aperture.den = 100;
    LOG1("EXIF: aperture=%u", sensorParams.aperture);

    // exposure bias. unit is APEX value. -99.99 to 99.99
    if (sensorParams.evBias > EV_LOWER_BOUND && sensorParams.evBias < EV_UPPER_BOUND) {
        exifAttributes.exposure_bias.num = (int)(sensorParams.evBias * 100);
        exifAttributes.exposure_bias.den = 100;
        LOG1("EXIF: Ev = %.2f", sensorParams.evBias);
    } else {
        LOGW("EXIF: Invalid Ev!");
    }
}

void EXIFMaker::initializeHWSpecific(const atomisp_makernote_info &makerNote)
{
    LOG1("@%s", __FUNCTION__);

    // f number
    if (makerNote.f_number_curr > 0) {
        // Error handler: if driver does not support Fnumber achieving, just give the default value.
        exifAttributes.fnumber.num = EXIF_DEF_FNUMBER_NUM;
        exifAttributes.fnumber.den = EXIF_DEF_FNUMBER_DEN;
    } else {
        exifAttributes.fnumber.num = makerNote.f_number_curr >> 16;
        exifAttributes.fnumber.den = makerNote.f_number_curr & 0xffff;
    }
    LOG1("EXIF: fnumber=%u (num=%d, den=%d)", makerNote.f_number_curr, exifAttributes.fnumber.num, exifAttributes.fnumber.den);

    exifAttributes.max_aperture.num = exifAttributes.fnumber.num;
    exifAttributes.max_aperture.den = exifAttributes.fnumber.den;

    if (mAAA->is3ASupported()) {
        // exp_time's unit is 100us
        mAAA->getExposureInfo(&sensorParams.expTime, &sensorParams.aperture, &sensorParams.aecApexTv, &sensorParams.aecApexSv, &sensorParams.aecApexAv);
        // exposure bias. unit is APEX value. -99.99 to 99.99
        if (mAAA->getEv(&sensorParams.evBias) != NO_ERROR) {
            sensorParams.evBias = EV_UPPER_BOUND;
            LOGW("EXIF: Could not query Ev!");
        }
        setSensorParams(sensorParams);

        // brightness, -99.99 to 99.99. FFFFFFFF.H means unknown.
        float brightness;
        if (mAAA->getAeManualBrightness(&brightness) == NO_ERROR) {
            exifAttributes.brightness.num = (int)(brightness * 100);
            exifAttributes.brightness.den = 100;
            LOG1("EXIF: brightness = %.2f", brightness);
        } else {
            LOGW("EXIF: Could not query brightness!");
        }

        // set the exposure program mode
        AeMode aeMode = mAAA->getAeMode();
        switch (aeMode) {
        case CAM_AE_MODE_MANUAL:
            exifAttributes.exposure_program = EXIF_EXPOSURE_PROGRAM_MANUAL;
            LOG1("EXIF: Exposure Program = Manual");
            exifAttributes.exposure_mode = EXIF_EXPOSURE_MANUAL;
            LOG1("EXIF: Exposure Mode = Manual");
            break;
        case CAM_AE_MODE_SHUTTER_PRIORITY:
            exifAttributes.exposure_program = EXIF_EXPOSURE_PROGRAM_SHUTTER_PRIORITY;
            LOG1("EXIF: Exposure Program = Shutter Priority");
            break;
        case CAM_AE_MODE_APERTURE_PRIORITY:
            exifAttributes.exposure_program = EXIF_EXPOSURE_PROGRAM_APERTURE_PRIORITY;
            LOG1("EXIF: Exposure Program = Aperture Priority");
            break;
        case CAM_AE_MODE_AUTO:
        default:
            exifAttributes.exposure_program = EXIF_EXPOSURE_PROGRAM_NORMAL;
            LOG1("EXIF: Exposure Program = Normal");
            exifAttributes.exposure_mode = EXIF_EXPOSURE_AUTO;
            LOG1("EXIF: Exposure Mode = Auto");
            break;
        }

        // indicates the ISO speed of the camera
        int isoSpeed;
        if (mAAA->getManualIso(&isoSpeed) == NO_ERROR) {
            exifAttributes.iso_speed_rating = isoSpeed;
        } else {
            LOGW("EXIF: Could not query ISO speed!");
            exifAttributes.iso_speed_rating = DEFAULT_ISO_SPEED;
        }
        LOG1("EXIF: ISO=%d", isoSpeed);

        // the metering mode.
        MeteringMode meteringMode  = mAAA->getAeMeteringMode();
        switch (meteringMode) {
        case CAM_AE_METERING_MODE_AUTO:
            exifAttributes.metering_mode = EXIF_METERING_AVERAGE;
            LOG1("EXIF: Metering Mode = Average");
            break;
        case CAM_AE_METERING_MODE_SPOT:
            exifAttributes.metering_mode = EXIF_METERING_SPOT;
            LOG1("EXIF: Metering Mode = Spot");
            break;
        case CAM_AE_METERING_MODE_CENTER:
            exifAttributes.metering_mode = EXIF_METERING_CENTER;
            LOG1("EXIF: Metering Mode = Center");
            break;
        case CAM_AE_METERING_MODE_CUSTOMIZED:
        default:
            exifAttributes.metering_mode = EXIF_METERING_OTHER;
            LOG1("EXIF: Metering Mode = Other");
            break;
        }

        // white balance mode. 0: auto; 1: manual
        AwbMode awbMode = mAAA->getAwbMode();
        switch (awbMode) {
        case CAM_AWB_MODE_AUTO:
        case CAM_AWB_MODE_NOT_SET:
            exifAttributes.white_balance = EXIF_WB_AUTO;
            LOG1("EXIF: Whitebalance = Auto");
            break;
        default:
            exifAttributes.white_balance = EXIF_WB_MANUAL;
            LOG1("EXIF: Whitebalance = Manual");
            break;
        }

        // scene mode
        SceneMode sceneMode = mAAA->getAeSceneMode();
        switch (sceneMode) {
        case CAM_AE_SCENE_MODE_PORTRAIT:
            exifAttributes.scene_capture_type = EXIF_SCENE_PORTRAIT;
            LOG1("EXIF: Scene Mode = Portrait");
            break;
        case CAM_AE_SCENE_MODE_LANDSCAPE:
            exifAttributes.scene_capture_type = EXIF_SCENE_LANDSCAPE;
            LOG1("EXIF: Scene Mode = Landscape");
            break;
        case CAM_AE_SCENE_MODE_NIGHT:
            exifAttributes.scene_capture_type = EXIF_SCENE_NIGHT;
            LOG1("EXIF: Scene Mode = Night");
            break;
        default:
            exifAttributes.scene_capture_type = EXIF_SCENE_STANDARD;
            LOG1("EXIF: Scene Mode = Standard");
            break;
        }
    }

    // the actual focal length of the lens, in mm.
    // there is no API for lens position.
    if (makerNote.focal_length > 0) {
        exifAttributes.focal_length.num = makerNote.focal_length >> 16;
        exifAttributes.focal_length.den = makerNote.focal_length & 0xffff;
    } else {
        exifAttributes.focal_length.num = EXIF_DEF_FOCAL_LEN_NUM;
        exifAttributes.focal_length.den = EXIF_DEF_FOCAL_LEN_DEN;
    }
    LOG1("EXIF: focal length=%u (num=%d, den=%d)", makerNote.focal_length, exifAttributes.focal_length.num, exifAttributes.focal_length.den);
}

void EXIFMaker::clear()
{
    LOG1("@%s", __FUNCTION__);
    // Reset all the attributes
    memset(&exifAttributes, 0, sizeof(exifAttributes));

    // Initialize the common values
    exifAttributes.enableThumb = false;
    strncpy((char*)exifAttributes.image_description, EXIF_DEF_IMAGE_DESCRIPTION, sizeof(exifAttributes.image_description));
    strncpy((char*)exifAttributes.maker, EXIF_DEF_MAKER, sizeof(exifAttributes.maker));
    strncpy((char*)exifAttributes.model, EXIF_DEF_MODEL, sizeof(exifAttributes.model));
    strncpy((char*)exifAttributes.software, EXIF_DEF_SOFTWARE, sizeof(exifAttributes.software));

    memcpy(exifAttributes.exif_version, EXIF_DEF_EXIF_VERSION, sizeof(exifAttributes.exif_version));
    memcpy(exifAttributes.flashpix_version, EXIF_DEF_FLASHPIXVERSION, sizeof(exifAttributes.flashpix_version));

    // initially, set default flash
    exifAttributes.flash = EXIF_DEF_FLASH;

    // normally it is sRGB, 1 means sRGB. FFFF.H means uncalibrated
    exifAttributes.color_space = EXIF_DEF_COLOR_SPACE;

    // the number of pixels per ResolutionUnit in the w or h direction
    // 72 means the image resolution is unknown
    exifAttributes.x_resolution.num = EXIF_DEF_RESOLUTION_NUM;
    exifAttributes.x_resolution.den = EXIF_DEF_RESOLUTION_DEN;
    exifAttributes.y_resolution.num = exifAttributes.x_resolution.num;
    exifAttributes.y_resolution.den = exifAttributes.x_resolution.den;
    // resolution unit, 2 means inch
    exifAttributes.resolution_unit = EXIF_DEF_RESOLUTION_UNIT;
    // when thumbnail uses JPEG compression, this tag 103H's value is set to 6
    exifAttributes.compression_scheme = EXIF_DEF_COMPRESSION;

    // the TIFF default is 1 (centered)
    exifAttributes.ycbcr_positioning = EXIF_DEF_YCBCR_POSITIONING;

    initialized = false;
}

void EXIFMaker::enableFlash()
{
    LOG1("@%s", __FUNCTION__);
    // bit 0: flash fired; bit 1 to 2: flash return; bit 3 to 4: flash mode;
    // bit 5: flash function; bit 6: red-eye mode;
    exifAttributes.flash = EXIF_FLASH_ON;
}

void EXIFMaker::setThumbnail(unsigned char *data, size_t size)
{
    LOG1("@%s: data = %p, size = %u", __FUNCTION__, data, size);
    exifAttributes.enableThumb = true;
    exifAttributes.widthThumb = thumbWidth;
    exifAttributes.heightThumb = thumbHeight;
    encoder.setThumbData(data, size);
}

size_t EXIFMaker::makeExif(unsigned char **data)
{
    LOG1("@%s", __FUNCTION__);
    if (*data == NULL) {
        LOGE("NULL pointer passed for EXIF. Cannot generate EXIF!");
        return 0;
    }
    if (encoder.makeExif(*data, &exifAttributes, &exifSize, false) == JPG_SUCCESS) {
        LOG1("Generated EXIF (@%p) of size: %u", *data, exifSize);
        return exifSize;
    }
    return 0;
}

}; // namespace android
