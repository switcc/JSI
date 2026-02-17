#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace jsi {

// ---------------------------------------------------------------------------
// EngineType – identifies which backend is in use
// ---------------------------------------------------------------------------
enum class EngineType { QuickJS, V8 };

inline const char* engineTypeName(EngineType t) {
    switch (t) {
        case EngineType::QuickJS: return "QuickJS";
        case EngineType::V8:      return "V8";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// JSError – thrown on evaluation / call failures
// ---------------------------------------------------------------------------
class JSError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// JSValue – engine-agnostic JavaScript value
// ---------------------------------------------------------------------------
class JSValue {
public:
    enum class Type {
        Undefined,
        Null,
        Boolean,
        Number,
        String,
        Object,  // represented as string→JSValue map
        Array,   // represented as vector<JSValue>
    };

    using ObjectMap = std::unordered_map<std::string, JSValue>;
    using Array     = std::vector<JSValue>;

    // -- Factories ----------------------------------------------------------
    static JSValue undefined()              { return JSValue(Type::Undefined); }
    static JSValue null()                   { return JSValue(Type::Null); }
    static JSValue from(bool v)             { return JSValue(v); }
    static JSValue from(double v)           { return JSValue(v); }
    static JSValue from(int32_t v)          { return JSValue(static_cast<double>(v)); }
    static JSValue from(const std::string& v) { return JSValue(v); }
    static JSValue from(const char* v)      { return JSValue(std::string(v)); }
    static JSValue object(ObjectMap m = {}) {
        return JSValue(Type::Object, std::move(m));
    }
    static JSValue array(Array v = {}) {
        return JSValue(Type::Array, std::move(v));
    }

    // JSValue must be copyable so that it can be passed by value across
    // the engine boundary.  The recursive types (object / array) live
    // behind unique_ptr in the variant, so we implement copy manually.
    JSValue(const JSValue& o);
    JSValue& operator=(const JSValue& o);
    JSValue(JSValue&&) noexcept = default;
    JSValue& operator=(JSValue&&) noexcept = default;
    ~JSValue() = default;

    // -- Type queries -------------------------------------------------------
    Type  type()        const { return type_; }
    bool  isUndefined() const { return type_ == Type::Undefined; }
    bool  isNull()      const { return type_ == Type::Null; }
    bool  isBool()      const { return type_ == Type::Boolean; }
    bool  isNumber()    const { return type_ == Type::Number; }
    bool  isString()    const { return type_ == Type::String; }
    bool  isObject()    const { return type_ == Type::Object; }
    bool  isArray()     const { return type_ == Type::Array; }

    // -- Accessors (throw on type mismatch) ---------------------------------
    bool asBool() const {
        if (type_ != Type::Boolean) throw JSError("JSValue is not a boolean");
        return std::get<bool>(data_);
    }
    double asNumber() const {
        if (type_ != Type::Number) throw JSError("JSValue is not a number");
        return std::get<double>(data_);
    }
    const std::string& asString() const {
        if (type_ != Type::String) throw JSError("JSValue is not a string");
        return std::get<std::string>(data_);
    }
    const ObjectMap& asObject() const {
        if (type_ != Type::Object) throw JSError("JSValue is not an object");
        return *std::get<std::unique_ptr<ObjectMap>>(data_);
    }
    const Array& asArray() const {
        if (type_ != Type::Array) throw JSError("JSValue is not an array");
        return *std::get<std::unique_ptr<Array>>(data_);
    }

    // -- Coerce to human-readable string ------------------------------------
    std::string toString() const {
        switch (type_) {
            case Type::Undefined: return "undefined";
            case Type::Null:      return "null";
            case Type::Boolean:   return asBool() ? "true" : "false";
            case Type::Number: {
                double n = asNumber();
                if (n == static_cast<int64_t>(n))
                    return std::to_string(static_cast<int64_t>(n));
                return std::to_string(n);
            }
            case Type::String:    return asString();
            case Type::Object:    return "[object Object]";
            case Type::Array: {
                std::string r = "[";
                const auto& a = asArray();
                for (size_t i = 0; i < a.size(); ++i) {
                    if (i) r += ", ";
                    r += a[i].toString();
                }
                r += "]";
                return r;
            }
        }
        return "undefined";
    }

private:
    explicit JSValue(Type t) : type_(t) {}
    explicit JSValue(bool v) : type_(Type::Boolean), data_(v) {}
    explicit JSValue(double v) : type_(Type::Number), data_(v) {}
    explicit JSValue(std::string v) : type_(Type::String), data_(std::move(v)) {}
    JSValue(Type t, ObjectMap m)
        : type_(t), data_(std::make_unique<ObjectMap>(std::move(m))) {}
    JSValue(Type t, Array v)
        : type_(t), data_(std::make_unique<Array>(std::move(v))) {}

    Type type_ = Type::Undefined;
    // Recursive types are wrapped in unique_ptr so that the variant
    // never requires JSValue to be a complete type.
    using Storage = std::variant<
        std::monostate,
        bool,
        double,
        std::string,
        std::unique_ptr<ObjectMap>,
        std::unique_ptr<Array>>;
    Storage data_;
};

// ---------------------------------------------------------------------------
// NativeFunction – signature for host functions callable from JS
// ---------------------------------------------------------------------------
using NativeFunction = std::function<JSValue(const std::vector<JSValue>& args)>;

// ---------------------------------------------------------------------------
// JSEngine – abstract interface every backend must implement
// ---------------------------------------------------------------------------
class JSEngine {
public:
    virtual ~JSEngine() = default;

    // Identity
    virtual EngineType  engineType() const = 0;
    virtual const char* engineName() const = 0;
    virtual std::string engineVersion() const = 0;

    // Evaluate source code and return the result
    virtual JSValue evaluate(const std::string& code,
                             const std::string& sourceURL = "<eval>") = 0;

    // Global variable access
    virtual void    setGlobal(const std::string& name, JSValue value) = 0;
    virtual JSValue getGlobal(const std::string& name) = 0;

    // Call a global function by name
    virtual JSValue call(const std::string& funcName,
                         const std::vector<JSValue>& args = {}) = 0;

    // Register a C++ function that is callable from JS
    virtual void registerNativeFunction(const std::string& name,
                                        NativeFunction fn) = 0;

    // Trigger garbage collection (best-effort)
    virtual void gc() = 0;
};

// ---------------------------------------------------------------------------
// JSRuntime – owns a JSEngine and allows switching at runtime
// ---------------------------------------------------------------------------
class JSRuntime {
public:
    explicit JSRuntime(EngineType initial = EngineType::QuickJS);
    ~JSRuntime();

    // Switch to a different engine. The previous engine context is destroyed.
    void switchEngine(EngineType target);

    // Access the active engine
    JSEngine&       engine();
    const JSEngine& engine() const;

    EngineType currentEngineType() const;

    // Query available backends (compiled-in)
    static bool isAvailable(EngineType t);
    static std::vector<EngineType> availableEngines();

private:
    std::unique_ptr<JSEngine> engine_;
};

// ---------------------------------------------------------------------------
// Factory – used internally (and can be used directly)
// ---------------------------------------------------------------------------
std::unique_ptr<JSEngine> createEngine(EngineType t);

} // namespace jsi
