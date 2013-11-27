/*
 * Copyright (C) 2013 The Android Open Source Project
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
#define LOG_TAG "Camera_SensorHW"

#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include "SensorHW.h"
#include "v4l2device.h"
#include "PerformanceTraces.h"

namespace android {

static sensorPrivateData gSensorDataCache[MAX_CAMERAS];

SensorHW::SensorHW(int cameraId):
    mCameraId(cameraId),
    mInitialModeDataValid(false)
{
    CLEAR(mCameraInput);
    CLEAR(mInitialModeData);
}

SensorHW::~SensorHW()
{
   mSensorSubdevice.clear();
   mIspSubdevice.clear();
   mSyncEventDevice.clear();
}

int SensorHW::getCurrentCameraId(void)
{
    return mCameraId;
}

size_t SensorHW::enumerateInputs(Vector<struct cameraInfo> &inputs)
{
    LOG1("@%s", __FUNCTION__);
    status_t ret;
    size_t numCameras = 0;
    struct v4l2_input input;
    struct cameraInfo sCamInfo;

    for (int i = 0; i < PlatformData::numberOfCameras(); i++) {
        memset(&input, 0, sizeof(input));
        memset(&sCamInfo, 0, sizeof(sCamInfo));
        input.index = i;
        ret = mDevice->enumerateInputs(&input);
        if (ret != NO_ERROR) {
            if (ret == INVALID_OPERATION || ret == BAD_INDEX)
                break;
            LOGE("Device input enumeration failed for sensor input %d", i);
        } else {
            sCamInfo.index = i;
            strncpy(sCamInfo.name, (const char *)input.name, sizeof(sCamInfo.name)-1);
            LOG1("Detected sensor \"%s\"", sCamInfo.name);
        }
        inputs.push(sCamInfo);
        numCameras++;
    }
    return numCameras;
}

void SensorHW::getPadFormat(sp<V4L2DeviceBase> &subdev, int padIndex, int &width, int &height)
{
    LOG1("@%s", __FUNCTION__);
    struct v4l2_subdev_format subdevFormat;
    int ret = 0;
    width = 0;
    height = 0;
    if (subdev == NULL)
        return;

    CLEAR(subdevFormat);
    subdevFormat.pad = padIndex;
    subdevFormat.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    ret = subdev->xioctl(VIDIOC_SUBDEV_G_FMT, &subdevFormat);
    if (ret < 0) {
        LOGE("Failed VIDIOC_SUBDEV_G_FMT");
    } else {
        width = subdevFormat.format.width;
        height = subdevFormat.format.height;
    }
}

status_t SensorHW::waitForFrameSync()
{
    Mutex::Autolock lock(mFrameSyncMutex);
    if (!mFrameSyncEnabled)
        return NO_INIT;

    return mFrameSyncCondition.wait(mFrameSyncMutex);
}

status_t SensorHW::selectActiveSensor(sp<V4L2VideoNode> &device)
{
    LOG1("@%s", __FUNCTION__);
    mDevice = device;
    status_t status = NO_ERROR;
    Vector<struct cameraInfo> camInfo;
    size_t numCameras = enumerateInputs(camInfo);

    mInitialModeDataValid = false;

    if (numCameras < (size_t) PlatformData::numberOfCameras()) {
        LOGE("Number of detected sensors not matching static Platform data!");
    }

    if (numCameras < 1) {
        LOGE("No detected sensors!");
        return UNKNOWN_ERROR;
    }

    // Static mapping of v4l2_input.index to android camera id
    if (numCameras == 1) {
        mCameraInput = camInfo[0];
    } else if (PlatformData::cameraFacing(mCameraId) == CAMERA_FACING_BACK) {
        mCameraInput = camInfo[0];
    } else if (PlatformData::cameraFacing(mCameraId) == CAMERA_FACING_FRONT) {
        mCameraInput = camInfo[1];
    }

    // Choose the camera sensor
    LOG1("Selecting camera sensor: %s", mCameraInput.name);
    status = mDevice->setInput(mCameraInput.index);
    if (status != NO_ERROR) {
        status = UNKNOWN_ERROR;
    } else {
        PERFORMANCE_TRACES_BREAKDOWN_STEP("capture_s_input");
        mSensorType = PlatformData::sensorType(mCameraId);

        // Query now the supported pixel formats
        Vector<v4l2_fmtdesc> formats;
        status = mDevice->queryCapturePixelFormats(formats);
        if (status != NO_ERROR) {
            LOGW("Cold not query capture formats from sensor: %s", mCameraInput.name);
            status = NO_ERROR;   // This is not critical
        }
        sensorStoreRawFormat(formats);
    }

    return status;
}

/**
 * Find and open V4L2 subdevices for direct access
 *
 * SensorHW class needs access to both sensor subdevice
 * and ATOMISP subdevice at the moment. In CSS2 there
 * are multiple ATOMISP subdevices (dual stream). To find
 * the correct one we travel through the pads and links
 * exposed by Media Controller API.
 *
 * Note: Current sensor selection above uses VIDIOC_ENUMINPUTS and
 *       VIDIOC_S_INPUT on  atomisp main device. The preferred method
 *       would be to have separate control over V4L2 subdevices, their
 *       pad formats and links using Media Controller API. Here in
 *       SensorHW class it would be natural to have direct controls and
 *       queries to sensor subdevice. This is not fully supported in our
 *       drivers so workarounds are done here to hide the facts from
 *       above layers.
 *
 * Workaround 1: use ISP subdev sink pad format temporarily to fetch
 *               reliable sensor output size.
 */
