// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../common/kutil.hh"
#include "mco_init.hh"

HeapArray<const char *> mco_resource_directories;
HeapArray<const char *> mco_table_directories;
const char *mco_authorization_filename;

bool mco_InitTableSet(Span<const char *const> resource_directories,
                      Span<const char *const> table_directories,
                      mco_TableSet *out_set)
{
    LinkedAllocator temp_alloc;

    HeapArray<const char *> filenames2;
    {
        bool success = true;
        for (const char *resource_dir: resource_directories) {
            const char *tab_dir = Fmt(&temp_alloc, "%1%/tables", resource_dir).ptr;
            if (TestPath(tab_dir, FileType::Directory)) {
                success &= EnumerateDirectoryFiles(tab_dir, "*.tab*", &temp_alloc,
                                                   &filenames2, 1024);
                success &= EnumerateDirectoryFiles(tab_dir, "*.dpri*", &temp_alloc,
                                                   &filenames2, 1024);
            }
        }
        for (const char *dir: table_directories) {
            success &= EnumerateDirectoryFiles(dir, "*.tab*", &temp_alloc, &filenames2, 1024);
            success &= EnumerateDirectoryFiles(dir, "*.dpri*", &temp_alloc, &filenames2, 1024);
        }
        if (!success)
            return false;
    }

    if (!filenames2.len) {
        LogError("No table specified or found");
    }

    {
        mco_TableSetBuilder table_set_builder;
        if (!table_set_builder.LoadFiles(filenames2))
            return false;
        if (!table_set_builder.Finish(out_set))
            return false;
    }

    return true;
}

bool mco_InitAuthorizationSet(Span<const char *const> resource_directories,
                              const char *authorization_filename,
                              mco_AuthorizationSet *out_set)
{
    LinkedAllocator temp_alloc;

    const char *filename = nullptr;
    {
        if (authorization_filename) {
            filename = authorization_filename;
        } else {
            for (Size i = resource_directories.len; i-- > 0;) {
                const char *resource_dir = resource_directories[i];

                const char *test_filename = Fmt(&temp_alloc, "%1%/config/authorizations.json",
                                                resource_dir).ptr;
                if (TestPath(test_filename, FileType::File)) {
                    filename = test_filename;
                    break;
                }
            }
        }
    }

    if (filename && filename[0]) {
        if (!mco_LoadAuthorizationFile(filename, out_set))
            return false;
    } else {
        LogError("No authorization file specified or found");
    }

    return true;
}

const mco_TableSet *mco_GetMainTableSet()
{
    static mco_TableSet table_set;
    static bool loaded = false;

    if (!loaded) {
        if (!mco_InitTableSet(mco_resource_directories, mco_table_directories, &table_set))
            return nullptr;
        loaded = true;
    }

    return &table_set;
}

const mco_AuthorizationSet *mco_GetMainAuthorizationSet()
{
    static mco_AuthorizationSet authorization_set;
    static bool loaded = false;

    if (!loaded) {
        if (!mco_InitAuthorizationSet(mco_resource_directories, mco_authorization_filename,
                                      &authorization_set))
            return nullptr;
        loaded = true;
    }

    return &authorization_set;
}

bool mco_HandleMainOption(OptionParser &opt_parser, void (*usage_func)(FILE *fp))
{
    if (opt_parser.TestOption("-O", "--output")) {
        const char *filename = opt_parser.RequireValue(usage_func);
        if (!filename)
            return false;

        if (!freopen(filename, "w", stdout)) {
            LogError("Cannot open '%1': %2", filename, strerror(errno));
            return false;
        }
        return true;
    } else if (opt_parser.TestOption("-D", "--resource-dir")) {
        if (!opt_parser.RequireValue(usage_func))
            return false;

        mco_resource_directories.Append(opt_parser.current_value);
        return true;
    } else if (opt_parser.TestOption("--table-dir")) {
        if (!opt_parser.RequireValue(usage_func))
            return false;

        mco_table_directories.Append(opt_parser.current_value);
        return true;
    } else if (opt_parser.TestOption("--auth-file")) {
        if (!opt_parser.RequireValue(usage_func))
            return false;

        mco_authorization_filename = opt_parser.current_value;
        return true;
    } else {
        LogError("Unknown option '%1'", opt_parser.current_option);
        usage_func(stderr);
        return false;
    }
}
