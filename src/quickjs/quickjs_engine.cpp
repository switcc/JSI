#include "quickjs_engine.h"

extern "C" {
#include <quickjs.h>
}

#include <cstring>
#include <unordered_map>

// QuickJS uses `JSValue` as a C type; our jsi::JSValue is in the jsi namespace.
// In this file we use fully-qualified names to avoid ambiguity.

namespace jsi {

// ---------------------------------------------------------------------------
// Helpers – convert between jsi::JSValue and QuickJS JSValue (::JSValue)
// ---------------------------------------------------------------------------
static jsi::JSValue qjsToJSI(::JSContext* ctx, ::JSValue qval) {
    if (JS_IsUndefined(qval))  return jsi::JSValue::undefined();
    if (JS_IsNull(qval))       return jsi::JSValue::null();
    if (JS_IsBool(qval))       return jsi::JSValue::from(static_cast<bool>(JS_ToBool(ctx, qval)));
    if (JS_IsNumber(qval)) {
        double d;
        JS_ToFloat64(ctx, &d, qval);
        return jsi::JSValue::from(d);
    }
    if (JS_IsString(qval)) {
        const char* s = JS_ToCString(ctx, qval);
        std::string str(s ? s : "");
        JS_FreeCString(ctx, s);
        return jsi::JSValue::from(std::move(str));
    }
    if (JS_IsArray(ctx, qval)) {
        std::vector<jsi::JSValue> elems;
        ::JSValue lenVal = JS_GetPropertyStr(ctx, qval, "length");
        int32_t len = 0;
        JS_ToInt32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        for (int32_t i = 0; i < len; ++i) {
            ::JSValue elem = JS_GetPropertyUint32(ctx, qval, static_cast<uint32_t>(i));
            elems.push_back(qjsToJSI(ctx, elem));
            JS_FreeValue(ctx, elem);
        }
        return jsi::JSValue::array(std::move(elems));
    }
    if (JS_IsObject(qval)) {
        return jsi::JSValue::object();
    }
    return jsi::JSValue::undefined();
}

static ::JSValue jsiToQJS(::JSContext* ctx, const jsi::JSValue& val) {
    switch (val.type()) {
        case jsi::JSValue::Type::Undefined: return JS_UNDEFINED;
        case jsi::JSValue::Type::Null:      return JS_NULL;
        case jsi::JSValue::Type::Boolean:   return JS_NewBool(ctx, val.asBool());
        case jsi::JSValue::Type::Number:    return JS_NewFloat64(ctx, val.asNumber());
        case jsi::JSValue::Type::String:
            return JS_NewStringLen(ctx, val.asString().c_str(), val.asString().size());
        case jsi::JSValue::Type::Array: {
            ::JSValue arr = JS_NewArray(ctx);
            const auto& elems = val.asArray();
            for (size_t i = 0; i < elems.size(); ++i) {
                JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                                     jsiToQJS(ctx, elems[i]));
            }
            return arr;
        }
        case jsi::JSValue::Type::Object: {
            ::JSValue obj = JS_NewObject(ctx);
            for (const auto& [k, v] : val.asObject()) {
                JS_SetPropertyStr(ctx, obj, k.c_str(), jsiToQJS(ctx, v));
            }
            return obj;
        }
    }
    return JS_UNDEFINED;
}

// ---------------------------------------------------------------------------
// Impl – hides QuickJS types from the public header
// ---------------------------------------------------------------------------
struct QuickJSEngine::Impl {
    ::JSRuntime* rt  = nullptr;
    ::JSContext*  ctx = nullptr;

    struct NativeFuncEntry {
        NativeFunction fn;
    };
    std::unordered_map<std::string, NativeFuncEntry> nativeFuncs;
};

// ---------------------------------------------------------------------------
// C trampoline called by QuickJS for registered native functions
// ---------------------------------------------------------------------------
static ::JSValue nativeTrampoline(::JSContext* ctx, ::JSValueConst /*this_val*/,
                                  int argc, ::JSValueConst* argv,
                                  int /*magic*/, ::JSValue* func_data) {
    auto* entry = reinterpret_cast<QuickJSEngine::Impl::NativeFuncEntry*>(
        JS_VALUE_GET_PTR(func_data[0]));

    std::vector<jsi::JSValue> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i)
        args.push_back(qjsToJSI(ctx, argv[i]));

    try {
        jsi::JSValue result = entry->fn(args);
        return jsiToQJS(ctx, result);
    } catch (const std::exception& e) {
        return JS_ThrowInternalError(ctx, "%s", e.what());
    }
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------
QuickJSEngine::QuickJSEngine() : impl_(std::make_unique<Impl>()) {
    impl_->rt = JS_NewRuntime();
    if (!impl_->rt)
        throw JSError("QuickJS: failed to create runtime");

    impl_->ctx = JS_NewContext(impl_->rt);
    if (!impl_->ctx) {
        JS_FreeRuntime(impl_->rt);
        throw JSError("QuickJS: failed to create context");
    }
}

