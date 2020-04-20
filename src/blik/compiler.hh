// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../core/libcc/libcc.hh"
#include "types.hh"

namespace RG {

struct TokenSet;

enum class Opcode {
    #define OPCODE(Code) Code,
    #include "opcodes.inc"
};
static const char *const OpcodeNames[] = {
    #define OPCODE(Code) RG_STRINGIFY(Code),
    #include "opcodes.inc"
};

struct Instruction {
    Opcode code;
    union {
        bool b; // PushBool
        int64_t i; // PushInteger, Pop,
                   // StoreBool, StoreInt, StoreDouble, StoreString,
                   // LoadBool, LoadInt, LoadDouble, LoadString,
                   // Jump, BranchIfTrue, BranchIfFalse, Call, Return, Exit
        double d; // PushDouble
        const char *str; // PushString
        Type type; // Print
    } u;
};

struct Program {
    HeapArray<Instruction> ir;

    BucketArray<FunctionInfo> functions;
    HashTable<const char *, FunctionInfo *> functions_map;

    BucketArray<VariableInfo> globals;
    HashTable<const char *, VariableInfo *> globals_map;

    BlockAllocator str_alloc;
};

bool Compile(const TokenSet &set, const char *filename, Program *out_program);

}