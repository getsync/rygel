// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../../libcc/libcc.hh"
#include "../../web/libserver/libserver.hh"

namespace RG {

struct Session {
    enum class Permission {
        Admin = 1 << 0,
        Read = 1 << 1,
        Query = 1 << 2,
        New = 1 << 3,
        Remove = 1 << 4,
        Edit = 1 << 5,
        Validate = 1 << 6
    };

    uint32_t permissions;
};

const Session *GetCheckedSession(const http_RequestInfo &request, http_IO *io);

void HandleLogin(const http_RequestInfo &request, http_IO *io);
void HandleLogout(const http_RequestInfo &request, http_IO *io);

}