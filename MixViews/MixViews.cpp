/*
OFX MixViews plugin.
Mix two views together.

Copyright (C) 2013 INRIA
Author Frederic Devernay frederic.devernay@inria.fr

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

  Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

  Neither the name of the {organization} nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

INRIA
Domaine de Voluceau
Rocquencourt - B.P. 105
78153 Le Chesnay Cedex - France


The skeleton for this source file is from:
OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.

Copyright (C) 2004-2005 The Open Effects Association Ltd
Author Bruno Nicoletti bruno@thefoundry.co.uk

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.
* Neither the name The Open Effects Association Ltd, nor the names of its 
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The Open Effects Association Ltd
1 Wardour St
London W1D 6PA
England

*/

#include "MixViews.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "../include/ofxsProcessing.H"


// Base class for the RGBA and the Alpha processor
class MixViewsBase : public OFX::ImageProcessor {
protected :
  OFX::Image *_srcLeftImg;
  OFX::Image *_srcRightImg;
  double _mix;
public :
  /** @brief no arg ctor */
  MixViewsBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcLeftImg(0)
    , _srcRightImg(0)
  {        
  }

  /** @brief set the left src image */
  void setSrcLeftImg(OFX::Image *v) {_srcLeftImg = v;}

  /** @brief set the right src image */
  void setSrcRightImg(OFX::Image *v) {_srcRightImg = v;}

  /** @brief set the mix factor of right view */
  void setMix(float v) {_mix = v;}
};

// template to do the RGBA processing
template <class PIX, int nComponents, int max>
class ViewMixer : public MixViewsBase {
public :
  // ctor
  ViewMixer(OFX::ImageEffect &instance) 
    : MixViewsBase(instance)
  {}

  // and do some processing
  void multiThreadProcessImages(OfxRectI procWindow)
  {
    for(int y = procWindow.y1; y < procWindow.y2; y++) {
      if(_effect.abort()) break;

      PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

      for(int x = procWindow.x1; x < procWindow.x2; x++) {
        PIX *srcLeftPix = (PIX *)(_srcLeftImg ? _srcLeftImg->getPixelAddress(x, y) : 0);
        PIX *srcRightPix = (PIX *)(_srcRightImg ? _srcRightImg->getPixelAddress(x, y) : 0);

        for(int c = 0; c < nComponents; c++) {
          dstPix[c] = (srcLeftPix ? srcLeftPix[c] : 0)*(1-_mix) + (srcRightPix ? srcRightPix[c] : 0)*_mix;
        }

        // increment the dst pixel
        dstPix += nComponents;
      }
    }
  }
};

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class MixViewsPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

  OFX::DoubleParam *mix_;

public :
  /** @brief ctor */
  MixViewsPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , mix_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);
    mix_  = fetchDoubleParam("mix");
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* set up and run a processor */
  void setupAndProcess(MixViewsBase &, const OFX::RenderArguments &args);
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
MixViewsPlugin::setupAndProcess(MixViewsBase &processor, const OFX::RenderArguments &args)
{
  // get a dst image
  std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  if (!dst.get()) {
    OFX::throwSuiteStatusException(kOfxStatFailed);
  }
  if (dst->getRenderScale().x != args.renderScale.x ||
      dst->getRenderScale().y != args.renderScale.y ||
      dst->getField() == args.fieldToRender) {
    setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
    OFX::throwSuiteStatusException(kOfxStatFailed);
  }
  OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

  // fetch main input image
  std::auto_ptr<OFX::Image> srcLeft(srcClip_->fetchStereoscopicImage(args.time,0));
  std::auto_ptr<OFX::Image> srcRight(srcClip_->fetchStereoscopicImage(args.time,1));

  // make sure bit depths are sane
  if(srcLeft.get()) {
    OFX::BitDepthEnum    srcBitDepth      = srcLeft->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcLeft->getPixelComponents();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
      throw int(1); // HACK!! need to throw an sensible exception here!
  }
  if(srcRight.get()) {
    OFX::BitDepthEnum    srcBitDepth      = srcRight->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcRight->getPixelComponents();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
      throw int(1); // HACK!! need to throw an sensible exception here!
  }

  double mix = mix_->getValueAtTime(args.time);

  // set the images
  processor.setDstImg(dst.get());
  processor.setSrcLeftImg(srcLeft.get());
  processor.setSrcRightImg(srcRight.get());

  // set the render window
  processor.setRenderWindow(args.renderWindow);

  // set the parameters
  processor.setMix(mix);

  // Call the base class process member, this will call the derived templated process code
  processor.process();
}

