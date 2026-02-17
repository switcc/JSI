#include "quickjs_engine.h"
#include "quickjs_bridge.h"

#include <unordered_map>

namespace jsi {

// ---------------------------------------------------------------------------
// Helpers – convert between jsi::JSValue and QJS_Value
// ---------------------------------------------------------------------------
static JSValue qjsToJSI(QJS_Context* ctx, QJS_Value qval) {
    if (qjs_is_undefined(qval))  return JSValue::undefined();
    if (qjs_is_null(qval))       return JSValue::null();
    if (qjs_is_bool(qval))       return JSValue::from(static_cast<bool>(qjs_to_bool(ctx, qval)));
    if (qjs_is_number(qval)) {
        double d;
        qjs_to_float64(ctx, &d, qval);
        return JSValue::from(d);
    }
    if (qjs_is_string(qval)) {
        const char* s = qjs_to_cstring(ctx, qval);
        std::string str(s ? s : "");
        qjs_free_cstring(ctx, s);
        return JSValue::from(std::move(str));
    }
    if (qjs_is_array(ctx, qval)) {
        std::vector<JSValue> elems;
        QJS_Value lenVal = qjs_get_property_str(ctx, qval, "length");
        int32_t len = 0;
        qjs_to_int32(ctx, &len, lenVal);
        qjs_free_value(ctx, lenVal);
        for (int32_t i = 0; i < len; ++i) {
            QJS_Value elem = qjs_get_property_uint32(ctx, qval, static_cast<uint32_t>(i));
            elems.push_back(qjsToJSI(ctx, elem));
            qjs_free_value(ctx, elem);
        }
        return JSValue::array(std::move(elems));
    }
    if (qjs_is_object(qval)) {
        return JSValue::object();
    }
    return JSValue::undefined();
}

static QJS_Value jsiToQJS(QJS_Context* ctx, const JSValue& val) {
    switch (val.type()) {
        case JSValue::Type::Undefined: return qjs_undefined();
        case JSValue::Type::Null:      return qjs_null();
        case JSValue::Type::Boolean:   return qjs_new_bool(ctx, val.asBool());
        case JSValue::Type::Number:    return qjs_new_float64(ctx, val.asNumber());
        case JSValue::Type::String:
            return qjs_new_string(ctx, val.asString().c_str(), val.asString().size());
        case JSValue::Type::Array: {
            QJS_Value arr = qjs_new_array(ctx);
            const auto& elems = val.asArray();
            for (size_t i = 0; i < elems.size(); ++i) {
                qjs_set_property_uint32(ctx, arr, static_cast<uint32_t>(i),
                                        jsiToQJS(ctx, elems[i]));
            }
            return arr;
        }
        case JSValue::Type::Object: {
            QJS_Value obj = qjs_new_object(ctx);
            for (const auto& [k, v] : val.asObject()) {
                qjs_set_property_str(ctx, obj, k.c_str(), jsiToQJS(ctx, v));
            }
            return obj;
        }
    }
    return qjs_undefined();
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct QuickJSEngine::Impl {
    QJS_Runtime* rt  = nullptr;
    QJS_Context* ctx = nullptr;

    struct NativeFuncEntry {
        NativeFunction fn;
    };
    std::unordered_map<std::string, NativeFuncEntry> nativeFuncs;
};

// ---------------------------------------------------------------------------
// C trampoline – called from QuickJS through the bridge
// ---------------------------------------------------------------------------
static QJS_Value nativeTrampoline(QJS_Context* ctx, QJS_Value /*this_val*/,
                                   int argc, QJS_Value* argv,
                                   int /*magic*/, QJS_Value* func_data) {
    auto* entry = reinterpret_cast<QuickJSEngine::Impl::NativeFuncEntry*>(
        qjs_get_ptr(func_data[0]));

    std::vector<JSValue> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i)
        args.push_back(qjsToJSI(ctx, argv[i]));

    try {
        JSValue result = entry->fn(args);
        return jsiToQJS(ctx, result);
    } catch (const std::exception& e) {
        return qjs_throw_internal_error(ctx, "%s", e.what());
    }
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------
QuickJSEngine::QuickJSEngine() : impl_(std::make_unique<Impl>()) {
    impl_->rt = qjs_new_runtime();
    if (!impl_->rt)
        throw JSError("QuickJS: failed to create runtime");

    impl_->ctx = qjs_new_context(impl_->rt);
    if (!impl_->ctx) {
        qjs_free_runtime(impl_->rt);
        throw JSError("QuickJS: failed to create context");
    }
}

QuickJSEngine::~QuickJSEngine() {
    if (impl_->ctx) qjs_free_context(impl_->ctx);
    if (impl_->rt)  qjs_free_runtime(impl_->rt);
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
JSValue QuickJSEngine::evaluate(const std::string& code,
                                 const std::string& sourceURL) {
    QJS_Value result = qjs_eval(impl_->ctx, code.c_str(), code.size(),
                                 sourceURL.c_str(), qjs_eval_type_global());
    if (qjs_is_exception(result)) {
        QJS_Value exc = qjs_get_exception(impl_->ctx);
        const char* msg = qjs_to_cstring(impl_->ctx, exc);
        std::string errMsg = msg ? msg : "unknown error";
        qjs_free_cstring(impl_->ctx, msg);
        qjs_free_value(impl_->ctx, exc);
        throw JSError("QuickJS eval error: " + errMsg);
    }

    JSValue out = qjsToJSI(impl_->ctx, result);
    qjs_free_value(impl_->ctx, result);
    return out;
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
void QuickJSEngine::setGlobal(const std::string& name, JSValue value) {
    QJS_Value global = qjs_get_global_object(impl_->ctx);
    qjs_set_property_str(impl_->ctx, global, name.c_str(),
                          jsiToQJS(impl_->ctx, value));
    qjs_free_value(impl_->ctx, global);
}

JSValue QuickJSEngine::getGlobal(const std::string& name) {
    QJS_Value global = qjs_get_global_object(impl_->ctx);
    QJS_Value val    = qjs_get_property_str(impl_->ctx, global, name.c_str());
    qjs_free_value(impl_->ctx, global);

    JSValue out = qjsToJSI(impl_->ctx, val);
    qjs_free_value(impl_->ctx, val);
    return out;
}

// ---------------------------------------------------------------------------
// call
// ---------------------------------------------------------------------------
JSValue QuickJSEngine::call(const std::string& funcName,
                             const std::vector<JSValue>& args) {
    QJS_Value global = qjs_get_global_object(impl_->ctx);
    QJS_Value func   = qjs_get_property_str(impl_->ctx, global, funcName.c_str());

    if (!qjs_is_function(impl_->ctx, func)) {
        qjs_free_value(impl_->ctx, func);
        qjs_free_value(impl_->ctx, global);
        throw JSError("QuickJS: '" + funcName + "' is not a function");
    }

    std::vector<QJS_Value> qargs;
    qargs.reserve(args.size());
    for (const auto& a : args)
        qargs.push_back(jsiToQJS(impl_->ctx, a));

    QJS_Value result = qjs_call(impl_->ctx, func, global,
                                 static_cast<int>(qargs.size()), qargs.data());

    for (auto& qv : qargs) qjs_free_value(impl_->ctx, qv);
    qjs_free_value(impl_->ctx, func);
    qjs_free_value(impl_->ctx, global);

    if (qjs_is_exception(result)) {
        QJS_Value exc = qjs_get_exception(impl_->ctx);
        const char* msg = qjs_to_cstring(impl_->ctx, exc);
        std::string errMsg = msg ? msg : "unknown error";
        qjs_free_cstring(impl_->ctx, msg);
        qjs_free_value(impl_->ctx, exc);
        throw JSError("QuickJS call error: " + errMsg);
    }

    JSValue out = qjsToJSI(impl_->ctx, result);
    qjs_free_value(impl_->ctx, result);
    return out;
}

// ---------------------------------------------------------------------------
// registerNativeFunction
// ---------------------------------------------------------------------------
void QuickJSEngine::registerNativeFunction(const std::string& name,
                                            NativeFunction fn) {
    auto& entry = impl_->nativeFuncs[name];
    entry.fn    = std::move(fn);

    QJS_Value opaque = qjs_mkptr(qjs_tag_int(), reinterpret_cast<void*>(&entry));
    QJS_Value cfunc  = qjs_new_c_function_data(
        impl_->ctx, nativeTrampoline, 0, 0, 1, &opaque);

    QJS_Value global = qjs_get_global_object(impl_->ctx);
    qjs_set_property_str(impl_->ctx, global, name.c_str(), cfunc);
    qjs_free_value(impl_->ctx, global);
}

// ---------------------------------------------------------------------------
// gc
// ---------------------------------------------------------------------------
void QuickJSEngine::gc() {
    qjs_run_gc(impl_->rt);
}

} // namespace jsi
