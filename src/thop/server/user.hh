// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../../core/libcc/libcc.hh"
#include "structure.hh"
#include "../../drd/libdrd/libdrd.hh"
#include "../../web/libhttp/libhttp.hh"

namespace RG {

struct StructureEntity;

enum class UserPermission {
    McoCasemix = 1 << 0,
    McoResults = 1 << 1,
    McoFilter = 1 << 2,
    McoMutate = 1 << 3
};
static const char *const UserPermissionNames[] = {
    "McoCasemix",
    "McoResults",
    "McoFilter",
    "McoMutate"
};

// We don't need any extra session information for connected users, so we can
// use this as the user data pointer stored in session manager.
struct User: public RetainObject {
    const char *name;
    const char *password_hash;

    unsigned int permissions;
    unsigned int mco_dispense_modes;
    HashSet<drd_UnitCode> mco_allowed_units;

    bool CheckPermission(UserPermission permission) const { return permissions & (int)permission; }
    bool CheckMcoDispenseMode(mco_DispenseMode dispense_mode) const
        { return mco_dispense_modes & (1 << (int)dispense_mode); }

    RG_HASHTABLE_HANDLER(User, name);
};

struct UserSet {
    HeapArray<User> users;
    HashTable<const char *, const User *> map;

    BlockAllocator str_alloc;

    const User *FindUser(const char *name) const { return map.FindValue(name, nullptr); }
};

class UserSetBuilder {
    RG_DELETE_COPY(UserSetBuilder)

    struct UnitRuleSet {
        bool allow_default;
        Span<const char *> allow;
        Span<const char *> deny;
    };

    UserSet set;
    HashMap<const char *, Size> map;

    HeapArray<UnitRuleSet> rule_sets;
    BlockAllocator allow_alloc;
    BlockAllocator deny_alloc;

public:
    UserSetBuilder() = default;

    bool LoadIni(StreamReader *st);
    bool LoadFiles(Span<const char *const> filenames);

    void Finish(const StructureSet &structure_set, UserSet *out_set);

private:
    bool CheckUnitPermission(const UnitRuleSet &rule_set, const StructureEntity &ent);
};

bool LoadUserSet(Span<const char *const> filenames, const StructureSet &structure_set,
                 UserSet *out_set);

const User *CheckSessionUser(const http_RequestInfo &request, http_IO *io);

void HandleLogin(const http_RequestInfo &request, const User *user, http_IO *io);
void HandleLogout(const http_RequestInfo &request, const User *user, http_IO *io);

}