status_t SensorHW::openSubdevices()
{
    LOG1("@%s", __FUNCTION__);
    struct media_device_info mediaDeviceInfo;
    struct media_entity_desc mediaEntityDesc;
    struct media_entity_desc mediaEntityDescTmp;
    status_t status = NO_ERROR;
    int sinkPadIndex = -1;
    int ret = 0;

    sp<V4L2DeviceBase> mediaCtl = new V4L2DeviceBase("/dev/media0", 0);
    status = mediaCtl->open();
    if (status != NO_ERROR) {
        LOGE("Failed to open media device");
        return status;
    }

    CLEAR(mediaDeviceInfo);
    ret = mediaCtl->xioctl(MEDIA_IOC_DEVICE_INFO, &mediaDeviceInfo);
    if (ret < 0) {
        LOGE("Failed to get media device information");
        mediaCtl.clear();
        return UNKNOWN_ERROR;
    }

    LOG1("Media device : %s", mediaDeviceInfo.driver);

    status = findMediaEntityByName(mediaCtl, mCameraInput.name, mediaEntityDesc);
    if (status != NO_ERROR) {
        LOGE("Failed to find sensor subdevice");
        return status;
    }

    status = openSubdevice(mSensorSubdevice, mediaEntityDesc.v4l.major, mediaEntityDesc.v4l.minor);
    if (status != NO_ERROR) {
        LOGE("Failed to open sensor subdevice");
        return status;
    }

    while (status == NO_ERROR) {
        CLEAR(mediaEntityDescTmp);
        status = findConnectedEntity(mediaCtl, mediaEntityDesc, mediaEntityDescTmp, sinkPadIndex);
        if (status != NO_ERROR) {
            LOGE("Failed to find connections");
            break;
        }
        mediaEntityDesc = mediaEntityDescTmp;
        if (strncmp(mediaEntityDescTmp.name, "ATOM ISP SUBDEV", MAX_SENSOR_NAME_LENGTH) == 0) {
            LOG1("Connected ISP subdevice found");
            break;
        }
    }

    if (status != NO_ERROR) {
        LOGE("Unable to find connected ISP subdevice!");
        return status;
    }

    status = openSubdevice(mIspSubdevice, mediaEntityDescTmp.v4l.major, mediaEntityDescTmp.v4l.minor);
    if (status != NO_ERROR) {
        LOGE("Failed to open sensor subdevice");
        return status;
    }

    // Currently only ISP sink pad format gives reliable size information
    // so we store it in the beginning.
    getPadFormat(mIspSubdevice, sinkPadIndex, mOutputWidth, mOutputHeight);

    mediaCtl->close();
    mediaCtl.clear();

    return status;
}

/**
 * Find description for given entity index
 *
 * Using media controller temporarily here to query entity with given name.
 */
status_t SensorHW::findMediaEntityById(sp<V4L2DeviceBase> &mediaCtl, int index,
        struct media_entity_desc &mediaEntityDesc)
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    CLEAR(mediaEntityDesc);
    mediaEntityDesc.id = index;
    ret = mediaCtl->xioctl(MEDIA_IOC_ENUM_ENTITIES, &mediaEntityDesc);
    if (ret < 0) {
        LOG1("No more media entities");
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}


