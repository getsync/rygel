// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../common/kutil.hh"
#include "d_authorizations.hh"
#include "d_stays.hh"
#include "d_tables.hh"

enum class ClassifyFlag {
    IgnoreConfirmation = 1 << 0
};
static const OptionDesc ClassifyFlagOptions[] = {
    {"ignore_confirm", "Ignore RSS confirmation flag"}
};

struct ClassifyAggregate {
    enum class Flag {
        ChildbirthDiagnosis = 1 << 0,
        ChildbirthProcedure = 1 << 1,
        Childbirth = (1 << 0) | (1 << 1),
        ChildbirthType = 1 << 2
    };

    Span<const Stay> stays;

    const TableIndex *index;

    Stay stay;
    const DiagnosisInfo *main_diag_info;
    const DiagnosisInfo *linked_diag_info;
    Span<const DiagnosisInfo *> diagnoses;
    Span<const ProcedureInfo *> procedures;
    uint8_t proc_activities;

    uint16_t flags;
    int age;
    int age_days;
    int duration;

    const Stay *main_stay;
};

struct ClassifyErrorSet {
    int16_t main_error = 0;
    int priority = 0;
    Bitset<512> errors;
};

struct ClassifyResult {
    Span<const Stay> stays;
    Size main_stay_idx;

    GhmCode ghm;
    int16_t main_error;

    GhsCode ghs;
    int ghs_price_cents;
    SupplementCounters<int16_t> supplement_days;
    SupplementCounters<int32_t> supplement_cents;
    int price_cents;
};

struct ClassifySummary {
    Size results_count = 0;
    Size stays_count = 0;
    Size failures_count = 0;

    int64_t ghs_total_cents = 0;
    SupplementCounters<int32_t> supplement_days;
    SupplementCounters<int64_t> supplement_cents;
    int64_t total_cents = 0;

    ClassifySummary &operator+=(const ClassifySummary &other)
    {
        results_count += other.results_count;
        stays_count += other.stays_count;
        failures_count += other.failures_count;

        ghs_total_cents += other.ghs_total_cents;
        supplement_days += other.supplement_days;
        supplement_cents += other.supplement_cents;
        total_cents += other.total_cents;

        return *this;
    }
    ClassifySummary operator+(const ClassifySummary &other)
    {
        ClassifySummary copy = *this;
        copy += other;
        return copy;
    }
};

Span<const Stay> Cluster(Span<const Stay> stays, Span<const Stay> *out_remainder = nullptr);

GhmCode Aggregate(const TableSet &table_set, Span<const Stay> stays, unsigned int flags,
                  ClassifyAggregate *out_agg,
                  HeapArray<const DiagnosisInfo *> *out_diagnoses,
                  HeapArray<const ProcedureInfo *> *out_procedures,
                  ClassifyErrorSet *out_errors);

int GetMinimalDurationForSeverity(int severity);
int LimitSeverityWithDuration(int severity, int duration);

GhmCode ClassifyGhm(const ClassifyAggregate &agg, unsigned int flags, ClassifyErrorSet *out_errors);

GhsCode ClassifyGhs(const ClassifyAggregate &agg, const AuthorizationSet &authorization_set,
                    GhmCode ghm, unsigned int flags);
void CountSupplements(const ClassifyAggregate &agg, const AuthorizationSet &authorization_set,
                      GhsCode ghs, unsigned int flags,
                      SupplementCounters<int16_t> *out_counters);

int PriceGhs(const GhsPriceInfo &price_info, int duration, bool death);
int PriceGhs(const ClassifyAggregate &agg, GhsCode ghs);
int PriceSupplements(const TableIndex &index, const SupplementCounters<int16_t> &days,
                     SupplementCounters<int32_t> *out_prices);

Size ClassifyRaw(const TableSet &table_set, const AuthorizationSet &authorization_set,
                 Span<const Stay> stays, unsigned int flags, ClassifyResult out_results[]);
void Classify(const TableSet &table_set, const AuthorizationSet &authorization_set,
              Span<const Stay> stays, unsigned int flags, HeapArray<ClassifyResult> *out_results);
void ClassifyParallel(const TableSet &table_set, const AuthorizationSet &authorization_set,
                      Span<const Stay> stays, unsigned int flags,
                      HeapArray<ClassifyResult> *out_results);

void Summarize(Span<const ClassifyResult> results, ClassifySummary *out_summary);
