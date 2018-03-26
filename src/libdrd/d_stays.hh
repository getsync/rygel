// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../common/kutil.hh"
#include "d_common.hh"

struct ProcedureRealisation {
    ProcedureCode proc;
    int8_t phase;
    uint8_t activities;
    int16_t count;
    Date date;
};

struct Stay {
    enum class Error {
        UnknownRumVersion = 1 << 0,
        MalformedBillId = 1 << 1,
        MalformedBirthdate = 1 << 2,
        MalformedSex = 1 << 3,
        MalformedEntryDate = 1 << 4,
        MalformedEntryMode = 1 << 5,
        MalformedEntryOrigin = 1 << 6,
        MalformedExitDate = 1 << 7,
        MalformedExitMode = 1 << 8,
        MalformedExitDestination = 1 << 9,
        MalformedSessionCount = 1 << 10,
        MalformedGestationalAge = 1 << 11,
        MalformedNewbornWeight = 1 << 12,
        MalformedLastMenstrualPeriod = 1 << 13,
        MalformedIgs2 = 1 << 14,
        MalformedMainDiagnosis = 1 << 15,
        MalformedLinkedDiagnosis = 1 << 16,
        MissingOtherDiagnosesCount = 1 << 17,
        MalformedOtherDiagnosesCount = 1 << 18,
        MalformedOtherDiagnosis = 1 << 19,
        MissingProceduresCount = 1 << 20,
        MalformedProceduresCount = 1 << 21,
        MalformedProcedureCode = 1 << 22
    };

    uint32_t error_mask;

    int32_t admin_id;
    int32_t bill_id;

    int8_t sex;
    Date birthdate;
    struct {
        Date date;
        char mode;
        char origin;
    } entry;
    struct {
        Date date;
        char mode;
        char destination;
    } exit;
    UnitCode unit;
    int8_t bed_authorization;
    int16_t session_count;
    int16_t igs2;
    Date last_menstrual_period;
    int16_t gestational_age;
    int16_t newborn_weight;

    DiagnosisCode main_diagnosis;
    DiagnosisCode linked_diagnosis;

    // It's 2017, so let's assume 64-bit LE platforms are the majority. Use padding and
    // struct hacking (see StaySetBuilder::LoadPack and StaySet::SavePack) to support dspak
    // files on 32-bit platforms.
    Span<DiagnosisCode> diagnoses;
    Span<ProcedureRealisation> procedures;
#ifndef ARCH_64
    char _pad1[32 - 2 * SIZE(Size) - 2 * SIZE(void *)];
#endif
};

struct StayTest {
    int32_t bill_id;

    uint16_t cluster_len;

    GhmCode ghm;
    int16_t error;

    GhsCode ghs;
    SupplementCounters<int16_t> supplement_days;

    HASH_TABLE_HANDLER(StayTest, bill_id);
};

struct StaySet {
    HeapArray<Stay> stays;

    struct {
        HeapArray<DiagnosisCode> diagnoses;
        HeapArray<ProcedureRealisation> procedures;
    } store;

    bool SavePack(StreamWriter &st) const;
    bool SavePack(const char *filename) const;
};

class StaySetBuilder {
    StaySet set;

public:
    bool LoadPack(StreamReader &st, HashTable<int32_t, StayTest> *out_tests = nullptr);
    bool LoadGrp(StreamReader &st, HashTable<int32_t, StayTest> *out_tests = nullptr)
        { return LoadRssOrGrp(st, true, out_tests); }
    bool LoadRss(StreamReader &st, HashTable<int32_t, StayTest> *out_tests = nullptr)
        { return LoadRssOrGrp(st, false, out_tests); }
    bool LoadRsa(StreamReader &st, HashTable<int32_t, StayTest> *out_tests = nullptr);

    bool LoadFiles(Span<const char *const> filenames,
                   HashTable<int32_t, StayTest> *out_tests = nullptr);

    bool Finish(StaySet *out_set);

private:
    bool LoadRssOrGrp(StreamReader &st, bool grp, HashTable<int32_t, StayTest> *out_tests);
};
