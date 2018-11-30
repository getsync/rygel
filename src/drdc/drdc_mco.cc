// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../libdrd/libdrd.hh"

static void PrintSummary(const mco_Pricing &summary)
{
    PrintLn("  Results: %1", summary.results_count);
    PrintLn("  Stays: %1", summary.stays_count);
    PrintLn("  Failures: %1", summary.failures_count);
    PrintLn();
    PrintLn("  GHS-EXB+EXH: %1 €", FmtDouble((double)summary.price_cents / 100.0, 2));
    PrintLn("    GHS: %1 €", FmtDouble((double)summary.ghs_cents / 100.0, 2));
    PrintLn("  Supplements: %1 €",
            FmtDouble((double)(summary.total_cents - summary.price_cents) / 100.0, 2));
    for (Size i = 0; i < ARRAY_SIZE(mco_SupplementTypeNames); i++) {
        PrintLn("    %1: %2 € [%3]",
                mco_SupplementTypeNames[i],
                FmtDouble((double)summary.supplement_cents.values[i] / 100.0, 2),
                summary.supplement_days.values[i]);
    }
    PrintLn("  Total: %1 €", FmtDouble((double)summary.total_cents / 100.0, 2));
    PrintLn();
};

static void ExportResults(Span<const mco_Result> results, Span<const mco_Result> mono_results,
                          Span<const mco_Pricing> pricings, Span<const mco_Pricing> mono_pricings,
                          bool verbose)
{
    const auto ExportResult = [&](int depth, const mco_Result &result,
                                  const mco_Pricing &pricing) {
        FmtArg padding = FmtArg("").Pad(-2 * depth);

        PrintLn("  %1%2 [%3 -- %4] = GHM %5 [%6] / GHS %7",
                padding, result.stays[0].bill_id, result.duration,
                result.stays[result.stays.len - 1].exit.date, result.ghm, result.main_error, result.ghs);

        if (verbose) {
            PrintLn("    %1GHS-EXB+EXH: %2 € [%3, coefficient = %4]",
                    padding, FmtDouble((double)pricing.price_cents / 100.0, 2),
                    pricing.exb_exh, FmtDouble(pricing.ghs_coefficient, 4));
            if (pricing.price_cents != pricing.ghs_cents) {
                PrintLn("      %1GHS: %2 €",
                        padding, FmtDouble((double)pricing.ghs_cents / 100.0, 2));
            }
            if (pricing.total_cents > pricing.price_cents) {
                PrintLn("    %1Supplements: %2 €", padding,
                        FmtDouble((double)(pricing.total_cents - pricing.price_cents) / 100.0, 2));
                for (Size j = 0; j < ARRAY_SIZE(mco_SupplementTypeNames); j++) {
                    if (pricing.supplement_cents.values[j]) {
                        PrintLn("      %1%2: %3 € [%4]", padding, mco_SupplementTypeNames[j],
                                FmtDouble((double)pricing.supplement_cents.values[j] / 100.0, 2),
                                result.supplement_days.values[j]);
                    }
                }
            }
            PrintLn("    %1Total: %2 €",
                    padding, FmtDouble((double)pricing.total_cents / 100.0, 2));
            PrintLn();
        }
    };

    Size j = 0;
    for (Size i = 0; i < results.len; i++) {
        const mco_Result &result = results[i];
        const mco_Pricing &pricing = pricings[i];

        ExportResult(0, result, pricing);

        if (mono_results.len && result.stays.len > 1) {
            for (Size k = j; k < j + result.stays.len; k++) {
                const mco_Result &mono_result = mono_results[k];
                const mco_Pricing &mono_pricing = mono_pricings[k];
                DebugAssert(mono_result.stays[0].bill_id == result.stays[0].bill_id);

                ExportResult(1, mono_result, mono_pricing);
            }
            j += result.stays.len;
        } else {
            j++;
        }
    }
    PrintLn();
}

