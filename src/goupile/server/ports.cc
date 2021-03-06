// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../core/libcc/libcc.hh"
#include "../../core/libwrap/json.hh"
#include "config.hh"
#include "goupile.hh"
#include "ports.hh"

namespace RG {

extern "C" const RG::AssetInfo *const pack_asset_ports_pk_js;

static std::mutex js_mutex;
static std::condition_variable js_cv;
static ScriptPort js_ports[16];
static LocalArray<ScriptPort *, 16> js_idle_ports;

// This functions does not try to deal with null/undefined values
static int ConsumeValueInt(JSContext *ctx, JSValue value)
{
    RG_DEFER { JS_FreeValue(ctx, value); };
    return JS_VALUE_GET_INT(value);
}

// Returns invalid Span (with ptr set to nullptr) if value is null/undefined
static Span<const char> ConsumeValueStr(JSContext *ctx, JSValue value)
{
    RG_DEFER { JS_FreeValue(ctx, value); };

    if (!JS_IsNull(value) && !JS_IsUndefined(value)) {
        Span<const char> str;
        str.ptr = JS_ToCStringLen(ctx, (size_t *)&str.len, value);
        return str;
    } else {
        return {};
    }
}

ScriptPort::~ScriptPort()
{
    if (ctx) {
        JS_FreeValue(ctx, validate_func);
        JS_FreeContext(ctx);
    }
    if (rt) {
        JS_FreeRuntime(rt);
    }
}

// XXX: Ugly memory behaviour, aud se actual JS exceptions
static JSValue ReadCode(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    BlockAllocator temp_alloc;

    const char *page = JS_ToCString(ctx, argv[0]);
    if (!page)
        return JS_EXCEPTION;
    RG_DEFER { JS_FreeCString(ctx, page); };

    if (PathIsAbsolute(page) || PathContainsDotDot(page)) {
        LogError("Unsafe page name '%1'", page);
        return JS_NULL;
    }

    const char *filename = Fmt(&temp_alloc, "%1%/pages%/%2.js",
                               goupile_config.files_directory, page).ptr;

    // Load page code
    HeapArray<char> code;
    if (ReadFile(filename, goupile_config.max_file_size, &code) < 0) {
        LogError("Cannot load page '%1'", page);
        return JS_NULL;
    }

    return JS_NewStringLen(ctx, code.ptr, (size_t)code.len);
}

bool ScriptPort::ParseFragments(StreamReader *st, ScriptHandle *out_handle)
{
    BlockAllocator temp_alloc;

    // Reinitialize (just in case)
    out_handle->~ScriptHandle();

    json_Parser parser(st, &temp_alloc);

    JSValue fragments = JS_NewArray(ctx);
    uint32_t fragments_len = 0;
    RG_DEFER_N(out_guard) { JS_FreeValue(ctx, fragments); };

    parser.ParseArray();
    while (parser.InArray()) {
        const char *mtime = nullptr;
        const char *page = nullptr;
        bool deletion = false;

        JSValue frag = JS_NewObject(ctx);
        JSValue values = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, frag, "values", values);
        JS_SetPropertyUint32(ctx, fragments, fragments_len++, frag);

        parser.ParseObject();
        while (parser.InObject()) {
            const char *key = nullptr;
            parser.ParseKey(&key);

            if (TestStr(key, "mtime")) {
                parser.ParseString(&mtime);
            } else if (TestStr(key, "page")) {
                if (parser.PeekToken() == json_TokenType::Null) {
                    parser.ParseNull();
                    deletion = true;
                } else {
                    parser.ParseString(&page);
                    deletion = false;
                }
            } else if (TestStr(key, "values")) {
                parser.ParseObject();
                while (parser.InObject()) {
                    const char *obj_key = nullptr;
                    parser.ParseKey(&obj_key);

                    JSAtom obj_prop = JS_NewAtom(ctx, obj_key);
                    RG_DEFER { JS_FreeAtom(ctx, obj_prop); };

                    switch (parser.PeekToken()) {
                        case json_TokenType::Null: {
                            parser.ParseNull();
                            JS_SetProperty(ctx, values, obj_prop, JS_NULL);
                        } break;
                        case json_TokenType::Bool: {
                            bool b = false;
                            parser.ParseBool(&b);

                            JS_SetProperty(ctx, values, obj_prop, b ? JS_TRUE : JS_FALSE);
                        } break;
                        case json_TokenType::Integer: {
                            int64_t i = 0;
                            parser.ParseInteger(&i);

                            if (i >= INT_MIN || i <= INT_MAX) {
                                JS_SetProperty(ctx, values, obj_prop, JS_NewInt32(ctx, (int32_t)i));
                            } else {
                                JS_SetProperty(ctx, values, obj_prop, JS_NewBigInt64(ctx, i));
                            }
                        } break;
                        case json_TokenType::Double: {
                            double d = 0.0;
                            parser.ParseDouble(&d);

                            JS_SetProperty(ctx, values, obj_prop, JS_NewFloat64(ctx, d));
                        } break;
                        case json_TokenType::String: {
                            Span<const char> str;
                            parser.ParseString(&str);

                            JS_SetProperty(ctx, values, obj_prop, JS_NewStringLen(ctx, str.ptr, (size_t)str.len));
                        } break;

                        case json_TokenType::StartArray: {
                            JSValue array = JS_NewArray(ctx);
                            uint32_t len = 0;

                            JS_SetProperty(ctx, values, obj_prop, array);

                            parser.ParseArray();
                            while (parser.InArray()) {
                                switch (parser.PeekToken()) {
                                    case json_TokenType::Null: {
                                        parser.ParseNull();
                                        JS_SetPropertyUint32(ctx, array, len++, JS_NULL);
                                    } break;
                                    case json_TokenType::Bool: {
                                        bool b = false;
                                        parser.ParseBool(&b);

                                        JS_SetPropertyUint32(ctx, array, len++, b ? JS_TRUE : JS_FALSE);
                                    } break;
                                    case json_TokenType::Integer: {
                                        int64_t i = 0;
                                        parser.ParseInteger(&i);

                                        if (i >= INT_MIN || i <= INT_MAX) {
                                            JS_SetPropertyUint32(ctx, array, len++, JS_NewInt32(ctx, (int32_t)i));
                                        } else {
                                            JS_SetPropertyUint32(ctx, array, len++, JS_NewBigInt64(ctx, i));
                                        }
                                    } break;
                                    case json_TokenType::Double: {
                                        double d = 0.0;
                                        parser.ParseDouble(&d);

                                        JS_SetPropertyUint32(ctx, array, len++, JS_NewFloat64(ctx, d));
                                    } break;
                                    case json_TokenType::String: {
                                        Span<const char> str;
                                        parser.ParseString(&str);

                                        JS_SetPropertyUint32(ctx, array, len++, JS_NewStringLen(ctx, str.ptr, (size_t)str.len));
                                    } break;

                                    default: {
                                        LogError("Unexpected token type '%1'", json_TokenTypeNames[(int)parser.PeekToken()]);
                                        return false;
                                    } break;
                                }
                            }
                        } break;

                        default: {
                            LogError("Unexpected token type '%1'", json_TokenTypeNames[(int)parser.PeekToken()]);
                            return false;
                        } break;
                    }
                }
            } else {
                LogError("Unknown key '%1' in fragment object", key);
                return false;
            }
        }

        if (((!page || !page[0]) && !deletion) || !mtime || !mtime[0]) {
            LogError("Missing page or mtime attribute");
            return false;
        }

        JS_SetPropertyStr(ctx, frag, "page", !deletion ? JS_NewString(ctx, page) : JS_NULL);
        JS_SetPropertyStr(ctx, frag, "mtime", JS_NewString(ctx, mtime));
    }
    if (!parser.IsValid())
        return false;

