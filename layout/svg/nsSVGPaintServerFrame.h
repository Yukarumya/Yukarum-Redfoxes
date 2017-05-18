/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_SVGPAINTSERVERFRAME_H__
#define __NS_SVGPAINTSERVERFRAME_H__

#include "mozilla/Attributes.h"
#include "nsCOMPtr.h"
#include "nsFrame.h"
#include "nsIFrame.h"
#include "nsQueryFrame.h"
#include "nsSVGContainerFrame.h"
#include "nsSVGUtils.h"

namespace mozilla {
namespace gfx {
class DrawTarget;
} // namespace gfx
} // namespace mozilla

class gfxContext;
class gfxPattern;
class nsStyleContext;

struct gfxRect;

class nsSVGPaintServerFrame : public nsSVGContainerFrame
{
protected:
  typedef mozilla::gfx::DrawTarget DrawTarget;

  explicit nsSVGPaintServerFrame(nsStyleContext* aContext)
    : nsSVGContainerFrame(aContext)
  {
    AddStateBits(NS_FRAME_IS_NONDISPLAY);
  }

public:
  NS_DECL_ABSTRACT_FRAME(nsSVGPaintServerFrame)

  /**
   * Constructs a gfxPattern of the paint server rendering.
   *
   * @param aContextMatrix The transform matrix that is currently applied to
   *   the gfxContext that is being drawn to. This is needed by SVG patterns so
   *   that surfaces of the correct size can be created. (SVG gradients are
   *   vector based, so it's not used there.)
   */
  virtual already_AddRefed<gfxPattern>
    GetPaintServerPattern(nsIFrame *aSource,
                          const DrawTarget* aDrawTarget,
                          const gfxMatrix& aContextMatrix,
                          nsStyleSVGPaint nsStyleSVG::*aFillOrStroke,
                          float aOpacity,
                          const gfxRect *aOverrideBounds = nullptr) = 0;

  // nsIFrame methods:
  virtual void BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                const nsRect&           aDirtyRect,
                                const nsDisplayListSet& aLists) override {}

  virtual bool IsFrameOfType(uint32_t aFlags) const override
  {
    return nsSVGContainerFrame::IsFrameOfType(aFlags & ~nsIFrame::eSVGPaintServer);
  }
};

#endif // __NS_SVGPAINTSERVERFRAME_H__
