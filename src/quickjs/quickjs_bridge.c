/*
 * quickjs_bridge.c – Plain-C wrapper around quickjs.h.
 *
 * Compiled as C so that compound-literal macros (JS_MKVAL etc.)
 * and the JSValue struct work correctly.  The C++ side only sees
 * the opaque QJS_Value defined in quickjs_bridge.h.
 */
#include "quickjs_bridge.h"
#include <quickjs.h>

#include <stdarg.h>
#include <string.h>

/* ── Helpers to shuttle values across the ABI boundary ─────────────── */
static inline QJS_Value wrap(JSValue v) {
    QJS_Value q;
    memcpy(&q, &v, sizeof(q));
    return q;
}

static inline JSValue unwrap(QJS_Value q) {
    JSValue v;
    memcpy(&v, &q, sizeof(v));
    return v;
}

/* ── Lifecycle ─────────────────────────────────────────────────────── */
QJS_Runtime* qjs_new_runtime(void)                { return JS_NewRuntime(); }
void         qjs_free_runtime(QJS_Runtime* rt)    { JS_FreeRuntime(rt); }
QJS_Context* qjs_new_context(QJS_Runtime* rt)     { return JS_NewContext(rt); }
void         qjs_free_context(QJS_Context* ctx)   { JS_FreeContext(ctx); }
void         qjs_run_gc(QJS_Runtime* rt)          { JS_RunGC(rt); }

/* ── Evaluation ────────────────────────────────────────────────────── */
QJS_Value qjs_eval(QJS_Context* ctx, const char* code, size_t len,
                    const char* filename, int eval_flags) {
    return wrap(JS_Eval(ctx, code, len, filename, eval_flags));
}

/* ── Value inspection ──────────────────────────────────────────────── */
int qjs_is_undefined(QJS_Value v) { return JS_IsUndefined(unwrap(v)); }
int qjs_is_null(QJS_Value v)      { return JS_IsNull(unwrap(v)); }
int qjs_is_bool(QJS_Value v)      { return JS_IsBool(unwrap(v)); }
int qjs_is_number(QJS_Value v)    { return JS_IsNumber(unwrap(v)); }
int qjs_is_string(QJS_Value v)    { return JS_IsString(unwrap(v)); }
int qjs_is_array(QJS_Context* ctx, QJS_Value v)
                                   { return JS_IsArray(ctx, unwrap(v)); }
int qjs_is_object(QJS_Value v)    { return JS_IsObject(unwrap(v)); }
int qjs_is_function(QJS_Context* ctx, QJS_Value v)
                                   { return JS_IsFunction(ctx, unwrap(v)); }
int qjs_is_exception(QJS_Value v) { return JS_IsException(unwrap(v)); }

int qjs_to_bool(QJS_Context* ctx, QJS_Value v)
                                   { return JS_ToBool(ctx, unwrap(v)); }
int qjs_to_float64(QJS_Context* ctx, double* out, QJS_Value v)
                                   { return JS_ToFloat64(ctx, out, unwrap(v)); }
int qjs_to_int32(QJS_Context* ctx, int32_t* out, QJS_Value v)
                                   { return JS_ToInt32(ctx, out, unwrap(v)); }

const char* qjs_to_cstring(QJS_Context* ctx, QJS_Value v)
                                   { return JS_ToCString(ctx, unwrap(v)); }
void qjs_free_cstring(QJS_Context* ctx, const char* s)
                                   { JS_FreeCString(ctx, s); }

/* ── Value construction ────────────────────────────────────────────── */
QJS_Value qjs_undefined(void)      { return wrap(JS_UNDEFINED); }
QJS_Value qjs_null(void)           { return wrap(JS_NULL); }
QJS_Value qjs_new_bool(QJS_Context* ctx, int val)
                                    { return wrap(JS_NewBool(ctx, val)); }
QJS_Value qjs_new_float64(QJS_Context* ctx, double val)
                                    { return wrap(JS_NewFloat64(ctx, val)); }
QJS_Value qjs_new_string(QJS_Context* ctx, const char* str, size_t len)
                                    { return wrap(JS_NewStringLen(ctx, str, len)); }
QJS_Value qjs_new_array(QJS_Context* ctx)
                                    { return wrap(JS_NewArray(ctx)); }
QJS_Value qjs_new_object(QJS_Context* ctx)
                                    { return wrap(JS_NewObject(ctx)); }

/* ── Ref-counting ──────────────────────────────────────────────────── */
void qjs_free_value(QJS_Context* ctx, QJS_Value v)
                                    { JS_FreeValue(ctx, unwrap(v)); }