QuickJSEngine::~QuickJSEngine() {
    if (impl_->ctx) JS_FreeContext(impl_->ctx);
    if (impl_->rt)  JS_FreeRuntime(impl_->rt);
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
EngineType  QuickJSEngine::engineType()    const { return EngineType::QuickJS; }
const char* QuickJSEngine::engineName()    const { return "QuickJS"; }
std::string QuickJSEngine::engineVersion() const { return "2024-02+"; }

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
jsi::JSValue QuickJSEngine::evaluate(const std::string& code,
                                      const std::string& sourceURL) {
    ::JSValue result = JS_Eval(impl_->ctx, code.c_str(), code.size(),
                               sourceURL.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        ::JSValue exc = JS_GetException(impl_->ctx);
        const char* msg = JS_ToCString(impl_->ctx, exc);
        std::string errMsg = msg ? msg : "unknown error";
        JS_FreeCString(impl_->ctx, msg);
        JS_FreeValue(impl_->ctx, exc);
        throw JSError("QuickJS eval error: " + errMsg);
    }

    jsi::JSValue out = qjsToJSI(impl_->ctx, result);
    JS_FreeValue(impl_->ctx, result);
    return out;
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
void QuickJSEngine::setGlobal(const std::string& name, jsi::JSValue value) {
    ::JSValue global = JS_GetGlobalObject(impl_->ctx);
    JS_SetPropertyStr(impl_->ctx, global, name.c_str(),
                      jsiToQJS(impl_->ctx, value));
    JS_FreeValue(impl_->ctx, global);
}

jsi::JSValue QuickJSEngine::getGlobal(const std::string& name) {
    ::JSValue global = JS_GetGlobalObject(impl_->ctx);
    ::JSValue val    = JS_GetPropertyStr(impl_->ctx, global, name.c_str());
    JS_FreeValue(impl_->ctx, global);

    jsi::JSValue out = qjsToJSI(impl_->ctx, val);
    JS_FreeValue(impl_->ctx, val);
    return out;
}

// ---------------------------------------------------------------------------
// call
// ---------------------------------------------------------------------------
jsi::JSValue QuickJSEngine::call(const std::string& funcName,
                                  const std::vector<jsi::JSValue>& args) {
    ::JSValue global = JS_GetGlobalObject(impl_->ctx);
    ::JSValue func   = JS_GetPropertyStr(impl_->ctx, global, funcName.c_str());

    if (!JS_IsFunction(impl_->ctx, func)) {
        JS_FreeValue(impl_->ctx, func);
        JS_FreeValue(impl_->ctx, global);
        throw JSError("QuickJS: '" + funcName + "' is not a function");
    }

    std::vector<::JSValue> qargs;
    qargs.reserve(args.size());
    for (const auto& a : args)
        qargs.push_back(jsiToQJS(impl_->ctx, a));

    ::JSValue result = JS_Call(impl_->ctx, func, global,
                               static_cast<int>(qargs.size()), qargs.data());

    for (auto& qv : qargs) JS_FreeValue(impl_->ctx, qv);
    JS_FreeValue(impl_->ctx, func);
    JS_FreeValue(impl_->ctx, global);

    if (JS_IsException(result)) {
        ::JSValue exc = JS_GetException(impl_->ctx);
        const char* msg = JS_ToCString(impl_->ctx, exc);
        std::string errMsg = msg ? msg : "unknown error";
        JS_FreeCString(impl_->ctx, msg);
        JS_FreeValue(impl_->ctx, exc);
        throw JSError("QuickJS call error: " + errMsg);
    }

    jsi::JSValue out = qjsToJSI(impl_->ctx, result);
    JS_FreeValue(impl_->ctx, result);
    return out;
}

// ---------------------------------------------------------------------------
// registerNativeFunction
// ---------------------------------------------------------------------------
void QuickJSEngine::registerNativeFunction(const std::string& name,
                                            NativeFunction fn) {
    auto& entry = impl_->nativeFuncs[name];
    entry.fn    = std::move(fn);

    // Store a pointer to the entry as opaque data for the trampoline
    ::JSValue opaque = JS_MKPTR(JS_TAG_INT, reinterpret_cast<void*>(&entry));
    ::JSValue cfunc  = JS_NewCFunctionData(impl_->ctx, nativeTrampoline,
                                           /*length=*/0, /*magic=*/0,
                                           /*data_len=*/1, &opaque);

    ::JSValue global = JS_GetGlobalObject(impl_->ctx);
    JS_SetPropertyStr(impl_->ctx, global, name.c_str(), cfunc);
    JS_FreeValue(impl_->ctx, global);
}

// ---------------------------------------------------------------------------
// gc
// ---------------------------------------------------------------------------
void QuickJSEngine::gc() {
    JS_RunGC(impl_->rt);
}

} // namespace jsi