static void ExportTests(Span<const mco_Result> results, Span<const mco_Pricing> pricings,
                        Span<const mco_Result> mono_results,
                        const HashTable<int32_t, mco_Test> &tests, bool verbose)
{
    Size tested_clusters = 0, failed_clusters = 0;
    Size tested_ghm = 0, failed_ghm = 0;
    Size tested_ghs = 0, failed_ghs = 0;
    Size tested_supplements = 0, failed_supplements = 0;
    Size tested_auth_supplements = 0, failed_auth_supplements = 0;
    Size tested_exb_exh = 0, failed_exb_exh = 0;

    Size j = 0;
    for (Size i = 0; i < results.len; i++) {
        const mco_Result &result = results[i];
        const mco_Pricing &pricing = pricings[i];

        Span<const mco_Result> sub_mono_results = {};
        if (mono_results.len) {
            sub_mono_results = mono_results.Take(j, result.stays.len);
            j += result.stays.len;
        }

        const mco_Test *test = tests.Find(result.stays[0].bill_id);
        if (!test)
            continue;

        if (test->cluster_len) {
            tested_clusters++;
            if (result.stays.len != test->cluster_len) {
                failed_clusters++;
                if (verbose) {
                    PrintLn("    %1 [%2] has inadequate cluster %3 != %4",
                            test->bill_id, result.stays[result.stays.len - 1].exit.date,
                            result.stays.len, test->cluster_len);
                }
            }
        }

        if (test->ghm.IsValid()) {
            tested_ghm++;
            if (test->ghm != result.ghm || test->error != result.main_error) {
                failed_ghm++;
                if (verbose) {
                    PrintLn("    %1 [%2] has inadequate GHM %3 [%4] != %5 [%6]",
                            test->bill_id, result.stays[result.stays.len - 1].exit.date,
                            result.ghm, FmtArg(result.main_error).Pad(-3),
                            test->ghm, FmtArg(test->error).Pad(-3));
                }
            }
        }

        if (test->ghs.IsValid()) {
            tested_ghs++;
            if (test->ghs != result.ghs) {
                failed_ghs++;
                if (verbose) {
                    PrintLn("    %1 [%2] has inadequate GHS %3 != %4",
                            test->bill_id, result.stays[result.stays.len - 1].exit.date,
                            result.ghs, test->ghs);
                }
            }
        }

        if (test->ghs.IsValid()) {
            tested_supplements++;
            if (test->supplement_days != result.supplement_days) {
                failed_supplements++;
                if (verbose) {
                    for (Size i = 0; i < ARRAY_SIZE(mco_SupplementTypeNames); i++) {
                        if (test->supplement_days.values[i] != result.supplement_days.values[i]) {
                            PrintLn("    %1 [%2] has inadequate %3 %4 != %5",
                                    test->bill_id, result.stays[result.stays.len - 1].exit.date,
                                    mco_SupplementTypeNames[i], result.supplement_days.values[i],
                                    test->supplement_days.values[i]);
                        }
                    }
                }
            }
        }

        if (test->ghs.IsValid() && mono_results.len) {
            tested_auth_supplements += sub_mono_results.len;

            Size max_auth_tests = sub_mono_results.len;
            if (max_auth_tests > ARRAY_SIZE(mco_Test::auth_supplements)) {
                LogError("Testing only first %1 unit authorizations for stay %2",
                         ARRAY_SIZE(mco_Test::auth_supplements), result.stays[0].bill_id);
                max_auth_tests = ARRAY_SIZE(mco_Test::auth_supplements);
            }

            for (Size i = 0; i < max_auth_tests; i++) {
                const mco_Result &mono_result = sub_mono_results[i];

                int8_t type;
                int16_t days;
                if (mono_result.supplement_days.st.rea) {
                    type = (int)mco_SupplementType::Rea;
                } else if (mono_result.supplement_days.st.reasi) {
                    type = (int)mco_SupplementType::Reasi;
                } else if (mono_result.supplement_days.st.si) {
                    type = (int)mco_SupplementType::Si;
                } else if (mono_result.supplement_days.st.src) {
                    type = (int)mco_SupplementType::Src;
                } else if (mono_result.supplement_days.st.nn1) {
                    type = (int)mco_SupplementType::Nn1;
                } else if (mono_result.supplement_days.st.nn2) {
                    type = (int)mco_SupplementType::Nn2;
                } else if (mono_result.supplement_days.st.nn3) {
                    type = (int)mco_SupplementType::Nn3;
                } else if (mono_result.supplement_days.st.rep) {
                    type = (int)mco_SupplementType::Rep;
                } else {
                    type = 0;
                }
                days = mono_result.supplement_days.values[type];

                if (type != test->auth_supplements[i].type ||
                        days != test->auth_supplements[i].days) {
                    failed_auth_supplements++;
                    if (verbose) {
                        PrintLn("    %1/%2 has inadequate %3 %4 != %5 %6",
                                test->bill_id, i, mco_SupplementTypeNames[type], days,
                                mco_SupplementTypeNames[test->auth_supplements[i].type],
                                test->auth_supplements[i].days);
                    }
                }
            }
        }

        if (test->ghs.IsValid()) {
            tested_exb_exh++;
            if (test->exb_exh != pricing.exb_exh) {
                failed_exb_exh++;
                if (verbose) {
                    PrintLn("    %1 [%2] has inadequate EXB/EXH %3 != %4",
                            test->bill_id, result.stays[result.stays.len - 1].exit.date,
                            pricing.exb_exh, test->exb_exh);
                }
            }
        }
    }
    if (verbose && (failed_clusters || failed_ghm || failed_ghs ||
                    failed_supplements || failed_auth_supplements)) {
        PrintLn();
    }

    PrintLn("    Failed cluster tests: %1 / %2 (missing %3)",
            failed_clusters, tested_clusters, results.len - tested_clusters);
    PrintLn("    Failed GHM tests: %1 / %2 (missing %3)",
            failed_ghm, tested_ghm, results.len - tested_ghm);
    PrintLn("    Failed GHS tests: %1 / %2 (missing %3)",
            failed_ghs, tested_ghs, results.len - tested_ghs);
    PrintLn("    Failed supplements tests: %1 / %2 (missing %3)",
            failed_supplements, tested_supplements, results.len - tested_supplements);
    if (mono_results.len) {
        PrintLn("    Failed auth supplements tests: %1 / %2 (missing %3)",
                failed_auth_supplements, tested_auth_supplements,
                mono_results.len - tested_auth_supplements);
    } else {
        PrintLn("    Auth supplements tests not performed, needs --mono");
    }
    PrintLn("    Failed EXB/EXH tests: %1 / %2 (missing %3)",
            failed_exb_exh, tested_exb_exh, results.len - tested_exb_exh);
    PrintLn();
}

