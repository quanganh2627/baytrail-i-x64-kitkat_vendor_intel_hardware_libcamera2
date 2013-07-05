/*
 * Copyright (c) 2013 Intel Corporation
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

#ifndef __LIBCAMERA2_ICAMERA_HW_CONTROLS_H__
#define __LIBCAMERA2_ICAMERA_HW_CONTROLS_H__

namespace android {

/* Abstraction of HW algorithms control interface for 3A support*/
class IHWIspControl
{
public:
    virtual ~IHWIspControl() { };

    // TODO: place for getStatistics(), setAicParameters()...
};

/* Abstraction of HW sensor control interface for 3A support */
class IHWSensorControl
{
public:
    virtual ~IHWSensorControl() { };

    virtual float getFrameRate() const = 0;

    virtual unsigned int getExposureDelay() = 0;

    virtual int setExposure(struct atomisp_exposure *) = 0;

    // TODO: place for all the IQ related sensor controls
};

/* Abstraction of HW flash control interface for 3A support */
class IHWFlashControl
{
public:
    virtual ~IHWFlashControl() { };
};

/* Abstraction of HW lens control interface for 3A support */
class IHWLensControl
{
public:
    virtual ~IHWLensControl() { };
};

/* Compound object for HW control interfaces for 3A */
class HWControlGroup
{
public:
    HWControlGroup():
        mIspCI(NULL),
        mSensorCI(NULL),
        mFlashCI(NULL),
        mLensCI(NULL) { };
    ~HWControlGroup() { };

    IHWIspControl       *mIspCI;
    IHWSensorControl    *mSensorCI;
    IHWFlashControl     *mFlashCI;
    IHWLensControl      *mLensCI;
};


} // namespace android

#endif
