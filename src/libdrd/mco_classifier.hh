// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../common/kutil.hh"
#include "mco_authorizations.hh"
#include "mco_stays.hh"
#include "mco_tables.hh"

enum class mco_ClassifyFlag {
    MonoResults = 1 << 0,
    IgnoreConfirmation = 1 << 1,
    IgnoreProcedureDoc = 1 << 2,
    IgnoreProcedureExtension = 1 << 3
};
static const OptionDesc mco_ClassifyFlagOptions[] = {
    {"mono", "Perform mono-stay classification"},
    {"ignore_confirm", "Ignore RSS confirmation flag"},
    {"ignore_proc_doc", "Ignore procedure documentation check"},
    {"ignore_proc_ext", "Ignore ATIH procedure extension check"}
};

struct mco_Aggregate {
    enum class Flag {
        ChildbirthDiagnosis = 1 << 0,
        ChildbirthProcedure = 1 << 1,
        Childbirth = (1 << 0) | (1 << 1),
        ChildbirthType = 1 << 2
    };

    struct StayInfo {
        const mco_Stay *stay;
        int duration;

        const mco_DiagnosisInfo *main_diag_info;
        const mco_DiagnosisInfo *linked_diag_info;
        Span<const mco_DiagnosisInfo *> diagnoses;

        Span<const mco_ProcedureInfo *> procedures;
        uint8_t proc_activities;
    };

    const mco_TableIndex *index;

    Span<const mco_Stay> stays;
    mco_Stay stay;

    StayInfo info;
    HeapArray<StayInfo> stays_info;
    const StayInfo *main_stay_info;

    uint16_t flags;
    int age;
    int age_days;

    struct {
        HeapArray<const mco_DiagnosisInfo *> diagnoses;
        HeapArray<const mco_ProcedureInfo *> procedures;
    } store;
};

struct mco_ErrorSet {
    int16_t main_error = 0;
    int priority = 0;
    Bitset<512> errors;
};

struct mco_Result {
    Span<const mco_Stay> stays;
    Size main_stay_idx;
    int duration;

    mco_GhmCode ghm;
    int16_t main_error;

    mco_GhsCode ghs;
    int exb_exh;
    int ghs_cents;
    int price_cents;
    mco_SupplementCounters<int16_t> supplement_days;
    mco_SupplementCounters<int32_t> supplement_cents;
    int total_cents;
};

struct mco_Summary {
    Size results_count = 0;
    Size stays_count = 0;
    Size failures_count = 0;

    int64_t price_cents = 0;
    mco_SupplementCounters<int32_t> supplement_days;
    mco_SupplementCounters<int64_t> supplement_cents;
    int64_t total_cents = 0;

    mco_Summary &operator+=(const mco_Summary &other)
    {
        results_count += other.results_count;
        stays_count += other.stays_count;
        failures_count += other.failures_count;

        price_cents += other.price_cents;
        supplement_days += other.supplement_days;
        supplement_cents += other.supplement_cents;
        total_cents += other.total_cents;

        return *this;
    }
    mco_Summary operator+(const mco_Summary &other)
    {
        mco_Summary copy = *this;
        copy += other;
        return copy;
    }
};

Span<const mco_Stay> mco_Split(Span<const mco_Stay> stays,
                               Span<const mco_Stay> *out_remainder = nullptr);

mco_GhmCode mco_Prepare(const mco_TableSet &table_set, Span<const mco_Stay> stays,
                        unsigned int flags, mco_Aggregate *out_agg, mco_ErrorSet *out_errors);

int mco_GetMinimalDurationForSeverity(int severity);
int mco_LimitSeverityWithDuration(int severity, int duration);

mco_GhmCode mco_ClassifyGhm(const mco_Aggregate &agg, unsigned int flags, mco_ErrorSet *out_errors);

mco_GhsCode mco_ClassifyGhs(const mco_Aggregate &agg, const mco_AuthorizationSet &authorization_set,
                            mco_GhmCode ghm, unsigned int flags, int *out_ghs_duration = nullptr);
void mco_CountSupplements(const mco_Aggregate &agg, const mco_AuthorizationSet &authorization_set,
                          mco_GhsCode ghs, unsigned int flags,
                          mco_SupplementCounters<int16_t> *out_counters);

int mco_PriceGhs(const mco_GhsPriceInfo &price_info, int ghs_duration, bool death,
                 int *out_exb_exh = nullptr);
int mco_PriceGhs(const mco_Aggregate &agg, mco_GhsCode ghs, int ghs_duration,
                 int *out_ghs_cents = nullptr, int *out_exb_exh = nullptr);
int mco_PriceSupplements(const mco_Aggregate &agg, mco_GhsCode ghs,
                         const mco_SupplementCounters<int16_t> &days,
                         mco_SupplementCounters<int32_t> *out_prices);

Size mco_ClassifyRaw(const mco_TableSet &table_set, const mco_AuthorizationSet &authorization_set,
                     Span<const mco_Stay> stays, unsigned int flags, mco_Result out_results[],
                     mco_Result out_mono_results[] = nullptr);
void mco_Classify(const mco_TableSet &table_set, const mco_AuthorizationSet &authorization_set,
                  Span<const mco_Stay> stays, unsigned int flags, HeapArray<mco_Result> *out_results,
                  HeapArray<mco_Result> *out_mono_results = nullptr);
void mco_ClassifyParallel(const mco_TableSet &table_set, const mco_AuthorizationSet &authorization_set,
                          Span<const mco_Stay> stays, unsigned int flags,
                          HeapArray<mco_Result> *out_results,
                          HeapArray<mco_Result> *out_mono_results = nullptr);

void mco_Summarize(Span<const mco_Result> results, mco_Summary *out_summary);
