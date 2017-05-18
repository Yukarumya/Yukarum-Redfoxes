/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "QueuedInput.h"

#include "AsyncPanZoomController.h"
#include "InputBlockState.h"
#include "InputData.h"
#include "OverscrollHandoffState.h"

namespace mozilla {
namespace layers {

QueuedInput::QueuedInput(const MultiTouchInput& aInput, TouchBlockState& aBlock)
  : mInput(MakeUnique<MultiTouchInput>(aInput))
  , mBlock(&aBlock)
{
}

QueuedInput::QueuedInput(const ScrollWheelInput& aInput, WheelBlockState& aBlock)
  : mInput(MakeUnique<ScrollWheelInput>(aInput))
  , mBlock(&aBlock)
{
}

QueuedInput::QueuedInput(const MouseInput& aInput, DragBlockState& aBlock)
  : mInput(MakeUnique<MouseInput>(aInput))
  , mBlock(&aBlock)
{
}

QueuedInput::QueuedInput(const PanGestureInput& aInput, PanGestureBlockState& aBlock)
  : mInput(MakeUnique<PanGestureInput>(aInput))
  , mBlock(&aBlock)
{
}

InputData*
QueuedInput::Input()
{
  return mInput.get();
}

CancelableBlockState*
QueuedInput::Block()
{
  return mBlock.get();
}

} // namespace layers
} // namespace mozilla
