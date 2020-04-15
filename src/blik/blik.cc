// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../core/libcc/libcc.hh"
#include "lexer.hh"
#include "parser.hh"
#include "vm.hh"

namespace RG {

int Main(int argc, char **argv)
{
    // Options
    bool run_inline = false;
    const char *filename = nullptr;

    const auto print_usage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: blik [options] <file>

Options:
    -i, --inline                 Run code directly from argument)");
    };

    // Handle version
    if (argc >= 2 && TestStr(argv[1], "--version")) {
        PrintLn("blik %1", FelixVersion);
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

    TokenSet token_set;
    if (run_inline) {
        if (!Tokenize(filename, "<inline>", &token_set))
            return 1;

        filename = "<inline>";
    } else {
        HeapArray<char> code;
        if (ReadFile(filename, Megabytes(32), &code) < 0)
            return 1;

        if (!Tokenize(code, filename, &token_set))
            return 1;
    }

    Program program;
    if (!Parse(token_set.tokens, filename, &program))
        return 1;

    return Run(program);
}

}

// C++ namespaces are stupid
int main(int argc, char **argv) { return RG::Main(argc, argv); }
