// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "thop.hh"

struct Config {
    enum class IPVersion {
        Dual,
        IPv4,
        IPv6
    };

    HeapArray<const char *> table_directories;
    const char *profile_directory = nullptr;
    const char *authorization_filename = nullptr;

    mco_DispenseMode dispense_mode = mco_DispenseMode::J;
    HeapArray<const char *> mco_stay_directories;
    HeapArray<const char *> mco_stay_filenames;

    IPVersion ip_version = IPVersion::Dual;
    int port = 8888;
    int pool_size = 4;

    BlockAllocator str_alloc { Kibibytes(16) };
};

class ConfigBuilder {
    Config config;

public:
    bool LoadIni(StreamReader &st);
    bool LoadFiles(Span<const char *const> filenames);

    void Finish(Config *out_config);
};

bool LoadConfig(Span<const char *const> filenames, Config *out_config);
