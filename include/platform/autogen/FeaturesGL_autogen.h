// GENERATED FILE - DO NOT EDIT.
// Generated by gen_features.py using data from gl_features.json.
//
// Copyright 2022 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// FeaturesGL_autogen.h: angle::Features and workarounds for GL driver bugs and other issues.

#ifndef ANGLE_PLATFORM_AUTOGEN_FEATURESGL_H_
#define ANGLE_PLATFORM_AUTOGEN_FEATURESGL_H_

#include "platform/Feature.h"

namespace angle
{

struct FeaturesGL : FeatureSetBase
{
    FeaturesGL();
    ~FeaturesGL();

    FeatureInfo avoid1BitAlphaTextureFormats = {
        "avoid1BitAlphaTextureFormats",
        FeatureCategory::OpenGLWorkarounds,
        "Issue with 1-bit alpha framebuffer formats",
        &members,
    };

    FeatureInfo RGBA4IsNotSupportedForColorRendering = {
        "RGBA4IsNotSupportedForColorRendering",
        FeatureCategory::OpenGLWorkarounds,
        "GL_RGBA4 is not color renderable",
        &members,
    };

    FeatureInfo allowETCFormats = {
        "allowETCFormats",
        FeatureCategory::OpenGLWorkarounds,
        "Enable ETC2/EAC on desktop OpenGL",
        &members,
    };

    FeatureInfo allowAstcFormats = {
        "allowAstcFormats",
        FeatureCategory::OpenGLWorkarounds,
        "Enable ASTC on desktop OpenGL",
        &members,
    };

    FeatureInfo clearsWithGapsNeedFlush = {
        "clearsWithGapsNeedFlush",
        FeatureCategory::OpenGLWorkarounds,
        "Clearing an FBO with multiple attachments may need a flush to avoid race conditions",
        &members,
    };

    FeatureInfo doesSRGBClearsOnLinearFramebufferAttachments = {
        "doesSRGBClearsOnLinearFramebufferAttachments",
        FeatureCategory::OpenGLWorkarounds,
        "Issue clearing framebuffers with linear attachments when GL_FRAMEBUFFER_SRGB is enabled",
        &members,
    };

    FeatureInfo doWhileGLSLCausesGPUHang = {
        "doWhileGLSLCausesGPUHang",
        FeatureCategory::OpenGLWorkarounds,
        "Some GLSL constructs involving do-while loops cause GPU hangs",
        &members, "http://crbug.com/644669"
    };

    FeatureInfo vertexIDDoesNotIncludeBaseVertex = {
        "vertexIDDoesNotIncludeBaseVertex",
        FeatureCategory::OpenGLWorkarounds,
        "gl_VertexID in GLSL vertex shader doesn't include base vertex value",
        &members,
    };

    FeatureInfo finishDoesNotCauseQueriesToBeAvailable = {
        "finishDoesNotCauseQueriesToBeAvailable",
        FeatureCategory::OpenGLWorkarounds,
        "glFinish doesn't cause all queries to report available result",
        &members,
    };

    FeatureInfo alwaysCallUseProgramAfterLink = {
        "alwaysCallUseProgramAfterLink",
        FeatureCategory::OpenGLWorkarounds,
        "Always call useProgram after a successful link to avoid a driver bug",
        &members, "http://crbug.com/110263"
    };

    FeatureInfo unpackOverlappingRowsSeparatelyUnpackBuffer = {
        "unpackOverlappingRowsSeparatelyUnpackBuffer",
        FeatureCategory::OpenGLWorkarounds,
        "In the case of unpacking from a pixel unpack buffer, unpack overlapping rows row by row",
        &members,
    };

    FeatureInfo packOverlappingRowsSeparatelyPackBuffer = {
        "packOverlappingRowsSeparatelyPackBuffer",
        FeatureCategory::OpenGLWorkarounds,
        "In the case of packing to a pixel pack buffer, pack overlapping rows row by row",
        &members,
    };

