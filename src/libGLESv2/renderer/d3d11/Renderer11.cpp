#include "precompiled.h"
//
// Copyright (c) 2012-2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Renderer11.cpp: Implements a back-end specific class for the D3D11 renderer.

#include "libGLESv2/main.h"
#include "common/utilities.h"
#include "libGLESv2/Buffer.h"
#include "libGLESv2/ProgramBinary.h"
#include "libGLESv2/Framebuffer.h"
#include "libGLESv2/RenderBuffer.h"
#include "libGLESv2/renderer/d3d11/Renderer11.h"
#include "libGLESv2/renderer/d3d11/RenderTarget11.h"
#include "libGLESv2/renderer/d3d11/renderer11_utils.h"
#include "libGLESv2/renderer/d3d11/formatutils11.h"
#include "libGLESv2/renderer/d3d11/ShaderExecutable11.h"
#include "libGLESv2/renderer/d3d11/SwapChain11.h"
#include "libGLESv2/renderer/d3d11/Image11.h"
#include "libGLESv2/renderer/d3d11/VertexBuffer11.h"
#include "libGLESv2/renderer/d3d11/IndexBuffer11.h"
#include "libGLESv2/renderer/d3d11/BufferStorage11.h"
#include "libGLESv2/renderer/VertexDataManager.h"
#include "libGLESv2/renderer/IndexDataManager.h"
#include "libGLESv2/renderer/d3d11/TextureStorage11.h"
#include "libGLESv2/renderer/d3d11/Query11.h"
#include "libGLESv2/renderer/d3d11/Fence11.h"
#include "libGLESv2/renderer/d3d11/Blit11.h"
#include "libGLESv2/renderer/d3d11/Clear11.h"
#include "libGLESv2/renderer/d3d11/PixelTransfer11.h"
#include "libEGL/Display.h"

#ifdef _DEBUG
// this flag enables suppressing some spurious warnings that pop up in certain WebGL samples
// and conformance tests. to enable all warnings, remove this define.
#define ANGLE_SUPPRESS_D3D11_HAZARD_WARNINGS 1
#endif

namespace rx
{
static const DXGI_FORMAT RenderTargetFormats[] =
    {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM
    };

static const DXGI_FORMAT DepthStencilFormats[] =
    {
        DXGI_FORMAT_UNKNOWN,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        DXGI_FORMAT_D16_UNORM
    };

enum
{
    MAX_TEXTURE_IMAGE_UNITS_VTF_SM4 = 16
};

Renderer11::Renderer11(egl::Display *display, HDC hDc) : Renderer(display), mDc(hDc)
{
    mVertexDataManager = NULL;
    mIndexDataManager = NULL;

    mLineLoopIB = NULL;
    mTriangleFanIB = NULL;

    mBlit = NULL;
    mPixelTransfer = NULL;

    mClear = NULL;

    mSyncQuery = NULL;

    mD3d11Module = NULL;
    mDxgiModule = NULL;

    mDeviceLost = false;

    mMaxSupportedSamples = 0;

    mDevice = NULL;
    mDeviceContext = NULL;
    mDxgiAdapter = NULL;
    mDxgiFactory = NULL;

    mDriverConstantBufferVS = NULL;
    mDriverConstantBufferPS = NULL;

    mBGRATextureSupport = false;

    mIsGeometryShaderActive = false;
}

Renderer11::~Renderer11()
{
    release();
}

Renderer11 *Renderer11::makeRenderer11(Renderer *renderer)
{
    ASSERT(HAS_DYNAMIC_TYPE(rx::Renderer11*, renderer));
    return static_cast<rx::Renderer11*>(renderer);
}

#ifndef __d3d11_1_h__
#define D3D11_MESSAGE_ID_DEVICE_DRAW_RENDERTARGETVIEW_NOT_SET ((D3D11_MESSAGE_ID)3146081)
#endif

EGLint Renderer11::initialize()
{
    if (!initializeCompiler())
    {
        return EGL_NOT_INITIALIZED;
    }

    mDxgiModule = LoadLibrary(TEXT("dxgi.dll"));
    mD3d11Module = LoadLibrary(TEXT("d3d11.dll"));

    if (mD3d11Module == NULL || mDxgiModule == NULL)
    {
        ERR("Could not load D3D11 or DXGI library - aborting!\n");
        return EGL_NOT_INITIALIZED;
    }

    // create the D3D11 device
    ASSERT(mDevice == NULL);
    PFN_D3D11_CREATE_DEVICE D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(mD3d11Module, "D3D11CreateDevice");

    if (D3D11CreateDevice == NULL)
    {
        ERR("Could not retrieve D3D11CreateDevice address - aborting!\n");
        return EGL_NOT_INITIALIZED;
    }

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT result = S_OK;

#ifdef _DEBUG
    result = D3D11CreateDevice(NULL,
                               D3D_DRIVER_TYPE_HARDWARE,
                               NULL,
                               D3D11_CREATE_DEVICE_DEBUG,
                               featureLevels,
                               ArraySize(featureLevels),
                               D3D11_SDK_VERSION,
                               &mDevice,
                               &mFeatureLevel,
                               &mDeviceContext);

    if (!mDevice || FAILED(result))
    {
        ERR("Failed creating Debug D3D11 device - falling back to release runtime.\n");
    }

    if (!mDevice || FAILED(result))
#endif
    {
        result = D3D11CreateDevice(NULL,
                                   D3D_DRIVER_TYPE_HARDWARE,
                                   NULL,
                                   0,
                                   featureLevels,
                                   ArraySize(featureLevels),
                                   D3D11_SDK_VERSION,
                                   &mDevice,
                                   &mFeatureLevel,
                                   &mDeviceContext);

        if (!mDevice || FAILED(result))
        {
            ERR("Could not create D3D11 device - aborting!\n");
            return EGL_NOT_INITIALIZED;   // Cleanup done by destructor through glDestroyRenderer
        }
    }

    IDXGIDevice *dxgiDevice = NULL;
    result = mDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

    if (FAILED(result))
    {
        ERR("Could not query DXGI device - aborting!\n");
        return EGL_NOT_INITIALIZED;
    }

    result = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&mDxgiAdapter);

    if (FAILED(result))
    {
        ERR("Could not retrieve DXGI adapter - aborting!\n");
        return EGL_NOT_INITIALIZED;
    }

    SafeRelease(dxgiDevice);

    mDxgiAdapter->GetDesc(&mAdapterDescription);
    memset(mDescription, 0, sizeof(mDescription));
    wcstombs(mDescription, mAdapterDescription.Description, sizeof(mDescription) - 1);

    result = mDxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&mDxgiFactory);

    if (!mDxgiFactory || FAILED(result))
    {
        ERR("Could not create DXGI factory - aborting!\n");
        return EGL_NOT_INITIALIZED;
    }

    // Disable some spurious D3D11 debug warnings to prevent them from flooding the output log
#if defined(ANGLE_SUPPRESS_D3D11_HAZARD_WARNINGS) && defined(_DEBUG)
    ID3D11InfoQueue *infoQueue;
    result = mDevice->QueryInterface(__uuidof(ID3D11InfoQueue),  (void **)&infoQueue);

    if (SUCCEEDED(result))
    {
        D3D11_MESSAGE_ID hideMessages[] =
        {
            D3D11_MESSAGE_ID_DEVICE_DRAW_RENDERTARGETVIEW_NOT_SET
        };

        D3D11_INFO_QUEUE_FILTER filter = {0};
        filter.DenyList.NumIDs = ArraySize(hideMessages);
        filter.DenyList.pIDList = hideMessages;

        infoQueue->AddStorageFilterEntries(&filter);
        SafeRelease(infoQueue);
    }
#endif

    mMaxSupportedSamples = 0;

    const d3d11::DXGIFormatSet &dxgiFormats = d3d11::GetAllUsedDXGIFormats();
    for (d3d11::DXGIFormatSet::const_iterator i = dxgiFormats.begin(); i != dxgiFormats.end(); ++i)
    {
        MultisampleSupportInfo support = getMultisampleSupportInfo(*i);
        mMultisampleSupportMap.insert(std::make_pair(*i, support));
        mMaxSupportedSamples = std::max(mMaxSupportedSamples, support.maxSupportedSamples);
    }

    initializeDevice();

    // BGRA texture support is optional in feature levels 10 and 10_1
    UINT formatSupport;
    result = mDevice->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &formatSupport);
    if (FAILED(result))
    {
        ERR("Error checking BGRA format support: 0x%08X", result);
    }
    else
    {
        const int flags = (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET);
        mBGRATextureSupport = (formatSupport & flags) == flags;
    }

    // Check floating point texture support
    static const unsigned int requiredTextureFlags = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_TEXTURECUBE;
    static const unsigned int requiredRenderableFlags = D3D11_FORMAT_SUPPORT_RENDER_TARGET;
    static const unsigned int requiredFilterFlags = D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;

    DXGI_FORMAT float16Formats[] =
    {
        DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
    };

    DXGI_FORMAT float32Formats[] =
    {
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32G32_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
    };

    mFloat16TextureSupport = true;
    mFloat16FilterSupport = true;
    mFloat16RenderSupport = true;
    for (unsigned int i = 0; i < ArraySize(float16Formats); i++)
    {
        if (SUCCEEDED(mDevice->CheckFormatSupport(float16Formats[i], &formatSupport)))
        {
            mFloat16TextureSupport = mFloat16TextureSupport && (formatSupport & requiredTextureFlags) == requiredTextureFlags;
            mFloat16FilterSupport = mFloat16FilterSupport && (formatSupport & requiredFilterFlags) == requiredFilterFlags;
            mFloat16RenderSupport = mFloat16RenderSupport && (formatSupport & requiredRenderableFlags) == requiredRenderableFlags;
        }
        else
        {
            mFloat16TextureSupport = false;
            mFloat16RenderSupport = false;
            mFloat16FilterSupport = false;
        }
    }

    mFloat32TextureSupport = true;
    mFloat32FilterSupport = true;
    mFloat32RenderSupport = true;
    for (unsigned int i = 0; i < ArraySize(float32Formats); i++)
    {
        if (SUCCEEDED(mDevice->CheckFormatSupport(float32Formats[i], &formatSupport)))
        {
            mFloat32TextureSupport = mFloat32TextureSupport && (formatSupport & requiredTextureFlags) == requiredTextureFlags;
            mFloat32FilterSupport = mFloat32FilterSupport && (formatSupport & requiredFilterFlags) == requiredFilterFlags;
            mFloat32RenderSupport = mFloat32RenderSupport && (formatSupport & requiredRenderableFlags) == requiredRenderableFlags;
        }
        else
        {
            mFloat32TextureSupport = false;
            mFloat32FilterSupport = false;
            mFloat32RenderSupport = false;
        }
    }

    DXGI_FORMAT rgTextureFormats[] =
    {
        DXGI_FORMAT_R8_UNORM,
        DXGI_FORMAT_R8G8_UNORM,
        DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32G32_FLOAT,
    };

    mRGTextureSupport = true;
    for (unsigned int i = 0; i < ArraySize(rgTextureFormats); i++)
    {
        if (SUCCEEDED(mDevice->CheckFormatSupport(rgTextureFormats[i], &formatSupport)))
        {
            mRGTextureSupport = mRGTextureSupport && (formatSupport & requiredTextureFlags) == requiredTextureFlags;
            mRGTextureSupport = mRGTextureSupport && (formatSupport & requiredFilterFlags) == requiredFilterFlags;
            mRGTextureSupport = mRGTextureSupport && (formatSupport & requiredRenderableFlags) == requiredRenderableFlags;
        }
        else
        {
            mRGTextureSupport = false;
        }
    }

    // Check compressed texture support
    const unsigned int requiredCompressedTextureFlags = D3D11_FORMAT_SUPPORT_TEXTURE2D;

    if (SUCCEEDED(mDevice->CheckFormatSupport(DXGI_FORMAT_BC1_UNORM, &formatSupport)))
    {
        mDXT1TextureSupport = (formatSupport & requiredCompressedTextureFlags) == requiredCompressedTextureFlags;
    }
    else
    {
        mDXT1TextureSupport = false;
    }

    if (SUCCEEDED(mDevice->CheckFormatSupport(DXGI_FORMAT_BC3_UNORM, &formatSupport)))
    {
        mDXT3TextureSupport = (formatSupport & requiredCompressedTextureFlags) == requiredCompressedTextureFlags;
    }
    else
    {
        mDXT3TextureSupport = false;
    }

    if (SUCCEEDED(mDevice->CheckFormatSupport(DXGI_FORMAT_BC5_UNORM, &formatSupport)))
    {
        mDXT5TextureSupport = (formatSupport & requiredCompressedTextureFlags) == requiredCompressedTextureFlags;
    }
    else
    {
        mDXT5TextureSupport = false;
    }

    // Check depth texture support
    DXGI_FORMAT depthTextureFormats[] =
    {
        DXGI_FORMAT_D16_UNORM,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
    };

    static const unsigned int requiredDepthTextureFlags = D3D11_FORMAT_SUPPORT_DEPTH_STENCIL |
                                                          D3D11_FORMAT_SUPPORT_TEXTURE2D;

    mDepthTextureSupport = true;
    for (unsigned int i = 0; i < ArraySize(depthTextureFormats); i++)
    {
        if (SUCCEEDED(mDevice->CheckFormatSupport(depthTextureFormats[i], &formatSupport)))
        {
            mDepthTextureSupport = mDepthTextureSupport && ((formatSupport & requiredDepthTextureFlags) == requiredDepthTextureFlags);
        }
        else
        {
            mDepthTextureSupport = false;
        }
    }

    return EGL_SUCCESS;
}

// do any one-time device initialization
// NOTE: this is also needed after a device lost/reset
// to reset the scene status and ensure the default states are reset.
void Renderer11::initializeDevice()
{
    mStateCache.initialize(mDevice);
    mInputLayoutCache.initialize(mDevice, mDeviceContext);

    ASSERT(!mVertexDataManager && !mIndexDataManager);
    mVertexDataManager = new VertexDataManager(this);
    mIndexDataManager = new IndexDataManager(this);

    ASSERT(!mBlit);
    mBlit = new Blit11(this);

    ASSERT(!mClear);
    mClear = new Clear11(this);

    ASSERT(!mPixelTransfer);
    mPixelTransfer = new PixelTransfer11(this);

    markAllStateDirty();
}