bool RunMcoClassify(Span<const char *> arguments)
{
    static const auto PrintUsage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: drdc mco_classify [options] stay_file ...

Options:
    -T, --table_dir <dir>        Add table directory
    -A, --auth_file <file>       Set authorization file

    -o, --option <options>       Classifier options (see below)
    -d, --dispense <mode>        Run dispensation algorithm (see below)
        --coeff                  Apply GHS coefficients

    -v, --verbose                Show more classification details (cumulative)

        --test                   Enable testing against GenRSA values
        --torture [N]            Benchmark classifier with N runs

Classifier options:)");
        for (const OptionDesc &desc: mco_ClassifyFlagOptions) {
            PrintLn(fp, "    %1  %2", FmtArg(desc.name).Pad(27), desc.help);
        }
        PrintLn(fp, R"(
Dispensation modes:)");
        for (const OptionDesc &desc: mco_DispenseModeOptions) {
            PrintLn(fp, "    %1  Algorithm %2", FmtArg(desc.name).Pad(27), desc.help);
        }
    };

    OptionParser opt_parser(arguments);

    HeapArray<const char *> table_directories;
    const char *authorization_filename = nullptr;
    unsigned int flags = 0;
    int dispense_mode = -1;
    bool apply_coefficient = false;
    int verbosity = 0;
    bool test = false;
    int torture = 0;
    HeapArray<const char *> filenames;
    {
        const char *opt;
        while ((opt = opt_parser.Next())) {
            if (TestOption(opt, "--help")) {
                PrintUsage(stdout);
                return true;
            } else if (TestOption(opt, "-T", "--table_dir")) {
                if (!opt_parser.RequireValue())
                    return false;

                table_directories.Append(opt_parser.current_value);
            } else if (TestOption(opt, "-A", "--auth_file")) {
                authorization_filename = opt_parser.RequireValue();
                if (!authorization_filename)
                    return false;
            } else if (TestOption(opt, "-o", "--option")) {
                const char *flags_str = opt_parser.RequireValue();
                if (!flags_str)
                    return false;

                while (flags_str[0]) {
                    Span<const char> flag = TrimStr(SplitStr(flags_str, ',', &flags_str), " ");
                    const OptionDesc *desc = std::find_if(std::begin(mco_ClassifyFlagOptions),
                                                          std::end(mco_ClassifyFlagOptions),
                                                          [&](const OptionDesc &desc) { return TestStr(desc.name, flag); });
                    if (desc == std::end(mco_ClassifyFlagOptions)) {
                        LogError("Unknown classifier flag '%1'", flag);
                        return false;
                    }
                    flags |= 1u << (desc - mco_ClassifyFlagOptions);
                }
            } else if (TestOption(opt, "-d", "--dispense")) {
                const char *mode_str = opt_parser.RequireValue();
                if (!mode_str)
                    return false;

                const OptionDesc *desc = std::find_if(std::begin(mco_DispenseModeOptions),
                                                      std::end(mco_DispenseModeOptions),
                                                      [&](const OptionDesc &desc) { return TestStr(desc.name, mode_str); });
                if (desc == std::end(mco_DispenseModeOptions)) {
                    LogError("Unknown dispensation mode '%1'", mode_str);
                    return false;
                }
                dispense_mode = (int)(desc - mco_DispenseModeOptions);
            } else if (TestOption(opt, "--coeff")) {
                apply_coefficient = true;
            } else if (TestOption(opt, "-v", "--verbose")) {
                verbosity++;
            } else if (TestOption(opt, "--test")) {
                test = true;
            } else if (TestOption(opt, "--torture")) {
                if (!opt_parser.RequireValue())
                    return false;
                if (!ParseDec(opt_parser.current_value, &torture))
                    return false;
            } else {
                LogError("Unknown option '%1'", opt);
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&filenames);
        if (!filenames.len) {
            LogError("No filename provided");
            return false;
        }
    }
    if (dispense_mode >= 0) {
        flags |= (int)mco_ClassifyFlag::Mono;
    }

    mco_TableSet table_set;
    if (!mco_InitTableSet(table_directories, {}, &table_set) || !table_set.indexes.len)
        return false;
    mco_AuthorizationSet authorization_set;
    if (!mco_InitAuthorizationSet({}, authorization_filename, &authorization_set))
        return false;

    mco_StaySet stay_set;
    HashTable<int32_t, mco_Test> tests;

    // Load
    {
        mco_StaySetBuilder stay_set_builder;
        for (const char *filename: filenames) {
            LogInfo("Load '%1'", filename);
            if (!stay_set_builder.LoadFiles(filename, test ? &tests : nullptr))
                return false;
        }
        if (!stay_set_builder.Finish(&stay_set))
            return false;
    }

    LogInfo("Classify");
    HeapArray<mco_Result> results;
    HeapArray<mco_Result> mono_results;
    HeapArray<mco_Pricing> pricings;
    HeapArray<mco_Pricing> mono_pricings;
    mco_Pricing summary = {};
    uint64_t classify_time = 0;
    uint64_t pricing_time = 0;
    for (int j = 0; j < std::max(torture, 1); j++) {
        results.RemoveFrom(0);
        mono_results.RemoveFrom(0);
        pricings.RemoveFrom(0);
        mono_pricings.RemoveFrom(0);
        summary = {};

        {
            uint64_t start_time = GetMonotonicTime();
            mco_Classify(table_set, authorization_set, stay_set.stays, flags,
                         &results, &mono_results);
            classify_time += GetMonotonicTime() - start_time;
        }

        {
            uint64_t start_time = GetMonotonicTime();
            if (dispense_mode >= 0 || verbosity || test) {
                mco_Price(results, apply_coefficient, &pricings);
                if (dispense_mode >= 0) {
                    mco_Dispense(pricings, mono_results, (mco_DispenseMode)dispense_mode,
                                 &mono_pricings);
                } else {
                    mco_Price(mono_results, apply_coefficient, &mono_pricings);
                }
                mco_Summarize(pricings, &summary);
            } else {
                mco_PriceTotal(results, apply_coefficient, &summary);
            }
            pricing_time += GetMonotonicTime() - start_time;
        }
    }

    LogInfo("Export");
    if (verbosity - test >= 1) {
        PrintLn("Results:");
        ExportResults(results, mono_results, pricings, mono_pricings, verbosity - test >= 2);
    }
    PrintLn("Summary:");
    PrintSummary(summary);
    if (test) {
        PrintLn("Tests:");
        ExportTests(results, pricings, mono_results, tests, verbosity >= 1);
    }

    PrintLn("GHS coefficients have%1 been applied!", apply_coefficient ? "" : " NOT");

    if (torture) {
        uint64_t total_time = classify_time + pricing_time;
        int64_t perf = (int64_t)summary.results_count * torture * 1000 / total_time;
        int64_t mono_perf = (int64_t)summary.stays_count * torture * 1000 / total_time;

        PrintLn();
        PrintLn("Performance (with %1 runs):", torture);
        PrintLn("  Results: %1/sec", perf);
        PrintLn("  Stays: %1/sec", mono_perf);
        PrintLn();
        PrintLn("  Profile: Classify = %1%%, Pricing = %2%%",
                FmtDouble(100.0 * classify_time / total_time, 2),
                FmtDouble(100.0 * pricing_time / total_time, 2));
    }

    return true;
}