    out_handle->ctx = ctx;
    out_handle->value = fragments;

    out_guard.Disable();
    return true;
}

// XXX: Detect errors (such as allocation failures) in calls to QuickJS
bool ScriptPort::RunRecord(Span<const char> json, const ScriptHandle &handle,
                           HeapArray<ScriptFragment> *out_fragments, Span<const char> *out_json)
{
    JSValue args[] = {
        JS_NewStringLen(ctx, json.ptr, (size_t)json.len),
        JS_DupValue(ctx, handle.value)
    };
    RG_DEFER {
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
    };

    JSValue ret = JS_Call(ctx, validate_func, JS_UNDEFINED, RG_LEN(args), args);
    RG_DEFER { JS_FreeValue(ctx, ret); };
    if (JS_IsException(ret)) {
        const char *msg = ConsumeValueStr(ctx, JS_GetException(ctx)).ptr;
        RG_DEFER { JS_FreeCString(ctx, msg); };

        LogError("JS: %1", msg);
        return false;
    }

    JSValue fragments = JS_GetPropertyStr(ctx, ret, "fragments");
    int fragments_len = ConsumeValueInt(ctx, JS_GetPropertyStr(ctx, fragments, "length"));
    RG_DEFER { JS_FreeValue(ctx, fragments); };
    *out_json = ConsumeValueStr(ctx, JS_GetPropertyStr(ctx, ret, "json"));

    for (int i = 0; i< fragments_len; i++) {
        JSValue frag = JS_GetPropertyUint32(ctx, fragments, i);
        RG_DEFER { JS_FreeValue(ctx, frag); };

        ScriptFragment *frag2 = out_fragments->AppendDefault();
        frag2->ctx = ctx;

        frag2->mtime = ConsumeValueStr(ctx, JS_GetPropertyStr(ctx, frag, "mtime")).ptr;
        frag2->page = ConsumeValueStr(ctx, JS_GetPropertyStr(ctx, frag, "page")).ptr;
        frag2->json = ConsumeValueStr(ctx, JS_GetPropertyStr(ctx, frag, "json"));
        frag2->errors = ConsumeValueInt(ctx, JS_GetPropertyStr(ctx, frag, "errors"));

        JSValue columns = JS_GetPropertyStr(ctx, frag, "columns");
        RG_DEFER { JS_FreeValue(ctx, columns); };

        if (!JS_IsNull(columns) && !JS_IsUndefined(columns)) {
            int columns_len = ConsumeValueInt(ctx, JS_GetPropertyStr(ctx, columns, "length"));

            for (int j = 0; j < columns_len; j++) {
                JSValue col = JS_GetPropertyUint32(ctx, columns, j);
                RG_DEFER { JS_FreeValue(ctx, col); };

                ScriptFragment::Column *col2 = frag2->columns.AppendDefault();

                col2->key = ConsumeValueStr(ctx, JS_GetPropertyStr(ctx, col, "key")).ptr;
                col2->prop = ConsumeValueStr(ctx, JS_GetPropertyStr(ctx, col, "prop")).ptr;
            }
        }
    }

    return true;
}