int Renderer11::generateConfigs(ConfigDesc **configDescList)
{
    unsigned int numRenderFormats = ArraySize(RenderTargetFormats);
    unsigned int numDepthFormats = ArraySize(DepthStencilFormats);
    (*configDescList) = new ConfigDesc[numRenderFormats * numDepthFormats];
    int numConfigs = 0;
    
    for (unsigned int formatIndex = 0; formatIndex < numRenderFormats; formatIndex++)
    {
        for (unsigned int depthStencilIndex = 0; depthStencilIndex < numDepthFormats; depthStencilIndex++)
        {
            DXGI_FORMAT renderTargetFormat = RenderTargetFormats[formatIndex];

            UINT formatSupport = 0;
            HRESULT result = mDevice->CheckFormatSupport(renderTargetFormat, &formatSupport);

            if (SUCCEEDED(result) && (formatSupport & D3D11_FORMAT_SUPPORT_RENDER_TARGET))
            {
                DXGI_FORMAT depthStencilFormat = DepthStencilFormats[depthStencilIndex];

                bool depthStencilFormatOK = true;

                if (depthStencilFormat != DXGI_FORMAT_UNKNOWN)
                {
                    UINT formatSupport = 0;
                    result = mDevice->CheckFormatSupport(depthStencilFormat, &formatSupport);
                    depthStencilFormatOK = SUCCEEDED(result) && (formatSupport & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL);
                }

                if (depthStencilFormatOK)
                {
                    // FIXME: parse types from context version
                    ASSERT(d3d11_gl::GetInternalFormat(renderTargetFormat, 2) == d3d11_gl::GetInternalFormat(renderTargetFormat, 3));
                    ASSERT(d3d11_gl::GetInternalFormat(depthStencilFormat, 2) == d3d11_gl::GetInternalFormat(depthStencilFormat, 3));

                    ConfigDesc newConfig;
                    newConfig.renderTargetFormat = d3d11_gl::GetInternalFormat(renderTargetFormat, getCurrentClientVersion());
                    newConfig.depthStencilFormat = d3d11_gl::GetInternalFormat(depthStencilFormat, getCurrentClientVersion());
                    newConfig.multiSample = 0;     // FIXME: enumerate multi-sampling
                    newConfig.fastConfig = true;   // Assume all DX11 format conversions to be fast
                    newConfig.es3Capable = true;

                    (*configDescList)[numConfigs++] = newConfig;
                }
            }
        }
    }

    return numConfigs;
}

void Renderer11::deleteConfigs(ConfigDesc *configDescList)
{
    delete [] (configDescList);
}

void Renderer11::sync(bool block)
{
    if (block)
    {
        HRESULT result;

        if (!mSyncQuery)
        {
            D3D11_QUERY_DESC queryDesc;
            queryDesc.Query = D3D11_QUERY_EVENT;
            queryDesc.MiscFlags = 0;

            result = mDevice->CreateQuery(&queryDesc, &mSyncQuery);
            ASSERT(SUCCEEDED(result));
        }

        mDeviceContext->End(mSyncQuery);
        mDeviceContext->Flush();

        do
        {
            result = mDeviceContext->GetData(mSyncQuery, NULL, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);

            // Keep polling, but allow other threads to do something useful first
            Sleep(0);

            if (testDeviceLost(true))
            {
                return;
            }
        }
        while (result == S_FALSE);
    }
    else
    {
        mDeviceContext->Flush();
    }
}

SwapChain *Renderer11::createSwapChain(HWND window, HANDLE shareHandle, GLenum backBufferFormat, GLenum depthBufferFormat)
{
    return new rx::SwapChain11(this, window, shareHandle, backBufferFormat, depthBufferFormat);
}

void Renderer11::generateSwizzle(gl::Texture *texture)
{
    if (texture)
    {
        TextureStorageInterface *texStorage = texture->getNativeTexture();
        if (texStorage)
        {
            TextureStorage11 *storage11 = TextureStorage11::makeTextureStorage11(texStorage->getStorageInstance());

            storage11->generateSwizzles(texture->getSwizzleRed(), texture->getSwizzleGreen(), texture->getSwizzleBlue(),
                                        texture->getSwizzleAlpha());
        }
    }
}

void Renderer11::setSamplerState(gl::SamplerType type, int index, const gl::SamplerState &samplerState)
{
    if (type == gl::SAMPLER_PIXEL)
    {
        if (index < 0 || index >= gl::MAX_TEXTURE_IMAGE_UNITS)
        {
            ERR("Pixel shader sampler index %i is not valid.", index);
            return;
        }

        if (mForceSetPixelSamplerStates[index] || memcmp(&samplerState, &mCurPixelSamplerStates[index], sizeof(gl::SamplerState)) != 0)
        {
            ID3D11SamplerState *dxSamplerState = mStateCache.getSamplerState(samplerState);

            if (!dxSamplerState)
            {
                ERR("NULL sampler state returned by RenderStateCache::getSamplerState, setting the default"
                    "sampler state for pixel shaders at slot %i.", index);
            }

            mDeviceContext->PSSetSamplers(index, 1, &dxSamplerState);

            mCurPixelSamplerStates[index] = samplerState;
        }

        mForceSetPixelSamplerStates[index] = false;
    }
    else if (type == gl::SAMPLER_VERTEX)
    {
        if (index < 0 || index >= (int)getMaxVertexTextureImageUnits())
        {
            ERR("Vertex shader sampler index %i is not valid.", index);
            return;
        }

        if (mForceSetVertexSamplerStates[index] || memcmp(&samplerState, &mCurVertexSamplerStates[index], sizeof(gl::SamplerState)) != 0)
        {
            ID3D11SamplerState *dxSamplerState = mStateCache.getSamplerState(samplerState);

            if (!dxSamplerState)
            {
                ERR("NULL sampler state returned by RenderStateCache::getSamplerState, setting the default"
                    "sampler state for vertex shaders at slot %i.", index);
            }

            mDeviceContext->VSSetSamplers(index, 1, &dxSamplerState);

            mCurVertexSamplerStates[index] = samplerState;
        }

        mForceSetVertexSamplerStates[index] = false;
    }
    else UNREACHABLE();
}

void Renderer11::setTexture(gl::SamplerType type, int index, gl::Texture *texture)
{
    ID3D11ShaderResourceView *textureSRV = NULL;
    bool forceSetTexture = false;

    if (texture)
    {
        TextureStorageInterface *texStorage = texture->getNativeTexture();
        if (texStorage)
        {
            TextureStorage11 *storage11 = TextureStorage11::makeTextureStorage11(texStorage->getStorageInstance());
            textureSRV = storage11->getSRV(texture->getSwizzleRed(), texture->getSwizzleGreen(), texture->getSwizzleBlue(),
                                           texture->getSwizzleAlpha());
        }

        // If we get NULL back from getSRV here, something went wrong in the texture class and we're unexpectedly
        // missing the shader resource view
        ASSERT(textureSRV != NULL);

        forceSetTexture = texture->hasDirtyImages();
    }

    if (type == gl::SAMPLER_PIXEL)
    {
        if (index < 0 || index >= gl::MAX_TEXTURE_IMAGE_UNITS)
        {
            ERR("Pixel shader sampler index %i is not valid.", index);
            return;
        }

        if (forceSetTexture || mCurPixelSRVs[index] != textureSRV)
        {
            mDeviceContext->PSSetShaderResources(index, 1, &textureSRV);
        }

        mCurPixelSRVs[index] = textureSRV;
    }
    else if (type == gl::SAMPLER_VERTEX)
    {
        if (index < 0 || index >= (int)getMaxVertexTextureImageUnits())
        {
            ERR("Vertex shader sampler index %i is not valid.", index);
            return;
        }

        if (forceSetTexture || mCurVertexSRVs[index] != textureSRV)
        {
            mDeviceContext->VSSetShaderResources(index, 1, &textureSRV);
        }

        mCurVertexSRVs[index] = textureSRV;
    }
    else UNREACHABLE();
}

bool Renderer11::setUniformBuffers(const gl::Buffer *vertexUniformBuffers[], const gl::Buffer *fragmentUniformBuffers[])
{
    // convert buffers to ID3D11Buffer*
    ID3D11Buffer *vertexConstantBuffers[gl::IMPLEMENTATION_MAX_VERTEX_SHADER_UNIFORM_BUFFERS] = { NULL };
    ID3D11Buffer *pixelConstantBuffers[gl::IMPLEMENTATION_MAX_FRAGMENT_SHADER_UNIFORM_BUFFERS] = { NULL };

    for (unsigned int uniformBufferIndex = 0; uniformBufferIndex < gl::IMPLEMENTATION_MAX_VERTEX_SHADER_UNIFORM_BUFFERS; uniformBufferIndex++)
    {
        const gl::Buffer *uniformBuffer = vertexUniformBuffers[uniformBufferIndex];
        if (uniformBuffer)
        {
            BufferStorage11 *bufferStorage = BufferStorage11::makeBufferStorage11(uniformBuffer->getStorage());
            ID3D11Buffer *constantBuffer = bufferStorage->getBuffer(BUFFER_USAGE_UNIFORM);

            if (!constantBuffer)
            {
                return false;
            }

            if (mCurrentConstantBufferVS[uniformBufferIndex] != bufferStorage->getSerial())
            {
                mDeviceContext->VSSetConstantBuffers(getReservedVertexUniformBuffers() + uniformBufferIndex,
                                                     1, &constantBuffer);
                mCurrentConstantBufferVS[uniformBufferIndex] = bufferStorage->getSerial();
            }
        }
    }

    for (unsigned int uniformBufferIndex = 0; uniformBufferIndex < gl::IMPLEMENTATION_MAX_FRAGMENT_SHADER_UNIFORM_BUFFERS; uniformBufferIndex++)
    {
        const gl::Buffer *uniformBuffer = fragmentUniformBuffers[uniformBufferIndex];
        if (uniformBuffer)
        {
            BufferStorage11 *bufferStorage = BufferStorage11::makeBufferStorage11(uniformBuffer->getStorage());
            ID3D11Buffer *constantBuffer = bufferStorage->getBuffer(BUFFER_USAGE_UNIFORM);

            if (!constantBuffer)
            {
                return false;
            }

            if (mCurrentConstantBufferPS[uniformBufferIndex] != bufferStorage->getSerial())
            {
                mDeviceContext->PSSetConstantBuffers(getReservedFragmentUniformBuffers() + uniformBufferIndex,
                                                     1, &constantBuffer);
                mCurrentConstantBufferPS[uniformBufferIndex] = bufferStorage->getSerial();
            }

            pixelConstantBuffers[uniformBufferIndex] = constantBuffer;
        }
    }

    return true;
}

void Renderer11::setRasterizerState(const gl::RasterizerState &rasterState)
{
    if (mForceSetRasterState || memcmp(&rasterState, &mCurRasterState, sizeof(gl::RasterizerState)) != 0)
    {
        ID3D11RasterizerState *dxRasterState = mStateCache.getRasterizerState(rasterState, mScissorEnabled,
                                                                              mCurDepthSize);
        if (!dxRasterState)
        {
            ERR("NULL rasterizer state returned by RenderStateCache::getRasterizerState, setting the default"
                "rasterizer state.");
        }

        mDeviceContext->RSSetState(dxRasterState);

        mCurRasterState = rasterState;
    }

    mForceSetRasterState = false;
}

void Renderer11::setBlendState(gl::Framebuffer *framebuffer, const gl::BlendState &blendState, const gl::ColorF &blendColor,
                               unsigned int sampleMask)
{
    if (mForceSetBlendState ||
        memcmp(&blendState, &mCurBlendState, sizeof(gl::BlendState)) != 0 ||
        memcmp(&blendColor, &mCurBlendColor, sizeof(gl::ColorF)) != 0 ||
        sampleMask != mCurSampleMask)
    {
        ID3D11BlendState *dxBlendState = mStateCache.getBlendState(framebuffer, blendState);
        if (!dxBlendState)
        {
            ERR("NULL blend state returned by RenderStateCache::getBlendState, setting the default "
                "blend state.");
        }

        float blendColors[4] = {0.0f};
        if (blendState.sourceBlendRGB != GL_CONSTANT_ALPHA && blendState.sourceBlendRGB != GL_ONE_MINUS_CONSTANT_ALPHA &&
            blendState.destBlendRGB != GL_CONSTANT_ALPHA && blendState.destBlendRGB != GL_ONE_MINUS_CONSTANT_ALPHA)
        {
            blendColors[0] = blendColor.red;
            blendColors[1] = blendColor.green;
            blendColors[2] = blendColor.blue;
            blendColors[3] = blendColor.alpha;
        }
        else
        {
            blendColors[0] = blendColor.alpha;
            blendColors[1] = blendColor.alpha;
            blendColors[2] = blendColor.alpha;
            blendColors[3] = blendColor.alpha;
        }

        mDeviceContext->OMSetBlendState(dxBlendState, blendColors, sampleMask);

        mCurBlendState = blendState;
        mCurBlendColor = blendColor;
        mCurSampleMask = sampleMask;
    }

    mForceSetBlendState = false;
}

void Renderer11::setDepthStencilState(const gl::DepthStencilState &depthStencilState, int stencilRef,
                                      int stencilBackRef, bool frontFaceCCW)
{
    if (mForceSetDepthStencilState ||
        memcmp(&depthStencilState, &mCurDepthStencilState, sizeof(gl::DepthStencilState)) != 0 ||
        stencilRef != mCurStencilRef || stencilBackRef != mCurStencilBackRef)
    {
        if (depthStencilState.stencilWritemask != depthStencilState.stencilBackWritemask ||
            stencilRef != stencilBackRef ||
            depthStencilState.stencilMask != depthStencilState.stencilBackMask)
        {
            ERR("Separate front/back stencil writemasks, reference values, or stencil mask values are "
                "invalid under WebGL.");
            return gl::error(GL_INVALID_OPERATION);
        }

        ID3D11DepthStencilState *dxDepthStencilState = mStateCache.getDepthStencilState(depthStencilState);
        if (!dxDepthStencilState)
        {
            ERR("NULL depth stencil state returned by RenderStateCache::getDepthStencilState, "
                "setting the default depth stencil state.");
        }

        // Max D3D11 stencil reference value is 0xFF, corresponding to the max 8 bits in a stencil buffer
        // GL specifies we should clamp the ref value to the nearest bit depth when doing stencil ops
        META_ASSERT(D3D11_DEFAULT_STENCIL_READ_MASK == 0xFF);
        META_ASSERT(D3D11_DEFAULT_STENCIL_WRITE_MASK == 0xFF);
        UINT stencilRef = std::min(stencilRef, 0xFFu);

        mDeviceContext->OMSetDepthStencilState(dxDepthStencilState, stencilRef);

        mCurDepthStencilState = depthStencilState;
        mCurStencilRef = stencilRef;
        mCurStencilBackRef = stencilBackRef;
    }

    mForceSetDepthStencilState = false;
}

void Renderer11::setScissorRectangle(const gl::Rectangle &scissor, bool enabled)
{
    if (mForceSetScissor || memcmp(&scissor, &mCurScissor, sizeof(gl::Rectangle)) != 0 ||
        enabled != mScissorEnabled)
    {
        if (enabled)
        {
            D3D11_RECT rect;
            rect.left = std::max(0, scissor.x);
            rect.top = std::max(0, scissor.y);
            rect.right = scissor.x + std::max(0, scissor.width);
            rect.bottom = scissor.y + std::max(0, scissor.height);

            mDeviceContext->RSSetScissorRects(1, &rect);
        }

        if (enabled != mScissorEnabled)
        {
            mForceSetRasterState = true;
        }

        mCurScissor = scissor;
        mScissorEnabled = enabled;
    }

    mForceSetScissor = false;
}

