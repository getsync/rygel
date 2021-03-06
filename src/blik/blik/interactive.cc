// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../core/libcc/libcc.hh"
#include "../libblik/libblik.hh"

namespace RG {

struct LogEntry {
    LogLevel level;
    const char *ctx;
    const char *msg;
};

class LogTrace {
    HeapArray<LogEntry> entries;
    BlockAllocator str_alloc;

public:
    void Store(LogLevel level, const char *ctx, const char *msg)
    {
        LogEntry entry = {};

        entry.level = level;
        entry.ctx = ctx ? DuplicateString(ctx, &str_alloc).ptr : nullptr;
        entry.msg = DuplicateString(msg, &str_alloc).ptr;

        entries.Append(entry);
    }

    void Dump()
    {
        for (const LogEntry &entry: entries) {
            DefaultLogHandler(entry.level, entry.ctx, entry.msg);
        }

        Clear();
    }

    void Clear()
    {
        entries.Clear();
        str_alloc.ReleaseAll();
    }
};

static bool TokenizeWithFakePrint(Span<const char> code, const char *filename, TokenizedFile *out_file)
{
    static TokenizedFile intro;
    static TokenizedFile outro;

    // Tokenize must only be called once for each TokenizedFile, so we need to cheat a little
    if (!intro.tokens.len) {
        bool success = Tokenize(R"(
begin
    let __result =
)", "<intro>", &intro);
        success &= Tokenize(R"(
    if typeOf(__result) != Null do printLn(__result)
end
)", "<outro>", &outro);

        RG_ASSERT(success);
    }

    out_file->tokens.Append(intro.tokens);
    if (!Tokenize(code, filename, out_file))
        return false;
    out_file->tokens.Append(outro.tokens);

    return true;
}

int RunCommand(Span<const char> code, bool execute)
{
    Program program;

    Compiler compiler(&program);
    ImportAll(&compiler);

    // Try to parse with fake print first...
    bool valid_with_fake_print;
    {
        TokenizedFile file;
        if (!TokenizeWithFakePrint(code, "<inline>", &file))
            return 1;

        // ... but don't tell the user if it fails!
        SetLogHandler([](LogLevel level, const char *ctx, const char *msg) {});
        RG_DEFER { SetLogHandler(DefaultLogHandler); };

        valid_with_fake_print = compiler.Compile(file);
    }

    // If the fake print has failed, reparse the code without the fake print
    if (!valid_with_fake_print) {
        TokenizedFile file;
        bool success = Tokenize(code, "<inline>", &file);
        RG_ASSERT(success);

        if (!compiler.Compile(file))
            return 1;
    }

    return execute ? !Run(program) : 0;
}

int RunInteractive(bool execute)
{
    LogInfo("%!R..blik%!0 %1", FelixVersion);

    Program program;

    Compiler compiler(&program);
    ImportAll(&compiler);

    VirtualMachine vm(&program);
    bool run = true;

    // Functions specific to interactive mode
    compiler.AddFunction("exit()", [&](VirtualMachine *vm, Span<const Value> args) {
        run = false;
        vm->SetInterrupt();
        return Value();
    });
    compiler.AddFunction("quit()", [&](VirtualMachine *vm, Span<const Value> args) {
        run = false;
        vm->SetInterrupt();
        return Value();
    });

    ConsolePrompter prompter;

    while (run && prompter.Read()) {
        // We need to intercept errors in order to hide them in some cases, such as
        // for unexpected EOF because we want to allow the user to add more lines!
        LogTrace trace;
        SetLogHandler([&](LogLevel level, const char *ctx, const char *msg) {
            if (level == LogLevel::Debug) {
                DefaultLogHandler(level, ctx, msg);
            } else {
                trace.Store(level, ctx, msg);
            }
        });
        RG_DEFER_N(try_guard) {
            prompter.Commit();
            trace.Dump();
        };

        Span<const char> code = TrimStrRight((Span<const char>)prompter.str);
        if (!code.len)
            continue;

        Size prev_variables_len = program.variables.len;
        Size prev_stack_len = vm.stack.len;

        bool valid_with_fake_print;
        {
            TokenizedFile file;
            if (!TokenizeWithFakePrint(code, "<inline>", &file))
                continue;

            valid_with_fake_print = compiler.Compile(file);
        }

        if (!valid_with_fake_print) {
            trace.Clear();

            TokenizedFile file;
            bool success = Tokenize(code, "<interactive>", &file);
            RG_ASSERT(success);

            CompileReport report;
            if (!compiler.Compile(file, &report)) {
                if (report.unexpected_eof) {
                    prompter.str.len = TrimStrRight((Span<const char>)prompter.str, "\t ").len;
                    if (!prompter.str.len || prompter.str[prompter.str.len - 1] != '\n') {
                        prompter.str.Append('\n');
                    }
                    Fmt(&prompter.str, "%1", FmtArg("    ").Repeat(report.depth + 1));

                    try_guard.Disable();
                }

                continue;
            }
        }

        if (execute && !vm.Run()) {
            // Destroying global variables should be enough, because we execute single statements.
            // Thus, if the user defines a function, pretty much no execution can occur, and
            // execution should not even be able to fail in this case.
            // Besides, since thore are global variables, no shadowing has occured and we don't
            // need to deal with this.
            for (Size i = prev_variables_len; i < program.variables.len; i++) {
                program.variables_map.Remove(program.variables[i].name);
            }
            program.variables.RemoveFrom(prev_variables_len);

            // XXX: We don't yet manage memory so this works for now
            vm.stack.RemoveFrom(prev_stack_len);

            vm.frames.RemoveFrom(1);
            vm.frames[0].pc = program.ir.len;
        }
    }

    return 0;
}

}
