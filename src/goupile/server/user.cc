// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../libcc/libcc.hh"
#include "goupile.hh"
#include "user.hh"
#include "../../../vendor/libsodium/src/libsodium/include/sodium.h"

namespace RG {

static http_SessionManager sessions;

const Session *GetCheckedSession(const http_RequestInfo &request, http_IO *io)
{
    std::shared_ptr<const Session> udata = sessions.Find<const Session>(request, io);
    return udata ? udata.get() : nullptr;
}

void HandleLogin(const http_RequestInfo &request, http_IO *io)
{
    io->RunAsync([=]() {
        // Read POST values
        HashMap<const char *, const char *> values;
        if (!io->ReadPostValues(&io->allocator, &values)) {
            io->AttachError(422);
            return;
        }

        const char *username = values.FindValue("username", nullptr);
        const char *password = values.FindValue("password", nullptr);
        if (!username || !password) {
            LogError("Missing parameters");
            io->AttachError(422);
            return;
        }

        const char *sql = R"(SELECT u.password_hash, u.admin,
                                    p.read, p.query, p.new, p.remove, p.edit, p.validate
                             FROM users u
                             INNER JOIN permissions p ON (p.username = u.username)
                             WHERE u.username = ?)";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(goupile_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            LogError("SQLite Error: %1", sqlite3_errmsg(goupile_db));
            return;
        }
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        RG_DEFER { sqlite3_finalize(stmt); };

        // Execute it!
        int rc = sqlite3_step(stmt);
        int64_t now = GetMonotonicTime();

        if (rc == SQLITE_ROW) {
            const char *password_hash = (const char *)sqlite3_column_text(stmt, 0);

            if (crypto_pwhash_str_verify(password_hash, password, strlen(password)) == 0) {
                std::shared_ptr<Session> session = std::make_shared<Session>();

                session->permissions |= !!sqlite3_column_int(stmt, 1) * (int)Session::Permission::Admin;
                session->permissions |= !!sqlite3_column_int(stmt, 2) * (int)Session::Permission::Read;
                session->permissions |= !!sqlite3_column_int(stmt, 3) * (int)Session::Permission::Query;
                session->permissions |= !!sqlite3_column_int(stmt, 4) * (int)Session::Permission::New;
                session->permissions |= !!sqlite3_column_int(stmt, 5) * (int)Session::Permission::Remove;
                session->permissions |= !!sqlite3_column_int(stmt, 6) * (int)Session::Permission::Edit;
                session->permissions |= !!sqlite3_column_int(stmt, 7) * (int)Session::Permission::Validate;

                sessions.Open(request, io, session);

                // Success!
                io->AttachText(200, "{}", "application/json");
                return;
            }
        } else if (rc != SQLITE_DONE) {
            LogError("SQLite Error: %1", sqlite3_errmsg(goupile_db));
            return;
        }

        // Enforce constant delay if authentification fails
        int64_t safety_delay = std::max(2000 - GetMonotonicTime() + now, (int64_t)0);
        WaitForDelay(safety_delay);

        LogError("Incorrect username or password");
        io->AttachError(403);
    });
}

void HandleLogout(const http_RequestInfo &request, http_IO *io)
{
    sessions.Close(request, io);
    io->AttachText(200, "{}", "application/json");
}

}