bool RunMcoDump(Span<const char *> arguments)
{
    static const auto PrintUsage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: drdc mco_dump [options] [filename] ...

Options:
    -T, --table_dir <dir>        Tables directory

    -d, --dump                   Dump content of (readable) tables)");
    };

    OptionParser opt_parser(arguments);

    HeapArray<const char *> table_directories;
    bool dump = false;
    HeapArray<const char *> filenames;
    {
        const char *opt;
        while ((opt = opt_parser.Next())) {
            if (TestOption(opt, "--help")) {
                PrintUsage(stdout);
                return true;
            } else if (TestOption(opt, "-T", "--table_dir")) {
                if (!opt_parser.RequireValue())
                    return false;

                table_directories.Append(opt_parser.current_value);
            } else if (TestOption(opt, "-d", "--dump")) {
                dump = true;
            } else {
                LogError("Unknown option '%1'", opt);
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&filenames);
    }

    mco_TableSet table_set;
    if (!mco_InitTableSet(table_directories, filenames, &table_set) || !table_set.indexes.len)
        return false;
    mco_DumpTableSetHeaders(table_set, &stdout_st);
    if (dump) {
        mco_DumpTableSetContent(table_set, &stdout_st);
    }

    return true;
}

bool RunMcoList(Span<const char *> arguments)
{
    static const auto PrintUsage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: drdc mco_list [options] list_name ...

Options:
    -T, --table_dir <dir>        Tables directory

    -d, --date <date>            Use tables valid on specified date
                                 (default: most recent tables))");
    };

    OptionParser opt_parser(arguments);

    HeapArray<const char *> table_directories;
    Date index_date = {};
    HeapArray<const char *> spec_strings;
    {
        const char *opt;
        while ((opt = opt_parser.Next())) {
            if (TestOption(opt, "--help")) {
                PrintUsage(stdout);
                return true;
            } else if (TestOption(opt, "-T", "--table_dir")) {
                if (!opt_parser.RequireValue())
                    return false;

                table_directories.Append(opt_parser.current_value);
            } else if (TestOption(opt_parser.current_option, "-d", "--date")) {
                if (!opt_parser.RequireValue())
                    return false;
                index_date = Date::FromString(opt_parser.current_value);
                if (!index_date.value)
                    return false;
            } else {
                LogError("Unknown option '%1'", opt);
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&spec_strings);
        if (!spec_strings.len) {
            LogError("No specifier string provided");
            return false;
        }
    }

    mco_TableSet table_set;
    const mco_TableIndex *index;
    {
        if (!mco_InitTableSet(table_directories, {}, &table_set))
            return false;
        index = table_set.FindIndex(index_date);
        if (!index) {
            LogError("No table index available at '%1'", index_date);
            return false;
        }
    }

    for (const char *spec_str: spec_strings) {
        mco_ListSpecifier spec = mco_ListSpecifier::FromString(spec_str);
        if (!spec.IsValid())
            continue;

        PrintLn("%1:", spec_str);
        switch (spec.table) {
            case mco_ListSpecifier::Table::Invalid: { /* Handled above */ } break;

            case mco_ListSpecifier::Table::Diagnoses: {
                for (const mco_DiagnosisInfo &diag: index->diagnoses) {
                    if (diag.flags & (int)mco_DiagnosisInfo::Flag::SexDifference) {
                        if (spec.Match(diag.Attributes(1).raw)) {
                            PrintLn("  %1 (male)", diag.diag);
                        }
                        if (spec.Match(diag.Attributes(2).raw)) {
                            PrintLn("  %1 (female)", diag.diag);
                        }
                    } else {
                        if (spec.Match(diag.Attributes(1).raw)) {
                            PrintLn("  %1", diag.diag);
                        }
                    }
                }
            } break;

            case mco_ListSpecifier::Table::Procedures: {
                for (const mco_ProcedureInfo &proc: index->procedures) {
                    if (spec.Match(proc.bytes)) {
                        PrintLn("  %1", proc.proc);
                    }
                }
            } break;
        }
        PrintLn();
    }

    return true;
}

