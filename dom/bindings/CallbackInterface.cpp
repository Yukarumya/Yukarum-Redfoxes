/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/CallbackInterface.h"
#include "jsapi.h"
#include "mozilla/dom/BindingUtils.h"
#include "nsPrintfCString.h"

namespace mozilla {
namespace dom {

bool
CallbackInterface::GetCallableProperty(JSContext* cx, JS::Handle<jsid> aPropId,
                                       JS::MutableHandle<JS::Value> aCallable)
{
  if (!JS_GetPropertyById(cx, CallbackKnownNotGray(), aPropId, aCallable)) {
    return false;
  }
  if (!aCallable.isObject() ||
      !JS::IsCallable(&aCallable.toObject())) {
    char* propName =
      JS_EncodeString(cx, JS_FORGET_STRING_FLATNESS(JSID_TO_FLAT_STRING(aPropId)));
    nsPrintfCString description("Property '%s'", propName);
    JS_free(cx, propName);
    ThrowErrorMessage(cx, MSG_NOT_CALLABLE, description.get());
    return false;
  }

  return true;
}

} // namespace dom
} // namespace mozilla
