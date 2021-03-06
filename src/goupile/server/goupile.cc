// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../core/libcc/libcc.hh"
#include "config.hh"
#include "data.hh"
#include "files.hh"
#include "goupile.hh"
#include "ports.hh"
#include "records.hh"
#include "schedule.hh"
#include "user.hh"
#include "../../web/libhttp/libhttp.hh"
#include "../../core/libwrap/sqlite.hh"
#include "../../../vendor/libsodium/src/libsodium/include/sodium.h"

namespace RG {

Config goupile_config;
sq_Database goupile_db;

#ifndef NDEBUG
static const char *assets_filename;
static AssetSet asset_set;
extern "C" const RG::AssetInfo *pack_asset_ports_pk_js;
const RG::AssetInfo *pack_asset_ports_pk_js;
#else
extern "C" const Span<const AssetInfo> pack_assets;
#endif
static HeapArray<AssetInfo> assets;
static HashTable<const char *, const AssetInfo *> assets_map;
static BlockAllocator assets_alloc;

static char etag[33];

static void HandleEvents(const http_RequestInfo &request, http_IO *io)
{
    // Do this to renew session and clear invalid session cookies
    GetCheckedSession(request, io);

    io->AttachText(200, "{}", "application/json");
}

static void HandleFileStatic(const http_RequestInfo &request, http_IO *io)
{
    http_JsonPageBuilder json(request.compression_type);

    json.StartArray();
    for (const AssetInfo &asset: assets) {
        char buf[512];
        json.String(Fmt(buf, "%1%2", goupile_config.http.base_url, asset.name + 1).ptr);
    }
    json.EndArray();

    json.Finish(io);
}

static Span<const uint8_t> PatchGoupileVariables(const AssetInfo &asset, Allocator *alloc)
{
    Span<const uint8_t> data = PatchAssetVariables(asset, alloc,
                                                   [](const char *key, StreamWriter *writer) {
        if (TestStr(key, "VERSION")) {
            writer->Write(FelixVersion);
            return true;
        } else if (TestStr(key, "APP_KEY")) {
            writer->Write(goupile_config.app_key);
            return true;
        } else if (TestStr(key, "APP_NAME")) {
            writer->Write(goupile_config.app_name);
            return true;
        } else if (TestStr(key, "BASE_URL")) {
            writer->Write(goupile_config.http.base_url);
            return true;
        } else if (TestStr(key, "USE_OFFLINE")) {
            writer->Write(goupile_config.use_offline ? "true" : "false");
            return true;
        } else if (TestStr(key, "ALLOW_GUESTS")) {
            writer->Write(goupile_config.allow_guests ? "true" : "false");
            return true;
        } else if (TestStr(key, "CACHE_KEY")) {
#ifdef NDEBUG
            writer->Write(FelixVersion);
#else
            writer->Write(etag);
#endif
            return true;
        } else if (TestStr(key, "LINK_MANIFEST")) {
            if (goupile_config.use_offline) {
                Print(writer, "<link rel=\"manifest\" href=\"%1manifest.json\"/>", goupile_config.http.base_url);
            }
            return true;
        } else {
            return false;
        }
    });

    return data;
}

static void InitAssets()
{
#ifndef NDEBUG
    Span<const AssetInfo> pack_assets = asset_set.assets;
#endif

    LogInfo(assets.len ? "Reload assets" : "Init assets");

    assets.Clear();
    assets_map.Clear();
    assets_alloc.ReleaseAll();

    // We can use a global ETag because everything is in the binary
    {
        uint64_t buf[2];
        randombytes_buf(&buf, RG_SIZE(buf));
        Fmt(etag, "%1%2", FmtHex(buf[0]).Pad0(-16), FmtHex(buf[1]).Pad0(-16));
    }

    // Packed static assets
    for (Size i = 0; i < pack_assets.len; i++) {
        AssetInfo asset = pack_assets[i];

        if (TestStr(asset.name, "goupile.html")) {
            asset.name = "/static/goupile.html";
            asset.data = PatchGoupileVariables(asset, &assets_alloc);
        } else if (TestStr(asset.name, "manifest.json")) {
            if (!goupile_config.use_offline)
                continue;

            asset.name = "/manifest.json";
            asset.data = PatchGoupileVariables(asset, &assets_alloc);
        } else if (TestStr(asset.name, "sw.pk.js")) {
            asset.name = "/sw.pk.js";
            asset.data = PatchGoupileVariables(asset, &assets_alloc);
        } else if (TestStr(asset.name, "favicon.png")) {
            asset.name = "/favicon.png";
        } else if (TestStr(asset.name, "ports.pk.js")) {
#ifndef NDEBUG
            pack_asset_ports_pk_js = &pack_assets[i];
#endif
            continue;
        } else {
            asset.name = Fmt(&assets_alloc, "/static/%1", asset.name).ptr;
        }

        assets.Append(asset);
    }
    for (const AssetInfo &asset: assets) {
        assets_map.Set(&asset);
    }
}

static void HandleRequest(const http_RequestInfo &request, http_IO *io)
{
#ifndef NDEBUG
    if (asset_set.LoadFromLibrary(assets_filename) == AssetLoadStatus::Loaded) {
        InitAssets();
    }
#endif

    // Send these headers whenever possible
    io->AddHeader("Referrer-Policy", "no-referrer");

    if (TestStr(request.method, "GET")) {
        // Try application files first
        if (HandleFileGet(request, io))
            return;

        // Now try static assets
        {
            const AssetInfo *asset;

            if (TestStr(request.url, "/") || !strncmp(request.url, "/app/", 5) ||
                                             !strncmp(request.url, "/main/", 6)) {
                asset = assets_map.FindValue("/static/goupile.html", nullptr);
                RG_ASSERT(asset);
            } else {
                asset = assets_map.FindValue(request.url, nullptr);
            }

            if (asset) {
                const char *client_etag = request.GetHeaderValue("If-None-Match");

                if (client_etag && TestStr(client_etag, etag)) {
                    MHD_Response *response = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
                    io->AttachResponse(304, response);
                } else {
                    const char *mimetype = http_GetMimeType(GetPathExtension(asset->name));
                    io->AttachBinary(200, asset->data, mimetype, asset->compression_type);

                    io->AddCachingHeaders(goupile_config.max_age, etag);
                    if (asset->source_map) {
                        io->AddHeader("SourceMap", asset->source_map);
                    }
                }

                return;
            }
        }

        // And last (but not least), API endpoints
        {
            void (*func)(const http_RequestInfo &request, http_IO *io) = nullptr;

            if (TestStr(request.url, "/api/events")) {
                func = HandleEvents;
            } else if (TestStr(request.url, "/api/user/profile")) {
                func = HandleUserProfile;
            } else if (TestStr(request.url, "/api/files/list")) {
                func = HandleFileList;
            } else if (TestStr(request.url, "/api/files/static")) {
                func = HandleFileStatic;
            } else if (!strncmp(request.url, "/api/records/", 13)) {
                func = HandleRecordGet;
            } else if (TestStr(request.url, "/api/records/columns")) {
                func = HandleRecordColumns;
            } else if (TestStr(request.url, "/api/schedule/resources")) {
                func = HandleScheduleResources;
            } else if (TestStr(request.url, "/api/schedule/meetings")) {
                func = HandleScheduleMeetings;
            }

            if (func) {
                (*func)(request, io);
                return;
            }
        }

        // Found nothing
        io->AttachError(404);
    } else if (TestStr(request.method, "POST")) {
        if (TestStr(request.url, "/api/user/login")) {
            HandleUserLogin(request, io);
        } else if (TestStr(request.url, "/api/user/logout")) {
            HandleUserLogout(request, io);
        } else {
            io->AttachError(404);
        }
    } else if (TestStr(request.method, "PUT")) {
        if (!strncmp(request.url, "/files/", 7)) {
            HandleFilePut(request, io);
        } else if (!strncmp(request.url, "/api/records/", 13)) {
            HandleRecordPut(request, io);
        } else {
            io->AttachError(404);
        }
    } else if (TestStr(request.method, "DELETE")) {
        if (!strncmp(request.url, "/files/", 7)) {
            HandleFileDelete(request, io);
        } else {
            io->AttachError(404);
        }
    } else {
        io->AttachError(405);
    }
}

int Main(int argc, char **argv)
{
    BlockAllocator temp_alloc;

    const auto print_usage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: %!..+goupile [options]%!0

Options:
    %!..+-C, --config_file <file>%!0     Set configuration file

        %!..+--port <port>%!0            Change web server port
                                 %!D..(default: %1)%!0
        %!..+--base_url <url>%!0         Change base URL
                                 %!D..(default: %2)%!0)",
                goupile_config.http.port, goupile_config.http.base_url);
    };

    // Handle version
    if (argc >= 2 && TestStr(argv[1], "--version")) {
        PrintLn("%!R..goupile%!0 %1", FelixVersion);
        return 0;
    }

    // Find config filename
    const char *config_filename = nullptr;
    {
        OptionParser opt(argc, argv, (int)OptionParser::Flag::SkipNonOptions);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-C", "--config_file", OptionType::OptionalValue)) {
                config_filename = opt.current_value;
            }
        }
    }

    // Load config file
    if (!config_filename) {
        // Using GetWorkingDirectory() is not strictly necessary, but this will make sure
        // filenames are normalized as absolute paths when config file is loaded.
        config_filename = Fmt(&temp_alloc, "%1%/goupile.ini", GetWorkingDirectory()).ptr;

        if (!TestFile(config_filename, FileType::File)) {
            LogError("Configuration file must be specified");
            return 1;
        }
    }
    if (!LoadConfig(config_filename, &goupile_config))
        return 1;

    // Parse arguments
    {
        OptionParser opt(argc, argv);

        while (opt.Next()) {
            if (opt.Test("-C", "--config_file", OptionType::Value)) {
                // Already handled
            } else if (opt.Test("--port", OptionType::Value)) {
                if (!ParseDec(opt.current_value, &goupile_config.http.port))
                    return 1;
            } else if (opt.Test("--base_url", OptionType::Value)) {
                goupile_config.http.base_url = opt.current_value;
            } else {
                LogError("Cannot handle option '%1'", opt.current_option);
                return 1;
            }
        }
    }

    // Check project configuration
    {
        bool valid = true;

        if (!goupile_config.app_key || !goupile_config.app_key[0]) {
            LogError("Project key must not be empty");
            valid = false;
        }
        if (!goupile_config.files_directory) {
            LogError("Application directory not specified");
            valid = false;
        } else if (!TestFile(goupile_config.files_directory, FileType::Directory)) {
            LogError("Application directory '%1' does not exist", goupile_config.files_directory);
            valid = false;
        }
        if (!goupile_config.database_filename) {
            LogError("Database file not specified");
            valid = false;
        }

        if (!valid)
            return 1;
    }

    // Init database
    if (!goupile_db.Open(goupile_config.database_filename, SQLITE_OPEN_READWRITE))
        return 1;

    // Check database version
    {
        sq_Statement stmt;
        if (!goupile_db.Prepare("PRAGMA user_version;", &stmt))
            return 1;

        bool success = stmt.Next();
        RG_ASSERT(success);

        int version = sqlite3_column_int(stmt, 0);

        if (version > DatabaseVersion) {
            LogError("Profile is too recent for goupile version %1", FelixVersion);
            return 1;
        } else if (version < DatabaseVersion) {
            LogError("Outdated profile version, use %!..+goupile_admin migrate%!0");
            return 1;
        }
    }

    // Init assets and files
#ifndef NDEBUG
    assets_filename = Fmt(&temp_alloc, "%1%/goupile_assets%2",
                          GetApplicationDirectory(), RG_SHARED_LIBRARY_EXTENSION).ptr;
    if (asset_set.LoadFromLibrary(assets_filename) == AssetLoadStatus::Error)
        return 1;
#endif
    InitAssets();
    if (goupile_config.files_directory && !InitFiles())
        return 1;

    // Init QuickJS
    InitPorts();

    // Run!
    http_Daemon daemon;
    if (!daemon.Start(goupile_config.http, HandleRequest))
        return 1;
    LogInfo("Listening on port %1 (%2 stack)",
            goupile_config.http.port, IPStackNames[(int)goupile_config.http.ip_stack]);

    WaitForInterruption();

    // Make sure the "Exit" message comes after the daemon has effectively stopped
    daemon.Stop();
    LogInfo("Exit");

    return 0;
}

}

// C++ namespaces are stupid
int main(int argc, char **argv) { return RG::Main(argc, argv); }