QJS_Value qjs_dup_value(QJS_Context* ctx, QJS_Value v)
                                    { return wrap(JS_DupValue(ctx, unwrap(v))); }

/* ── Object / array property access ────────────────────────────────── */
QJS_Value qjs_get_global_object(QJS_Context* ctx)
                                    { return wrap(JS_GetGlobalObject(ctx)); }
QJS_Value qjs_get_property_str(QJS_Context* ctx, QJS_Value obj,
                                const char* prop)
                                    { return wrap(JS_GetPropertyStr(ctx, unwrap(obj), prop)); }
int qjs_set_property_str(QJS_Context* ctx, QJS_Value obj,
                          const char* prop, QJS_Value val)
                          { return JS_SetPropertyStr(ctx, unwrap(obj), prop, unwrap(val)); }
QJS_Value qjs_get_property_uint32(QJS_Context* ctx, QJS_Value obj,
                                   uint32_t idx)
                                   { return wrap(JS_GetPropertyUint32(ctx, unwrap(obj), idx)); }
int qjs_set_property_uint32(QJS_Context* ctx, QJS_Value obj,
                             uint32_t idx, QJS_Value val)
                             { return JS_SetPropertyUint32(ctx, unwrap(obj), idx, unwrap(val)); }

/* ── Function calls ────────────────────────────────────────────────── */
QJS_Value qjs_call(QJS_Context* ctx, QJS_Value func, QJS_Value this_val,
                    int argc, QJS_Value* argv) {
    /* Convert QJS_Value array to JSValue array on the stack. */
    JSValue jargv[64];          /* reasonable upper bound */
    int n = argc < 64 ? argc : 64;
    for (int i = 0; i < n; i++) jargv[i] = unwrap(argv[i]);
    return wrap(JS_Call(ctx, unwrap(func), unwrap(this_val), n, jargv));
}

QJS_Value qjs_get_exception(QJS_Context* ctx)
                                    { return wrap(JS_GetException(ctx)); }

QJS_Value qjs_throw_internal_error(QJS_Context* ctx, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return wrap(JS_ThrowInternalError(ctx, "%s", buf));
}

/* ── Native function registration ──────────────────────────────────── */

/* QuickJS callback trampoline: converts between JSValue and QJS_Value,
   then forwards to the registered QJS_CFunctionData. */
typedef struct {
    QJS_CFunctionData cb;
} BridgeCBData;

static JSValue bridge_trampoline(JSContext* ctx, JSValueConst this_val,
                                  int argc, JSValueConst* argv,
                                  int magic, JSValue* func_data) {
    /* func_data[0] holds a pointer to BridgeCBData (or the user pointer)
       func_data[1] holds the user's original opaque QJS_Value */
    QJS_CFunctionData cb = (QJS_CFunctionData)JS_VALUE_GET_PTR(func_data[0]);

    QJS_Value qthis = wrap(this_val);
    QJS_Value qargv[64];
    int n = argc < 64 ? argc : 64;
    for (int i = 0; i < n; i++) qargv[i] = wrap(argv[i]);

    QJS_Value qfdata = wrap(func_data[1]);
    QJS_Value result = cb(ctx, qthis, n, qargv, magic, &qfdata);
    return unwrap(result);
}

QJS_Value qjs_new_c_function_data(QJS_Context* ctx, QJS_CFunctionData func,
                                   int length, int magic,
                                   int data_len, QJS_Value* data) {
    /* We pass two func_data slots to the real QuickJS:
       [0] = opaque ptr to the C callback
       [1] = the user's opaque data (forwarded to the callback) */
    JSValue fdata[2];
    fdata[0] = JS_MKPTR(JS_TAG_INT, (void*)func);
    fdata[1] = (data && data_len > 0) ? unwrap(data[0]) : JS_UNDEFINED;
    return wrap(JS_NewCFunctionData(ctx, bridge_trampoline, length, magic,
                                    2, fdata));
}

QJS_Value qjs_mkptr(int tag, void* ptr) {
    return wrap(JS_MKPTR(tag, ptr));
}

void* qjs_get_ptr(QJS_Value v) {
    return JS_VALUE_GET_PTR(unwrap(v));
}

/* ── Constants ─────────────────────────────────────────────────────── */
int qjs_tag_int(void)          { return JS_TAG_INT; }
int qjs_eval_type_global(void) { return JS_EVAL_TYPE_GLOBAL; }