bool RunMcoMap(Span<const char *> arguments)
{
    static const auto PrintUsage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: drdc mco_map [options]

Options:
    -T, --table_dir <dir>        Tables directory

    -d, --date <date>            Use tables valid on specified date
                                 (default: most recent tables))");
    };

    OptionParser opt_parser(arguments);

    HeapArray<const char *> table_directories;
    Date index_date = {};
    {
        const char *opt;
        while ((opt = opt_parser.Next())) {
            if (TestOption(opt, "--help")) {
                PrintUsage(stdout);
                return true;
            } else if (TestOption(opt, "-T", "--table_dir")) {
                if (!opt_parser.RequireValue())
                    return false;

                table_directories.Append(opt_parser.current_value);
            } else if (TestOption(opt_parser.current_option, "-d", "--date")) {
                if (!opt_parser.RequireValue())
                    return false;

                index_date = Date::FromString(opt_parser.current_value);
                if (!index_date.value)
                    return false;
            } else {
                LogError("Unknown option '%1'", opt);
                return false;
            }
        }
    }

    mco_TableSet table_set;
    const mco_TableIndex *index;
    {
        if (!mco_InitTableSet(table_directories, {}, &table_set))
            return false;
        index = table_set.FindIndex(index_date);
        if (!index) {
            LogError("No table index available at '%1'", index_date);
            return false;
        }
    }

    LogInfo("Computing");
    HashTable<mco_GhmCode, mco_GhmConstraint> ghm_constraints;
    if (!mco_ComputeGhmConstraints(*index, &ghm_constraints))
        return false;

    LogInfo("Export");
    for (const mco_GhmToGhsInfo &ghm_to_ghs_info: index->ghs)  {
        const mco_GhmConstraint *constraint = ghm_constraints.Find(ghm_to_ghs_info.ghm);
        if (constraint) {
            PrintLn("Constraint for %1", ghm_to_ghs_info.ghm);
            PrintLn("  Duration = 0x%1",
                    FmtHex(constraint->durations).Pad0(-2 * SIZE(constraint->durations)));
            PrintLn("  Warnings = 0x%1",
                    FmtHex(constraint->warnings).Pad0(-2 * SIZE(constraint->warnings)));
        } else {
            PrintLn("%1 unreached!", ghm_to_ghs_info.ghm);
        }
    }

    return true;
}

