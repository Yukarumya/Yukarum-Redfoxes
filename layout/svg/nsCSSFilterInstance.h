/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_CSSFILTERINSTANCE_H__
#define __NS_CSSFILTERINSTANCE_H__

#include "FilterSupport.h"
#include "gfxMatrix.h"
#include "gfxRect.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "nsColor.h"
#include "nsTArrayForwardDeclare.h"

struct nsStyleFilter;

/**
 * This class helps nsFilterInstance build its filter graph. It turns a CSS
 * filter function (e.g. blur(3px)) from the style system into a
 * FilterPrimitiveDescription connected to the filter graph.
 */
class nsCSSFilterInstance
{
  typedef mozilla::gfx::Color Color;
  typedef mozilla::gfx::FilterPrimitiveDescription FilterPrimitiveDescription;
  typedef mozilla::gfx::IntPoint IntPoint;
  typedef mozilla::gfx::PrimitiveType PrimitiveType;
  typedef mozilla::gfx::Size Size;

public:
  /**
   * @param aFilter The CSS filter from the style system. This class stores
   *   aFilter by reference, so callers should avoid modifying or deleting
   *   aFilter during the lifetime of nsCSSFilterInstance.
   * @param aShadowFallbackColor The color that should be used for
   *   drop-shadow() filters that don't specify a shadow color.
   * @param aTargetBoundsInFilterSpace The pre-filter visual overflow rect of
   *   the frame being filtered, in filter space.
   * @param aFrameSpaceInCSSPxToFilterSpaceTransform The transformation from
   *   the filtered element's frame space in CSS pixels to filter space.
   */
  nsCSSFilterInstance(const nsStyleFilter& aFilter,
                      nscolor aShadowFallbackColor,
                      const nsIntRect& aTargetBoundsInFilterSpace,
                      const gfxMatrix& aFrameSpaceInCSSPxToFilterSpaceTransform);

  /**
   * Creates at least one new FilterPrimitiveDescription based on the filter
   * from the style system. Appends the new FilterPrimitiveDescription(s) to the
   * aPrimitiveDescrs list.
   * aInputIsTainted describes whether the input to this filter is tainted, i.e.
   * whether it contains security-sensitive content. This is needed to propagate
   * taintedness to the FilterPrimitive that take tainted inputs. Something being
   * tainted means that it contains security sensitive content.
   * The input to this filter is the previous filter's output, i.e. the last
   * element in aPrimitiveDescrs, or the SourceGraphic input if this is the first
   * filter in the filter chain.
   */
  nsresult BuildPrimitives(nsTArray<FilterPrimitiveDescription>& aPrimitiveDescrs,
                           bool aInputIsTainted);

private:
  /**
   * Returns a new FilterPrimitiveDescription with its basic properties set up.
   * See the comment above BuildPrimitives for the meaning of aInputIsTainted.
   */
  FilterPrimitiveDescription CreatePrimitiveDescription(PrimitiveType aType,
                                                        const nsTArray<FilterPrimitiveDescription>& aPrimitiveDescrs,
                                                        bool aInputIsTainted);

  /**
   * Sets aDescr's attributes using the style info in mFilter.
   */
  nsresult SetAttributesForBlur(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForBrightness(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForContrast(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForDropShadow(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForGrayscale(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForHueRotate(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForInvert(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForOpacity(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForSaturate(FilterPrimitiveDescription& aDescr);
  nsresult SetAttributesForSepia(FilterPrimitiveDescription& aDescr);

  /**
   * Returns the index of the last result in the aPrimitiveDescrs, which we'll
   * use as the input to this CSS filter.
   */
  int32_t GetLastResultIndex(const nsTArray<FilterPrimitiveDescription>& aPrimitiveDescrs);

  /**
   * Sets aDescr's filter region and primitive subregion to appropriate values
   * based on this CSS filter's input and its attributes. For example, a CSS
   * blur filter will have bounds equal to its input bounds, inflated by the
   * blur extents.
   */
  void SetBounds(FilterPrimitiveDescription& aDescr,
                 const nsTArray<FilterPrimitiveDescription>& aPrimitiveDescrs);

  /**
   * Converts an nscolor to a Color, suitable for use as a
   * FilterPrimitiveDescription attribute.
   */
  Color ToAttributeColor(nscolor aColor);

  /**
   * Converts a blur radius in frame space to filter space.
   */
  Size BlurRadiusToFilterSpace(nscoord aRadiusInFrameSpace);

  /**
   * Converts a point defined by a pair of nscoord x, y coordinates from frame
   * space to filter space.
   */
  IntPoint OffsetToFilterSpace(nscoord aXOffsetInFrameSpace,
                               nscoord aYOffsetInFrameSpace);

  /**
   * The CSS filter originally from the style system.
   */
  const nsStyleFilter& mFilter;

  /**
   * The color that should be used for drop-shadow() filters that don't
   * specify a shadow color.
   */
  nscolor mShadowFallbackColor;

  /**
   * The pre-filter overflow rect of the frame being filtered, in filter space.
   * Used for input bounds if this CSS filter is the first in the filter chain.
   */
  nsIntRect mTargetBoundsInFilterSpace;

  /**
   * The transformation from the filtered element's frame space in CSS pixels to
   * filter space. Used to transform style values to filter space.
   */
  gfxMatrix mFrameSpaceInCSSPxToFilterSpaceTransform;
};

#endif