/**
 * Find description for given entity name
 *
 * Using media controller temporarily here to query entity with given name.
 */
status_t SensorHW::findMediaEntityByName(sp<V4L2DeviceBase> &mediaCtl, char const* entityName,
        struct media_entity_desc &mediaEntityDesc)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = UNKNOWN_ERROR;
    for (int i = 0; ; i++) {
        status = findMediaEntityById(mediaCtl, i | MEDIA_ENT_ID_FLAG_NEXT, mediaEntityDesc);
        if (status != NO_ERROR)
            break;
        LOG2("Media entity %d : %s", i, mediaEntityDesc.name);
        if (strncmp(mediaEntityDesc.name, entityName, MAX_SENSOR_NAME_LENGTH) == 0)
            break;
    }

    return status;
}

/**
 * Find entity description for first outbound connection
 */
status_t SensorHW::findConnectedEntity(sp<V4L2DeviceBase> &mediaCtl,
        const struct media_entity_desc &mediaEntityDescSrc,
        struct media_entity_desc &mediaEntityDescDst, int &padIndex)
{
    LOG1("@%s", __FUNCTION__);
    struct media_links_enum links;
    status_t status = UNKNOWN_ERROR;
    int connectedEntity = -1;
    int ret = 0;

    LOG2("%s : pads %d links %d", mediaEntityDescSrc.name, mediaEntityDescSrc.pads, mediaEntityDescSrc.links);

    links.entity = mediaEntityDescSrc.id;
    links.pads = (struct media_pad_desc*) malloc(mediaEntityDescSrc.pads * sizeof(struct media_pad_desc));
    links.links = (struct media_link_desc*) malloc(mediaEntityDescSrc.links * sizeof(struct media_link_desc));

    ret = mediaCtl->xioctl(MEDIA_IOC_ENUM_LINKS, &links);
    if (ret < 0) {
        LOGE("Failed to query any links");
    } else {
        for (int i = 0; i < mediaEntityDescSrc.links; i++) {
            if (links.links[i].sink.entity != mediaEntityDescSrc.id) {
                connectedEntity = links.links[0].sink.entity;
                padIndex = links.links[0].sink.index;
            }
        }
        if (connectedEntity >= 0)
            status = NO_ERROR;
    }

    free(links.pads);
    free(links.links);

    if (status != NO_ERROR)
        return status;

    status = findMediaEntityById(mediaCtl, connectedEntity, mediaEntityDescDst);
    if (status != NO_ERROR)
        return status;

    LOG2("Connected entity ==> %s, pad %d", mediaEntityDescDst.name, padIndex);
    return status;
}

/**
 * Open device node based on device identifier
 *
 * Helper method to find the device node name for V4L2 subdevices
 * from sysfs.
 */
status_t SensorHW::openSubdevice(sp<V4L2DeviceBase> &subdev, int major, int minor)
{
    LOG1("@%s :  major %d, minor %d", __FUNCTION__, major, minor);
    status_t status = UNKNOWN_ERROR;
    int ret = 0;
    char sysname[1024];
    char devname[1024];
    sprintf(devname, "/sys/dev/char/%u:%u", major, minor);
    ret = readlink(devname, sysname, sizeof(sysname));
    if (ret < 0) {
        LOGE("Unable to find subdevice node");
    } else {
        sysname[ret] = 0;
        char *lastSlash = strrchr(sysname, '/');
        if (lastSlash == NULL) {
            LOGE("Invalid sysfs subdev path");
            return status;
        }
        sprintf(devname, "/dev/%s", lastSlash + 1);
        LOG1("Subdevide node : %s", devname);
        subdev.clear();
        subdev = new V4L2DeviceBase(devname, 0);
        status = subdev->open();
        if (status != NO_ERROR) {
            LOGE("Failed to open subdevice");
            subdev.clear();
        }
    }
    return status;
}

/**
 * Prepare Sensor HW for start streaming
 *
 * This function is to be called once V4L2 pipeline is fully
 * configured. Here we do the final settings or query the initial
 * sensor parameters.
*
 * Note: Set or query means hiding the fact that sensor controls
 * in legacy V4L2 are passed through ISP driver and mostly basing
 * on its format configuration. Media Controller API is not used to
 * build the links, but drivers are exposing the subdevices with
 * certain controls provided. SensorHW class is in roadmap to
 * utilize direct v4l2 subdevice IO meanwhile maintaining
 * transparent controls to clients through IHWSensorControl.
 *
 * After this call certain IHWSensorControls
 * are unavailable (controls that are not supported while streaming)
 *
 * TODO: Make controls not available during streaming more explicit
 *       by protecting the IOCs with streaming state.
 */
