// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../core/libcc/libcc.hh"
#include "lexer.hh"
#include "parser.hh"

namespace RG {

int RunBlik(int argc, char **argv)
{
    if (argc < 2) {
        PrintLn(stderr, "Usage: blik <expression> ...");
        return 1;
    }

    for (Size i = 1; i < argc; i++) {
        TokenSet token_set;
        if (!Tokenize(argv[i], "<argv>", &token_set))
            return 1;
        if (!ParseExpression(token_set.tokens))
            return 1;
    }

    return 0;
}

}

// C++ namespaces are stupid
int main(int argc, char **argv) { return RG::RunBlik(argc, argv); }
