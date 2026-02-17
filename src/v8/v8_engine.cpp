#include "v8_engine.h"

#include <v8.h>
#include <libplatform/libplatform.h>

#include <cstring>
#include <iostream>
#include <mutex>
#include <unordered_map>

namespace jsi {

// ---------------------------------------------------------------------------
// V8 platform – initialized once across all V8Engine instances
// ---------------------------------------------------------------------------
static std::once_flag g_v8InitFlag;
static std::unique_ptr<v8::Platform> g_platform;

static void ensureV8Initialized() {
    std::call_once(g_v8InitFlag, [] {
        g_platform = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(g_platform.get());
        v8::V8::Initialize();
    });
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct V8Engine::Impl {
    v8::Isolate*                          isolate  = nullptr;
    v8::Global<v8::Context>               context;
    v8::Isolate::CreateParams             createParams;

    // Stored native functions
    struct NativeFuncEntry {
        NativeFunction fn;
        v8::Isolate*   isolate;
    };
    std::unordered_map<std::string, std::unique_ptr<NativeFuncEntry>> nativeFuncs;

    // Convert a V8 value to JSValue
    JSValue toJSValue(v8::Isolate* iso, v8::Local<v8::Context> ctx,
                      v8::Local<v8::Value> val) const {
        if (val.IsEmpty() || val->IsUndefined()) return JSValue::undefined();
        if (val->IsNull())    return JSValue::null();
        if (val->IsBoolean()) return JSValue::from(val->BooleanValue(iso));
        if (val->IsNumber())  return JSValue::from(val->NumberValue(ctx).FromJust());
        if (val->IsString()) {
            v8::String::Utf8Value utf8(iso, val);
            return JSValue::from(std::string(*utf8, utf8.length()));
        }
        if (val->IsArray()) {
            auto arr = val.As<v8::Array>();
            std::vector<JSValue> elems;
            for (uint32_t i = 0; i < arr->Length(); ++i) {
                auto elem = arr->Get(ctx, i).ToLocalChecked();
                elems.push_back(toJSValue(iso, ctx, elem));
            }
            return JSValue::array(std::move(elems));
        }
        if (val->IsObject()) {
            return JSValue::object();
        }
        return JSValue::undefined();
    }

    // Convert JSValue to a V8 value
    v8::Local<v8::Value> fromJSValue(v8::Isolate* iso,
                                      v8::Local<v8::Context> ctx,
                                      const JSValue& val) const {
        switch (val.type()) {
            case JSValue::Type::Undefined:
                return v8::Undefined(iso);
            case JSValue::Type::Null:
                return v8::Null(iso);
            case JSValue::Type::Boolean:
                return v8::Boolean::New(iso, val.asBool());
            case JSValue::Type::Number:
                return v8::Number::New(iso, val.asNumber());
            case JSValue::Type::String:
                return v8::String::NewFromUtf8(iso, val.asString().c_str(),
                           v8::NewStringType::kNormal,
                           static_cast<int>(val.asString().size()))
                       .ToLocalChecked();
            case JSValue::Type::Array: {
                const auto& arr = val.asArray();
                auto v8arr = v8::Array::New(iso, static_cast<int>(arr.size()));
                for (size_t i = 0; i < arr.size(); ++i) {
                    v8arr->Set(ctx, static_cast<uint32_t>(i),
                               fromJSValue(iso, ctx, arr[i])).Check();
                }
                return v8arr;
            }
            case JSValue::Type::Object: {
                auto obj = v8::Object::New(iso);
                for (const auto& [k, v] : val.asObject()) {
                    auto key = v8::String::NewFromUtf8(iso, k.c_str(),
                                   v8::NewStringType::kNormal,
                                   static_cast<int>(k.size()))
                               .ToLocalChecked();
                    obj->Set(ctx, key, fromJSValue(iso, ctx, v)).Check();
                }
                return obj;
            }
        }
        return v8::Undefined(iso);
    }
};

// ---------------------------------------------------------------------------
// C callback trampoline for native functions
// ---------------------------------------------------------------------------
static void v8NativeTrampoline(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* entry = reinterpret_cast<V8Engine::Impl::NativeFuncEntry*>(
        info.Data().As<v8::External>()->Value());

    v8::Isolate* iso = info.GetIsolate();
    v8::HandleScope scope(iso);
    auto ctx = iso->GetCurrentContext();

    // Build a temporary Impl just for conversion
    V8Engine::Impl helper;

    std::vector<JSValue> args;
    args.reserve(info.Length());
    for (int i = 0; i < info.Length(); ++i) {
        args.push_back(helper.toJSValue(iso, ctx, info[i]));
    }

    try {
        JSValue result = entry->fn(args);
        info.GetReturnValue().Set(helper.fromJSValue(iso, ctx, result));
    } catch (const std::exception& e) {
        iso->ThrowException(
            v8::String::NewFromUtf8(iso, e.what()).ToLocalChecked());
    }
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------
V8Engine::V8Engine() : impl_(std::make_unique<Impl>()) {
    ensureV8Initialized();

    impl_->createParams.array_buffer_allocator =
        v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    impl_->isolate = v8::Isolate::New(impl_->createParams);

    v8::Isolate::Scope isolateScope(impl_->isolate);
    v8::HandleScope handleScope(impl_->isolate);
    v8::Local<v8::Context> ctx = v8::Context::New(impl_->isolate);
    impl_->context.Reset(impl_->isolate, ctx);
}

V8Engine::~V8Engine() {
    impl_->nativeFuncs.clear();
    impl_->context.Reset();
    if (impl_->isolate) {
        impl_->isolate->Dispose();
    }
    delete impl_->createParams.array_buffer_allocator;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
EngineType  V8Engine::engineType()    const { return EngineType::V8; }
const char* V8Engine::engineName()    const { return "V8"; }
std::string V8Engine::engineVersion() const {
    return v8::V8::GetVersion();
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
JSValue V8Engine::evaluate(const std::string& code,
                            const std::string& sourceURL) {
    v8::Isolate::Scope isolateScope(impl_->isolate);
    v8::HandleScope handleScope(impl_->isolate);
    auto ctx = impl_->context.Get(impl_->isolate);
    v8::Context::Scope contextScope(ctx);

    v8::TryCatch tryCatch(impl_->isolate);

    v8::ScriptOrigin origin(
        v8::String::NewFromUtf8(impl_->isolate, sourceURL.c_str())
            .ToLocalChecked());

    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(impl_->isolate, code.c_str(),
                                v8::NewStringType::kNormal,
                                static_cast<int>(code.size()))
            .ToLocalChecked();

    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(ctx, source, &origin).ToLocal(&script)) {
        v8::String::Utf8Value err(impl_->isolate, tryCatch.Exception());
        throw JSError(std::string("V8 compile error: ") + *err);
    }

    v8::Local<v8::Value> result;
    if (!script->Run(ctx).ToLocal(&result)) {
        v8::String::Utf8Value err(impl_->isolate, tryCatch.Exception());
        throw JSError(std::string("V8 eval error: ") + *err);
    }

    return impl_->toJSValue(impl_->isolate, ctx, result);
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
void V8Engine::setGlobal(const std::string& name, JSValue value) {
    v8::Isolate::Scope isolateScope(impl_->isolate);
    v8::HandleScope handleScope(impl_->isolate);
    auto ctx = impl_->context.Get(impl_->isolate);
    v8::Context::Scope contextScope(ctx);

    auto global = ctx->Global();
    auto key = v8::String::NewFromUtf8(impl_->isolate, name.c_str(),
                   v8::NewStringType::kNormal,
                   static_cast<int>(name.size()))
               .ToLocalChecked();
    global->Set(ctx, key, impl_->fromJSValue(impl_->isolate, ctx, value)).Check();
}

JSValue V8Engine::getGlobal(const std::string& name) {
    v8::Isolate::Scope isolateScope(impl_->isolate);
    v8::HandleScope handleScope(impl_->isolate);
    auto ctx = impl_->context.Get(impl_->isolate);
    v8::Context::Scope contextScope(ctx);

    auto global = ctx->Global();
    auto key = v8::String::NewFromUtf8(impl_->isolate, name.c_str(),
                   v8::NewStringType::kNormal,
                   static_cast<int>(name.size()))
               .ToLocalChecked();
    auto val = global->Get(ctx, key).ToLocalChecked();
    return impl_->toJSValue(impl_->isolate, ctx, val);
}

// ---------------------------------------------------------------------------
// call
// ---------------------------------------------------------------------------
JSValue V8Engine::call(const std::string& funcName,
                        const std::vector<JSValue>& args) {
    v8::Isolate::Scope isolateScope(impl_->isolate);
    v8::HandleScope handleScope(impl_->isolate);
    auto ctx = impl_->context.Get(impl_->isolate);
    v8::Context::Scope contextScope(ctx);

    v8::TryCatch tryCatch(impl_->isolate);

    auto global = ctx->Global();
    auto key = v8::String::NewFromUtf8(impl_->isolate, funcName.c_str())
               .ToLocalChecked();
    auto val = global->Get(ctx, key).ToLocalChecked();

    if (!val->IsFunction()) {
        throw JSError("V8: '" + funcName + "' is not a function");
    }

    auto func = val.As<v8::Function>();
    std::vector<v8::Local<v8::Value>> v8args;
    v8args.reserve(args.size());
    for (const auto& a : args) {
        v8args.push_back(impl_->fromJSValue(impl_->isolate, ctx, a));
    }

    v8::Local<v8::Value> result;
    if (!func->Call(ctx, global, static_cast<int>(v8args.size()), v8args.data())
             .ToLocal(&result)) {
        v8::String::Utf8Value err(impl_->isolate, tryCatch.Exception());
        throw JSError(std::string("V8 call error: ") + *err);
    }

    return impl_->toJSValue(impl_->isolate, ctx, result);
}

// ---------------------------------------------------------------------------
// registerNativeFunction
// ---------------------------------------------------------------------------
void V8Engine::registerNativeFunction(const std::string& name,
                                       NativeFunction fn) {
    v8::Isolate::Scope isolateScope(impl_->isolate);
    v8::HandleScope handleScope(impl_->isolate);
    auto ctx = impl_->context.Get(impl_->isolate);
    v8::Context::Scope contextScope(ctx);

    auto entry   = std::make_unique<Impl::NativeFuncEntry>();
    entry->fn    = std::move(fn);
    entry->isolate = impl_->isolate;
    auto* raw    = entry.get();
    impl_->nativeFuncs[name] = std::move(entry);

    auto external = v8::External::New(impl_->isolate, raw);
    auto funcTemplate =
        v8::FunctionTemplate::New(impl_->isolate, v8NativeTrampoline, external);

    auto global = ctx->Global();
    auto key = v8::String::NewFromUtf8(impl_->isolate, name.c_str())
               .ToLocalChecked();
    global->Set(ctx, key, funcTemplate->GetFunction(ctx).ToLocalChecked()).Check();
}

// ---------------------------------------------------------------------------
// gc
// ---------------------------------------------------------------------------
void V8Engine::gc() {
    impl_->isolate->LowMemoryNotification();
}

} // namespace jsi