status_t SensorHW::prepare()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int ret = 0;

    // Open subdevice for direct IOCTL.
    //
    status = openSubdevices();

    // Sensor is configured, readout the initial mode info
    ret = getModeInfo(&mInitialModeData);
    if (ret != 0)
        LOGW("Reading initial sensor mode info failed!");

    if (mInitialModeData.frame_length_lines != 0 &&
        mInitialModeData.binning_factor_y != 0 &&
        mInitialModeData.vt_pix_clk_freq_mhz != 0) {
        mInitialModeDataValid = true;

#ifdef LIBCAMERA_RD_FEATURES
        // Debug logging for timings from SensorModeData
        long long vbi_ll = mInitialModeData.frame_length_lines - (mInitialModeData.crop_vertical_end - mInitialModeData.crop_vertical_start + 1) / mInitialModeData.binning_factor_y;

        LOG2("SensorModeData timings: FL %lldus, VBI %lldus, FPS %f",
             ((long long) mInitialModeData.line_length_pck
              * mInitialModeData.frame_length_lines) * 1000000
              / mInitialModeData.vt_pix_clk_freq_mhz,
             ((long long) mInitialModeData.line_length_pck * vbi_ll) * 1000000
              / mInitialModeData.vt_pix_clk_freq_mhz,
             ((double) mInitialModeData.vt_pix_clk_freq_mhz)
              / (mInitialModeData.line_length_pck
              * mInitialModeData.frame_length_lines));
#endif
    }

    LOG1("Sensor output size %dx%d, FPS %f", mOutputWidth, mOutputHeight, getFramerate());

    return status;
}

/**
 * Start sensor HW (virtual concept)
 *
 * Atomisp driver is responsible of starting the actual sensor streaming IO
 * after its pipeline is configured and it has received VIDIOC_STREAMON for
 * video nodes it exposes.
 *
 * In virtual concept the SensorHW shall be started once the pipeline
 * configuration is ready and before the actual VIDIOC_STREAMON in order to
 * not to loose track of the initial frames. This context is also the first
 * place to query or set the initial sensor parameters.
 */
status_t SensorHW::start()
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;
    Mutex::Autolock lock(mFrameSyncMutex);
    // Subscribe to frame sync event in case of RAW sensor
    if (mIspSubdevice != NULL && mSensorType == SENSOR_TYPE_RAW) {
        ret = mIspSubdevice->subscribeEvent(V4L2_EVENT_FRAME_SYNC);
        if (ret < 0) {
            LOGE("Failed to subscribe to frame sync event!");
            return UNKNOWN_ERROR;
        }
        mFrameSyncEnabled = true;
    }
    return NO_ERROR;
}

/**
 * Stop sensor HW (virtual concept)
 *
 * Atomisp driver is responsible of stopping the actual sensor streaming IO.
 *
 * In virtual concept the SensorHW shall be stopped once sensor controls or
 * frame synchronization provided by the object are no longer needed.
 */
status_t SensorHW::stop()
{
    LOG1("@%s", __FUNCTION__);
    Mutex::Autolock lock(mFrameSyncMutex);
    if (mIspSubdevice != NULL) {
        mIspSubdevice->unsubscribeEvent(V4L2_EVENT_FRAME_SYNC);
        mFrameSyncEnabled = false;
    }
    return NO_ERROR;
}

/**
 * Helper method for the sensor to select the prefered BAYER format
 * the supported pixel formats are retrieved when the sensor is selected.
 *
 * This helper method finds the first Bayer format and saves it to mRawBayerFormat
 * so that if raw dump feature is enabled we know what is the sensor
 * preferred format.
 *
 * TODO: sanity check, who needs a preferred format
 */
status_t SensorHW::sensorStoreRawFormat(Vector<v4l2_fmtdesc> &formats)
{
    LOG1("@%s", __FUNCTION__);
    Vector<v4l2_fmtdesc>::iterator it = formats.begin();

    for (;it != formats.end(); ++it) {
        /* store it only if is one of the Bayer formats */
        if (isBayerFormat(it->pixelformat)) {
            mRawBayerFormat = it->pixelformat;
            break;  //we take the first one, sensors tend to support only one
        }
    }
    return NO_ERROR;
}

