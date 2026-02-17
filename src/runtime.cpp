#include "jsi/jsi.h"

// Conditionally include backends based on build configuration
#ifdef JSI_HAS_QUICKJS
#include "quickjs/quickjs_engine.h"
#endif

#ifdef JSI_HAS_V8
#include "v8/v8_engine.h"
#endif

namespace jsi {

// ---------------------------------------------------------------------------
// JSValue – copy operations (recursive types live behind unique_ptr)
// ---------------------------------------------------------------------------
JSValue::JSValue(const JSValue& o) : type_(o.type_) {
    switch (type_) {
        case Type::Object:
            data_ = std::make_unique<ObjectMap>(
                *std::get<std::unique_ptr<ObjectMap>>(o.data_));
            break;
        case Type::Array:
            data_ = std::make_unique<Array>(
                *std::get<std::unique_ptr<Array>>(o.data_));
            break;
        case Type::Boolean:   data_ = std::get<bool>(o.data_); break;
        case Type::Number:    data_ = std::get<double>(o.data_); break;
        case Type::String:    data_ = std::get<std::string>(o.data_); break;
        default:              break; // monostate for Undefined/Null
    }
}

JSValue& JSValue::operator=(const JSValue& o) {
    if (this != &o) {
        JSValue tmp(o);
        *this = std::move(tmp);
    }
    return *this;
}

// ---------------------------------------------------------------------------
// createEngine – factory function
// ---------------------------------------------------------------------------
std::unique_ptr<JSEngine> createEngine(EngineType t) {
    switch (t) {
#ifdef JSI_HAS_QUICKJS
        case EngineType::QuickJS:
            return std::make_unique<QuickJSEngine>();
#endif
#ifdef JSI_HAS_V8
        case EngineType::V8:
            return std::make_unique<V8Engine>();
#endif
        default:
            throw JSError(std::string("Engine not available: ") +
                          engineTypeName(t));
    }
}

// ---------------------------------------------------------------------------
// JSRuntime
// ---------------------------------------------------------------------------
JSRuntime::JSRuntime(EngineType initial)
    : engine_(createEngine(initial)) {}

JSRuntime::~JSRuntime() = default;

void JSRuntime::switchEngine(EngineType target) {
    if (engine_ && engine_->engineType() == target) return;
    engine_ = createEngine(target);
}

JSEngine&       JSRuntime::engine()       { return *engine_; }
const JSEngine& JSRuntime::engine() const { return *engine_; }

EngineType JSRuntime::currentEngineType() const {
    return engine_->engineType();
}

bool JSRuntime::isAvailable(EngineType t) {
    switch (t) {
#ifdef JSI_HAS_QUICKJS
        case EngineType::QuickJS: return true;
#endif
#ifdef JSI_HAS_V8
        case EngineType::V8:      return true;
#endif
        default: return false;
    }
}

std::vector<EngineType> JSRuntime::availableEngines() {
    std::vector<EngineType> out;
#ifdef JSI_HAS_QUICKJS
    out.push_back(EngineType::QuickJS);
#endif
#ifdef JSI_HAS_V8
    out.push_back(EngineType::V8);
#endif
    return out;
}

} // namespace jsi