void InitPorts()
{
    // QuickJS requires NUL termination, so we need to make a copy anyway
    HeapArray<char> code;
    {
        StreamReader st(pack_asset_ports_pk_js->data, nullptr, pack_asset_ports_pk_js->compression_type);

        Size read_len = st.ReadAll(Megabytes(1), &code);
        RG_ASSERT(read_len >= 0);

        code.Grow(1);
        code.ptr[code.len] = 0;
    }

    for (Size i = 0; i < RG_LEN(js_ports); i++) {
        ScriptPort *port = &js_ports[i];

        port->rt = JS_NewRuntime();
        port->ctx = JS_NewContext(port->rt);

        JSValue ret = JS_Eval(port->ctx, code.ptr, code.len, "ports.pk.js", JS_EVAL_TYPE_GLOBAL);
        RG_ASSERT(!JS_IsException(ret));

        JSValue global = JS_GetGlobalObject(port->ctx);
        JSValue server = JS_GetPropertyStr(port->ctx, global, "server");
        RG_DEFER {
            JS_FreeValue(port->ctx, server);
            JS_FreeValue(port->ctx, global);
        };

        JS_SetPropertyStr(port->ctx, server, "readCode", JS_NewCFunction(port->ctx, ReadCode, "readCode", 1));
        port->validate_func = JS_GetPropertyStr(port->ctx, server, "validateFragments");

        js_idle_ports.Append(port);
    }
}

ScriptPort *LockPort()
{
    std::unique_lock<std::mutex> lock(js_mutex);

    while (!js_idle_ports.len) {
        js_cv.wait(lock);
    }

    ScriptPort *port = js_idle_ports[js_idle_ports.len - 1];
    js_idle_ports.RemoveLast(1);

    return port;
}

void UnlockPort(ScriptPort *port)
{
    std::unique_lock<std::mutex> lock(js_mutex);

    js_idle_ports.Append(port);

    js_cv.notify_one();
}

}