void SensorHW::getMotorData(sensorPrivateData *sensor_data)
{
    LOG2("@%s", __FUNCTION__);
    int rc;
    struct v4l2_private_int_data motorPrivateData;

    motorPrivateData.size = 0;
    motorPrivateData.data = NULL;
    motorPrivateData.reserved[0] = 0;
    motorPrivateData.reserved[1] = 0;

    sensor_data->data = NULL;
    sensor_data->size = 0;
    // First call with size = 0 will return motor private data size.
    rc = mDevice->xioctl(ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA, &motorPrivateData);
    LOG2("%s IOCTL ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA to get motor private data size ret: %d\n", __FUNCTION__, rc);
    if (rc != 0 || motorPrivateData.size == 0) {
        LOGD("Failed to get motor private data size. Error: %d", rc);
        return;
    }

    motorPrivateData.data = malloc(motorPrivateData.size);
    if (motorPrivateData.data == NULL) {
        LOGD("Failed to allocate memory for motor private data.");
        return;
    }

    // Second call with correct size will return motor private data.
    rc = mDevice->xioctl(ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA, &motorPrivateData);
    LOG2("%s IOCTL ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA to get motor private data ret: %d\n", __FUNCTION__, rc);

    if (rc != 0 || motorPrivateData.size == 0) {
        LOGD("Failed to read motor private data. Error: %d", rc);
        free(motorPrivateData.data);
        return;
    }

    sensor_data->data = motorPrivateData.data;
    sensor_data->size = motorPrivateData.size;
    sensor_data->fetched = true;
}

void SensorHW::getSensorData(sensorPrivateData *sensor_data)
{
    LOG2("@%s", __FUNCTION__);
    int rc;
    struct v4l2_private_int_data otpdata;
    int cameraId = getCurrentCameraId();

    sensor_data->data = NULL;
    sensor_data->size = 0;

    if ((gControlLevel & CAMERA_DISABLE_FRONT_NVM) || (gControlLevel & CAMERA_DISABLE_BACK_NVM)) {
        LOG1("NVM data reading disabled");
        sensor_data->fetched = false;
    }
    else {
        otpdata.size = 0;
        otpdata.data = NULL;
        otpdata.reserved[0] = 0;
        otpdata.reserved[1] = 0;

        if (gSensorDataCache[cameraId].fetched) {
            sensor_data->data = gSensorDataCache[cameraId].data;
            sensor_data->size = gSensorDataCache[cameraId].size;
            return;
        }
        // First call with size = 0 will return OTP data size.
        rc = mDevice->xioctl(ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA, &otpdata);
        LOG2("%s IOCTL ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA to get OTP data size ret: %d\n", __FUNCTION__, rc);
        if (rc != 0 || otpdata.size == 0) {
            LOGD("Failed to get OTP size. Error: %d", rc);
            return;
        }

        otpdata.data = calloc(otpdata.size, 1);
        if (otpdata.data == NULL) {
            LOGD("Failed to allocate memory for OTP data.");
            return;
        }

        // Second call with correct size will return OTP data.
        rc = mDevice->xioctl(ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA, &otpdata);
        LOG2("%s IOCTL ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA to get OTP data ret: %d\n", __FUNCTION__, rc);

        if (rc != 0 || otpdata.size == 0) {
            LOGD("Failed to read OTP data. Error: %d", rc);
            free(otpdata.data);
            return;
        }

        sensor_data->data = otpdata.data;
        sensor_data->size = otpdata.size;
        sensor_data->fetched = true;
    }
    gSensorDataCache[cameraId] = *sensor_data;
}

int SensorHW::setExposureMode(v4l2_exposure_auto_type v4l2Mode)
{
    LOG2("@%s: %d", __FUNCTION__, v4l2Mode);
    return mDevice->setControl(V4L2_CID_EXPOSURE_AUTO, v4l2Mode, "AE mode");
}

int SensorHW::getExposureMode(v4l2_exposure_auto_type * type)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_EXPOSURE_AUTO, (int*)type);
}

int SensorHW::setExposureBias(int bias)
{
    LOG2("@%s: bias: %d", __FUNCTION__, bias);
    return mDevice->setControl(V4L2_CID_EXPOSURE, bias, "exposure");
}

