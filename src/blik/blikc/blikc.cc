// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../core/libcc/libcc.hh"
#include "../libblik/libblik.hh"

namespace RG {

int Main(int argc, char **argv)
{
    // Options
    bool run_inline = false;
    const char *filename = nullptr;

    const auto print_usage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: blikc [options] <file>
       blikc [options] -i <code>

Options:
    -i, --inline                 Run code directly from argument)");
    };

    // Handle version
    if (argc >= 2 && TestStr(argv[1], "--version")) {
        PrintLn("blikc %1", FelixVersion);
        return 0;
    }

    // Parse arguments
    {
        OptionParser opt(argc, argv);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-i", "--inline")) {
                run_inline = true;
            } else {
                LogError("Cannot handle option '%1'", opt.current_option);
                return 1;
            }
        }

        filename = opt.ConsumeNonOption();
        if (!filename) {
            LogError("No script provided");
            return 1;
        }
    }

    HeapArray<char> code;
    if (run_inline) {
        code.Append(filename);
        filename = "<inline>";
    } else {
        if (ReadFile(filename, Megabytes(64), &code) < 0)
            return 1;
    }

    TokenizedFile file;
    if (!Tokenize(code, filename, &file))
        return 1;

    Program program;
    if (!Parse(file, &program))
        return 1;

    int exit_code;
    return Run(program, &exit_code) ? exit_code : 1;
}

}

// C++ namespaces are stupid
int main(int argc, char **argv) { return RG::Main(argc, argv); }