    FeatureInfo initializeCurrentVertexAttributes = {
        "initializeCurrentVertexAttributes",
        FeatureCategory::OpenGLWorkarounds,
        "During initialization, assign the current vertex attributes to the spec-mandated defaults",
        &members,
    };

    FeatureInfo emulateAbsIntFunction = {
        "emulateAbsIntFunction",
        FeatureCategory::OpenGLWorkarounds,
        "abs(i) where i is an integer returns unexpected result",
        &members, "http://crbug.com/642227"
    };

    FeatureInfo addAndTrueToLoopCondition = {
        "addAndTrueToLoopCondition",
        FeatureCategory::OpenGLWorkarounds,
        "Calculation of loop conditions in for and while loop has bug",
        &members,
    };

    FeatureInfo unpackLastRowSeparatelyForPaddingInclusion = {
        "unpackLastRowSeparatelyForPaddingInclusion",
        FeatureCategory::OpenGLWorkarounds,
        "When uploading textures from an unpack buffer, some drivers count an extra row padding",
        &members, "http://anglebug.com/42260492"
    };

    FeatureInfo packLastRowSeparatelyForPaddingInclusion = {
        "packLastRowSeparatelyForPaddingInclusion",
        FeatureCategory::OpenGLWorkarounds,
        "When uploading textures from an pack buffer, some drivers count an extra row padding",
        &members, "http://anglebug.com/42260492"
    };

    FeatureInfo emulateIsnanFloat = {
        "emulateIsnanFloat",
        FeatureCategory::OpenGLWorkarounds,
        "Using isnan() on highp float will get wrong answer",
        &members, "http://crbug.com/650547"
    };

    FeatureInfo useUnusedBlocksWithStandardOrSharedLayout = {
        "useUnusedBlocksWithStandardOrSharedLayout",
        FeatureCategory::OpenGLWorkarounds,
        "Unused std140 or shared uniform blocks will be treated as inactive",
        &members,
    };

    FeatureInfo removeInvariantAndCentroidForESSL3 = {
        "removeInvariantAndCentroidForESSL3",
        FeatureCategory::OpenGLWorkarounds,
        "Fix spec difference between GLSL 4.1 or lower and ESSL3",
        &members,
    };

    FeatureInfo rewriteFloatUnaryMinusOperator = {
        "rewriteFloatUnaryMinusOperator",
        FeatureCategory::OpenGLWorkarounds,
        "Using '-<float>' will get wrong answer",
        &members, "http://crbug.com/308366"
    };

    FeatureInfo emulateAtan2Float = {
        "emulateAtan2Float",
        FeatureCategory::OpenGLWorkarounds,
        "atan(y, x) may return a wrong answer",
        &members, "http://crbug.com/672380"
    };

    FeatureInfo reapplyUBOBindingsAfterUsingBinaryProgram = {
        "reapplyUBOBindingsAfterUsingBinaryProgram",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers forget about UBO bindings when using program binaries",
        &members, "http://anglebug.com/42260591"
    };

    FeatureInfo emulateMaxVertexAttribStride = {
        "emulateMaxVertexAttribStride",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers return 0 when MAX_VERTEX_ATTRIB_STRIED queried",
        &members, "http://anglebug.com/42260722"
    };

    FeatureInfo dontInitializeUninitializedLocals = {
        "dontInitializeUninitializedLocals",
        FeatureCategory::OpenGLWorkarounds,
        "Initializing uninitialized locals caused odd behavior in a few WebGL 2 tests",
        &members, "http://anglebug.com/40096454"
    };

    FeatureInfo clampPointSize = {
        "clampPointSize",
        FeatureCategory::OpenGLWorkarounds,
        "The point size range reported from the API is inconsistent with the actual behavior",
        &members,
    };

