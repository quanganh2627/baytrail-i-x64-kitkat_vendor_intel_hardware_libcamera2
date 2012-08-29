/*
 * Copyright (C) 2011 The Android Open Source Project
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
#define LOG_TAG "Camera_JpegCompressor"

#include "JpegCompressor.h"
#include "LogHelper.h"
#include <string.h>
#include "SWJpegEncoder.h"

namespace android {


JpegCompressor::JpegCompressor() :
    mSWEncoder(NULL)
{
    LOG1("@%s", __FUNCTION__);
    mSWEncoder = new SWJpegEncoder();

}

JpegCompressor::~JpegCompressor()
{
    LOG1("@%s", __FUNCTION__);

    if(mSWEncoder != NULL)
        delete mSWEncoder;
}

int JpegCompressor::swEncode(const InputBuffer &in, const OutputBuffer &out)
{
    LOG1("@%s, use the libjpeg to do sw jpeg encoding", __FUNCTION__);
    int status = 0;

    if (NULL == mSWEncoder) {
        LOGE("@%s, line:%d, mSWEncoder is NULL", __FUNCTION__, __LINE__);
        mJpegSize = -1;
        return -1;
    }

    mSWEncoder->init();
    mSWEncoder->setJpegQuality(out.quality);
    status = mSWEncoder->configEncoding(in.width, in.height, (JSAMPLE *)out.buf, out.size);
    if (status)
        goto exit;

    status = mSWEncoder->doJpegEncoding(in.buf);
    if (status)
        goto exit;

exit:
    mSWEncoder->deInit();

    if (status)
        mJpegSize = -1;
    else
        mSWEncoder->getJpegSize(&mJpegSize);

    return (status ? -1 : 0);
}

// Takes YUV data (NV12 or YUV420) and outputs JPEG encoded stream
int JpegCompressor::encode(const InputBuffer &in, const OutputBuffer &out)
{
    LOG1("@%s:\n\t IN  = {buf:%p, w:%u, h:%u, sz:%u, f:%s}" \
             "\n\t OUT = {buf:%p, w:%u, h:%u, sz:%u, q:%d}",
            __FUNCTION__,
            in.buf, in.width, in.height, in.size, v4l2Fmt2Str(in.format),
            out.buf, out.width, out.height, out.size, out.quality);

    if (in.width == 0 || in.height == 0 || in.format == 0) {
        LOGE("Invalid input received!");
        mJpegSize = -1;
        goto exit;
    }
    if (swEncode(in, out) < 0)
        goto exit;

    return mJpegSize;
exit:
    return (mJpegSize = -1);
}


} // namespace android