bool Renderer11::setViewport(const gl::Rectangle &viewport, float zNear, float zFar, GLenum drawMode, GLenum frontFace, 
                             bool ignoreViewport)
{
    gl::Rectangle actualViewport = viewport;
    float actualZNear = gl::clamp01(zNear);
    float actualZFar = gl::clamp01(zFar);
    if (ignoreViewport)
    {
        actualViewport.x = 0;
        actualViewport.y = 0;
        actualViewport.width = mRenderTargetDesc.width;
        actualViewport.height = mRenderTargetDesc.height;
        actualZNear = 0.0f;
        actualZFar = 1.0f;
    }

    // Get D3D viewport bounds, which depends on the feature level
    const Range& viewportBounds = getViewportBounds();

    // Clamp width and height first to the gl maximum, then clamp further if we extend past the D3D maximum bounds
    D3D11_VIEWPORT dxViewport;
    dxViewport.TopLeftX = gl::clamp(actualViewport.x, viewportBounds.start, viewportBounds.end);
    dxViewport.TopLeftY = gl::clamp(actualViewport.y, viewportBounds.start, viewportBounds.end);
    dxViewport.Width = gl::clamp(actualViewport.width, 0, getMaxViewportDimension());
    dxViewport.Height = gl::clamp(actualViewport.height, 0, getMaxViewportDimension());
    dxViewport.Width = std::min((int)dxViewport.Width, viewportBounds.end - static_cast<int>(dxViewport.TopLeftX));
    dxViewport.Height = std::min((int)dxViewport.Height, viewportBounds.end - static_cast<int>(dxViewport.TopLeftY));
    dxViewport.MinDepth = actualZNear;
    dxViewport.MaxDepth = actualZFar;

    if (dxViewport.Width <= 0 || dxViewport.Height <= 0)
    {
        return false;   // Nothing to render
    }

    bool viewportChanged = mForceSetViewport || memcmp(&actualViewport, &mCurViewport, sizeof(gl::Rectangle)) != 0 ||
                           actualZNear != mCurNear || actualZFar != mCurFar;

    if (viewportChanged)
    {
        mDeviceContext->RSSetViewports(1, &dxViewport);

        mCurViewport = actualViewport;
        mCurNear = actualZNear;
        mCurFar = actualZFar;

        mPixelConstants.viewCoords[0] = actualViewport.width  * 0.5f;
        mPixelConstants.viewCoords[1] = actualViewport.height * 0.5f;
        mPixelConstants.viewCoords[2] = actualViewport.x + (actualViewport.width  * 0.5f);
        mPixelConstants.viewCoords[3] = actualViewport.y + (actualViewport.height * 0.5f);

        mPixelConstants.depthFront[0] = (actualZFar - actualZNear) * 0.5f;
        mPixelConstants.depthFront[1] = (actualZNear + actualZFar) * 0.5f;

        mVertexConstants.depthRange[0] = actualZNear;
        mVertexConstants.depthRange[1] = actualZFar;
        mVertexConstants.depthRange[2] = actualZFar - actualZNear;

        mPixelConstants.depthRange[0] = actualZNear;
        mPixelConstants.depthRange[1] = actualZFar;
        mPixelConstants.depthRange[2] = actualZFar - actualZNear;
    }

    mForceSetViewport = false;
    return true;
}

bool Renderer11::applyPrimitiveType(GLenum mode, GLsizei count)
{
    D3D11_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    GLsizei minCount = 0;

    switch (mode)
    {
      case GL_POINTS:         primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;   minCount = 1; break;
      case GL_LINES:          primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;      minCount = 2; break;
      case GL_LINE_LOOP:      primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;     minCount = 2; break;
      case GL_LINE_STRIP:     primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;     minCount = 2; break;
      case GL_TRIANGLES:      primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;  minCount = 3; break;
      case GL_TRIANGLE_STRIP: primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; minCount = 3; break;
          // emulate fans via rewriting index buffer
      case GL_TRIANGLE_FAN:   primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;  minCount = 3; break;
      default:
        return gl::error(GL_INVALID_ENUM, false);
    }

    if (primitiveTopology != mCurrentPrimitiveTopology)
    {
        mDeviceContext->IASetPrimitiveTopology(primitiveTopology);
        mCurrentPrimitiveTopology = primitiveTopology;
    }

    return count >= minCount;
}

bool Renderer11::applyRenderTarget(gl::Framebuffer *framebuffer)
{
    // Get the color render buffer and serial
    // Also extract the render target dimensions and view
    unsigned int renderTargetWidth = 0;
    unsigned int renderTargetHeight = 0;
    GLenum renderTargetFormat = 0;
    unsigned int renderTargetSerials[gl::IMPLEMENTATION_MAX_DRAW_BUFFERS] = {0};
    ID3D11RenderTargetView* framebufferRTVs[gl::IMPLEMENTATION_MAX_DRAW_BUFFERS] = {NULL};
    bool missingColorRenderTarget = true;

    for (unsigned int colorAttachment = 0; colorAttachment < gl::IMPLEMENTATION_MAX_DRAW_BUFFERS; colorAttachment++)
    {
        const GLenum drawBufferState = framebuffer->getDrawBufferState(colorAttachment);

        if (framebuffer->getColorbufferType(colorAttachment) != GL_NONE && drawBufferState != GL_NONE)
        {
            // the draw buffer must be either "none", "back" for the default buffer or the same index as this color (in order)
            ASSERT(drawBufferState == GL_BACK || drawBufferState == (GL_COLOR_ATTACHMENT0_EXT + colorAttachment));

            gl::Renderbuffer *colorbuffer = framebuffer->getColorbuffer(colorAttachment);

            if (!colorbuffer)
            {
                ERR("render target pointer unexpectedly null.");
                return false;
            }

            // check for zero-sized default framebuffer, which is a special case.
            // in this case we do not wish to modify any state and just silently return false.
            // this will not report any gl error but will cause the calling method to return.
            if (colorbuffer->getWidth() == 0 || colorbuffer->getHeight() == 0)
            {
                return false;
            }

            renderTargetSerials[colorAttachment] = colorbuffer->getSerial();

            // Extract the render target dimensions and view
            RenderTarget11 *renderTarget = RenderTarget11::makeRenderTarget11(colorbuffer->getRenderTarget());
            if (!renderTarget)
            {
                ERR("render target pointer unexpectedly null.");
                return false;
            }

            framebufferRTVs[colorAttachment] = renderTarget->getRenderTargetView();
            if (!framebufferRTVs[colorAttachment])
            {
                ERR("render target view pointer unexpectedly null.");
                return false;
            }

            if (missingColorRenderTarget)
            {
                renderTargetWidth = colorbuffer->getWidth();
                renderTargetHeight = colorbuffer->getHeight();
                renderTargetFormat = colorbuffer->getActualFormat();
                missingColorRenderTarget = false;
            }

            // TODO: Detect if this color buffer is already bound as a texture and unbind it first to prevent
            //       D3D11 warnings.
        }
    }

    // Get the depth stencil render buffer and serials
    gl::Renderbuffer *depthStencil = NULL;
    unsigned int depthbufferSerial = 0;
    unsigned int stencilbufferSerial = 0;
    if (framebuffer->getDepthbufferType() != GL_NONE)
    {
        depthStencil = framebuffer->getDepthbuffer();
        if (!depthStencil)
        {
            ERR("Depth stencil pointer unexpectedly null.");
            SafeRelease(framebufferRTVs);
            return false;
        }

        depthbufferSerial = depthStencil->getSerial();
    }
    else if (framebuffer->getStencilbufferType() != GL_NONE)
    {
        depthStencil = framebuffer->getStencilbuffer();
        if (!depthStencil)
        {
            ERR("Depth stencil pointer unexpectedly null.");
            SafeRelease(framebufferRTVs);
            return false;
        }

        stencilbufferSerial = depthStencil->getSerial();
    }

    // Extract the depth stencil sizes and view
    unsigned int depthSize = 0;
    unsigned int stencilSize = 0;
    ID3D11DepthStencilView* framebufferDSV = NULL;
    if (depthStencil)
    {
        RenderTarget11 *depthStencilRenderTarget = RenderTarget11::makeRenderTarget11(depthStencil->getDepthStencil());
        if (!depthStencilRenderTarget)
        {
            ERR("render target pointer unexpectedly null.");
            SafeRelease(framebufferRTVs);
            return false;
        }

        framebufferDSV = depthStencilRenderTarget->getDepthStencilView();
        if (!framebufferDSV)
        {
            ERR("depth stencil view pointer unexpectedly null.");
            SafeRelease(framebufferRTVs);
            return false;
        }

        // If there is no render buffer, the width, height and format values come from
        // the depth stencil
        if (missingColorRenderTarget)
        {
            renderTargetWidth = depthStencil->getWidth();
            renderTargetHeight = depthStencil->getHeight();
            renderTargetFormat = depthStencil->getActualFormat();
        }

        depthSize = depthStencil->getDepthSize();
        stencilSize = depthStencil->getStencilSize();
    }

    // Apply the render target and depth stencil
    if (!mRenderTargetDescInitialized || !mDepthStencilInitialized ||
        memcmp(renderTargetSerials, mAppliedRenderTargetSerials, sizeof(renderTargetSerials)) != 0 ||
        depthbufferSerial != mAppliedDepthbufferSerial ||
        stencilbufferSerial != mAppliedStencilbufferSerial)
    {
        mDeviceContext->OMSetRenderTargets(getMaxRenderTargets(), framebufferRTVs, framebufferDSV);

        mRenderTargetDesc.width = renderTargetWidth;
        mRenderTargetDesc.height = renderTargetHeight;
        mRenderTargetDesc.format = renderTargetFormat;
        mForceSetViewport = true;
        mForceSetScissor = true;
        mForceSetBlendState = true;

        if (!mDepthStencilInitialized || depthSize != mCurDepthSize)
        {
            mCurDepthSize = depthSize;
            mForceSetRasterState = true;
        }

        mCurStencilSize = stencilSize;

        for (unsigned int rtIndex = 0; rtIndex < gl::IMPLEMENTATION_MAX_DRAW_BUFFERS; rtIndex++)
        {
            mAppliedRenderTargetSerials[rtIndex] = renderTargetSerials[rtIndex];
        }
        mAppliedDepthbufferSerial = depthbufferSerial;
        mAppliedStencilbufferSerial = stencilbufferSerial;
        mRenderTargetDescInitialized = true;
        mDepthStencilInitialized = true;
    }

    invalidateFramebufferSwizzles(framebuffer);

    return true;
}

GLenum Renderer11::applyVertexBuffer(gl::ProgramBinary *programBinary, const gl::VertexAttribute vertexAttributes[], gl::VertexAttribCurrentValueData currentValues[],
                                     GLint first, GLsizei count, GLsizei instances)
{
    TranslatedAttribute attributes[gl::MAX_VERTEX_ATTRIBS];
    GLenum err = mVertexDataManager->prepareVertexData(vertexAttributes, currentValues, programBinary, first, count, attributes, instances);
    if (err != GL_NO_ERROR)
    {
        return err;
    }

    return mInputLayoutCache.applyVertexBuffers(attributes, programBinary);
}

GLenum Renderer11::applyIndexBuffer(const GLvoid *indices, gl::Buffer *elementArrayBuffer, GLsizei count, GLenum mode, GLenum type, TranslatedIndexData *indexInfo)
{
    GLenum err = mIndexDataManager->prepareIndexData(type, count, elementArrayBuffer, indices, indexInfo);

    if (err == GL_NO_ERROR)
    {
        if (indexInfo->storage)
        {
            if (indexInfo->serial != mAppliedStorageIBSerial || indexInfo->startOffset != mAppliedIBOffset)
            {
                BufferStorage11 *storage = BufferStorage11::makeBufferStorage11(indexInfo->storage);
                IndexBuffer11* indexBuffer = IndexBuffer11::makeIndexBuffer11(indexInfo->indexBuffer);

                mDeviceContext->IASetIndexBuffer(storage->getBuffer(BUFFER_USAGE_INDEX), indexBuffer->getIndexFormat(), indexInfo->startOffset);

                mAppliedIBSerial = 0;
                mAppliedStorageIBSerial = storage->getSerial();
                mAppliedIBOffset = indexInfo->startOffset;
            }
        }
        else if (indexInfo->serial != mAppliedIBSerial || indexInfo->startOffset != mAppliedIBOffset)
        {
            IndexBuffer11* indexBuffer = IndexBuffer11::makeIndexBuffer11(indexInfo->indexBuffer);

            mDeviceContext->IASetIndexBuffer(indexBuffer->getBuffer(), indexBuffer->getIndexFormat(), indexInfo->startOffset);

            mAppliedIBSerial = indexInfo->serial;
            mAppliedStorageIBSerial = 0;
            mAppliedIBOffset = indexInfo->startOffset;
        }
    }

    return err;
}

void Renderer11::drawArrays(GLenum mode, GLsizei count, GLsizei instances)
{
    if (mode == GL_LINE_LOOP)
    {
        drawLineLoop(count, GL_NONE, NULL, 0, NULL);
    }
    else if (mode == GL_TRIANGLE_FAN)
    {
        drawTriangleFan(count, GL_NONE, NULL, 0, NULL, instances);
    }
    else if (instances > 0)
    {
        mDeviceContext->DrawInstanced(count, instances, 0, 0);
    }
    else
    {
        mDeviceContext->Draw(count, 0);
    }
}

void Renderer11::drawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, gl::Buffer *elementArrayBuffer, const TranslatedIndexData &indexInfo, GLsizei instances)
{
    if (mode == GL_LINE_LOOP)
    {
        drawLineLoop(count, type, indices, indexInfo.minIndex, elementArrayBuffer);
    }
    else if (mode == GL_TRIANGLE_FAN)
    {
        drawTriangleFan(count, type, indices, indexInfo.minIndex, elementArrayBuffer, instances);
    }
    else if (instances > 0)
    {
        mDeviceContext->DrawIndexedInstanced(count, instances, 0, -static_cast<int>(indexInfo.minIndex), 0);
    }
    else
    {
        mDeviceContext->DrawIndexed(count, 0, -static_cast<int>(indexInfo.minIndex));
    }
}

void Renderer11::drawLineLoop(GLsizei count, GLenum type, const GLvoid *indices, int minIndex, gl::Buffer *elementArrayBuffer)
{
    // Get the raw indices for an indexed draw
    if (type != GL_NONE && elementArrayBuffer)
    {
        gl::Buffer *indexBuffer = elementArrayBuffer;
        BufferStorage *storage = indexBuffer->getStorage();
        intptr_t offset = reinterpret_cast<intptr_t>(indices);
        indices = static_cast<const GLubyte*>(storage->getData()) + offset;
    }

    if (!mLineLoopIB)
    {
        mLineLoopIB = new StreamingIndexBufferInterface(this);
        if (!mLineLoopIB->reserveBufferSpace(INITIAL_INDEX_BUFFER_SIZE, GL_UNSIGNED_INT))
        {
            delete mLineLoopIB;
            mLineLoopIB = NULL;

            ERR("Could not create a 32-bit looping index buffer for GL_LINE_LOOP.");
            return gl::error(GL_OUT_OF_MEMORY);
        }
    }

    // Checked by Renderer11::applyPrimitiveType
    ASSERT(count >= 0);

    if (static_cast<unsigned int>(count) + 1 > (std::numeric_limits<unsigned int>::max() / sizeof(unsigned int)))
    {
        ERR("Could not create a 32-bit looping index buffer for GL_LINE_LOOP, too many indices required.");
        return gl::error(GL_OUT_OF_MEMORY);
    }

    const unsigned int spaceNeeded = (static_cast<unsigned int>(count) + 1) * sizeof(unsigned int);
    if (!mLineLoopIB->reserveBufferSpace(spaceNeeded, GL_UNSIGNED_INT))
    {
        ERR("Could not reserve enough space in looping index buffer for GL_LINE_LOOP.");
        return gl::error(GL_OUT_OF_MEMORY);
    }

    void* mappedMemory = NULL;
    unsigned int offset;
    if (!mLineLoopIB->mapBuffer(spaceNeeded, &mappedMemory, &offset))
    {
        ERR("Could not map index buffer for GL_LINE_LOOP.");
        return gl::error(GL_OUT_OF_MEMORY);
    }

    unsigned int *data = reinterpret_cast<unsigned int*>(mappedMemory);
    unsigned int indexBufferOffset = offset;

    switch (type)
    {
      case GL_NONE:   // Non-indexed draw
        for (int i = 0; i < count; i++)
        {
            data[i] = i;
        }
        data[count] = 0;
        break;
      case GL_UNSIGNED_BYTE:
        for (int i = 0; i < count; i++)
        {
            data[i] = static_cast<const GLubyte*>(indices)[i];
        }
        data[count] = static_cast<const GLubyte*>(indices)[0];
        break;
      case GL_UNSIGNED_SHORT:
        for (int i = 0; i < count; i++)
        {
            data[i] = static_cast<const GLushort*>(indices)[i];
        }
        data[count] = static_cast<const GLushort*>(indices)[0];
        break;
      case GL_UNSIGNED_INT:
        for (int i = 0; i < count; i++)
        {
            data[i] = static_cast<const GLuint*>(indices)[i];
        }
        data[count] = static_cast<const GLuint*>(indices)[0];
        break;
      default: UNREACHABLE();
    }

    if (!mLineLoopIB->unmapBuffer())
    {
        ERR("Could not unmap index buffer for GL_LINE_LOOP.");
        return gl::error(GL_OUT_OF_MEMORY);
    }

    if (mAppliedIBSerial != mLineLoopIB->getSerial() || mAppliedIBOffset != indexBufferOffset)
    {
        IndexBuffer11 *indexBuffer = IndexBuffer11::makeIndexBuffer11(mLineLoopIB->getIndexBuffer());

        mDeviceContext->IASetIndexBuffer(indexBuffer->getBuffer(), indexBuffer->getIndexFormat(), indexBufferOffset);
        mAppliedIBSerial = mLineLoopIB->getSerial();
        mAppliedStorageIBSerial = 0;
        mAppliedIBOffset = indexBufferOffset;
    }

    mDeviceContext->DrawIndexed(count + 1, 0, -minIndex);
}