    FeatureInfo dontUseLoopsToInitializeVariables = {
        "dontUseLoopsToInitializeVariables",
        FeatureCategory::OpenGLWorkarounds,
        "For loops used to initialize variables hit native GLSL compiler bugs",
        &members, "http://crbug.com/809422"
    };

    FeatureInfo clampFragDepth = {
        "clampFragDepth",
        FeatureCategory::OpenGLWorkarounds,
        "gl_FragDepth is not clamped correctly when rendering to a floating point depth buffer",
        &members,
    };

    FeatureInfo rewriteRepeatedAssignToSwizzled = {
        "rewriteRepeatedAssignToSwizzled",
        FeatureCategory::OpenGLWorkarounds,
        "Repeated assignment to swizzled values inside a "
        "GLSL user-defined function have incorrect results",
        &members,
    };

    FeatureInfo preTransformTextureCubeGradDerivatives = {
        "preTransformTextureCubeGradDerivatives",
        FeatureCategory::OpenGLWorkarounds,
        "Apply a vendor-specific transformation to explicit cubemap derivatives",
        &members,
    };

    FeatureInfo disableBlendFuncExtended = {
        "disableBlendFuncExtended",
        FeatureCategory::OpenGLWorkarounds,
        "ARB_blend_func_extended does not pass the tests",
        &members, "http://anglebug.com/40644593"
    };

    FeatureInfo unsizedSRGBReadPixelsDoesntTransform = {
        "unsizedSRGBReadPixelsDoesntTransform",
        FeatureCategory::OpenGLWorkarounds,
        "Drivers returning raw sRGB values instead of linearized values when calling glReadPixels "
        "on unsized sRGB texture formats",
        &members, "http://crbug.com/550292 http://crbug.com/565179"
    };

    FeatureInfo queryCounterBitsGeneratesErrors = {
        "queryCounterBitsGeneratesErrors",
        FeatureCategory::OpenGLWorkarounds,
        "Drivers generate errors when querying the number of bits in timer queries",
        &members, "http://anglebug.com/42261713"
    };

    FeatureInfo limitWebglMaxTextureSizeTo4096 = {
        "limitWebglMaxTextureSizeTo4096",
        FeatureCategory::OpenGLWorkarounds,
        "Limit webgl max texture size to 4096 to avoid frequent "
        "out-of-memory errors",
        &members, "http://crbug.com/927470"
    };

    FeatureInfo limitWebglMaxTextureSizeTo8192 = {
        "limitWebglMaxTextureSizeTo8192",
        FeatureCategory::OpenGLWorkarounds,
        "Limit webgl max texture size to 8192 to avoid frequent "
        "out-of-memory errors",
        &members, "http://anglebug.com/42267045"
    };

    FeatureInfo limitMaxMSAASamplesTo4 = {
        "limitMaxMSAASamplesTo4",
        FeatureCategory::OpenGLWorkarounds,
        "Various rendering bugs have been observed when using higher MSAA counts",
        &members, "http://crbug.com/797243"
    };

    FeatureInfo allowClearForRobustResourceInit = {
        "allowClearForRobustResourceInit",
        FeatureCategory::OpenGLWorkarounds,
        "Using glClear for robust resource initialization is buggy on some drivers and leads to "
        "texture corruption. Default to data uploads except on MacOS where it is very slow.",
        &members, "https://crbug.com/848952 http://crbug.com/883276"
    };

    FeatureInfo clampArrayAccess = {
        "clampArrayAccess",
        FeatureCategory::OpenGLWorkarounds,
        "Clamp uniform array access to avoid reading invalid memory.",
        &members, "http://anglebug.com/40096539"
    };

    FeatureInfo resetTexImage2DBaseLevel = {
        "resetTexImage2DBaseLevel",
        FeatureCategory::OpenGLWorkarounds,
        "Reset texture base level before calling glTexImage2D to "
        "work around pixel comparison failure.",
        &members, "https://crbug.com/705865"
    };