int SensorHW::getExposureBias(int * bias)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_EXPOSURE, bias);
}

int SensorHW::setSceneMode(v4l2_scene_mode mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_SCENE_MODE, mode, "scene mode");
}

int SensorHW::getSceneMode(v4l2_scene_mode * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_SCENE_MODE, (int*)mode);
}

int SensorHW::setWhiteBalance(v4l2_auto_n_preset_white_balance mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE, mode, "white balance");
}

int SensorHW::getWhiteBalance(v4l2_auto_n_preset_white_balance * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE, (int*)mode);
}

int SensorHW::setIso(int iso)
{
    LOG2("@%s: ISO: %d", __FUNCTION__, iso);
    return mDevice->setControl(V4L2_CID_ISO_SENSITIVITY, iso, "iso");
}

int SensorHW::getIso(int * iso)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_ISO_SENSITIVITY, iso);
}

int SensorHW::setAeMeteringMode(v4l2_exposure_metering mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_EXPOSURE_METERING, mode, "AE metering mode");
}

int SensorHW::getAeMeteringMode(v4l2_exposure_metering * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_EXPOSURE_METERING, (int*)mode);
}

int SensorHW::setAeFlickerMode(v4l2_power_line_frequency mode)
{
    LOG2("@%s: %d", __FUNCTION__, (int) mode);
    return mDevice->setControl(V4L2_CID_POWER_LINE_FREQUENCY,
                                    mode, "light frequency");
}

int SensorHW::setAfMode(v4l2_auto_focus_range mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_AUTO_FOCUS_RANGE , mode, "AF mode");
}

int SensorHW::getAfMode(v4l2_auto_focus_range * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_AUTO_FOCUS_RANGE, (int*)mode);
}

int SensorHW::setAfEnabled(bool enable)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->setControl(V4L2_CID_FOCUS_AUTO, enable, "Auto Focus");
}

int SensorHW::set3ALock(int aaaLock)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->setControl(V4L2_CID_3A_LOCK, aaaLock, "AE Lock");
}

int SensorHW::get3ALock(int * aaaLock)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_3A_LOCK, aaaLock);
}


int SensorHW::setAeFlashMode(v4l2_flash_led_mode mode)
{
    LOG2("@%s: %d", __FUNCTION__, mode);
    return mDevice->setControl(V4L2_CID_FLASH_LED_MODE, mode, "Flash mode");
}

int SensorHW::getAeFlashMode(v4l2_flash_led_mode * mode)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_FLASH_LED_MODE, (int*)mode);
}

int SensorHW::getModeInfo(struct atomisp_sensor_mode_data *mode_data)
{
    LOG2("@%s", __FUNCTION__);
    int ret;
    ret = mDevice->xioctl(ATOMISP_IOC_G_SENSOR_MODE_DATA, mode_data);
    LOG2("%s IOCTL ATOMISP_IOC_G_SENSOR_MODE_DATA ret: %d\n", __FUNCTION__, ret);
    return ret;
}

int SensorHW::setExposure(struct atomisp_exposure *exposure)
{
    int ret;
    ret = mDevice->xioctl(ATOMISP_IOC_S_EXPOSURE, exposure);
    LOG2("%s IOCTL ATOMISP_IOC_S_EXPOSURE ret: %d, gain A:%d D:%d, itg C:%d F:%d\n", __FUNCTION__, ret, exposure->gain[0], exposure->gain[1], exposure->integration_time[0], exposure->integration_time[1]);
    return ret;
}

int SensorHW::setExposureTime(int time)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->setControl(V4L2_CID_EXPOSURE_ABSOLUTE, time, "Exposure time");
}

int SensorHW::getExposureTime(int *time)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_EXPOSURE_ABSOLUTE, time);
}

int SensorHW::getAperture(int *aperture)
{
    LOG2("@%s", __FUNCTION__);
    return mDevice->getControl(V4L2_CID_IRIS_ABSOLUTE, aperture);
}

int SensorHW::getFNumber(unsigned short *fnum_num, unsigned short *fnum_denom)
{
    LOG2("@%s", __FUNCTION__);
    int fnum = 0, ret;

    ret = mDevice->getControl(V4L2_CID_FNUMBER_ABSOLUTE, &fnum);

    *fnum_num = (unsigned short)(fnum >> 16);
    *fnum_denom = (unsigned short)(fnum & 0xFFFF);
    return ret;
}