void Renderer11::drawTriangleFan(GLsizei count, GLenum type, const GLvoid *indices, int minIndex, gl::Buffer *elementArrayBuffer, int instances)
{
    // Get the raw indices for an indexed draw
    if (type != GL_NONE && elementArrayBuffer)
    {
        gl::Buffer *indexBuffer = elementArrayBuffer;
        BufferStorage *storage = indexBuffer->getStorage();
        intptr_t offset = reinterpret_cast<intptr_t>(indices);
        indices = static_cast<const GLubyte*>(storage->getData()) + offset;
    }

    if (!mTriangleFanIB)
    {
        mTriangleFanIB = new StreamingIndexBufferInterface(this);
        if (!mTriangleFanIB->reserveBufferSpace(INITIAL_INDEX_BUFFER_SIZE, GL_UNSIGNED_INT))
        {
            delete mTriangleFanIB;
            mTriangleFanIB = NULL;

            ERR("Could not create a scratch index buffer for GL_TRIANGLE_FAN.");
            return gl::error(GL_OUT_OF_MEMORY);
        }
    }

    // Checked by Renderer11::applyPrimitiveType
    ASSERT(count >= 3);

    const unsigned int numTris = count - 2;

    if (numTris > (std::numeric_limits<unsigned int>::max() / (sizeof(unsigned int) * 3)))
    {
        ERR("Could not create a scratch index buffer for GL_TRIANGLE_FAN, too many indices required.");
        return gl::error(GL_OUT_OF_MEMORY);
    }

    const unsigned int spaceNeeded = (numTris * 3) * sizeof(unsigned int);
    if (!mTriangleFanIB->reserveBufferSpace(spaceNeeded, GL_UNSIGNED_INT))
    {
        ERR("Could not reserve enough space in scratch index buffer for GL_TRIANGLE_FAN.");
        return gl::error(GL_OUT_OF_MEMORY);
    }

    void* mappedMemory = NULL;
    unsigned int offset;
    if (!mTriangleFanIB->mapBuffer(spaceNeeded, &mappedMemory, &offset))
    {
        ERR("Could not map scratch index buffer for GL_TRIANGLE_FAN.");
        return gl::error(GL_OUT_OF_MEMORY);
    }

    unsigned int *data = reinterpret_cast<unsigned int*>(mappedMemory);
    unsigned int indexBufferOffset = offset;

    switch (type)
    {
      case GL_NONE:   // Non-indexed draw
        for (unsigned int i = 0; i < numTris; i++)
        {
            data[i*3 + 0] = 0;
            data[i*3 + 1] = i + 1;
            data[i*3 + 2] = i + 2;
        }
        break;
      case GL_UNSIGNED_BYTE:
        for (unsigned int i = 0; i < numTris; i++)
        {
            data[i*3 + 0] = static_cast<const GLubyte*>(indices)[0];
            data[i*3 + 1] = static_cast<const GLubyte*>(indices)[i + 1];
            data[i*3 + 2] = static_cast<const GLubyte*>(indices)[i + 2];
        }
        break;
      case GL_UNSIGNED_SHORT:
        for (unsigned int i = 0; i < numTris; i++)
        {
            data[i*3 + 0] = static_cast<const GLushort*>(indices)[0];
            data[i*3 + 1] = static_cast<const GLushort*>(indices)[i + 1];
            data[i*3 + 2] = static_cast<const GLushort*>(indices)[i + 2];
        }
        break;
      case GL_UNSIGNED_INT:
        for (unsigned int i = 0; i < numTris; i++)
        {
            data[i*3 + 0] = static_cast<const GLuint*>(indices)[0];
            data[i*3 + 1] = static_cast<const GLuint*>(indices)[i + 1];
            data[i*3 + 2] = static_cast<const GLuint*>(indices)[i + 2];
        }
        break;
      default: UNREACHABLE();
    }

    if (!mTriangleFanIB->unmapBuffer())
    {
        ERR("Could not unmap scratch index buffer for GL_TRIANGLE_FAN.");
        return gl::error(GL_OUT_OF_MEMORY);
    }

    if (mAppliedIBSerial != mTriangleFanIB->getSerial() || mAppliedIBOffset != indexBufferOffset)
    {
        IndexBuffer11 *indexBuffer = IndexBuffer11::makeIndexBuffer11(mTriangleFanIB->getIndexBuffer());

        mDeviceContext->IASetIndexBuffer(indexBuffer->getBuffer(), indexBuffer->getIndexFormat(), indexBufferOffset);
        mAppliedIBSerial = mTriangleFanIB->getSerial();
        mAppliedStorageIBSerial = 0;
        mAppliedIBOffset = indexBufferOffset;
    }

    if (instances > 0)
    {
        mDeviceContext->DrawIndexedInstanced(numTris * 3, instances, 0, -minIndex, 0);
    }
    else
    {
        mDeviceContext->DrawIndexed(numTris * 3, 0, -minIndex);
    }
}

void Renderer11::applyShaders(gl::ProgramBinary *programBinary)
{
    unsigned int programBinarySerial = programBinary->getSerial();
    const bool updateProgramState = (programBinarySerial != mAppliedProgramBinarySerial);

    if (updateProgramState)
    {
        ShaderExecutable *vertexExe = programBinary->getVertexExecutable();
        ShaderExecutable *pixelExe = programBinary->getPixelExecutable();

        ID3D11VertexShader *vertexShader = NULL;
        if (vertexExe) vertexShader = ShaderExecutable11::makeShaderExecutable11(vertexExe)->getVertexShader();

        ID3D11PixelShader *pixelShader = NULL;
        if (pixelExe) pixelShader = ShaderExecutable11::makeShaderExecutable11(pixelExe)->getPixelShader();

        mDeviceContext->PSSetShader(pixelShader, NULL, 0);
        mDeviceContext->VSSetShader(vertexShader, NULL, 0);

        programBinary->dirtyAllUniforms();

        mAppliedProgramBinarySerial = programBinarySerial;
    }

    // Only use the geometry shader currently for point sprite drawing
    const bool usesGeometryShader = (programBinary->usesGeometryShader() && mCurRasterState.pointDrawMode);

    if (updateProgramState || usesGeometryShader != mIsGeometryShaderActive)
    {
        if (usesGeometryShader)
        {
            ShaderExecutable *geometryExe = programBinary->getGeometryExecutable();
            ID3D11GeometryShader *geometryShader = ShaderExecutable11::makeShaderExecutable11(geometryExe)->getGeometryShader();
            mDeviceContext->GSSetShader(geometryShader, NULL, 0);
        }
        else
        {
            mDeviceContext->GSSetShader(NULL, NULL, 0);
        }

        mIsGeometryShaderActive = usesGeometryShader;
    }
}