    FeatureInfo clearToZeroOrOneBroken = {
        "clearToZeroOrOneBroken",
        FeatureCategory::OpenGLWorkarounds,
        "Clears when the clear color is all zeros or ones do not work.",
        &members, "https://crbug.com/710443"
    };

    FeatureInfo limitMax3dArrayTextureSizeTo1024 = {
        "limitMax3dArrayTextureSizeTo1024",
        FeatureCategory::OpenGLWorkarounds,
        "Limit max 3d texture size and max array texture layers to 1024 to avoid system hang",
        &members, "http://crbug.com/927470"
    };

    FeatureInfo adjustSrcDstRegionForBlitFramebuffer = {
        "adjustSrcDstRegionForBlitFramebuffer",
        FeatureCategory::OpenGLWorkarounds,
        "Many platforms have issues with blitFramebuffer when the parameters are large.",
        &members, "http://crbug.com/830046"
    };

    FeatureInfo clipSrcRegionForBlitFramebuffer = {
        "clipSrcRegionForBlitFramebuffer",
        FeatureCategory::OpenGLWorkarounds,
        "Issues with blitFramebuffer when the parameters don't match the framebuffer size.",
        &members, "http://crbug.com/830046"
    };

    FeatureInfo RGBDXT1TexturesSampleZeroAlpha = {
        "RGBDXT1TexturesSampleZeroAlpha",
        FeatureCategory::OpenGLWorkarounds,
        "Sampling BLACK texels from RGB DXT1 textures returns transparent black on Mac.",
        &members, "http://anglebug.com/42262386"
    };

    FeatureInfo unfoldShortCircuits = {
        "unfoldShortCircuits",
        FeatureCategory::OpenGLWorkarounds,
        "Mac incorrectly executes both sides of && and || expressions when they should "
        "short-circuit.",
        &members, "http://anglebug.com/42263407"
    };

    FeatureInfo emulatePrimitiveRestartFixedIndex = {
        "emulatePrimitiveRestartFixedIndex",
        FeatureCategory::OpenGLWorkarounds,
        "When GL_PRIMITIVE_RESTART_FIXED_INDEX is not available, emulate it with "
        "GL_PRIMITIVE_RESTART and glPrimitiveRestartIndex.",
        &members, "http://anglebug.com/40096648"
    };

    FeatureInfo setPrimitiveRestartFixedIndexForDrawArrays = {
        "setPrimitiveRestartFixedIndexForDrawArrays",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers discard vertex data in DrawArrays calls when the fixed primitive restart "
        "index is within the number of primitives being drawn.",
        &members, "http://anglebug.com/40096648"
    };

    FeatureInfo removeDynamicIndexingOfSwizzledVector = {
        "removeDynamicIndexingOfSwizzledVector",
        FeatureCategory::OpenGLWorkarounds,
        "Dynamic indexing of swizzled l-values doesn't work correctly on various platforms.",
        &members, "http://crbug.com/709351"
    };

    FeatureInfo preAddTexelFetchOffsets = {
        "preAddTexelFetchOffsets",
        FeatureCategory::OpenGLWorkarounds,
        "Intel Mac drivers mistakenly consider the parameter position of nagative vaule as invalid "
        "even if the sum of position and offset is in range, so we need to add workarounds by "
        "rewriting texelFetchOffset(sampler, position, lod, offset) into texelFetch(sampler, "
        "position + offset, lod).",
        &members, "http://crbug.com/642605"
    };

    FeatureInfo regenerateStructNames = {
        "regenerateStructNames",
        FeatureCategory::OpenGLWorkarounds,
        "All Mac drivers do not handle struct scopes correctly. This workaround overwrites a struct"
        "name with a unique prefix.",
        &members, "http://crbug.com/403957"
    };

