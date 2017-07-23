/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2017 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX TestWarp plugin.
 */

#if defined(OFX_SUPPORTS_OPENGLRENDER) || defined(HAVE_OSMESA) // at least one is required for this plugin

#include "TestWarp.h"

#include <cfloat> // DBL_MAX
#ifdef DEBUG
#include <iostream>
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxOpenGLRender.h"

using namespace OFX;

//OFXS_NAMESPACE_ANONYMOUS_ENTER // defines external classes

#define kPluginName "TestWarp"
#define kPluginGrouping "Other/Test"
#define kPluginDescription \
    "Test mesh-based image warping using OpenGL."

#define kPluginIdentifier "net.sf.openfx.TestWarp"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs true
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamFrom "from"
#define kParamFromLabel "From", "Point in the source image to warp from."

#define kParamTo "to"
#define kParamToLabel "To", "Point in the target image to warp to."

#define kParamDirection "direction"
#define kParamDirectionLabel "Direction", "Warp direction."
#define kParamDirectionOptionForward "Forward", "Forward warp: deform the mesh vertices.", "forward"
#define kParamDirectionOptionBackward "Backward", "Backward warp: deform the texture coordinates.", "backward"

#define kParamMipmap "mipmap"
#define kParamMipmapLabel "Mipmap"
#define kParamMipmapHint "Use mipmapping (available only with CPU rendering)"

#define kParamAnisotropic "anisotropic"
#define kParamAnisotropicLabel "Anisotropic"
#define kParamAnisotropicHint "Use anisotropic texture filtering. Available with GPU if supported (check for the presence of the GL_EXT_texture_filter_anisotropic extension in the Renderer Info) and with \"softpipe\" CPU driver."

#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
#define kParamEnableGPU "enableGPU"
#define kParamEnableGPULabel "Enable GPU Render"
#define kParamEnableGPUHint \
    "Enable GPU-based OpenGL render.\n" \
    "If the checkbox is checked but is not enabled (i.e. it cannot be unchecked), GPU render can not be enabled or disabled from the plugin and is probably part of the host options.\n" \
    "If the checkbox is not checked and is not enabled (i.e. it cannot be checked), GPU render is not available on this host."
#endif

#ifdef HAVE_OSMESA
#define kParamCPUDriver "cpuDriver"
#define kParamCPUDriverLabel "CPU Driver"
#define kParamCPUDriverHint "Driver for CPU rendering. May be \"softpipe\" (slower, has GL_EXT_texture_filter_anisotropic GL_ARB_texture_query_lod GL_ARB_pipeline_statistics_query), \"llvmpipe\" (faster, has GL_ARB_buffer_storage GL_EXT_polygon_offset_clamp) or \"swr\" (OpenSWR, not always available)."
#define kParamCPUDriverOptionSoftPipe "softpipe"
#define kParamCPUDriverOptionLLVMPipe "llvmpipe"
#define kParamCPUDriverOptionSWR "swr"
#define kParamCPUDriverDefault TestWarpPlugin::eCPUDriverLLVMPipe
#endif

#define kParamRendererInfo "rendererInfo"
#define kParamRendererInfoLabel "Renderer Info..."
#define kParamRendererInfoHint "Retrieve information about the current OpenGL renderer."

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */

TestWarpPlugin::TestWarpPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(NULL)
    , _srcClip(NULL)
    , _from(NULL)
    , _to(NULL)
    , _direction(NULL)
    , _mipmap(NULL)
    , _anisotropic(NULL)
    , _enableGPU(NULL)
    , _cpuDriver(NULL)
    , _openGLContextData()
    , _openGLContextAttached(false)
{
    try {
        _rendererInfoMutex.reset(new Mutex);
#if defined(HAVE_OSMESA)
        _osmesaMutex.reset(new Mutex);
#endif
    } catch (const std::exception& e) {
#      ifdef DEBUG
        std::cout << "ERROR in createInstance(): Multithread::Mutex creation returned " << e.what() << std::endl;
#      endif
    }

    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                         _dstClip->getPixelComponents() == ePixelComponentAlpha) );
    _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert( (!_srcClip && getContext() == eContextGenerator) ||
            ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGBA ||
                           _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );

    _from = fetchDouble2DParam(kParamFrom);
    _to = fetchDouble2DParam(kParamTo);
    _direction = fetchChoiceParam(kParamDirection);
    _mipmap = fetchBooleanParam(kParamMipmap);
    _anisotropic = fetchBooleanParam(kParamAnisotropic);
    assert(_mipmap && _anisotropic);
