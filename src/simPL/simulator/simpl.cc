// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../libcc/libcc.hh"
#include "simpl.hh"
#include "human.hh"
#include "random.hh"

static void DumpHumanInfo(Size iteration, const Human &human)
{
    PrintLn("%1;%2;%3;%4;%5;%6;%7;%8;%9",
            iteration, human.id, human.age, (int)human.sex, (int)human.smoking_status,
            human.bmi, human.systolic_pressure, human.total_cholesterol,
            (int)human.death_happened);
}

int main(int argc, char **argv)
{
    int random_seed = 0;
    Size max_humans = 10000;
    Size max_iterations = 100;

    static const auto PrintUsage = [=](FILE *fp) {
        PrintLn(fp, R"(Usage: simpl [options]

Options:
    -S, --seed <seed>            Seed random generator with <seed>
                                 (default: %1)
    -H, --humans <count>         Simulate <count> humans
                                 (default: %2)

    -i, --iterations <count>         Run <count> iterations
                                 (default: %3))",
                random_seed, max_humans, max_iterations);
    };

    // Parse options
    {
        OptionParser opt(argc, argv);
        while (opt.Next()) {
            if (opt.Test("--help")) {
                PrintUsage(stdout);
                return 0;
            } else if (opt.Test("-S", "--seed", OptionType::Value)) {
                if (!ParseDec(opt.current_value, &random_seed))
                    return 1;
            } else if (opt.Test("-H", "--humans", OptionType::Value)) {
                if (!ParseDec(opt.current_value, &max_humans))
                    return 1;
            } else if (opt.Test("-i", "--iterations", OptionType::Value)) {
                if (!ParseDec(opt.current_value, &max_iterations))
                    return 1;
            } else {
                LogError("Cannot handle option '%1'", opt.current_option);
                return 1;
            }
        }
    }

    InitRandom(random_seed);

    // Init population
    HeapArray<Human> humans;
    for (Size i = 0; i < max_humans; i++) {
        Human human = CreateHuman(i);
        humans.Append(human);
    }

    // CSV header
    PrintLn("iteration;id;age;sex;smoker;bmi;sbp;tc;dead");

    // Run simulation
    for (Size i = 0; i < max_iterations && humans.len; i++) {
        for (Size j = 0; j < humans.len; j++) {
            humans[j] = SimulateOneYear(humans[j]);
            DumpHumanInfo(i, humans[j]);

            if (humans[j].death_happened) {
                humans.ptr[j--] = humans.ptr[--humans.len];
            }
        }
    }

    if (humans.len) {
        LogError("%1/%2 humans are alive after %3 iterations",
                 humans.len, max_humans, max_iterations);
    }

    return 0;
}
