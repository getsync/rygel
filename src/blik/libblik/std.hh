// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../../core/libcc/libcc.hh"

namespace RG {

class Compiler;

void ImportAll(Compiler *out_compiler);

void ImportPrint(Compiler *out_compiler);
void ImportMath(Compiler *out_compiler);

}