    FeatureInfo readPixelsUsingImplementationColorReadFormatForNorm16 = {
        "readPixelsUsingImplementationColorReadFormatForNorm16",
        FeatureCategory::OpenGLWorkarounds,
        "Quite some OpenGL ES drivers don't implement readPixels for RGBA/UNSIGNED_SHORT from "
        "EXT_texture_norm16 correctly",
        &members, "http://anglebug.com/40096661"
    };

    FeatureInfo flushBeforeDeleteTextureIfCopiedTo = {
        "flushBeforeDeleteTextureIfCopiedTo",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers track CopyTex{Sub}Image texture dependencies incorrectly. Flush"
        " before glDeleteTextures in this case",
        &members, "http://anglebug.com/40644715"
    };

    FeatureInfo rewriteRowMajorMatrices = {
        "rewriteRowMajorMatrices",
        FeatureCategory::OpenGLWorkarounds,
        "Rewrite row major matrices in shaders as column major as a driver bug workaround",
        &members, "http://anglebug.com/40096480"
    };

    FeatureInfo disableDrawBuffersIndexed = {
        "disableDrawBuffersIndexed",
        FeatureCategory::OpenGLWorkarounds,
        "Disable OES_draw_buffers_indexed extension.",
        &members,
    };

    FeatureInfo disableSemaphoreFd = {
        "disableSemaphoreFd",
        FeatureCategory::OpenGLWorkarounds,
        "Disable GL_EXT_semaphore_fd extension",
        &members, "https://crbug.com/1046462"
    };

    FeatureInfo disableTimestampQueries = {
        "disableTimestampQueries",
        FeatureCategory::OpenGLWorkarounds,
        "Disable GL_EXT_disjoint_timer_query extension",
        &members, "https://crbug.com/811661"
    };

    FeatureInfo decodeEncodeSRGBForGenerateMipmap = {
        "decodeEncodeSRGBForGenerateMipmap",
        FeatureCategory::OpenGLWorkarounds,
        "Decode and encode before generateMipmap for srgb format textures.",
        &members, "http://anglebug.com/40644730"
    };

    FeatureInfo emulateCopyTexImage2D = {
        "emulateCopyTexImage2D",
        FeatureCategory::OpenGLWorkarounds,
        "Replace CopyTexImage2D with TexImage2D + CopyTexSubImage2D.",
        &members,
    };

    FeatureInfo emulateCopyTexImage2DFromRenderbuffers = {
        "emulateCopyTexImage2DFromRenderbuffers",
        FeatureCategory::OpenGLWorkarounds,
        "CopyTexImage2D spuriously returns errors on iOS when copying from renderbuffers.",
        &members, "https://anglebug.com/42263273"
    };

    FeatureInfo disableGPUSwitchingSupport = {
        "disableGPUSwitchingSupport",
        FeatureCategory::OpenGLWorkarounds,
        "Disable GPU switching support (use only the low-power GPU) on older MacBook Pros.",
        &members, "https://crbug.com/1091824"
    };

    FeatureInfo disableNativeParallelCompile = {
        "disableNativeParallelCompile",
        FeatureCategory::OpenGLWorkarounds,
        "Do not use native KHR_parallel_shader_compile even when available.",
        &members, "http://crbug.com/1094869"
    };

    FeatureInfo emulatePackSkipRowsAndPackSkipPixels = {
        "emulatePackSkipRowsAndPackSkipPixels",
        FeatureCategory::OpenGLWorkarounds,
        "GL_PACK_SKIP_ROWS and GL_PACK_SKIP_PIXELS are ignored in Apple's OpenGL driver.",
        &members, "https://anglebug.com/40096712"
    };

    FeatureInfo clampMscRate = {
        "clampMscRate",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers return bogus values for GetMscRate, so we clamp it to 30Hz",
        &members, "https://crbug.com/1042393"
    };

