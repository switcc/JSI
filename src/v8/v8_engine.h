#pragma once

#include "jsi/jsi.h"

namespace jsi {

class V8Engine final : public JSEngine {
public:
    V8Engine();
    ~V8Engine() override;

    // Identity
    EngineType  engineType()    const override;
    const char* engineName()    const override;
    std::string engineVersion() const override;

    // Core operations
    JSValue evaluate(const std::string& code,
                     const std::string& sourceURL) override;
    void    setGlobal(const std::string& name, JSValue value) override;
    JSValue getGlobal(const std::string& name) override;
    JSValue call(const std::string& funcName,
                 const std::vector<JSValue>& args) override;
    void    registerNativeFunction(const std::string& name,
                                   NativeFunction fn) override;
    void    gc() override;

    // Impl is public so the C++ trampoline can access NativeFuncEntry
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace jsi