// the overridden render function
void
MixViewsPlugin::render(const OFX::RenderArguments &args)
{
  if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    OFX::throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
  }

  // instantiate the render code based on the pixel depth of the dst clip
  OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

  // do the rendering
  if(dstComponents == OFX::ePixelComponentRGBA) {
    switch(dstBitDepth) {
      case OFX::eBitDepthUByte : {      
        ViewMixer<unsigned char, 4, 255> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ViewMixer<unsigned short, 4, 65535> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ViewMixer<float, 4, 1> fred(*this);
        setupAndProcess(fred, args);
      }
        break;
      default :
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
  } else if (dstComponents == OFX::ePixelComponentRGB) {
    switch(dstBitDepth) {
      case OFX::eBitDepthUByte : {      
        ViewMixer<unsigned char, 3, 255> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ViewMixer<unsigned short, 3, 65535> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ViewMixer<float, 3, 1> fred(*this);
        setupAndProcess(fred, args);
      }
        break;
      default :
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
  } else {
    assert(dstComponents == OFX::ePixelComponentAlpha);
    switch(dstBitDepth) {
      case OFX::eBitDepthUByte : {      
        ViewMixer<unsigned char, 1, 255> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ViewMixer<unsigned short, 1, 65535> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ViewMixer<float, 1, 1> fred(*this);
        setupAndProcess(fred, args);
      }
        break;
      default :
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
  }
}


using namespace OFX;
void MixViewsPluginFactory::load()
{
  // we can't be used on hosts that don't support the stereoscopic suite
  // returning an error here causes a blank menu entry in Nuke
  //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
  //    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
  //}
}

void MixViewsPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("MixViewsOFX", "MixViewsOFX", "MixViewsOFX");
  desc.setPluginGrouping("Views/Stereo");
  desc.setPluginDescription("Mix two views together.");

  // add the supported contexts, only filter at the moment
  desc.addSupportedContext(eContextFilter);

  // add supported pixel depths
  desc.addSupportedBitDepth(eBitDepthUByte);
  desc.addSupportedBitDepth(eBitDepthUShort);
  desc.addSupportedBitDepth(eBitDepthFloat);

  // set a few flags
  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setSupportsMultiResolution(true);
  desc.setSupportsTiles(true);
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);

  // returning an error here crashes Nuke
  //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
  //  throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
  //}
}

void MixViewsPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
  if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
  }

  // Source clip only in the filter context
  // create the mandated source clip
  ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(ePixelComponentRGB);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->addSupportedComponent(ePixelComponentAlpha);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(true);
  srcClip->setIsMask(false);

  // create the mandated output clip
  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGB);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->addSupportedComponent(ePixelComponentAlpha);
  dstClip->setSupportsTiles(true);

  // make some pages and to things in 
  PageParamDescriptor *page = desc.definePageParam("Controls");

  DoubleParamDescriptor *mix = desc.defineDoubleParam("mix");
  mix->setLabels("mix", "mix", "mix");
  mix->setScriptName("mix");
  mix->setHint("Mix factor for the right view");
  mix->setDefault(0.);
  mix->setRange(0., 1.);
  mix->setIncrement(0.01);
  mix->setDisplayRange(0., 1.);
  mix->setDoubleType(eDoubleTypeScale);
  mix->setAnimates(true);

  page->addChild(*mix);
}

OFX::ImageEffect* MixViewsPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
  return new MixViewsPlugin(handle);
}