    FeatureInfo bindTransformFeedbackBufferBeforeBindBufferRange = {
        "bindTransformFeedbackBufferBeforeBindBufferRange",
        FeatureCategory::OpenGLWorkarounds,
        "Bind transform feedback buffers to the generic binding point before calling "
        "glBindBufferBase or glBindBufferRange.",
        &members, "https://anglebug.com/42263702"
    };

    FeatureInfo disableSyncControlSupport = {
        "disableSyncControlSupport",
        FeatureCategory::OpenGLWorkarounds,
        "Speculative fix for issues on Linux/Wayland where exposing GLX_OML_sync_control renders "
        "Chrome unusable",
        &members, "https://crbug.com/1137851"
    };

    FeatureInfo keepBufferShadowCopy = {
        "keepBufferShadowCopy",
        FeatureCategory::OpenGLWorkarounds,
        "Maintain a shadow copy of buffer data when the GL API does not permit reading data back.",
        &members,
    };

    FeatureInfo setZeroLevelBeforeGenerateMipmap = {
        "setZeroLevelBeforeGenerateMipmap",
        FeatureCategory::OpenGLWorkarounds,
        "glGenerateMipmap fails if the zero texture level is not set on some Mac drivers.",
        &members,
    };

    FeatureInfo promotePackedFormatsTo8BitPerChannel = {
        "promotePackedFormatsTo8BitPerChannel",
        FeatureCategory::OpenGLWorkarounds,
        "Packed color formats are buggy on Macs with AMD GPUs",
        &members, "http://anglebug.com/42264008"
    };

    FeatureInfo initFragmentOutputVariables = {
        "initFragmentOutputVariables",
        FeatureCategory::OpenGLWorkarounds,
        "No init gl_FragColor causes context lost",
        &members, "http://crbug.com/1171371"
    };

    FeatureInfo shiftInstancedArrayDataWithOffset = {
        "shiftInstancedArrayDataWithOffset",
        FeatureCategory::OpenGLWorkarounds,
        "glDrawArraysInstanced is buggy on certain new Mac Intel GPUs",
        &members, "http://crbug.com/1144207"
    };

    FeatureInfo syncVertexArraysToDefault = {
        "syncVertexArraysToDefault",
        FeatureCategory::OpenGLWorkarounds,
        "Only use the default VAO because of missing support or driver bugs",
        &members, "http://anglebug.com/40096758"
    };

    FeatureInfo sanitizeAMDGPURendererString = {
        "sanitizeAMDGPURendererString",
        FeatureCategory::OpenGLWorkarounds,
        "Strip precise kernel and DRM version information from amdgpu renderer strings.",
        &members, "http://crbug.com/1181193"
    };

    FeatureInfo unbindFBOBeforeSwitchingContext = {
        "unbindFBOBeforeSwitchingContext",
        FeatureCategory::OpenGLWorkarounds,
        "Imagination GL drivers are buggy with context switching.",
        &members, "http://crbug.com/1181193"
    };

    FeatureInfo flushOnFramebufferChange = {
        "flushOnFramebufferChange",
        FeatureCategory::OpenGLWorkarounds,
        "Switching framebuffers without a flush can lead to "
        "crashes on Intel 9th Generation GPU Macs.",
        &members, "http://crbug.com/1181068"
    };

    FeatureInfo disableMultisampledRenderToTexture = {
        "disableMultisampledRenderToTexture",
        FeatureCategory::OpenGLWorkarounds,
        "Many drivers have bugs when using GL_EXT_multisampled_render_to_texture",
        &members, "http://anglebug.com/40096530"
    };

    FeatureInfo uploadTextureDataInChunks = {
        "uploadTextureDataInChunks",
        FeatureCategory::OpenGLWorkarounds,
        "Upload texture data in <120kb chunks to work around Mac driver hangs and crashes.",
        &members, "http://crbug.com/1181068"
    };