bool RunMcoPack(Span<const char *> arguments)
{
    static const auto PrintUsage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: drdc mco_pack [options] stay_file ... -O output_file)");
    };

    OptionParser opt_parser(arguments);

    HeapArray<const char *> table_directories;
    const char *dest_filename = nullptr;
    HeapArray<const char *> filenames;
    {
        const char *opt;
        while ((opt = opt_parser.Next())) {
            if (TestOption(opt, "--help")) {
                PrintUsage(stdout);
                return true;
            } else if (TestOption(opt, "-O", "--output")) {
                if (!opt_parser.RequireValue())
                    return false;

                dest_filename = opt_parser.current_value;
            } else {
                LogError("Unknown option '%1'", opt);
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&filenames);
        if (!dest_filename) {
            LogError("A destination file must be provided (--output)");
            return false;
        }
        if (!filenames.len) {
            LogError("No stay file provided");
            return false;
        }
    }

    LogInfo("Load stays");
    mco_StaySet stay_set;
    {
        mco_StaySetBuilder stay_set_builder;

        if (!stay_set_builder.LoadFiles(filenames))
            return false;
        if (!stay_set_builder.Finish(&stay_set))
            return false;
    }

    LogInfo("Pack stays");
    if (!stay_set.SavePack(dest_filename))
        return false;

    return true;
}

