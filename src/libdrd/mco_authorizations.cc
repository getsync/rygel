// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../common/kutil.hh"
#include "../common/json.hh"
#include "mco_authorizations.hh"
#include "mco_tables.hh"

class JsonAuthorizationHandler: public BaseJsonHandler<JsonAuthorizationHandler> {
    enum class State {
        Default,
        AuthArray,
        AuthObject
    };

    State state = State::Default;

    mco_Authorization auth = {};

public:
    HeapArray<mco_Authorization> *out_authorizations;

    JsonAuthorizationHandler(HeapArray<mco_Authorization> *out_authorizations = nullptr)
        : out_authorizations(out_authorizations) {}

    bool Branch(JsonBranchType type, const char *)
    {
        switch (state) {
            case State::Default: {
                switch (type) {
                    case JsonBranchType::Array: { state = State::AuthArray; } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::AuthArray: {
                switch (type) {
                    case JsonBranchType::Object: { state = State::AuthObject; } break;
                    case JsonBranchType::EndArray: { state = State::Default; } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::AuthObject: {
                switch (type) {
                    case JsonBranchType::EndObject: {
                        if (!auth.dates[1].value) {
                            static const Date default_end_date = mco_ConvertDate1980(UINT16_MAX);
                            auth.dates[1] = default_end_date;
                        }

                        out_authorizations->Append(auth);
                        auth = {};

                        state = State::AuthArray;
                    } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;
        }

        return true;
    }

    bool Value(const char *key, const JsonValue &value)
    {
        if (state == State::AuthObject) {
            if (TestStr(key, "authorization")) {
                if (value.type == JsonValue::Type::Int) {
                    if (value.u.i >= 0 && value.u.i < 100) {
                        auth.type = (int8_t)value.u.i;
                    } else {
                        LogError("Invalid authorization type %1", value.u.i);
                    }
                } else if (value.type == JsonValue::Type::String) {
                    int8_t type;
                    if (sscanf(value.u.str.ptr, "%" SCNd8, &type) == 1 &&
                            type >= 0 && type < 100) {
                        auth.type = type;
                    } else {
                        LogError("Invalid authorization type '%1'", value.u.str);
                    }
                } else {
                    UnexpectedType(value.type);
                }
            } else if (TestStr(key, "begin_date")) {
                SetDate(value, &auth.dates[0]);
            } else if (TestStr(key, "end_date")) {
                SetDate(value, &auth.dates[1]);
            } else if (TestStr(key, "unit")) {
                if (value.type == JsonValue::Type::Int) {
                    if (value.u.i >= 0 && value.u.i < 10000) {
                        auth.unit.number = (int16_t)value.u.i;
                    } else {
                        LogError("Invalid unit code %1", value.u.i);
                    }
                } else if (value.type == JsonValue::Type::String) {
                    int16_t unit;
                    if (TestStr(value.u.str, "facility")) {
                        auth.unit.number = INT16_MAX;
                    } else if (sscanf(value.u.str.ptr, "%" SCNd16, &unit) == 1 &&
                               unit >= 0 && unit < 10000) {
                        auth.unit.number = unit;
                    } else {
                        LogError("Invalid unit code '%1'", value.u.str);
                    }
                } else {
                    UnexpectedType(value.type);
                }
            } else {
                return UnknownAttribute(key);
            }
        } else {
            return UnexpectedValue();
        }

        return true;
    }
};

bool mco_LoadAuthorizationFile(const char *filename, mco_AuthorizationSet *out_set)
{
    DEFER_NC(out_guard, len = out_set->authorizations.len)
        { out_set->authorizations.RemoveFrom(len); };

    {
        StreamReader st(filename);
        if (st.error)
            return false;

        JsonAuthorizationHandler json_handler(&out_set->authorizations);
        if (!LoadJson(st, &json_handler))
            return false;
    }

    for (const mco_Authorization &auth: out_set->authorizations) {
        out_set->authorizations_map.Append(&auth);
    }

    out_guard.disable();
    return true;
}

Span<const mco_Authorization> mco_AuthorizationSet::FindUnit(UnitCode unit) const
{
    Span<const mco_Authorization> auths;
    auths.ptr = authorizations_map.FindValue(unit, nullptr);
    if (!auths.ptr)
        return {};

    {
        const mco_Authorization *end_auth = auths.ptr + 1;
        while (end_auth < authorizations.end() &&
               end_auth->unit == unit) {
            end_auth++;
        }
        auths.len = end_auth - auths.ptr;
    }

    return auths;
}

const mco_Authorization *mco_AuthorizationSet::FindUnit(UnitCode unit, Date date) const
{
    const mco_Authorization *auth = authorizations_map.FindValue(unit, nullptr);
    if (!auth)
        return nullptr;

    do {
        if (date >= auth->dates[0] && date < auth->dates[1])
            return auth;
    } while (++auth < authorizations.end() && auth->unit == unit);

    return nullptr;
}