    FeatureInfo emulateImmutableCompressedTexture3D = {
        "emulateImmutableCompressedTexture3D",
        FeatureCategory::OpenGLWorkarounds,
        "Use non-immutable texture allocation to work around a driver bug.",
        &members, "https://crbug.com/1060012"
    };

    FeatureInfo emulateRGB10 = {
        "emulateRGB10",
        FeatureCategory::OpenGLWorkarounds,
        "Emulate RGB10 support using RGB10_A2.",
        &members, "https://crbug.com/1300575"
    };

    FeatureInfo alwaysUnbindFramebufferTexture2D = {
        "alwaysUnbindFramebufferTexture2D",
        FeatureCategory::OpenGLWorkarounds,
        "Force unbind framebufferTexture2D before binding renderbuffer to work around driver bug.",
        &members, "https://anglebug.com/42264072"
    };

    FeatureInfo disableTextureClampToBorder = {
        "disableTextureClampToBorder",
        FeatureCategory::OpenGLWorkarounds,
        "Imagination devices generate INVALID_ENUM when setting the texture border color.",
        &members, "https://anglebug.com/42265877"
    };

    FeatureInfo passHighpToPackUnormSnormBuiltins = {
        "passHighpToPackUnormSnormBuiltins",
        FeatureCategory::OpenGLWorkarounds,
        "packUnorm4x8 fails on Pixel 4 if it is not passed a highp vec4.",
        &members, "http://anglebug.com/42265995"
    };

    FeatureInfo emulateClipDistanceState = {
        "emulateClipDistanceState",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers ignore GL_CLIP_DISTANCEi_EXT state.",
        &members,
    };

    FeatureInfo emulateClipOrigin = {
        "emulateClipOrigin",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers incorrectly apply GL_CLIP_ORIGIN_EXT state.",
        &members,
    };

    FeatureInfo bindCompleteFramebufferForTimerQueries = {
        "bindCompleteFramebufferForTimerQueries",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers require a complete framebuffer when beginQuery for TimeElapsed or"
        "Timestampis called.",
        &members, "https://crbug.com/1356053"
    };

    FeatureInfo disableBaseInstanceVertex = {
        "disableBaseInstanceVertex",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers have buggy implementations of glDraw*BaseVertex*.",
        &members, "http://anglebug.com/42266610"
    };

    FeatureInfo supportsFragmentShaderInterlockNV = {
        "supportsFragmentShaderInterlockNV",
        FeatureCategory::OpenGLFeatures,
        "Backend GL context supports NV_fragment_shader_interlock extension",
        &members, "http://anglebug.com/40096838"
    };

    FeatureInfo supportsFragmentShaderOrderingINTEL = {
        "supportsFragmentShaderOrderingINTEL",
        FeatureCategory::OpenGLFeatures,
        "Backend GL context supports GL_INTEL_fragment_shader_ordering extension",
        &members, "http://anglebug.com/40096838"
    };

    FeatureInfo supportsFragmentShaderInterlockARB = {
        "supportsFragmentShaderInterlockARB",
        FeatureCategory::OpenGLFeatures,
        "Backend GL context supports ARB_fragment_shader_interlock extension",
        &members, "http://anglebug.com/40096838"
    };

    FeatureInfo supportsShaderFramebufferFetchEXT = {
        "supportsShaderFramebufferFetchEXT",
        FeatureCategory::OpenGLFeatures,
        "Backend GL context supports EXT_shader_framebuffer_fetch extension",
        &members, "http://anglebug.com/40096838"
    };

    FeatureInfo supportsShaderFramebufferFetchNonCoherentEXT = {
        "supportsShaderFramebufferFetchNonCoherentEXT",
        FeatureCategory::OpenGLFeatures,
        "Backend GL context supports EXT_shader_framebuffer_fetch_non_coherent extension",
        &members, "http://anglebug.com/40096838"
    };