#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
    _enableGPU = fetchBooleanParam(kParamEnableGPU);
    assert(_enableGPU);
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        _enableGPU->setEnabled(false);
    }
    setSupportsOpenGLRender(true);
#endif
#if defined(HAVE_OSMESA)
    if ( OSMesaDriverSelectable() ) {
        _cpuDriver = fetchChoiceParam(kParamCPUDriver);
    }
    initMesa();
#endif

    // honor kParamDefaultsNormalised
    if ( paramExists(kParamDefaultsNormalised) ) {
        // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
        // handle these ourselves!
        BooleanParam* param = fetchBooleanParam(kParamDefaultsNormalised);
        assert(param);
        bool normalised = param->getValue();
        if (normalised) {
            OfxPointD size = getProjectExtent();
            OfxPointD origin = getProjectOffset();
            OfxPointD p;
            // we must denormalise all parameters for which setDefaultCoordinateSystem(eCoordinatesNormalised) couldn't be done
            beginEditBlock(kParamDefaultsNormalised);
            p = _to->getValue();
            _to->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
            p = _from->getValue();
            _from->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
            param->setValue(false);
            endEditBlock();
        }
    }
}

TestWarpPlugin::~TestWarpPlugin()
{
#if defined(HAVE_OSMESA)
    exitMesa();
#endif
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// the overridden render function
void
TestWarpPlugin::render(const RenderArguments &args)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

    bool openGLRender = false;
#if defined(OFX_SUPPORTS_OPENGLRENDER)
    openGLRender = args.openGLEnabled;

    // do the rendering
    if (openGLRender) {
        return renderGL(args);
    }
#endif
#ifdef HAVE_OSMESA
    if (!openGLRender) {
        return renderMesa(args);
    }
#endif // HAVE_OSMESA
    throwSuiteStatusException(kOfxStatFailed);
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
TestWarpPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                       RegionOfInterestSetter &rois)
{
    const double time = args.time;

    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    // ask for full RoD of srcClip
    const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
    rois.setRegionOfInterest(*_srcClip, srcRod);
}

// overriding getRegionOfDefinition is necessary to tell the host that we do not support render scale
bool
TestWarpPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD & rod)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    // use the default RoD
    //return false;

    // use the project RoD
    //OfxPointD projectExtent = getProjectExtent();
    OfxPointD projectSize = getProjectSize();
    OfxPointD projectOffset = getProjectOffset();
    rod.x1 = projectOffset.x;
    rod.y1 = projectOffset.y;
    rod.x2 = projectOffset.x + projectSize.x;
    rod.y2 = projectOffset.y + projectSize.y;

    return true;
}

void
TestWarpPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // We have to do this because the processing code does not support varying components for srcClip and dstClip
    // (The OFX spec doesn't state a default value for this)
    clipPreferences.setClipComponents( *_dstClip, _srcClip->getPixelComponents() );

    clipPreferences.setPixelAspectRatio( *_dstClip, getProjectPixelAspectRatio() );
}

void
TestWarpPlugin::changedParam(const InstanceChangedArgs &args,
                               const std::string &paramName)
{
    if (paramName == kParamRendererInfo) {
        std::string message;
        {
            AutoMutex lock( _rendererInfoMutex.get() );
            message = _rendererInfo;
        }
        if ( message.empty() ) {
            sendMessage(Message::eMessageMessage, "", "OpenGL renderer info not yet available.\n"
                        "Please execute at least one image render and try again.");
        } else {
            sendMessage(Message::eMessageMessage, "", message);
        }
#if defined(HAVE_OSMESA)
    } else if (paramName == kParamEnableGPU) {
        setSupportsOpenGLRender( _enableGPU->getValueAtTime(args.time) );
        {
            AutoMutex lock( _rendererInfoMutex.get() );
            _rendererInfo.clear();
        }
    } else if (paramName == kParamCPUDriver) {
        {
            AutoMutex lock( _rendererInfoMutex.get() );
            _rendererInfo.clear();
        }
#endif
    }
} // TestWarpPlugin::changedParam

mDeclarePluginFactory(TestWarpPluginFactory, {ofxsThreadSuiteCheck();}, {});
#if 0
void
TestWarpPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    //const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    //if (!gHostDescription.supportsOpenGLRender) {
    //    throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    //}
    //#endif
}
#endif

void
TestWarpPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // returning an error here crashes Nuke
    //#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    //const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    //if (!gHostDescription.supportsOpenGLRender) {
    //    throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    //}
    //#endif

    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    // We can render both fields in a fielded images in one hit if there is no animation
    // So set the flag that allows us to do this
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    // say we can support multiple pixel depths and let the clip preferences action deal with it all.
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    // we support OpenGL rendering (could also say "needed" here)
#ifdef OFX_SUPPORTS_OPENGLRENDER
#ifdef HAVE_OSMESA
    desc.setSupportsOpenGLRender(true);