/**
 * returns the V4L2 Bayer format preferred by the sensor
 */
int SensorHW::getRawFormat()
{
    return mRawBayerFormat;
}

const char * SensorHW::getSensorName(void)
{
    return mCameraInput.name;
}

/**
 * Set sensor framerate
 *
 * This function shall be called only before starting the stream and
 * also before querying sensor mode data.
 *
 * TODO: Make controls not available during streaming more explicit
 *       by protecting the IOCs with streaming state.
 */
status_t SensorHW::setFramerate(int fps)
{
    int ret = 0;
    LOG1("@%s: fps %d", __FUNCTION__, fps);

    if (mSensorSubdevice == NULL)
        return NO_INIT;

    struct v4l2_subdev_frame_interval subdevFrameInterval;
    CLEAR(subdevFrameInterval);
    subdevFrameInterval.pad = 0;
    subdevFrameInterval.interval.numerator = 1;
    subdevFrameInterval.interval.denominator = fps;
    ret = mSensorSubdevice->xioctl(VIDIOC_SUBDEV_S_FRAME_INTERVAL, &subdevFrameInterval);
    if (ret < 0){
        LOGE("Failed to set framerate to sensor subdevice");
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

/**
 * Returns maximum sensor framerate for active configuration
 */
float SensorHW::getFramerate() const
{
    LOG1("@%s", __FUNCTION__);
    int ret = 0;

    // Try initial mode data first
    if (mInitialModeDataValid) {
        LOG1("Using framerate from mode data");
        return ((float) mInitialModeData.vt_pix_clk_freq_mhz) /
               ( mInitialModeData.line_length_pck * mInitialModeData.frame_length_lines);
    }

    // Then subdev G_FRAME_INTERVAL
    if (mSensorSubdevice != NULL) {
        struct v4l2_subdev_frame_interval subdevFrameInterval;
        CLEAR(subdevFrameInterval);
        subdevFrameInterval.pad = 0;
        ret = mSensorSubdevice->xioctl(VIDIOC_SUBDEV_G_FRAME_INTERVAL, &subdevFrameInterval);
        if (ret >= 0 && subdevFrameInterval.interval.numerator != 0) {
            LOG1("Using framerate from sensor subdevice");
            return ((float) subdevFrameInterval.interval.denominator) / subdevFrameInterval.interval.numerator;
        }
    }

    // Finally fall into videonode given framerate
    float fps = 0.0;
    ret = mDevice->getFramerate(&fps, mOutputWidth, mOutputHeight, mRawBayerFormat);
    if (ret < 0) {
        LOGW("Failed to query the framerate");
        return 30.0;
    }
    LOG1("Using framerate provided by main video node");
    return fps;
}

/**
 * polls and dequeues frame synchronization events into IAtomIspObserver::Message
 */
status_t SensorHW::observe(IAtomIspObserver::Message *msg)
{
    LOG2("@%s", __FUNCTION__);
    struct v4l2_event event;
    int ret;

    ret = mIspSubdevice->poll(FRAME_SYNC_POLL_TIMEOUT);
    if (ret <= 0) {
        LOGE("FrameSync poll failed (%s), waiting recovery..", (ret == 0) ? "timeout" : "error");
        ret = -1;
    } else {
        mFrameSyncCondition.signal();
        // poll was successful, dequeue the event right away
        do {
            ret = mIspSubdevice->dequeueEvent(&event);
            if (ret < 0) {
                LOGE("Dequeue FrameSync event failed");
            }
        } while (event.pending > 0);
    }

    if (ret < 0) {
        msg->id = IAtomIspObserver::MESSAGE_ID_ERROR;
        // We sleep a moment but keep passing error messages to observers
        // until further client controls.
        usleep(ATOMISP_EVENT_RECOVERY_WAIT);
        return NO_ERROR;
    }

    // fill observer message
    msg->id = IAtomIspObserver::MESSAGE_ID_EVENT;
    msg->data.event.type = IAtomIspObserver::EVENT_TYPE_SOF;
    msg->data.event.timestamp.tv_sec  = event.timestamp.tv_sec;
    msg->data.event.timestamp.tv_usec = event.timestamp.tv_nsec / 1000;
    msg->data.event.sequence = event.sequence;

    return NO_ERROR;
}


}
