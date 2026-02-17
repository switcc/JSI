#include "jsi/jsi.h"

#include <iostream>
#include <string>

// Helper to run a demo on whichever engine is active
static void runDemo(jsi::JSEngine& engine) {
    std::cout << "\n=== Engine: " << engine.engineName()
              << " (version: " << engine.engineVersion() << ") ===\n";

    // 1. Basic evaluation
    auto result = engine.evaluate("1 + 2 + 3");
    std::cout << "  1 + 2 + 3 = " << result.toString() << "\n";

    // 2. String manipulation
    result = engine.evaluate("'Hello' + ' ' + 'from ' + '"
                             + std::string(engine.engineName()) + "'");
    std::cout << "  String concat: " << result.toString() << "\n";

    // 3. Define a function and call it
    engine.evaluate("function factorial(n) {"
                    "  if (n <= 1) return 1;"
                    "  return n * factorial(n - 1);"
                    "}");
    result = engine.call("factorial", {jsi::JSValue::from(10)});
    std::cout << "  factorial(10) = " << result.toString() << "\n";

    // 4. Set a global variable, use it in JS
    engine.setGlobal("greeting", jsi::JSValue::from("Hello from C++"));
    result = engine.evaluate("greeting + '!'");
    std::cout << "  greeting + '!' = " << result.toString() << "\n";

    // 5. Get a global back
    engine.evaluate("var pi = Math.PI");
    auto pi = engine.getGlobal("pi");
    std::cout << "  Math.PI = " << pi.toString() << "\n";

    // 6. Register a native function and call it from JS
    engine.registerNativeFunction("nativeAdd",
        [](const std::vector<jsi::JSValue>& args) -> jsi::JSValue {
            if (args.size() < 2 || !args[0].isNumber() || !args[1].isNumber())
                return jsi::JSValue::undefined();
            return jsi::JSValue::from(args[0].asNumber() + args[1].asNumber());
        });
    result = engine.evaluate("nativeAdd(100, 200)");
    std::cout << "  nativeAdd(100, 200) = " << result.toString() << "\n";

    // 7. Array handling
    result = engine.evaluate("[1, 2, 3].map(function(x) { return x * x; })");
    std::cout << "  [1,2,3].map(x => x*x) = " << result.toString() << "\n";

    // 8. Error handling
    try {
        engine.evaluate("throw new Error('intentional error')");
    } catch (const jsi::JSError& e) {
        std::cout << "  Caught expected error: " << e.what() << "\n";
    }
}

int main() {
    std::cout << "JSI – Switchable JavaScript Engine Interface\n";
    std::cout << "=============================================\n";

    // List available engines
    auto available = jsi::JSRuntime::availableEngines();
    std::cout << "Available engines:";
    for (auto e : available)
        std::cout << " " << jsi::engineTypeName(e);
    std::cout << "\n";

    if (available.empty()) {
        std::cerr << "No JS engines compiled in!\n";
        return 1;
    }

    // Create runtime with first available engine
    jsi::JSRuntime runtime(available.front());
    runDemo(runtime.engine());

    // If a second engine is available, switch to it at runtime
    if (available.size() > 1) {
        std::cout << "\n>>> Switching engine at runtime...\n";
        runtime.switchEngine(available[1]);
        runDemo(runtime.engine());

        // Switch back
        std::cout << "\n>>> Switching back to " << jsi::engineTypeName(available[0]) << "...\n";
        runtime.switchEngine(available[0]);
        runDemo(runtime.engine());
    } else {
        std::cout << "\nOnly one engine available. Build with both "
                     "JSI_ENABLE_QUICKJS and JSI_ENABLE_V8 to "
                     "demonstrate runtime switching.\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}