void Renderer11::applyUniforms(gl::ProgramBinary *programBinary, gl::UniformArray *uniformArray)
{
    ShaderExecutable11 *vertexExecutable = ShaderExecutable11::makeShaderExecutable11(programBinary->getVertexExecutable());
    ShaderExecutable11 *pixelExecutable = ShaderExecutable11::makeShaderExecutable11(programBinary->getPixelExecutable());

    unsigned int totalRegisterCountVS = 0;
    unsigned int totalRegisterCountPS = 0;

    bool vertexUniformsDirty = false;
    bool pixelUniformsDirty = false;

    for (gl::UniformArray::const_iterator uniform_iterator = uniformArray->begin(); uniform_iterator != uniformArray->end(); uniform_iterator++)
    {
        const gl::Uniform *uniform = *uniform_iterator;

        if (uniform->isReferencedByVertexShader())
        {
            totalRegisterCountVS += uniform->registerCount;
            vertexUniformsDirty = vertexUniformsDirty || uniform->dirty;
        }

        if (uniform->isReferencedByFragmentShader())
        {
            totalRegisterCountPS += uniform->registerCount;
            pixelUniformsDirty = pixelUniformsDirty || uniform->dirty;
        }
    }

    ID3D11Buffer *vertexConstantBuffer = vertexExecutable->getConstantBuffer(mDevice, totalRegisterCountVS);
    ID3D11Buffer *pixelConstantBuffer = pixelExecutable->getConstantBuffer(mDevice, totalRegisterCountPS);

    float (*mapVS)[4] = NULL;
    float (*mapPS)[4] = NULL;

    if (totalRegisterCountVS > 0 && vertexUniformsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE map = {0};
        HRESULT result = mDeviceContext->Map(vertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        ASSERT(SUCCEEDED(result));
        mapVS = (float(*)[4])map.pData;
    }

    if (totalRegisterCountPS > 0 && pixelUniformsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE map = {0};
        HRESULT result = mDeviceContext->Map(pixelConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        ASSERT(SUCCEEDED(result));
        mapPS = (float(*)[4])map.pData;
    }

    for (size_t uniformIndex = 0; uniformIndex < uniformArray->size(); uniformIndex++)
    {
        gl::Uniform *uniform = (*uniformArray)[uniformIndex];

        if (!gl::IsSampler(uniform->type))
        {
            unsigned int componentCount = (4 - uniform->registerElement);

            // we assume that uniforms from structs are arranged in struct order in our uniforms list. otherwise we would
            // overwrite previously written regions of memory.

            if (uniform->isReferencedByVertexShader() && mapVS)
            {
                memcpy(&mapVS[uniform->vsRegisterIndex][uniform->registerElement], uniform->data, uniform->registerCount * sizeof(float) * componentCount);
            }

            if (uniform->isReferencedByFragmentShader() && mapPS)
            {
                memcpy(&mapPS[uniform->psRegisterIndex][uniform->registerElement], uniform->data, uniform->registerCount * sizeof(float) * componentCount);
            }
        }

        uniform->dirty = false;
    }

    if (mapVS)
    {
        mDeviceContext->Unmap(vertexConstantBuffer, 0);
    }

    if (mapPS)
    {
        mDeviceContext->Unmap(pixelConstantBuffer, 0);
    }

    if (mCurrentVertexConstantBuffer != vertexConstantBuffer)
    {
        mDeviceContext->VSSetConstantBuffers(0, 1, &vertexConstantBuffer);
        mCurrentVertexConstantBuffer = vertexConstantBuffer;
    }

    if (mCurrentPixelConstantBuffer != pixelConstantBuffer)
    {
        mDeviceContext->PSSetConstantBuffers(0, 1, &pixelConstantBuffer);
        mCurrentPixelConstantBuffer = pixelConstantBuffer;
    }

    // Driver uniforms
    if (!mDriverConstantBufferVS)
    {
        D3D11_BUFFER_DESC constantBufferDescription = {0};
        constantBufferDescription.ByteWidth = sizeof(dx_VertexConstants);
        constantBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        constantBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantBufferDescription.CPUAccessFlags = 0;
        constantBufferDescription.MiscFlags = 0;
        constantBufferDescription.StructureByteStride = 0;

        HRESULT result = mDevice->CreateBuffer(&constantBufferDescription, NULL, &mDriverConstantBufferVS);
        ASSERT(SUCCEEDED(result));

        mDeviceContext->VSSetConstantBuffers(1, 1, &mDriverConstantBufferVS);
    }

    if (!mDriverConstantBufferPS)
    {
        D3D11_BUFFER_DESC constantBufferDescription = {0};
        constantBufferDescription.ByteWidth = sizeof(dx_PixelConstants);
        constantBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        constantBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantBufferDescription.CPUAccessFlags = 0;
        constantBufferDescription.MiscFlags = 0;
        constantBufferDescription.StructureByteStride = 0;

        HRESULT result = mDevice->CreateBuffer(&constantBufferDescription, NULL, &mDriverConstantBufferPS);
        ASSERT(SUCCEEDED(result));

        mDeviceContext->PSSetConstantBuffers(1, 1, &mDriverConstantBufferPS);
    }

    if (memcmp(&mVertexConstants, &mAppliedVertexConstants, sizeof(dx_VertexConstants)) != 0)
    {
        mDeviceContext->UpdateSubresource(mDriverConstantBufferVS, 0, NULL, &mVertexConstants, 16, 0);
        memcpy(&mAppliedVertexConstants, &mVertexConstants, sizeof(dx_VertexConstants));
    }

    if (memcmp(&mPixelConstants, &mAppliedPixelConstants, sizeof(dx_PixelConstants)) != 0)
    {
        mDeviceContext->UpdateSubresource(mDriverConstantBufferPS, 0, NULL, &mPixelConstants, 16, 0);
        memcpy(&mAppliedPixelConstants, &mPixelConstants, sizeof(dx_PixelConstants));
    }

    // needed for the point sprite geometry shader
    if (mCurrentGeometryConstantBuffer != mDriverConstantBufferPS)
    {
        mDeviceContext->GSSetConstantBuffers(0, 1, &mDriverConstantBufferPS);
        mCurrentGeometryConstantBuffer = mDriverConstantBufferPS;
    }
}

void Renderer11::clear(const gl::ClearParameters &clearParams, gl::Framebuffer *frameBuffer)
{
    mClear->clearFramebuffer(clearParams, frameBuffer);
    invalidateFramebufferSwizzles(frameBuffer);
}

void Renderer11::markAllStateDirty()
{
    for (unsigned int rtIndex = 0; rtIndex < gl::IMPLEMENTATION_MAX_DRAW_BUFFERS; rtIndex++)
    {
        mAppliedRenderTargetSerials[rtIndex] = 0;
    }
    mAppliedDepthbufferSerial = 0;
    mAppliedStencilbufferSerial = 0;
    mDepthStencilInitialized = false;
    mRenderTargetDescInitialized = false;

    for (int i = 0; i < gl::IMPLEMENTATION_MAX_VERTEX_TEXTURE_IMAGE_UNITS; i++)
    {
        mForceSetVertexSamplerStates[i] = true;
        mCurVertexSRVs[i] = NULL;
    }
    for (int i = 0; i < gl::MAX_TEXTURE_IMAGE_UNITS; i++)
    {
        mForceSetPixelSamplerStates[i] = true;
        mCurPixelSRVs[i] = NULL;
    }

    mForceSetBlendState = true;
    mForceSetRasterState = true;
    mForceSetDepthStencilState = true;
    mForceSetScissor = true;
    mForceSetViewport = true;

    mAppliedIBSerial = 0;
    mAppliedStorageIBSerial = 0;
    mAppliedIBOffset = 0;

    mAppliedProgramBinarySerial = 0;
    memset(&mAppliedVertexConstants, 0, sizeof(dx_VertexConstants));
    memset(&mAppliedPixelConstants, 0, sizeof(dx_PixelConstants));

    mInputLayoutCache.markDirty();

    for (unsigned int i = 0; i < gl::IMPLEMENTATION_MAX_VERTEX_SHADER_UNIFORM_BUFFERS; i++)
    {
        mCurrentConstantBufferVS[i] = -1;
        mCurrentConstantBufferPS[i] = -1;
    }

    mCurrentVertexConstantBuffer = NULL;
    mCurrentPixelConstantBuffer = NULL;
    mCurrentGeometryConstantBuffer = NULL;

    mCurrentPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

void Renderer11::releaseDeviceResources()
{
    mStateCache.clear();
    mInputLayoutCache.clear();

    SafeDelete(mVertexDataManager);
    SafeDelete(mIndexDataManager);
    SafeDelete(mLineLoopIB);
    SafeDelete(mTriangleFanIB);
    SafeDelete(mBlit);
    SafeDelete(mClear);
    SafeDelete(mPixelTransfer);

    SafeRelease(mDriverConstantBufferVS);
    SafeRelease(mDriverConstantBufferPS);
    SafeRelease(mSyncQuery);
}

void Renderer11::notifyDeviceLost()
{
    mDeviceLost = true;
    mDisplay->notifyDeviceLost();
}

bool Renderer11::isDeviceLost()
{
    return mDeviceLost;
}

// set notify to true to broadcast a message to all contexts of the device loss
bool Renderer11::testDeviceLost(bool notify)
{
    bool isLost = false;

    // GetRemovedReason is used to test if the device is removed
    HRESULT result = mDevice->GetDeviceRemovedReason();
    isLost = d3d11::isDeviceLostError(result);

    if (isLost)
    {
        // Log error if this is a new device lost event
        if (mDeviceLost == false)
        {
            ERR("The D3D11 device was removed: 0x%08X", result);
        }

        // ensure we note the device loss --
        // we'll probably get this done again by notifyDeviceLost
        // but best to remember it!
        // Note that we don't want to clear the device loss status here
        // -- this needs to be done by resetDevice
        mDeviceLost = true;
        if (notify)
        {
            notifyDeviceLost();
        }
    }

    return isLost;
}

bool Renderer11::testDeviceResettable()
{
    // determine if the device is resettable by creating a dummy device
    PFN_D3D11_CREATE_DEVICE D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(mD3d11Module, "D3D11CreateDevice");

    if (D3D11CreateDevice == NULL)
    {
        return false;
    }

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    ID3D11Device* dummyDevice;
    D3D_FEATURE_LEVEL dummyFeatureLevel;
    ID3D11DeviceContext* dummyContext;

    HRESULT result = D3D11CreateDevice(NULL,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       NULL,
                                       #if defined(_DEBUG)
                                       D3D11_CREATE_DEVICE_DEBUG,
                                       #else
                                       0,
                                       #endif
                                       featureLevels,
                                       ArraySize(featureLevels),
                                       D3D11_SDK_VERSION,
                                       &dummyDevice,
                                       &dummyFeatureLevel,
                                       &dummyContext);

    if (!mDevice || FAILED(result))
    {
        return false;
    }

    SafeRelease(dummyContext);
    SafeRelease(dummyDevice);

    return true;
}

void Renderer11::release()
{
    releaseDeviceResources();

    SafeRelease(mDxgiFactory);
    SafeRelease(mDxgiAdapter);

    if (mDeviceContext)
    {
        mDeviceContext->ClearState();
        mDeviceContext->Flush();
        SafeRelease(mDeviceContext);
    }

    SafeRelease(mDevice);

    if (mD3d11Module)
    {
        FreeLibrary(mD3d11Module);
        mD3d11Module = NULL;
    }

    if (mDxgiModule)
    {
        FreeLibrary(mDxgiModule);
        mDxgiModule = NULL;
    }
}

bool Renderer11::resetDevice()
{
    // recreate everything
    release();
    EGLint result = initialize();

    if (result != EGL_SUCCESS)
    {
        ERR("Could not reinitialize D3D11 device: %08X", result);
        return false;
    }

    mDeviceLost = false;

    return true;
}

DWORD Renderer11::getAdapterVendor() const
{
    return mAdapterDescription.VendorId;
}

std::string Renderer11::getRendererDescription() const
{
    std::ostringstream rendererString;

    rendererString << mDescription;
    rendererString << " Direct3D11";

    rendererString << " vs_" << getMajorShaderModel() << "_" << getMinorShaderModel();
    rendererString << " ps_" << getMajorShaderModel() << "_" << getMinorShaderModel();

    return rendererString.str();
}

GUID Renderer11::getAdapterIdentifier() const
{
    // Use the adapter LUID as our adapter ID
    // This number is local to a machine is only guaranteed to be unique between restarts
    META_ASSERT(sizeof(LUID) <= sizeof(GUID));
    GUID adapterId = {0};
    memcpy(&adapterId, &mAdapterDescription.AdapterLuid, sizeof(LUID));
    return adapterId;
}

bool Renderer11::getBGRATextureSupport() const
{
    return mBGRATextureSupport;
}

bool Renderer11::getDXT1TextureSupport() const
{
    return mDXT1TextureSupport;
}

bool Renderer11::getDXT3TextureSupport() const
{
    return mDXT3TextureSupport;
}

bool Renderer11::getDXT5TextureSupport() const
{
    return mDXT5TextureSupport;
}

bool Renderer11::getDepthTextureSupport() const
{
    return mDepthTextureSupport;
}

bool Renderer11::getFloat32TextureSupport() const
{
    return mFloat32TextureSupport;
}

bool Renderer11::getFloat32TextureFilteringSupport() const
{
    return mFloat32FilterSupport;
}

bool Renderer11::getFloat32TextureRenderingSupport() const
{
    return mFloat32RenderSupport;
}

bool Renderer11::getFloat16TextureSupport() const
{
    return mFloat16TextureSupport;
}

bool Renderer11::getFloat16TextureFilteringSupport() const
{
    return mFloat16FilterSupport;
}

bool Renderer11::getFloat16TextureRenderingSupport() const
{
    return mFloat16RenderSupport;
}

bool Renderer11::getRGB565TextureSupport() const
{
    return false;
}

bool Renderer11::getLuminanceTextureSupport() const
{
    return false;
}

bool Renderer11::getLuminanceAlphaTextureSupport() const
{
    return false;
}

bool Renderer11::getRGTextureSupport() const
{
    return mRGTextureSupport;
}

bool Renderer11::getTextureFilterAnisotropySupport() const
{
    return true;
}

float Renderer11::getTextureMaxAnisotropy() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
        return D3D11_MAX_MAXANISOTROPY;
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return D3D10_MAX_MAXANISOTROPY;
      default: UNREACHABLE();
        return 0;
    }
}

bool Renderer11::getEventQuerySupport() const
{
    return true;
}

Range Renderer11::getViewportBounds() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
        return Range(D3D11_VIEWPORT_BOUNDS_MIN, D3D11_VIEWPORT_BOUNDS_MAX);
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return Range(D3D10_VIEWPORT_BOUNDS_MIN, D3D10_VIEWPORT_BOUNDS_MAX);
      default: UNREACHABLE();
        return Range(0, 0);
    }
}

unsigned int Renderer11::getMaxVertexTextureImageUnits() const
{
    META_ASSERT(MAX_TEXTURE_IMAGE_UNITS_VTF_SM4 <= gl::IMPLEMENTATION_MAX_VERTEX_TEXTURE_IMAGE_UNITS);
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return MAX_TEXTURE_IMAGE_UNITS_VTF_SM4;
      default: UNREACHABLE();
        return 0;
    }
}

unsigned int Renderer11::getMaxCombinedTextureImageUnits() const
{
    return gl::MAX_TEXTURE_IMAGE_UNITS + getMaxVertexTextureImageUnits();
}

unsigned int Renderer11::getReservedVertexUniformVectors() const
{
    return 0;   // Driver uniforms are stored in a separate constant buffer
}

unsigned int Renderer11::getReservedFragmentUniformVectors() const
{
    return 0;   // Driver uniforms are stored in a separate constant buffer
}

unsigned int Renderer11::getMaxVertexUniformVectors() const
{
    META_ASSERT(MAX_VERTEX_UNIFORM_VECTORS_D3D11 <= D3D10_REQ_CONSTANT_BUFFER_ELEMENT_COUNT);
    ASSERT(mFeatureLevel >= D3D_FEATURE_LEVEL_10_0);
    return MAX_VERTEX_UNIFORM_VECTORS_D3D11;
}

unsigned int Renderer11::getMaxFragmentUniformVectors() const
{
    META_ASSERT(MAX_FRAGMENT_UNIFORM_VECTORS_D3D11 <= D3D10_REQ_CONSTANT_BUFFER_ELEMENT_COUNT);
    ASSERT(mFeatureLevel >= D3D_FEATURE_LEVEL_10_0);
    return MAX_FRAGMENT_UNIFORM_VECTORS_D3D11;
}

unsigned int Renderer11::getMaxVaryingVectors() const
{
    META_ASSERT(gl::IMPLEMENTATION_MAX_VARYING_VECTORS == D3D11_VS_OUTPUT_REGISTER_COUNT);
    META_ASSERT(D3D11_VS_OUTPUT_REGISTER_COUNT <= D3D11_PS_INPUT_REGISTER_COUNT);
    META_ASSERT(D3D10_VS_OUTPUT_REGISTER_COUNT <= D3D10_PS_INPUT_REGISTER_COUNT);
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
        return D3D11_VS_OUTPUT_REGISTER_COUNT;
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return D3D10_VS_OUTPUT_REGISTER_COUNT;
      default: UNREACHABLE();
        return 0;
    }
}

unsigned int Renderer11::getMaxVertexShaderUniformBuffers() const
{
    META_ASSERT(gl::IMPLEMENTATION_MAX_VERTEX_SHADER_UNIFORM_BUFFERS >= D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT &&
                gl::IMPLEMENTATION_MAX_VERTEX_SHADER_UNIFORM_BUFFERS >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
        return D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - getReservedVertexUniformBuffers();
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - getReservedVertexUniformBuffers();
      default: UNREACHABLE();
        return 0;
    }
}

unsigned int Renderer11::getMaxFragmentShaderUniformBuffers() const
{
    META_ASSERT(gl::IMPLEMENTATION_MAX_FRAGMENT_SHADER_UNIFORM_BUFFERS >= D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT &&
                gl::IMPLEMENTATION_MAX_FRAGMENT_SHADER_UNIFORM_BUFFERS >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
        return D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - getReservedFragmentUniformBuffers();
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - getReservedFragmentUniformBuffers();
      default: UNREACHABLE();
        return 0;
    }
}

unsigned int Renderer11::getReservedVertexUniformBuffers() const
{
    // we reserve one buffer for the application uniforms, and one for driver uniforms
    return 2;
}

unsigned int Renderer11::getReservedFragmentUniformBuffers() const
{
    // we reserve one buffer for the application uniforms, and one for driver uniforms
    return 2;
}

unsigned int Renderer11::getMaxTransformFeedbackBuffers() const
{
    META_ASSERT(gl::IMPLEMENTATION_MAX_TRANSFORM_FEEDBACK_BUFFERS >= D3D11_SO_BUFFER_SLOT_COUNT &&
                gl::IMPLEMENTATION_MAX_TRANSFORM_FEEDBACK_BUFFERS >= D3D10_SO_BUFFER_SLOT_COUNT);

    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
        return D3D11_SO_BUFFER_SLOT_COUNT;
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return D3D10_SO_BUFFER_SLOT_COUNT;
      default: UNREACHABLE();
        return 0;
    }
}

unsigned int Renderer11::getMaxUniformBufferSize() const
{
    // Each component is a 4-element vector of 4-byte units (floats)
    const unsigned int bytesPerComponent = 4 * sizeof(float);

    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
        return D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * bytesPerComponent;
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return D3D10_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * bytesPerComponent;
      default: UNREACHABLE();
        return 0;
    }
}

bool Renderer11::getNonPower2TextureSupport() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return true;
      default: UNREACHABLE();
        return false;
    }
}

bool Renderer11::getOcclusionQuerySupport() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return true;
      default: UNREACHABLE();
        return false;
    }
}

bool Renderer11::getInstancingSupport() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return true;
      default: UNREACHABLE();
        return false;
    }
}

bool Renderer11::getShareHandleSupport() const
{
    // We only currently support share handles with BGRA surfaces, because
    // chrome needs BGRA. Once chrome fixes this, we should always support them.
    // PIX doesn't seem to support using share handles, so disable them.
    return getBGRATextureSupport() && !gl::perfActive();
}

bool Renderer11::getDerivativeInstructionSupport() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return true;
      default: UNREACHABLE();
        return false;
    }
}

bool Renderer11::getPostSubBufferSupport() const
{
    // D3D11 does not support present with dirty rectangles until D3D11.1 and DXGI 1.2.
    return false;
}

int Renderer11::getMaxRecommendedElementsIndices() const
{
    META_ASSERT(D3D11_REQ_DRAWINDEXED_INDEX_COUNT_2_TO_EXP == 32);
    META_ASSERT(D3D10_REQ_DRAWINDEXED_INDEX_COUNT_2_TO_EXP == 32);

    // D3D11 allows up to 2^32 elements, but we report max signed int for convenience.
    return std::numeric_limits<GLint>::max();
}

int Renderer11::getMaxRecommendedElementsVertices() const
{
    META_ASSERT(D3D11_REQ_DRAW_VERTEX_COUNT_2_TO_EXP == 32);
    META_ASSERT(D3D10_REQ_DRAW_VERTEX_COUNT_2_TO_EXP == 32);

    // D3D11 allows up to 2^32 elements, but we report max signed int for convenience.
    return std::numeric_limits<GLint>::max();
}

int Renderer11::getMajorShaderModel() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0: return D3D11_SHADER_MAJOR_VERSION;   // 5
      case D3D_FEATURE_LEVEL_10_1: return D3D10_1_SHADER_MAJOR_VERSION; // 4
      case D3D_FEATURE_LEVEL_10_0: return D3D10_SHADER_MAJOR_VERSION;   // 4
      default: UNREACHABLE();      return 0;
    }
}

int Renderer11::getMinorShaderModel() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0: return D3D11_SHADER_MINOR_VERSION;   // 0
      case D3D_FEATURE_LEVEL_10_1: return D3D10_1_SHADER_MINOR_VERSION; // 1
      case D3D_FEATURE_LEVEL_10_0: return D3D10_SHADER_MINOR_VERSION;   // 0
      default: UNREACHABLE();      return 0;
    }
}

float Renderer11::getMaxPointSize() const
{
    // choose a reasonable maximum. we enforce this in the shader.
    // (nb: on a Radeon 2600xt, DX9 reports a 256 max point size)
    return 1024.0f;
}

