/*
 * Copyright (c) 2014 Intel Corporation.
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

#ifndef ANDROID_LIBCAMERA_GPUWARPER_H
#define ANDROID_LIBCAMERA_GPUWARPER_H

#include <gui/Surface.h>
#include <GLES/gl.h>
#include <GLES2/gl2.h>
#include "AtomCommon.h"

namespace android {

// dimension of projective matrix
static const int PROJ_MTRX_DIM = 3;

// max dimension of shader info log string
static const int MAX_SH_INFO_LOG_SIZE = 8192;

class GPUWarper {

// constructor destructor
public:
    explicit GPUWarper(GLuint width, GLuint height, GLuint meshSize);
    ~GPUWarper();

// prevent copy constructor and assignment operator
private:
    GPUWarper(const GPUWarper& other);
    GPUWarper& operator=(const GPUWarper& other);

// public methods
public:
    status_t init();
    status_t warpBackFrame(AtomBuffer *frame, double projective[PROJ_MTRX_DIM][PROJ_MTRX_DIM]);

// private methods
private:
    status_t initGPU();

    status_t setupWarper();
    void findMeshParameters();
    status_t allocateHostArrays();
    status_t createTextureObjects();
    status_t createInputGraficBuffers();
    status_t createOutputGraficBuffer();

    status_t compileShader(const char **source, GLenum type, GLuint &shader);
    status_t createProgram(const char **fragmentShaderSource, GLuint &fragmentShader, GLuint &program);
    status_t initShaders();

    status_t processFrame();
    status_t fillInputGraphicBuffers(GLuint indexX, GLuint indexY, GLuint &startX, GLuint &startY);
    status_t readOutputGraphicBuffer(GLuint indexX, GLuint indexY);

    void meshTileBackward(GLuint indexX, GLuint indexY, GLuint startX, GLuint startY);
    void getProjTexture(GLfloat *in, GLfloat *out);

    void RGBATexToREDorRG(GLuint iTexID,  GLenum actTex, EGLImageKHR image, GLint fb, GLint w, GLint h, GLint glslProgram, GLint vertex_pos, GLint vertex_texCoord);
    void warping(GLuint iTexID, GLenum actTex, GLint fb, GLint w, GLint h, GLint glslProgram, GLint vertex_pos, GLint vertex_texCoord);
    void combYandUVTexsIntoNV12();

    void clearGPU();
    void clearWarper();
    void deleteHostArrays();

// private data
private:

    // input NV12 frame
    GLubyte *mInFrame;
    // output NV12 frame
    GLubyte *mOutFrame;

    // picture dimensions
    GLuint mWidth;
    GLuint mHeight;

    // picture strides
    GLuint mStride;

    // when one of the picture dimensions is larger than mMaxTextureSize,
    // picture is divided into tiles.
    // mMaxTextureSize is equal to GL maximum texture size (GL_MAX_TEXTURE_SIZE)
    // and is used as maximum tile size
    // it was noticed that texture allocation and sampling from textures
    // which one dimension is larger than 4096 (GL_MAX_TEXTURE_SIZE on SGX GPU)is very slow
    // in current implementation mMaxTextureSize = min(4096, GL_MAX_TEXTURE_SIZE)
    // (GL_MAX_TEXTURE_SIZE on RGX GPU is 8192)
    GLuint mMaxTextureSize;

    // each tile is divided into number of squares
    // that compose mesh grid
    GLuint mMeshSize;

    // number of grid points in each dimension
    int mNGridPointsX;
    int mNGridPointsY;

    // tile size
    GLuint mTileSizeX;
    GLuint mTileSizeY;

    // number of tiles in each dimension
    GLuint mNTilesX;
    GLuint mNTilesY;

    // when warping back, in order to obtain complete destination quad,
    // additional pixels should be used in source quad
    // source quad size
    GLuint mInBuffWidth;
    GLuint mInBuffHeight;

    // mTileExpansionCoeff = mInBuffWidth/mTileSizeX = mInBuffHeight/mTileSizeY
    double mTileExpansionCoeff;

    // mBorderX = (mInBuffWidth - mTileSizeX) / 2
    GLuint mBorderX;
    GLuint mBorderY;

    // Input and output tiles are stored in Android GraphicBuffer.
    // GraphicBuffer destructor is private, and eglCreateImageKHR
    // increments reference count to it. These buffers are destructed
    // by decrementing the reference count via eglImageDestroyKHR.
    GraphicBuffer* mGraphicBufferOut;
    GraphicBuffer* mGraphicBufferInY;
    GraphicBuffer* mGraphicBufferInUV;

    // output GraphicsBuffer stride
    GLuint mOutGrBuffStride;

    // input GraphicsBuffer strides
    GLuint mInGrBuffStride;

    // EGL initialization
    EGLDisplay mDisplay;
    EGLContext mContext;
    EGLSurface mSurface;

    // input tile Y component texture
    GLuint mInTextureY;
    // input tile UV component texture
    GLuint mInTextureUV;

    // input tile Y component after packing from RGBA to RED texture
    GLuint mMidTextureY;
    // input tile UV component after packing from RGBA to RG texture
    GLuint mMidTextureUV;

    // output tile Y component texture
    GLuint mOutTextureY;
    // input tile UV component texture
    GLuint mOutTextureUV;

    // output tile NV12 texture
    GLuint mOutTextureNV12;

    // input EGL Images of input tile Y and UV texturea
    EGLImageKHR mInEGLImageY;
    EGLImageKHR mInEGLImageUV;

    // output EGL Image of output tile NV12 texture
    EGLImageKHR mOutEGLImageNV12;

    //Frame Buffer Object identifiers
    GLuint mMidFbY;
    GLuint mMidFbUV;
    GLuint mOutFbY;
    GLuint mOutFbUV;
    GLuint mOutFbNV12;

    // GLSL vars
    GLuint mVertexShader;

    // Y St
    GLuint mGlslProgramStY;
    GLuint mFragmentShaderStY;
    GLint mVertexPosStY;
    GLint mVertexTexCoordStY;
    // UV St
    GLuint mGlslProgramStUV;
    GLuint mFragmentShaderStUV;
    GLint mVertexPosStUV;
    GLint mVertexTexCoordStUV;
    // Y
    GLuint mGlslProgramY;
    GLuint mFragmentShaderY;
    GLint mVertexPosY;
    GLint mVertexTexCoordY;
    // UV
    GLuint mGlslProgramUV;
    GLuint mFragmentShaderUV;
    GLint mVertexPosUV;
    GLint mVertexTexCoordUV;
    // NV12
    GLuint mGlslProgramNV12;
    GLuint mFragmentShaderNV12;
    GLint mVertexPosNV12;
    GLint mVertexTexCoordNV12;

    // vertex shader texture and vertex coordinates
    GLfloat *mGlVertices;

    // vertex indices for drawing triangles
    GLushort *mGlIndices;

    // Projective matrix
    double mProjective[PROJ_MTRX_DIM][PROJ_MTRX_DIM];

    // Warping can not be performed if Warper is not properly initialized
    bool mIsInitialized;

}; // class GPUWarper
}
// namespace android

#endif // ANDROID_LIBCAMERA_GPUWARPER_H