bool RunMcoShow(Span<const char *> arguments)
{
    static const auto PrintUsage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: drdc mco_show [options] name ...

Options:
    -T, --table_dir <dir>        Tables directory)");
    };

    OptionParser opt_parser(arguments);

    HeapArray<const char *> table_directories;
    Date index_date = {};
    HeapArray<const char *> names;
    {
        const char *opt;
        while ((opt = opt_parser.Next())) {
            if (TestOption(opt, "--help")) {
                PrintUsage(stdout);
                return true;
            } else if (TestOption(opt, "-T", "--table_dir")) {
                if (!opt_parser.RequireValue())
                    return false;

                table_directories.Append(opt_parser.current_value);
            } else if (TestOption(opt_parser.current_option, "-d", "--date")) {
                if (!opt_parser.RequireValue())
                    return false;
                index_date = Date::FromString(opt_parser.current_value);
                if (!index_date.value)
                    return false;
            } else {
                LogError("Unknown option '%1'", opt);
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&names);
        if (!names.len) {
            LogError("No element name provided");
            return false;
        }
    }

    mco_TableSet table_set;
    const mco_TableIndex *index;
    {
        if (!mco_InitTableSet(table_directories, {}, &table_set))
            return false;
        index = table_set.FindIndex(index_date);
        if (!index) {
            LogError("No table index available at '%1'", index_date);
            return false;
        }
    }

    for (const char *name: names) {
        // Diagnosis?
        {
            DiagnosisCode diag =
                DiagnosisCode::FromString(name, DEFAULT_PARSE_FLAGS & ~(int)ParseFlag::Log);
            if (diag.IsValid()) {
                const mco_DiagnosisInfo *diag_info = index->FindDiagnosis(diag);
                if (diag_info) {
                    mco_DumpDiagnosisTable(*diag_info, index->exclusions, &stdout_st);
                    continue;
                }
            }
        }

        // Procedure?
        {
            ProcedureCode proc =
                ProcedureCode::FromString(name, DEFAULT_PARSE_FLAGS & ~(int)ParseFlag::Log);
            if (proc.IsValid()) {
                Span<const mco_ProcedureInfo> proc_info = index->FindProcedure(proc);
                if (proc_info.len) {
                    mco_DumpProcedureTable(proc_info, &stdout_st);
                    continue;
                }
            }
        }

        // GHM root?
        {
            mco_GhmRootCode ghm_root =
                mco_GhmRootCode::FromString(name, DEFAULT_PARSE_FLAGS & ~(int)ParseFlag::Log);
            if (ghm_root.IsValid()) {
                const mco_GhmRootInfo *ghm_root_info = index->FindGhmRoot(ghm_root);
                if (ghm_root_info) {
                    mco_DumpGhmRootTable(*ghm_root_info, &stdout_st);
                    PrintLn();

                    Span<const mco_GhmToGhsInfo> compatible_ghs = index->FindCompatibleGhs(ghm_root);
                    mco_DumpGhmToGhsTable(compatible_ghs, &stdout_st);

                    continue;
                }
            }
        }

        // GHS?
        {
            mco_GhsCode ghs = mco_GhsCode::FromString(name, DEFAULT_PARSE_FLAGS & ~(int)ParseFlag::Log);
            if (ghs.IsValid()) {
                const mco_GhsPriceInfo *pub_price_info = index->FindGhsPrice(ghs, Sector::Public);
                const mco_GhsPriceInfo *priv_price_info = index->FindGhsPrice(ghs, Sector::Private);
                if (pub_price_info || priv_price_info) {
                    for (const mco_GhmToGhsInfo &ghm_to_ghs_info: index->ghs) {
                        if (ghm_to_ghs_info.Ghs(Sector::Public) == ghs ||
                                ghm_to_ghs_info.Ghs(Sector::Private) == ghs) {
                            mco_DumpGhmToGhsTable(ghm_to_ghs_info, &stdout_st);
                        }
                    }
                    PrintLn();

                    if (pub_price_info) {
                        PrintLn("      Public:");
                        mco_DumpGhsPriceTable(*pub_price_info, &stdout_st);
                    }
                    if (priv_price_info) {
                        PrintLn("      Private:");
                        mco_DumpGhsPriceTable(*priv_price_info, &stdout_st);
                    }

                    continue;
                }
            }
        }

        LogError("Unknown element '%1'", name);
    }

    return true;
}