int Renderer11::getMaxViewportDimension() const
{
    // Maximum viewport size must be at least as large as the largest render buffer (or larger).
    // In our case return the maximum texture size, which is the maximum render buffer size.
    META_ASSERT(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION * 2 - 1 <= D3D11_VIEWPORT_BOUNDS_MAX);
    META_ASSERT(D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION * 2 - 1 <= D3D10_VIEWPORT_BOUNDS_MAX);

    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0: 
        return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;   // 16384
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0: 
        return D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;   // 8192
      default: UNREACHABLE();      
        return 0;
    }
}

int Renderer11::getMaxTextureWidth() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0: return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;   // 16384
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0: return D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;   // 8192
      default: UNREACHABLE();      return 0;
    }
}

int Renderer11::getMaxTextureHeight() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0: return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;   // 16384
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0: return D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;   // 8192
      default: UNREACHABLE();      return 0;
    }
}

int Renderer11::getMaxTextureDepth() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0: return D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;   // 2048
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0: return D3D10_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;   // 2048
      default: UNREACHABLE();      return 0;
    }
}

int Renderer11::getMaxTextureArrayLayers() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0: return D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;   // 2048
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0: return D3D10_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;   // 512
      default: UNREACHABLE();      return 0;
    }
}

bool Renderer11::get32BitIndexSupport() const
{
    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0: 
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0: return D3D10_REQ_DRAWINDEXED_INDEX_COUNT_2_TO_EXP >= 32;   // true
      default: UNREACHABLE();      return false;
    }
}

int Renderer11::getMinSwapInterval() const
{
    return 0;
}

int Renderer11::getMaxSwapInterval() const
{
    return 4;
}

int Renderer11::getMaxSupportedSamples() const
{
    return mMaxSupportedSamples;
}

GLsizei Renderer11::getMaxSupportedFormatSamples(GLenum internalFormat) const
{
    DXGI_FORMAT format = gl_d3d11::GetRenderableFormat(internalFormat, getCurrentClientVersion());
    MultisampleSupportMap::const_iterator iter = mMultisampleSupportMap.find(format);
    return (iter != mMultisampleSupportMap.end()) ? iter->second.maxSupportedSamples : 0;
}

GLsizei Renderer11::getNumSampleCounts(GLenum internalFormat) const
{
    unsigned int numCounts = 0;

    // D3D11 supports multisampling for signed and unsigned format, but ES 3.0 does not
    GLenum componentType = gl::GetComponentType(internalFormat, getCurrentClientVersion());
    if (componentType != GL_INT && componentType != GL_UNSIGNED_INT)
    {
        DXGI_FORMAT format = gl_d3d11::GetRenderableFormat(internalFormat, getCurrentClientVersion());
        MultisampleSupportMap::const_iterator iter = mMultisampleSupportMap.find(format);

        if (iter != mMultisampleSupportMap.end())
        {
            const MultisampleSupportInfo& info = iter->second;
            for (int i = 0; i < D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; i++)
            {
                if (info.qualityLevels[i] > 0)
                {
                    numCounts++;
                }
            }
        }
    }

    return numCounts;
}

void Renderer11::getSampleCounts(GLenum internalFormat, GLsizei bufSize, GLint *params) const
{
    // D3D11 supports multisampling for signed and unsigned format, but ES 3.0 does not
    GLenum componentType = gl::GetComponentType(internalFormat, getCurrentClientVersion());
    if (componentType == GL_INT || componentType == GL_UNSIGNED_INT)
    {
        return;
    }

    DXGI_FORMAT format = gl_d3d11::GetRenderableFormat(internalFormat, getCurrentClientVersion());
    MultisampleSupportMap::const_iterator iter = mMultisampleSupportMap.find(format);

    if (iter != mMultisampleSupportMap.end())
    {
        const MultisampleSupportInfo& info = iter->second;
        int bufPos = 0;
        for (int i = D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT - 1; i >= 0 && bufPos < bufSize; i--)
        {
            if (info.qualityLevels[i] > 0)
            {
                params[bufPos++] = i + 1;
            }
        }
    }
}

int Renderer11::getNearestSupportedSamples(DXGI_FORMAT format, unsigned int requested) const
{
    if (requested == 0)
    {
        return 0;
    }

    MultisampleSupportMap::const_iterator iter = mMultisampleSupportMap.find(format);
    if (iter != mMultisampleSupportMap.end())
    {
        const MultisampleSupportInfo& info = iter->second;
        for (unsigned int i = requested - 1; i < D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; i++)
        {
            if (info.qualityLevels[i] > 0)
            {
                return i + 1;
            }
        }
    }

    return -1;
}

unsigned int Renderer11::getMaxRenderTargets() const
{
    META_ASSERT(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT <= gl::IMPLEMENTATION_MAX_DRAW_BUFFERS);
    META_ASSERT(D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT <= gl::IMPLEMENTATION_MAX_DRAW_BUFFERS);

    switch (mFeatureLevel)
    {
      case D3D_FEATURE_LEVEL_11_0:
        return D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;  // 8
      case D3D_FEATURE_LEVEL_10_1:
      case D3D_FEATURE_LEVEL_10_0:
        return D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT;  // 8
      default:
        UNREACHABLE();
        return 1;
    }
}

bool Renderer11::copyToRenderTarget(TextureStorageInterface2D *dest, TextureStorageInterface2D *source)
{
    if (source && dest)
    {
        TextureStorage11_2D *source11 = TextureStorage11_2D::makeTextureStorage11_2D(source->getStorageInstance());
        TextureStorage11_2D *dest11 = TextureStorage11_2D::makeTextureStorage11_2D(dest->getStorageInstance());

        mDeviceContext->CopyResource(dest11->getBaseTexture(), source11->getBaseTexture());

        dest11->invalidateSwizzleCache();

        return true;
    }

    return false;
}

bool Renderer11::copyToRenderTarget(TextureStorageInterfaceCube *dest, TextureStorageInterfaceCube *source)
{
    if (source && dest)
    {
        TextureStorage11_Cube *source11 = TextureStorage11_Cube::makeTextureStorage11_Cube(source->getStorageInstance());
        TextureStorage11_Cube *dest11 = TextureStorage11_Cube::makeTextureStorage11_Cube(dest->getStorageInstance());

        mDeviceContext->CopyResource(dest11->getBaseTexture(), source11->getBaseTexture());

        dest11->invalidateSwizzleCache();

        return true;
    }

    return false;
}

bool Renderer11::copyToRenderTarget(TextureStorageInterface3D *dest, TextureStorageInterface3D *source)
{
    if (source && dest)
    {
        TextureStorage11_3D *source11 = TextureStorage11_3D::makeTextureStorage11_3D(source->getStorageInstance());
        TextureStorage11_3D *dest11 = TextureStorage11_3D::makeTextureStorage11_3D(dest->getStorageInstance());

        mDeviceContext->CopyResource(dest11->getBaseTexture(), source11->getBaseTexture());

        dest11->invalidateSwizzleCache();

        return true;
    }

    return false;
}

bool Renderer11::copyToRenderTarget(TextureStorageInterface2DArray *dest, TextureStorageInterface2DArray *source)
{
    if (source && dest)
    {
        TextureStorage11_2DArray *source11 = TextureStorage11_2DArray::makeTextureStorage11_2DArray(source->getStorageInstance());
        TextureStorage11_2DArray *dest11 = TextureStorage11_2DArray::makeTextureStorage11_2DArray(dest->getStorageInstance());

        mDeviceContext->CopyResource(dest11->getBaseTexture(), source11->getBaseTexture());

        dest11->invalidateSwizzleCache();

        return true;
    }

    return false;
}

