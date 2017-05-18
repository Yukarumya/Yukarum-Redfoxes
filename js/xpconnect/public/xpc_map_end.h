/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=4 et sw=4 tw=99: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// If you include this file you must also include xpc_make_class.h at the top
// of the file doing the including.

#ifndef XPC_MAP_CLASSNAME
#error "Must #define XPC_MAP_CLASSNAME before #including xpc_map_end.h"
#endif

#ifndef XPC_MAP_QUOTED_CLASSNAME
#error "Must #define XPC_MAP_QUOTED_CLASSNAME before #including xpc_map_end.h"
#endif

#ifndef XPC_MAP_FLAGS
#error "Must #define XPC_MAP_FLAGS before #including xpc_map_end.h"
#endif

#include "js/Id.h"

/**************************************************************/

NS_IMETHODIMP XPC_MAP_CLASSNAME::GetClassName(char * *aClassName)
{
    static const char sName[] = XPC_MAP_QUOTED_CLASSNAME;
    *aClassName = (char*) nsMemory::Clone(sName, sizeof(sName));
    return NS_OK;
}

/**************************************************************/

// virtual
uint32_t
XPC_MAP_CLASSNAME::GetScriptableFlags()
{
    return (XPC_MAP_FLAGS);
}

// virtual
const js::Class*
XPC_MAP_CLASSNAME::GetClass()
{
    static const js::ClassOps classOps =
        XPC_MAKE_CLASS_OPS(GetScriptableFlags());
    static const js::Class klass =
        XPC_MAKE_CLASS(XPC_MAP_QUOTED_CLASSNAME, GetScriptableFlags(),
                       &classOps);
    return &klass;
}

// virtual
const JSClass*
XPC_MAP_CLASSNAME::GetJSClass()
{
    return Jsvalify(GetClass());
}

/**************************************************************/

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_PRECREATE)
NS_IMETHODIMP XPC_MAP_CLASSNAME::PreCreate(nsISupports* nativeObj, JSContext * cx, JSObject * globalObj, JSObject * *parentObj)
    {NS_ERROR("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_GETPROPERTY)
NS_IMETHODIMP XPC_MAP_CLASSNAME::GetProperty(nsIXPConnectWrappedNative* wrapper, JSContext * cx, JSObject * obj, jsid id, JS::Value * vp, bool* _retval)
    {NS_WARNING("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_SETPROPERTY)
NS_IMETHODIMP XPC_MAP_CLASSNAME::SetProperty(nsIXPConnectWrappedNative* wrapper, JSContext * cx, JSObject * obj, jsid id, JS::Value * vp, bool* _retval)
    {NS_WARNING("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_NEWENUMERATE)
NS_IMETHODIMP XPC_MAP_CLASSNAME::NewEnumerate(nsIXPConnectWrappedNative* wrapper, JSContext * cx, JSObject * obj, JS::AutoIdVector& properties, bool* _retval)
    {NS_ERROR("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_ENUMERATE)
NS_IMETHODIMP XPC_MAP_CLASSNAME::Enumerate(nsIXPConnectWrappedNative* wrapper, JSContext * cx, JSObject * obj, bool* _retval)
    {NS_ERROR("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_RESOLVE)
NS_IMETHODIMP XPC_MAP_CLASSNAME::Resolve(nsIXPConnectWrappedNative* wrapper, JSContext * cx, JSObject * obj, jsid id, bool* resolvedp, bool* _retval)
    {NS_ERROR("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_FINALIZE)
NS_IMETHODIMP XPC_MAP_CLASSNAME::Finalize(nsIXPConnectWrappedNative* wrapper, JSFreeOp * fop, JSObject * obj)
    {NS_ERROR("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_CALL)
NS_IMETHODIMP XPC_MAP_CLASSNAME::Call(nsIXPConnectWrappedNative* wrapper, JSContext * cx, JSObject * obj, const JS::CallArgs& args, bool* _retval)
    {NS_ERROR("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_CONSTRUCT)
NS_IMETHODIMP XPC_MAP_CLASSNAME::Construct(nsIXPConnectWrappedNative* wrapper, JSContext * cx, JSObject * obj, const JS::CallArgs& args, bool* _retval)
    {NS_ERROR("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

#if !((XPC_MAP_FLAGS) & XPC_SCRIPTABLE_WANT_HASINSTANCE)
NS_IMETHODIMP XPC_MAP_CLASSNAME::HasInstance(nsIXPConnectWrappedNative* wrapper, JSContext * cx, JSObject * obj, JS::HandleValue val, bool* bp, bool* _retval)
    {NS_ERROR("never called"); return NS_ERROR_NOT_IMPLEMENTED;}
#endif

NS_IMETHODIMP XPC_MAP_CLASSNAME::PostCreatePrototype(JSContext* cx, JSObject* proto)
    {return NS_OK;}

/**************************************************************/

#undef XPC_MAP_CLASSNAME
#undef XPC_MAP_QUOTED_CLASSNAME
#undef XPC_MAP_FLAGS
