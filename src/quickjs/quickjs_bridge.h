#ifndef JSI_QUICKJS_BRIDGE_H
#define JSI_QUICKJS_BRIDGE_H

/*
 * C bridge for QuickJS – isolates quickjs.h from C++ so that compound
 * literals, designated initialisers and the JSValue type name never
 * clash with the C++ jsi::JSValue class.
 *
 * Every function here is compiled as plain C (quickjs_bridge.c) and
 * called from C++ through extern "C" linkage.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles – the C++ side never sees the real QuickJS types. */
typedef struct JSRuntime  QJS_Runtime;
typedef struct JSContext  QJS_Context;

/* A plain 64-bit blob that can hold any QuickJS JSValue.  We memcpy
   between this and the real JSValue on the C side. */
typedef struct { uint64_t opaque[2]; } QJS_Value;

/* ── Lifecycle ─────────────────────────────────────────────────────── */
QJS_Runtime* qjs_new_runtime(void);
void         qjs_free_runtime(QJS_Runtime* rt);
QJS_Context* qjs_new_context(QJS_Runtime* rt);
void         qjs_free_context(QJS_Context* ctx);
void         qjs_run_gc(QJS_Runtime* rt);

/* ── Evaluation ────────────────────────────────────────────────────── */
QJS_Value qjs_eval(QJS_Context* ctx, const char* code, size_t len,
                    const char* filename, int eval_flags);

/* ── Value inspection ──────────────────────────────────────────────── */
int    qjs_is_undefined(QJS_Value v);
int    qjs_is_null(QJS_Value v);
int    qjs_is_bool(QJS_Value v);
int    qjs_is_number(QJS_Value v);
int    qjs_is_string(QJS_Value v);
int    qjs_is_array(QJS_Context* ctx, QJS_Value v);
int    qjs_is_object(QJS_Value v);
int    qjs_is_function(QJS_Context* ctx, QJS_Value v);
int    qjs_is_exception(QJS_Value v);

int    qjs_to_bool(QJS_Context* ctx, QJS_Value v);
int    qjs_to_float64(QJS_Context* ctx, double* out, QJS_Value v);
int    qjs_to_int32(QJS_Context* ctx, int32_t* out, QJS_Value v);

const char* qjs_to_cstring(QJS_Context* ctx, QJS_Value v);
void        qjs_free_cstring(QJS_Context* ctx, const char* s);

/* ── Value construction ────────────────────────────────────────────── */
QJS_Value qjs_undefined(void);
QJS_Value qjs_null(void);
QJS_Value qjs_new_bool(QJS_Context* ctx, int val);
QJS_Value qjs_new_float64(QJS_Context* ctx, double val);
QJS_Value qjs_new_string(QJS_Context* ctx, const char* str, size_t len);
QJS_Value qjs_new_array(QJS_Context* ctx);
QJS_Value qjs_new_object(QJS_Context* ctx);

/* ── Ref-counting ──────────────────────────────────────────────────── */
void      qjs_free_value(QJS_Context* ctx, QJS_Value v);
QJS_Value qjs_dup_value(QJS_Context* ctx, QJS_Value v);

/* ── Object / array property access ────────────────────────────────── */
QJS_Value qjs_get_global_object(QJS_Context* ctx);
QJS_Value qjs_get_property_str(QJS_Context* ctx, QJS_Value obj,
                                const char* prop);
int       qjs_set_property_str(QJS_Context* ctx, QJS_Value obj,
                                const char* prop, QJS_Value val);
QJS_Value qjs_get_property_uint32(QJS_Context* ctx, QJS_Value obj,
                                   uint32_t idx);
int       qjs_set_property_uint32(QJS_Context* ctx, QJS_Value obj,
                                   uint32_t idx, QJS_Value val);

/* ── Function calls ────────────────────────────────────────────────── */
QJS_Value qjs_call(QJS_Context* ctx, QJS_Value func, QJS_Value this_val,
                    int argc, QJS_Value* argv);
QJS_Value qjs_get_exception(QJS_Context* ctx);
QJS_Value qjs_throw_internal_error(QJS_Context* ctx, const char* fmt, ...);

/* ── Native function registration (via CFunctionData) ──────────────── */
typedef QJS_Value (*QJS_CFunctionData)(QJS_Context* ctx, QJS_Value this_val,
                                       int argc, QJS_Value* argv,
                                       int magic, QJS_Value* func_data);

QJS_Value qjs_new_c_function_data(QJS_Context* ctx, QJS_CFunctionData func,
                                   int length, int magic,
                                   int data_len, QJS_Value* data);

QJS_Value qjs_mkptr(int tag, void* ptr);
void*     qjs_get_ptr(QJS_Value v);

/* ── Constants ─────────────────────────────────────────────────────── */
int qjs_tag_int(void);          /* JS_TAG_INT */
int qjs_eval_type_global(void); /* JS_EVAL_TYPE_GLOBAL */

#ifdef __cplusplus
}
#endif

#endif /* JSI_QUICKJS_BRIDGE_H */