bool Renderer11::copyImage(gl::Framebuffer *framebuffer, const gl::Rectangle &sourceRect, GLenum destFormat,
                           GLint xoffset, GLint yoffset, TextureStorageInterface2D *storage, GLint level)
{
    gl::Renderbuffer *colorbuffer = framebuffer->getReadColorbuffer();
    if (!colorbuffer)
    {
        ERR("Failed to retrieve the color buffer from the frame buffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    RenderTarget11 *sourceRenderTarget = RenderTarget11::makeRenderTarget11(colorbuffer->getRenderTarget());
    if (!sourceRenderTarget)
    {
        ERR("Failed to retrieve the render target from the frame buffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11ShaderResourceView *source = sourceRenderTarget->getShaderResourceView();
    if (!source)
    {
        ERR("Failed to retrieve the render target view from the render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    TextureStorage11_2D *storage11 = TextureStorage11_2D::makeTextureStorage11_2D(storage->getStorageInstance());
    if (!storage11)
    {
        ERR("Failed to retrieve the texture storage from the destination.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    RenderTarget11 *destRenderTarget = RenderTarget11::makeRenderTarget11(storage11->getRenderTarget(level));
    if (!destRenderTarget)
    {
        ERR("Failed to retrieve the render target from the destination storage.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11RenderTargetView *dest = destRenderTarget->getRenderTargetView();
    if (!dest)
    {
        ERR("Failed to retrieve the render target view from the destination render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    gl::Box sourceArea(sourceRect.x, sourceRect.y, 0, sourceRect.width, sourceRect.height, 1);
    gl::Extents sourceSize(sourceRenderTarget->getWidth(), sourceRenderTarget->getHeight(), 1);

    gl::Box destArea(xoffset, yoffset, 0, sourceRect.width, sourceRect.height, 1);
    gl::Extents destSize(destRenderTarget->getWidth(), destRenderTarget->getHeight(), 1);

    // Use nearest filtering because source and destination are the same size for the direct
    // copy
    bool ret = mBlit->copyTexture(source, sourceArea, sourceSize, dest, destArea, destSize, NULL,
                                  destFormat, GL_NEAREST);

    storage11->invalidateSwizzleCacheLevel(level);

    return ret;
}

bool Renderer11::copyImage(gl::Framebuffer *framebuffer, const gl::Rectangle &sourceRect, GLenum destFormat,
                           GLint xoffset, GLint yoffset, TextureStorageInterfaceCube *storage, GLenum target, GLint level)
{
    gl::Renderbuffer *colorbuffer = framebuffer->getReadColorbuffer();
    if (!colorbuffer)
    {
        ERR("Failed to retrieve the color buffer from the frame buffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    RenderTarget11 *sourceRenderTarget = RenderTarget11::makeRenderTarget11(colorbuffer->getRenderTarget());
    if (!sourceRenderTarget)
    {
        ERR("Failed to retrieve the render target from the frame buffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11ShaderResourceView *source = sourceRenderTarget->getShaderResourceView();
    if (!source)
    {
        ERR("Failed to retrieve the render target view from the render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    TextureStorage11_Cube *storage11 = TextureStorage11_Cube::makeTextureStorage11_Cube(storage->getStorageInstance());
    if (!storage11)
    {
        ERR("Failed to retrieve the texture storage from the destination.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    RenderTarget11 *destRenderTarget = RenderTarget11::makeRenderTarget11(storage11->getRenderTargetFace(target, level));
    if (!destRenderTarget)
    {
        ERR("Failed to retrieve the render target from the destination storage.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11RenderTargetView *dest = destRenderTarget->getRenderTargetView();
    if (!dest)
    {
        ERR("Failed to retrieve the render target view from the destination render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    gl::Box sourceArea(sourceRect.x, sourceRect.y, 0, sourceRect.width, sourceRect.height, 1);
    gl::Extents sourceSize(sourceRenderTarget->getWidth(), sourceRenderTarget->getHeight(), 1);

    gl::Box destArea(xoffset, yoffset, 0, sourceRect.width, sourceRect.height, 1);
    gl::Extents destSize(destRenderTarget->getWidth(), destRenderTarget->getHeight(), 1);

    // Use nearest filtering because source and destination are the same size for the direct
    // copy
    bool ret = mBlit->copyTexture(source, sourceArea, sourceSize, dest, destArea, destSize, NULL,
                                  destFormat, GL_NEAREST);

    storage11->invalidateSwizzleCacheLevel(level);

    return ret;
}

bool Renderer11::copyImage(gl::Framebuffer *framebuffer, const gl::Rectangle &sourceRect, GLenum destFormat,
                           GLint xoffset, GLint yoffset, GLint zOffset, TextureStorageInterface3D *storage, GLint level)
{
    gl::Renderbuffer *colorbuffer = framebuffer->getReadColorbuffer();
    if (!colorbuffer)
    {
        ERR("Failed to retrieve the color buffer from the frame buffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    RenderTarget11 *sourceRenderTarget = RenderTarget11::makeRenderTarget11(colorbuffer->getRenderTarget());
    if (!sourceRenderTarget)
    {
        ERR("Failed to retrieve the render target from the frame buffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11ShaderResourceView *source = sourceRenderTarget->getShaderResourceView();
    if (!source)
    {
        ERR("Failed to retrieve the render target view from the render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    TextureStorage11_3D *storage11 = TextureStorage11_3D::makeTextureStorage11_3D(storage->getStorageInstance());
    if (!storage11)
    {
        ERR("Failed to retrieve the texture storage from the destination.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    RenderTarget11 *destRenderTarget = RenderTarget11::makeRenderTarget11(storage11->getRenderTargetLayer(level, zOffset));
    if (!destRenderTarget)
    {
        ERR("Failed to retrieve the render target from the destination storage.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11RenderTargetView *dest = destRenderTarget->getRenderTargetView();
    if (!dest)
    {
        ERR("Failed to retrieve the render target view from the destination render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    gl::Box sourceArea(sourceRect.x, sourceRect.y, 0, sourceRect.width, sourceRect.height, 1);
    gl::Extents sourceSize(sourceRenderTarget->getWidth(), sourceRenderTarget->getHeight(), 1);

    gl::Box destArea(xoffset, yoffset, 0, sourceRect.width, sourceRect.height, 1);
    gl::Extents destSize(destRenderTarget->getWidth(), destRenderTarget->getHeight(), 1);

    // Use nearest filtering because source and destination are the same size for the direct
    // copy
    bool ret = mBlit->copyTexture(source, sourceArea, sourceSize, dest, destArea, destSize, NULL,
                                  destFormat, GL_NEAREST);

    storage11->invalidateSwizzleCacheLevel(level);

    return ret;
}

bool Renderer11::copyImage(gl::Framebuffer *framebuffer, const gl::Rectangle &sourceRect, GLenum destFormat,
                           GLint xoffset, GLint yoffset, GLint zOffset, TextureStorageInterface2DArray *storage, GLint level)
{
    gl::Renderbuffer *colorbuffer = framebuffer->getReadColorbuffer();
    if (!colorbuffer)
    {
        ERR("Failed to retrieve the color buffer from the frame buffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    RenderTarget11 *sourceRenderTarget = RenderTarget11::makeRenderTarget11(colorbuffer->getRenderTarget());
    if (!sourceRenderTarget)
    {
        ERR("Failed to retrieve the render target from the frame buffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11ShaderResourceView *source = sourceRenderTarget->getShaderResourceView();
    if (!source)
    {
        ERR("Failed to retrieve the render target view from the render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    TextureStorage11_2DArray *storage11 = TextureStorage11_2DArray::makeTextureStorage11_2DArray(storage->getStorageInstance());
    if (!storage11)
    {
        SafeRelease(source);
        ERR("Failed to retrieve the texture storage from the destination.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    RenderTarget11 *destRenderTarget = RenderTarget11::makeRenderTarget11(storage11->getRenderTargetLayer(level, zOffset));
    if (!destRenderTarget)
    {
        SafeRelease(source);
        ERR("Failed to retrieve the render target from the destination storage.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11RenderTargetView *dest = destRenderTarget->getRenderTargetView();
    if (!dest)
    {
        ERR("Failed to retrieve the render target view from the destination render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    gl::Box sourceArea(sourceRect.x, sourceRect.y, 0, sourceRect.width, sourceRect.height, 1);
    gl::Extents sourceSize(sourceRenderTarget->getWidth(), sourceRenderTarget->getHeight(), 1);

    gl::Box destArea(xoffset, yoffset, 0, sourceRect.width, sourceRect.height, 1);
    gl::Extents destSize(destRenderTarget->getWidth(), destRenderTarget->getHeight(), 1);

    // Use nearest filtering because source and destination are the same size for the direct
    // copy
    bool ret = mBlit->copyTexture(source, sourceArea, sourceSize, dest, destArea, destSize, NULL,
                                  destFormat, GL_NEAREST);

    storage11->invalidateSwizzleCacheLevel(level);

    return ret;
}

void Renderer11::unapplyRenderTargets()
{
    setOneTimeRenderTarget(NULL);
}

void Renderer11::setOneTimeRenderTarget(ID3D11RenderTargetView *renderTargetView)
{
    ID3D11RenderTargetView *rtvArray[gl::IMPLEMENTATION_MAX_DRAW_BUFFERS] = {NULL};

    rtvArray[0] = renderTargetView;

    mDeviceContext->OMSetRenderTargets(getMaxRenderTargets(), rtvArray, NULL);

    // Do not preserve the serial for this one-time-use render target
    for (unsigned int rtIndex = 0; rtIndex < gl::IMPLEMENTATION_MAX_DRAW_BUFFERS; rtIndex++)
    {
        mAppliedRenderTargetSerials[rtIndex] = 0;
    }
}

RenderTarget *Renderer11::createRenderTarget(SwapChain *swapChain, bool depth)
{
    SwapChain11 *swapChain11 = SwapChain11::makeSwapChain11(swapChain);
    RenderTarget11 *renderTarget = NULL;

    if (depth)
    {
        // Note: depth stencil may be NULL for 0 sized surfaces
        renderTarget = new RenderTarget11(this, swapChain11->getDepthStencil(),
                                          swapChain11->getDepthStencilTexture(),
                                          swapChain11->getDepthStencilShaderResource(),
                                          swapChain11->getWidth(), swapChain11->getHeight(), 1);
    }
    else
    {
        // Note: render target may be NULL for 0 sized surfaces
        renderTarget = new RenderTarget11(this, swapChain11->getRenderTarget(),
                                          swapChain11->getOffscreenTexture(),
                                          swapChain11->getRenderTargetShaderResource(),
                                          swapChain11->getWidth(), swapChain11->getHeight(), 1);
    }
    return renderTarget;
}

RenderTarget *Renderer11::createRenderTarget(int width, int height, GLenum format, GLsizei samples)
{
    RenderTarget11 *renderTarget = new RenderTarget11(this, width, height, format, samples);
    return renderTarget;
}

ShaderExecutable *Renderer11::loadExecutable(const void *function, size_t length, rx::ShaderType type)
{
    ShaderExecutable11 *executable = NULL;

    switch (type)
    {
      case rx::SHADER_VERTEX:
        {
            ID3D11VertexShader *vshader = NULL;
            HRESULT result = mDevice->CreateVertexShader(function, length, NULL, &vshader);
            ASSERT(SUCCEEDED(result));

            if (vshader)
            {
                executable = new ShaderExecutable11(function, length, vshader);
            }
        }
        break;
      case rx::SHADER_PIXEL:
        {
            ID3D11PixelShader *pshader = NULL;
            HRESULT result = mDevice->CreatePixelShader(function, length, NULL, &pshader);
            ASSERT(SUCCEEDED(result));

            if (pshader)
            {
                executable = new ShaderExecutable11(function, length, pshader);
            }
        }
        break;
      case rx::SHADER_GEOMETRY:
        {
            ID3D11GeometryShader *gshader = NULL;
            HRESULT result = mDevice->CreateGeometryShader(function, length, NULL, &gshader);
            ASSERT(SUCCEEDED(result));

            if (gshader)
            {
                executable = new ShaderExecutable11(function, length, gshader);
            }
        }
        break;
      default:
        UNREACHABLE();
        break;
    }

    return executable;
}

ShaderExecutable *Renderer11::compileToExecutable(gl::InfoLog &infoLog, const char *shaderHLSL, rx::ShaderType type, D3DWorkaroundType workaround)
{
    const char *profile = NULL;

    switch (type)
    {
      case rx::SHADER_VERTEX:
        profile = "vs_4_0";
        break;
      case rx::SHADER_PIXEL:
        profile = "ps_4_0";
        break;
      case rx::SHADER_GEOMETRY:
        profile = "gs_4_0";
        break;
      default:
        UNREACHABLE();
        return NULL;
    }

    ID3DBlob *binary = (ID3DBlob*)compileToBinary(infoLog, shaderHLSL, profile, D3DCOMPILE_OPTIMIZATION_LEVEL0, false);
    if (!binary)
    {
        return NULL;
    }

    ShaderExecutable *executable = loadExecutable((DWORD *)binary->GetBufferPointer(), binary->GetBufferSize(), type);
    SafeRelease(binary);

    return executable;
}

VertexBuffer *Renderer11::createVertexBuffer()
{
    return new VertexBuffer11(this);
}

IndexBuffer *Renderer11::createIndexBuffer()
{
    return new IndexBuffer11(this);
}

BufferStorage *Renderer11::createBufferStorage()
{
    return new BufferStorage11(this);
}

QueryImpl *Renderer11::createQuery(GLenum type)
{
    return new Query11(this, type);
}

FenceImpl *Renderer11::createFence()
{
    return new Fence11(this);
}

bool Renderer11::supportsFastCopyBufferToTexture(GLenum internalFormat) const
{
    int clientVersion = getCurrentClientVersion();

    // We only support buffer to texture copies in ES3
    if (clientVersion <= 2)
    {
        return false;
    }

    // sRGB formats do not work with D3D11 buffer SRVs
    if (gl::GetColorEncoding(internalFormat, clientVersion) == GL_SRGB)
    {
        return false;
    }

    // We cannot support direct copies to non-color-renderable formats
    if (!gl::IsColorRenderingSupported(internalFormat, this))
    {
        return false;
    }

    // We skip all 3-channel formats since sometimes format support is missing
    if (gl::GetComponentCount(internalFormat, clientVersion) == 3)
    {
        return false;
    }

    // We don't support formats which we can't represent without conversion
    if (getNativeTextureFormat(internalFormat) != internalFormat)
    {
        return false;
    }

    return true;
}

bool Renderer11::fastCopyBufferToTexture(const gl::PixelUnpackState &unpack, unsigned int offset, RenderTarget *destRenderTarget,
                                         GLenum destinationFormat, GLenum sourcePixelsType, const gl::Box &destArea)
{
    ASSERT(supportsFastCopyBufferToTexture(destinationFormat));
    return mPixelTransfer->copyBufferToTexture(unpack, offset, destRenderTarget, destinationFormat, sourcePixelsType, destArea);
}

bool Renderer11::getRenderTargetResource(gl::Renderbuffer *colorbuffer, unsigned int *subresourceIndex, ID3D11Texture2D **resource)
{
    ASSERT(colorbuffer != NULL);

    RenderTarget11 *renderTarget = RenderTarget11::makeRenderTarget11(colorbuffer->getRenderTarget());
    if (renderTarget)
    {
        *subresourceIndex = renderTarget->getSubresourceIndex();

        ID3D11RenderTargetView *colorBufferRTV = renderTarget->getRenderTargetView();
        if (colorBufferRTV)
        {
            ID3D11Resource *textureResource = NULL;
            colorBufferRTV->GetResource(&textureResource);

            if (textureResource)
            {
                HRESULT result = textureResource->QueryInterface(IID_ID3D11Texture2D, (void**)resource);
                SafeRelease(textureResource);

                if (SUCCEEDED(result))
                {
                    return true;
                }
                else
                {
                    ERR("Failed to extract the ID3D11Texture2D from the render target resource, "
                        "HRESULT: 0x%X.", result);
                }
            }
        }
    }

    return false;
}

bool Renderer11::blitRect(gl::Framebuffer *readTarget, const gl::Rectangle &readRect, gl::Framebuffer *drawTarget, const gl::Rectangle &drawRect,
                          const gl::Rectangle *scissor, bool blitRenderTarget, bool blitDepth, bool blitStencil, GLenum filter)
{
    if (blitRenderTarget)
    {
        gl::Renderbuffer *readBuffer = readTarget->getReadColorbuffer();

        if (!readBuffer)
        {
            ERR("Failed to retrieve the read buffer from the read framebuffer.");
            return gl::error(GL_OUT_OF_MEMORY, false);
        }

        RenderTarget *readRenderTarget = readBuffer->getRenderTarget();

        for (unsigned int colorAttachment = 0; colorAttachment < gl::IMPLEMENTATION_MAX_DRAW_BUFFERS; colorAttachment++)
        {
            if (drawTarget->isEnabledColorAttachment(colorAttachment))
            {
                gl::Renderbuffer *drawBuffer = drawTarget->getColorbuffer(colorAttachment);

                if (!drawBuffer)
                {
                    ERR("Failed to retrieve the draw buffer from the draw framebuffer.");
                    return gl::error(GL_OUT_OF_MEMORY, false);
                }

                RenderTarget *drawRenderTarget = drawBuffer->getRenderTarget();

                if (!blitRenderbufferRect(readRect, drawRect, readRenderTarget, drawRenderTarget, filter, scissor,
                                          blitRenderTarget, false, false))
                {
                    return false;
                }
            }
        }
    }

    if (blitDepth || blitStencil)
    {
        gl::Renderbuffer *readBuffer = readTarget->getDepthOrStencilbuffer();
        gl::Renderbuffer *drawBuffer = drawTarget->getDepthOrStencilbuffer();

        if (!readBuffer)
        {
            ERR("Failed to retrieve the read depth-stencil buffer from the read framebuffer.");
            return gl::error(GL_OUT_OF_MEMORY, false);
        }

        if (!drawBuffer)
        {
            ERR("Failed to retrieve the draw depth-stencil buffer from the draw framebuffer.");
            return gl::error(GL_OUT_OF_MEMORY, false);
        }

        RenderTarget *readRenderTarget = readBuffer->getDepthStencil();
        RenderTarget *drawRenderTarget = drawBuffer->getDepthStencil();

        if (!blitRenderbufferRect(readRect, drawRect, readRenderTarget, drawRenderTarget, filter, scissor,
                                  false, blitDepth, blitStencil))
        {
            return false;
        }
    }

    invalidateFramebufferSwizzles(drawTarget);

    return true;
}

void Renderer11::readPixels(gl::Framebuffer *framebuffer, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                            GLsizei outputPitch, bool packReverseRowOrder, GLint packAlignment, void* pixels)
{
    ID3D11Texture2D *colorBufferTexture = NULL;
    unsigned int subresourceIndex = 0;

    gl::Renderbuffer *colorbuffer = framebuffer->getReadColorbuffer();

    if (colorbuffer && getRenderTargetResource(colorbuffer, &subresourceIndex, &colorBufferTexture))
    {
        gl::Rectangle area;
        area.x = x;
        area.y = y;
        area.width = width;
        area.height = height;

        readTextureData(colorBufferTexture, subresourceIndex, area, format, type, outputPitch,
                        packReverseRowOrder, packAlignment, pixels);

        SafeRelease(colorBufferTexture);
    }
}

Image *Renderer11::createImage()
{
    return new Image11();
}

void Renderer11::generateMipmap(Image *dest, Image *src)
{
    Image11 *dest11 = Image11::makeImage11(dest);
    Image11 *src11 = Image11::makeImage11(src);
    Image11::generateMipmap(getCurrentClientVersion(), dest11, src11);
}

TextureStorage *Renderer11::createTextureStorage2D(SwapChain *swapChain)
{
    SwapChain11 *swapChain11 = SwapChain11::makeSwapChain11(swapChain);
    return new TextureStorage11_2D(this, swapChain11);
}

TextureStorage *Renderer11::createTextureStorage2D(int baseLevel, int maxLevel, GLenum internalformat, bool renderTarget, GLsizei width, GLsizei height)
{
    return new TextureStorage11_2D(this, baseLevel, maxLevel, internalformat, renderTarget, width, height);
}

TextureStorage *Renderer11::createTextureStorageCube(int baseLevel, int maxLevel, GLenum internalformat, bool renderTarget, int size)
{
    return new TextureStorage11_Cube(this, baseLevel, maxLevel, internalformat, renderTarget, size);
}

TextureStorage *Renderer11::createTextureStorage3D(int baseLevel, int maxLevel, GLenum internalformat, bool renderTarget, GLsizei width, GLsizei height, GLsizei depth)
{
    return new TextureStorage11_3D(this, baseLevel, maxLevel, internalformat, renderTarget, width, height, depth);
}

TextureStorage *Renderer11::createTextureStorage2DArray(int baseLevel, int maxLevel, GLenum internalformat, bool renderTarget, GLsizei width, GLsizei height, GLsizei depth)
{
    return new TextureStorage11_2DArray(this, baseLevel, maxLevel, internalformat, renderTarget, width, height, depth);
}

void Renderer11::readTextureData(ID3D11Texture2D *texture, unsigned int subResource, const gl::Rectangle &area,
                                 GLenum format, GLenum type, GLsizei outputPitch, bool packReverseRowOrder,
                                 GLint packAlignment, void *pixels)
{
    D3D11_TEXTURE2D_DESC textureDesc;
    texture->GetDesc(&textureDesc);

    D3D11_TEXTURE2D_DESC stagingDesc;
    stagingDesc.Width = area.width;
    stagingDesc.Height = area.height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = textureDesc.Format;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* stagingTex = NULL;
    HRESULT result = mDevice->CreateTexture2D(&stagingDesc, NULL, &stagingTex);
    if (FAILED(result))
    {
        ERR("Failed to create staging texture for readPixels, HRESULT: 0x%X.", result);
        return;
    }

    ID3D11Texture2D* srcTex = NULL;
    if (textureDesc.SampleDesc.Count > 1)
    {
        D3D11_TEXTURE2D_DESC resolveDesc;
        resolveDesc.Width = textureDesc.Width;
        resolveDesc.Height = textureDesc.Height;
        resolveDesc.MipLevels = 1;
        resolveDesc.ArraySize = 1;
        resolveDesc.Format = textureDesc.Format;
        resolveDesc.SampleDesc.Count = 1;
        resolveDesc.SampleDesc.Quality = 0;
        resolveDesc.Usage = D3D11_USAGE_DEFAULT;
        resolveDesc.BindFlags = 0;
        resolveDesc.CPUAccessFlags = 0;
        resolveDesc.MiscFlags = 0;

        result = mDevice->CreateTexture2D(&resolveDesc, NULL, &srcTex);
        if (FAILED(result))
        {
            ERR("Failed to create resolve texture for readPixels, HRESULT: 0x%X.", result);
            SafeRelease(stagingTex);
            return;
        }

        mDeviceContext->ResolveSubresource(srcTex, 0, texture, subResource, textureDesc.Format);
        subResource = 0;
    }
    else
    {
        srcTex = texture;
        srcTex->AddRef();
    }

    D3D11_BOX srcBox;
    srcBox.left = area.x;
    srcBox.right = area.x + area.width;
    srcBox.top = area.y;
    srcBox.bottom = area.y + area.height;
    srcBox.front = 0;
    srcBox.back = 1;

    mDeviceContext->CopySubresourceRegion(stagingTex, 0, 0, 0, 0, srcTex, subResource, &srcBox);

    SafeRelease(srcTex);

    D3D11_MAPPED_SUBRESOURCE mapping;
    mDeviceContext->Map(stagingTex, 0, D3D11_MAP_READ, 0, &mapping);

    unsigned char *source;
    int inputPitch;
    if (packReverseRowOrder)
    {
        source = static_cast<unsigned char*>(mapping.pData) + mapping.RowPitch * (area.height - 1);
        inputPitch = -static_cast<int>(mapping.RowPitch);
    }
    else
    {
        source = static_cast<unsigned char*>(mapping.pData);
        inputPitch = static_cast<int>(mapping.RowPitch);
    }

    GLuint clientVersion = getCurrentClientVersion();

    GLenum sourceInternalFormat = d3d11_gl::GetInternalFormat(textureDesc.Format, clientVersion);
    GLenum sourceFormat = gl::GetFormat(sourceInternalFormat, clientVersion);
    GLenum sourceType = gl::GetType(sourceInternalFormat, clientVersion);

    GLuint sourcePixelSize = gl::GetPixelBytes(sourceInternalFormat, clientVersion);

    if (sourceFormat == format && sourceType == type)
    {
        // Direct copy possible
        unsigned char *dest = static_cast<unsigned char*>(pixels);
        for (int y = 0; y < area.height; y++)
        {
            memcpy(dest + y * outputPitch, source + y * inputPitch, area.width * sourcePixelSize);
        }
    }
    else
    {
        GLenum destInternalFormat = gl::GetSizedInternalFormat(format, type, clientVersion);
        GLuint destPixelSize = gl::GetPixelBytes(destInternalFormat, clientVersion);

        ColorCopyFunction fastCopyFunc = d3d11::GetFastCopyFunction(textureDesc.Format, format, type, getCurrentClientVersion());
        if (fastCopyFunc)
        {
            // Fast copy is possible through some special function
            for (int y = 0; y < area.height; y++)
            {
                for (int x = 0; x < area.width; x++)
                {
                    void *dest = static_cast<unsigned char*>(pixels) + y * outputPitch + x * destPixelSize;
                    void *src = static_cast<unsigned char*>(source) + y * inputPitch + x * sourcePixelSize;

                    fastCopyFunc(src, dest);
                }
            }
        }
        else
        {
            ColorReadFunction readFunc = d3d11::GetColorReadFunction(textureDesc.Format, clientVersion);
            ColorWriteFunction writeFunc = gl::GetColorWriteFunction(format, type, clientVersion);

            unsigned char temp[16]; // Maximum size of any Color<T> type used.
            META_ASSERT(sizeof(temp) >= sizeof(gl::ColorF)  &&
                        sizeof(temp) >= sizeof(gl::ColorUI) &&
                        sizeof(temp) >= sizeof(gl::ColorI));

            for (int y = 0; y < area.height; y++)
            {
                for (int x = 0; x < area.width; x++)
                {
                    void *dest = static_cast<unsigned char*>(pixels) + y * outputPitch + x * destPixelSize;
                    void *src = static_cast<unsigned char*>(source) + y * inputPitch + x * sourcePixelSize;

                    // readFunc and writeFunc will be using the same type of color, CopyTexImage
                    // will not allow the copy otherwise.
                    readFunc(src, temp);
                    writeFunc(temp, dest);
                }
            }
        }
    }

    mDeviceContext->Unmap(stagingTex, 0);

    SafeRelease(stagingTex);
}

bool Renderer11::blitRenderbufferRect(const gl::Rectangle &readRect, const gl::Rectangle &drawRect, RenderTarget *readRenderTarget,
                                      RenderTarget *drawRenderTarget, GLenum filter, const gl::Rectangle *scissor,
                                      bool colorBlit, bool depthBlit, bool stencilBlit)
{
    // Since blitRenderbufferRect is called for each render buffer that needs to be blitted,
    // it should never be the case that both color and depth/stencil need to be blitted at
    // at the same time.
    ASSERT(colorBlit != (depthBlit || stencilBlit));

    bool result = true;

    RenderTarget11 *drawRenderTarget11 = RenderTarget11::makeRenderTarget11(drawRenderTarget);
    if (!drawRenderTarget)
    {
        ERR("Failed to retrieve the draw render target from the draw framebuffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11Resource *drawTexture = drawRenderTarget11->getTexture();
    unsigned int drawSubresource = drawRenderTarget11->getSubresourceIndex();
    ID3D11RenderTargetView *drawRTV = drawRenderTarget11->getRenderTargetView();
    ID3D11DepthStencilView *drawDSV = drawRenderTarget11->getDepthStencilView();

    RenderTarget11 *readRenderTarget11 = RenderTarget11::makeRenderTarget11(readRenderTarget);
    if (!readRenderTarget)
    {
        ERR("Failed to retrieve the read render target from the read framebuffer.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    ID3D11Resource *readTexture = NULL;
    ID3D11ShaderResourceView *readSRV = NULL;
    unsigned int readSubresource = 0;
    if (readRenderTarget->getSamples() > 0)
    {
        ID3D11Resource *unresolvedResource = readRenderTarget11->getTexture();
        ID3D11Texture2D *unresolvedTexture = d3d11::DynamicCastComObject<ID3D11Texture2D>(unresolvedResource);

        if (unresolvedTexture)
        {
            readTexture = resolveMultisampledTexture(unresolvedTexture, readRenderTarget11->getSubresourceIndex());
            readSubresource = 0;

            SafeRelease(unresolvedTexture);

            HRESULT result = mDevice->CreateShaderResourceView(readTexture, NULL, &readSRV);
            if (FAILED(result))
            {
                SafeRelease(readTexture);
                return gl::error(GL_OUT_OF_MEMORY, false);
            }
        }
    }
    else
    {
        readTexture = readRenderTarget11->getTexture();
        readTexture->AddRef();
        readSubresource = readRenderTarget11->getSubresourceIndex();
        readSRV = readRenderTarget11->getShaderResourceView();
        readSRV->AddRef();
    }

    if (!readTexture || !readSRV)
    {
        SafeRelease(readTexture);
        SafeRelease(readSRV);
        ERR("Failed to retrieve the read render target view from the read render target.");
        return gl::error(GL_OUT_OF_MEMORY, false);
    }

    gl::Extents readSize(readRenderTarget->getWidth(), readRenderTarget->getHeight(), 1);
    gl::Extents drawSize(drawRenderTarget->getWidth(), drawRenderTarget->getHeight(), 1);

    bool scissorNeeded = scissor && gl::ClipRectangle(drawRect, *scissor, NULL);

    bool wholeBufferCopy = !scissorNeeded &&
                           readRect.x == 0 && readRect.width == readSize.width &&
                           readRect.y == 0 && readRect.height == readSize.height &&
                           drawRect.x == 0 && drawRect.width == drawSize.width &&
                           drawRect.y == 0 && drawRect.height == drawSize.height;

    bool stretchRequired = readRect.width != drawRect.width || readRect.height != drawRect.height;

    bool flipRequired = readRect.width < 0 || readRect.height < 0 || drawRect.width < 0 || readRect.height < 0;

    bool outOfBounds = readRect.x < 0 || readRect.x + readRect.width > readSize.width ||
                       readRect.y < 0 || readRect.y + readRect.height > readSize.height ||
                       drawRect.x < 0 || drawRect.x + drawRect.width > drawSize.width ||
                       drawRect.y < 0 || drawRect.y + drawRect.height > drawSize.height;

    bool hasDepth = gl::GetDepthBits(drawRenderTarget11->getActualFormat(), getCurrentClientVersion()) > 0;
    bool hasStencil = gl::GetStencilBits(drawRenderTarget11->getActualFormat(), getCurrentClientVersion()) > 0;
    bool partialDSBlit = (hasDepth && depthBlit) != (hasStencil && stencilBlit);

    if (readRenderTarget11->getActualFormat() == drawRenderTarget->getActualFormat() &&
        !stretchRequired && !outOfBounds && !flipRequired && !partialDSBlit &&
        (!(depthBlit || stencilBlit) || wholeBufferCopy))
    {
        UINT dstX = drawRect.x;
        UINT dstY = drawRect.y;

        D3D11_BOX readBox;
        readBox.left = readRect.x;
        readBox.right = readRect.x + readRect.width;
        readBox.top = readRect.y;
        readBox.bottom = readRect.y + readRect.height;
        readBox.front = 0;
        readBox.back = 1;

        if (scissorNeeded)
        {
            // drawRect is guaranteed to have positive width and height because stretchRequired is false.
            ASSERT(drawRect.width >= 0 || drawRect.height >= 0);

            if (drawRect.x < scissor->x)
            {
                dstX = scissor->x;
                readBox.left += (scissor->x - drawRect.x);
            }
            if (drawRect.y < scissor->y)
            {
                dstY = scissor->y;
                readBox.top += (scissor->y - drawRect.y);
            }
            if (drawRect.x + drawRect.width > scissor->x + scissor->width)
            {
                readBox.right -= ((drawRect.x + drawRect.width) - (scissor->x + scissor->width));
            }
            if (drawRect.y + drawRect.height > scissor->y + scissor->height)
            {
                readBox.bottom -= ((drawRect.y + drawRect.height) - (scissor->y + scissor->height));
            }
        }

        // D3D11 needs depth-stencil CopySubresourceRegions to have a NULL pSrcBox
        // We also require complete framebuffer copies for depth-stencil blit.
        D3D11_BOX *pSrcBox = wholeBufferCopy ? NULL : &readBox;

        mDeviceContext->CopySubresourceRegion(drawTexture, drawSubresource, dstX, dstY, 0,
                                              readTexture, readSubresource, pSrcBox);
        result = true;
    }
    else
    {
        gl::Box readArea(readRect.x, readRect.y, 0, readRect.width, readRect.height, 1);
        gl::Box drawArea(drawRect.x, drawRect.y, 0, drawRect.width, drawRect.height, 1);

        if (depthBlit && stencilBlit)
        {
            result = mBlit->copyDepthStencil(readTexture, readSubresource, readArea, readSize,
                                             drawTexture, drawSubresource, drawArea, drawSize,
                                             scissor);
        }
        else if (depthBlit)
        {
            result = mBlit->copyDepth(readSRV, readArea, readSize, drawDSV, drawArea, drawSize,
                                      scissor);
        }
        else if (stencilBlit)
        {
            result = mBlit->copyStencil(readTexture, readSubresource, readArea, readSize,
                                        drawTexture, drawSubresource, drawArea, drawSize,
                                        scissor);
        }
        else
        {
            GLenum format = gl::GetFormat(drawRenderTarget->getInternalFormat(), getCurrentClientVersion());
            result = mBlit->copyTexture(readSRV, readArea, readSize, drawRTV, drawArea, drawSize,
                                        scissor, format, filter);
        }
    }

    SafeRelease(readTexture);
    SafeRelease(readSRV);

    return result;
}

ID3D11Texture2D *Renderer11::resolveMultisampledTexture(ID3D11Texture2D *source, unsigned int subresource)
{
    D3D11_TEXTURE2D_DESC textureDesc;
    source->GetDesc(&textureDesc);

    if (textureDesc.SampleDesc.Count > 1)
    {
        D3D11_TEXTURE2D_DESC resolveDesc;
        resolveDesc.Width = textureDesc.Width;
        resolveDesc.Height = textureDesc.Height;
        resolveDesc.MipLevels = 1;
        resolveDesc.ArraySize = 1;
        resolveDesc.Format = textureDesc.Format;
        resolveDesc.SampleDesc.Count = 1;
        resolveDesc.SampleDesc.Quality = 0;
        resolveDesc.Usage = textureDesc.Usage;
        resolveDesc.BindFlags = textureDesc.BindFlags;
        resolveDesc.CPUAccessFlags = 0;
        resolveDesc.MiscFlags = 0;

        ID3D11Texture2D *resolveTexture = NULL;
        HRESULT result = mDevice->CreateTexture2D(&resolveDesc, NULL, &resolveTexture);
        if (FAILED(result))
        {
            ERR("Failed to create a multisample resolve texture, HRESULT: 0x%X.", result);
            return NULL;
        }

        mDeviceContext->ResolveSubresource(resolveTexture, 0, source, subresource, textureDesc.Format);
        return resolveTexture;
    }
    else
    {
        source->AddRef();
        return source;
    }
}

void Renderer11::invalidateRenderbufferSwizzles(gl::Renderbuffer *renderBuffer, int mipLevel)
{
    TextureStorage *texStorage = renderBuffer->getTextureStorage();
    if (texStorage)
    {
        TextureStorage11 *texStorage11 = TextureStorage11::makeTextureStorage11(texStorage);
        if (!texStorage11)
        {
            ERR("texture storage pointer unexpectedly null.");
            return;
        }

        texStorage11->invalidateSwizzleCacheLevel(mipLevel);
    }
}

void Renderer11::invalidateFramebufferSwizzles(gl::Framebuffer *framebuffer)
{
    for (unsigned int colorAttachment = 0; colorAttachment < gl::IMPLEMENTATION_MAX_DRAW_BUFFERS; colorAttachment++)
    {
        gl::Renderbuffer *colorbuffer = framebuffer->getColorbuffer(colorAttachment);
        if (colorbuffer)
        {
            invalidateRenderbufferSwizzles(colorbuffer, framebuffer->getColorbufferMipLevel(colorAttachment));
        }
    }

    gl::Renderbuffer *depthBuffer = framebuffer->getDepthbuffer();
    if (depthBuffer)
    {
        invalidateRenderbufferSwizzles(depthBuffer, framebuffer->getDepthbufferMipLevel());
    }

    gl::Renderbuffer *stencilBuffer = framebuffer->getStencilbuffer();
    if (stencilBuffer)
    {
        invalidateRenderbufferSwizzles(stencilBuffer, framebuffer->getStencilbufferMipLevel());
    }
}

bool Renderer11::getLUID(LUID *adapterLuid) const
{
    adapterLuid->HighPart = 0;
    adapterLuid->LowPart = 0;

    if (!mDxgiAdapter)
    {
        return false;
    }

    DXGI_ADAPTER_DESC adapterDesc;
    if (FAILED(mDxgiAdapter->GetDesc(&adapterDesc)))
    {
        return false;
    }

    *adapterLuid = adapterDesc.AdapterLuid;
    return true;
}

GLenum Renderer11::getNativeTextureFormat(GLenum internalFormat) const
{
    int clientVersion = getCurrentClientVersion();
    return d3d11_gl::GetInternalFormat(gl_d3d11::GetTexFormat(internalFormat, clientVersion), clientVersion);
}

Renderer11::MultisampleSupportInfo Renderer11::getMultisampleSupportInfo(DXGI_FORMAT format)
{
    MultisampleSupportInfo supportInfo = { 0 };

    UINT formatSupport;
    HRESULT result;

    result = mDevice->CheckFormatSupport(format, &formatSupport);
    if (SUCCEEDED(result) && (formatSupport & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET))
    {
        for (unsigned int i = 1; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; i++)
        {
            result = mDevice->CheckMultisampleQualityLevels(format, i, &supportInfo.qualityLevels[i - 1]);
            if (SUCCEEDED(result) && supportInfo.qualityLevels[i - 1] > 0)
            {
                supportInfo.maxSupportedSamples = std::max(supportInfo.maxSupportedSamples, i);
            }
            else
            {
                supportInfo.qualityLevels[i - 1] = 0;
            }
        }
    }

    return supportInfo;
}

}
