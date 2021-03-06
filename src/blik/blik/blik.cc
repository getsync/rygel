// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../core/libcc/libcc.hh"
#include "../libblik/libblik.hh"

namespace RG {

int RunCommand(Span<const char> code, bool execute);
int RunInteractive(bool execute);

static int RunFile(const char *filename, bool execute)
{
    Program program;
    {
        HeapArray<char> code;
        if (ReadFile(filename, Megabytes(256), &code) < 0)
            return 1;

        Compiler compiler(&program);
        ImportAll(&compiler);

        if (!compiler.Compile(code, filename))
            return 1;
    }

    return execute ? !Run(program) : 0;
}

int Main(int argc, char **argv)
{
    enum class RunMode {
        Interactive,
        File,
        Command
    };

    // Options
    RunMode mode = RunMode::File;
    const char *filename_or_code = nullptr;
    bool execute = true;

    const auto print_usage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: %!..+blik [options] <file>
       blik [options] -c <code>
       blik [options] -i%!0

Options:
    %!..+-c, --command%!0                Run code directly from argument
    %!..+-i, --interactive%!0            Run code interactively (REPL)

        %!..+--no_execute%!0             Parse code but don't run it)");
    };

    // Handle version
    if (argc >= 2 && TestStr(argv[1], "--version")) {
        PrintLn("%!R..blik%!0 %1", FelixVersion);
        return 0;
    }

    // Parse arguments
    {
        OptionParser opt(argc, argv);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-c", "--command")) {
                if (mode != RunMode::File) {
                    LogError("You cannot use --command and --interactive at the same time");
                    return 1;
                }

                mode = RunMode::Command;
            } else if (opt.Test("-i", "--interactive")) {
                if (mode != RunMode::File) {
                    LogError("You cannot use --command and --interactive at the same time");
                    return 1;
                }

                mode = RunMode::Interactive;
            } else if (opt.Test("--no_execute")) {
                execute = false;
            } else {
                LogError("Cannot handle option '%1'", opt.current_option);
                return 1;
            }
        }

        filename_or_code = opt.ConsumeNonOption();
    }

    switch (mode) {
        case RunMode::Interactive: { return RunInteractive(execute); } break;

        case RunMode::File: {
            if (!filename_or_code) {
                LogError("No filename provided");
                return 1;
            }

            return RunFile(filename_or_code, execute);
        } break;
        case RunMode::Command: {
            if (!filename_or_code) {
                LogError("No command provided");
                return 1;
            }

            return RunCommand(filename_or_code, execute);
        } break;
    }

    RG_UNREACHABLE();
}

}

// C++ namespaces are stupid
int main(int argc, char **argv) { return RG::Main(argc, argv); }