    FeatureInfo supportsShaderPixelLocalStorageEXT = {
        "supportsShaderPixelLocalStorageEXT",
        FeatureCategory::OpenGLFeatures,
        "Backend GL context supports EXT_shader_pixel_local_storage extension",
        &members, "http://anglebug.com/40096838"
    };

    FeatureInfo disableClipControl = {
        "disableClipControl",
        FeatureCategory::OpenGLFeatures,
        "Some devices genenerate errors when querying the clip control state",
        &members, "http://crbug.com/1434317"
    };

    FeatureInfo scalarizeVecAndMatConstructorArgs = {
        "scalarizeVecAndMatConstructorArgs",
        FeatureCategory::OpenGLWorkarounds,
        "Rewrite vec/mat constructors to work around driver bugs",
        &members, "http://crbug.com/1420130"
    };

    FeatureInfo ensureNonEmptyBufferIsBoundForDraw = {
        "ensureNonEmptyBufferIsBoundForDraw",
        FeatureCategory::OpenGLFeatures,
        "Apple OpenGL drivers crash when drawing with a zero-sized buffer bound using a non-zero divisor.",
        &members, "http://crbug.com/1456243"
    };

    FeatureInfo explicitFragmentLocations = {
        "explicitFragmentLocations",
        FeatureCategory::OpenGLWorkarounds,
        "Always write explicit location layout qualifiers for fragment outputs.",
        &members, "https://anglebug.com/42266740"
    };

    FeatureInfo disableRenderSnorm = {
        "disableRenderSnorm",
        FeatureCategory::OpenGLWorkarounds,
        "Disable EXT_render_snorm extension.",
        &members, "https://anglebug.com/42266745"
    };

    FeatureInfo disableTextureMirrorClampToEdge = {
        "disableTextureMirrorClampToEdge",
        FeatureCategory::OpenGLWorkarounds,
        "Disable EXT_texture_mirror_clamp_to_edge extension.",
        &members, "https://anglebug.com/42266748"
    };

    FeatureInfo resyncDepthRangeOnClipControl = {
        "resyncDepthRangeOnClipControl",
        FeatureCategory::OpenGLWorkarounds,
        "Resync depth range to apply clip control updates.",
        &members, "https://anglebug.com/42266811"
    };

    FeatureInfo corruptProgramBinaryForTesting = {
        "corruptProgramBinaryForTesting",
        FeatureCategory::OpenGLWorkarounds,
        "Corrupt the program binary retrieved from the driver for testing purposes.",
        &members, "https://anglebug.com/41488638"
    };

    FeatureInfo useIntermediateTextureForGenerateMipmap = {
        "useIntermediateTextureForGenerateMipmap",
        FeatureCategory::OpenGLWorkarounds,
        "Some drivers lose context when repeatedly generating mipmaps on textures that were used as framebuffers.",
        &members, "https://crbug.com/40279678"
    };

    FeatureInfo srgbBlendingBroken = {
        "srgbBlendingBroken",
        FeatureCategory::OpenGLWorkarounds,
        "SRGB blending does not appear to work correctly on the some Qualcomm devices. "
        "Writing to an SRGB framebuffer with GL_FRAMEBUFFER_SRGB enabled and "
        "then reading back returns the same value. Disabling GL_FRAMEBUFFER_SRGB "
        "will then convert in the wrong direction.",
        &members, "https://crbug.com/40488750"
    };

    FeatureInfo bgraTexImageFormatsBroken = {
        "bgraTexImageFormatsBroken",
        FeatureCategory::OpenGLWorkarounds,
        "BGRA formats do not appear to be accepted by some qualcomm despite the extension being exposed.",
        &members, "https://anglebug.com/40096376"
    };

};

inline FeaturesGL::FeaturesGL()  = default;
inline FeaturesGL::~FeaturesGL() = default;

}  // namespace angle

#endif  // ANGLE_PLATFORM_AUTOGEN_FEATURESGL_H_