#else
    desc.setNeedsOpenGLRender(true);
    desc.setSupportsRenderQuality(true);

    /*
     * If a host supports OpenGL rendering then it flags this with the string
     * property ::kOfxImageEffectOpenGLRenderSupported on its descriptor property
     * set. Effects that cannot run without OpenGL support should examine this in
     * ::kOfxActionDescribe action and return a ::kOfxStatErrMissingHostFeature
     * status flag if it is not set to "true".
     */
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        throwSuiteStatusException(kOfxStatErrMissingHostFeature);
    }
#endif
#endif

    desc.setRenderThreadSafety(kRenderThreadSafety);
} // TestWarpPluginFactory::describe

void
TestWarpPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum /*context*/)
{
#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    }
#endif

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamFrom);
        param->setLabelAndHint(kParamFromLabel);
        param->setAnimates(true);
        param->setIncrement(1.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(-10000, -10000, 10000, 10000);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setUseHostNativeOverlayHandle(true);
        // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
        if ( param->supportsDefaultCoordinateSystem() ) {
            param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
        } else {
            gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
        }
        param->setDefault(0.5, 0.5);
        param->setDimensionLabels("x", "y");
        //if (group) {
        //    param->setParent(*group);
        //}
        if (page) {
            page->addChild(*param);
        }
    }

    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamTo);
        param->setLabelAndHint(kParamToLabel);
        param->setAnimates(true);
        param->setIncrement(1.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(-10000, -10000, 10000, 10000);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setUseHostNativeOverlayHandle(true);
        // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
        if ( param->supportsDefaultCoordinateSystem() ) {
            param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
        } else {
            gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
        }
        param->setDefault(0.5, 0.5);
        param->setDimensionLabels("x", "y");
        //if (group) {
        //    param->setParent(*group);
        //}
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDirection);
        param->setLabelAndHint(kParamDirectionLabel);
        param->appendOption(kParamDirectionOptionForward);
        param->appendOption(kParamDirectionOptionBackward);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMipmap);
        param->setLabel(kParamMipmapLabel);
        param->setHint(kParamMipmapHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAnisotropic);
        param->setLabel(kParamAnisotropicLabel);
        param->setHint(kParamAnisotropicHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamEnableGPU);
        param->setLabel(kParamEnableGPULabel);
        param->setHint(kParamEnableGPUHint);
        const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
        // Resolve advertises OpenGL support in its host description, but never calls render with OpenGL enabled
        if ( gHostDescription.supportsOpenGLRender && (gHostDescription.hostName.compare(0, 14, "DaVinciResolve") != 0) ) {
            param->setDefault(true);
            if (gHostDescription.APIVersionMajor * 100 + gHostDescription.APIVersionMinor < 104) {
                // Switching OpenGL render from the plugin was introduced in OFX 1.4
                param->setEnabled(false);
            }
        } else {
            param->setDefault(false);
            param->setEnabled(false);
        }

        if (page) {
            page->addChild(*param);
        }
    }
#endif
#if defined(HAVE_OSMESA)
    if ( TestWarpPlugin::OSMesaDriverSelectable() ) {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamCPUDriver);
        param->setLabel(kParamCPUDriverLabel);
        param->setHint(kParamCPUDriverHint);
        assert(param->getNOptions() == TestWarpPlugin::eCPUDriverSoftPipe);
        param->appendOption(kParamCPUDriverOptionSoftPipe);
        assert(param->getNOptions() == TestWarpPlugin::eCPUDriverLLVMPipe);
        param->appendOption(kParamCPUDriverOptionLLVMPipe);
        assert(param->getNOptions() == TestWarpPlugin::eCPUDriverSWR);
        param->appendOption(kParamCPUDriverOptionSWR);
        param->setDefault(kParamCPUDriverDefault);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

#endif

    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamRendererInfo);
        param->setLabel(kParamRendererInfoLabel);
        param->setHint(kParamRendererInfoHint);
        if (page) {
            page->addChild(*param);
        }
    }

    // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
    if (!gHostSupportsDefaultCoordinateSystem) {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamDefaultsNormalised);
        param->setDefault(true);
        param->setEvaluateOnChange(false);
        param->setIsSecretAndDisabled(true);
        param->setIsPersistent(true);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // TestWarpPluginFactory::describeInContext

ImageEffect*
TestWarpPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new TestWarpPlugin(handle);
}

static TestWarpPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

#endif // OFX_SUPPORTS_OPENGLRENDER