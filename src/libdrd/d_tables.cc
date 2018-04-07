// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../common/kutil.hh"
#include "../common/json.hh"
#include "d_tables.hh"

#define FAIL_PARSE_IF(Filename, Cond) \
    do { \
        if (Cond) { \
            LogError("Malformed binary table file '%1': %2", \
                     (Filename) ? (Filename) : "?", STRINGIFY(Cond)); \
            return false; \
        } \
    } while (false)

Date ConvertDate1980(uint16_t days)
{
    static const int base_days = Date(1979, 12, 31).ToJulianDays();
    return Date::FromJulianDays(base_days + days);
}

static DiagnosisCode ConvertDiagnosisCode(int16_t code123, uint16_t code456)
{
    DiagnosisCode code = {};

    snprintf(code.str, SIZE(code.str), "%c%02d", code123 / 100 + 65, code123 % 100);

    static const char code456_chars[] = " 0123456789+";
    code456 %= 1584;
    code.str[3] = code456_chars[code456 / 132]; code456 %= 132;
    code.str[4] = code456_chars[code456 / 11]; code456 %= 11;
    code.str[5] = code456_chars[code456];
    for (int i = 5; i >= 3 && code.str[i] == ' '; i--) {
        code.str[i] = '\0';
    }

    return code;
}

// TODO: Be careful with overflow in offset and length checks
bool ParseTableHeaders(Span<const uint8_t> file_data, const char *filename,
                       Allocator *str_alloc, HeapArray<TableInfo> *out_tables)
{
    DEFER_NC(out_tables_guard, len = out_tables->len) { out_tables->RemoveFrom(len); };

    // Since FG 10.10b, each tab file can contain several tables, with a different
    // date range for each. The struct layout changed a bit around FG 11.11, which is
    // the first version supported here.
#pragma pack(push, 1)
    struct PackedHeader1111 {
        char signature[8];
        char version[4];
        char date[6];
        char name[8];
        uint8_t pad1;
        uint8_t sections_count;
        uint8_t pad2[4];
	};
    StaticAssert(SIZE(TableInfo::raw_type) > SIZE(PackedHeader1111::name));
    struct PackedSection1111 {
        uint8_t pad1[18];
        uint16_t values_count;
        uint16_t value_len;
        uint32_t raw_len;
        uint32_t raw_offset;
        uint8_t pad2[3];
	};
    struct PackedTablePtr1111 {
        uint16_t date_range[2];
        uint8_t pad1[2]; // No idea what those two bytes are for
        uint32_t raw_offset;
    };
#pragma pack(pop)

    PackedHeader1111 raw_main_header;
    PackedSection1111 raw_main_section;
    {
        FAIL_PARSE_IF(filename, file_data.len < SIZE(PackedHeader1111) + SIZE(PackedSection1111));

        memcpy(&raw_main_header, file_data.ptr, SIZE(PackedHeader1111));
        FAIL_PARSE_IF(filename, raw_main_header.sections_count != 1);

        memcpy(&raw_main_section, file_data.ptr + SIZE(PackedHeader1111), SIZE(PackedSection1111));
        raw_main_section.values_count = BigEndian(raw_main_section.values_count);
        raw_main_section.value_len = BigEndian(raw_main_section.value_len);
        raw_main_section.raw_len = BigEndian(raw_main_section.raw_len);
        raw_main_section.raw_offset = BigEndian(raw_main_section.raw_offset);

        int version = 0, revision = 0;
        sscanf(raw_main_header.version, "%2u%2u", &version, &revision);
        FAIL_PARSE_IF(filename, version < 11 || (version == 11 && revision < 10));
        FAIL_PARSE_IF(filename, raw_main_section.value_len != SIZE(PackedTablePtr1111));
        FAIL_PARSE_IF(filename, file_data.len < SIZE(PackedHeader1111) +
                                raw_main_section.values_count * SIZE(PackedTablePtr1111));
    }

    for (int i = 0; i < raw_main_section.values_count; i++) {
        TableInfo table = {};

        PackedTablePtr1111 raw_table_ptr;
        {
            memcpy(&raw_table_ptr, file_data.ptr + SIZE(PackedHeader1111) +
                                   SIZE(PackedSection1111) + i * SIZE(PackedTablePtr1111),
                   SIZE(PackedTablePtr1111));
            raw_table_ptr.date_range[0] = BigEndian(raw_table_ptr.date_range[0]);
            raw_table_ptr.date_range[1] = BigEndian(raw_table_ptr.date_range[1]);
            raw_table_ptr.raw_offset = BigEndian(raw_table_ptr.raw_offset);

            FAIL_PARSE_IF(filename, file_data.len < (Size)(raw_table_ptr.raw_offset +
                                                           SIZE(PackedHeader1111)));
        }

        PackedHeader1111 raw_table_header;
        PackedSection1111 raw_table_sections[ARRAY_SIZE(table.sections.data)];
        {
            bool weird_section = false;

            memcpy(&raw_table_header, file_data.ptr + raw_table_ptr.raw_offset,
                   SIZE(PackedHeader1111));
            if (UNLIKELY(!memcmp(raw_table_header.signature, "GESTCOMP", 8))) {
                weird_section = true;

                memmove(&raw_table_header.pad1, raw_table_header.name,
                        SIZE(PackedHeader1111) - OFFSET_OF(PackedHeader1111, pad1));
                memcpy(&raw_table_header.name, raw_table_header.signature, SIZE(raw_table_header.name));
            }
            FAIL_PARSE_IF(filename, file_data.len < (Size)(raw_table_ptr.raw_offset +
                                                           (uint32_t)raw_table_header.sections_count * SIZE(PackedSection1111)));
            FAIL_PARSE_IF(filename, raw_table_header.sections_count > ARRAY_SIZE(raw_table_sections));

            for (int j = 0; j < raw_table_header.sections_count; j++) {
                memcpy(&raw_table_sections[j], file_data.ptr + raw_table_ptr.raw_offset +
                                               SIZE(PackedHeader1111) +
                                               j * SIZE(PackedSection1111),
                       SIZE(PackedSection1111));
                if (UNLIKELY(weird_section)) {
                    memmove((uint8_t *)&raw_table_sections[j] + 8, &raw_table_sections[j],
                            SIZE(PackedSection1111) - 8);
                }
                raw_table_sections[j].values_count = BigEndian(raw_table_sections[j].values_count);
                raw_table_sections[j].value_len = BigEndian(raw_table_sections[j].value_len);
                raw_table_sections[j].raw_len = BigEndian(raw_table_sections[j].raw_len);
                raw_table_sections[j].raw_offset = BigEndian(raw_table_sections[j].raw_offset);

                FAIL_PARSE_IF(filename, file_data.len < (Size)(raw_table_ptr.raw_offset +
                                                               raw_table_sections[j].raw_offset +
                                                               raw_table_sections[j].raw_len));
            }
        }

        if (str_alloc) {
            table.filename = DuplicateString(str_alloc, filename).ptr;
        }

        // Parse header information
        sscanf(raw_main_header.date, "%2" SCNd8 "%2" SCNd8 "%4" SCNd16,
               &table.build_date.st.day, &table.build_date.st.month,
               &table.build_date.st.year);
        table.build_date.st.year = (int16_t)(table.build_date.st.year + 2000);
        FAIL_PARSE_IF(filename, !table.build_date.IsValid());
        sscanf(raw_table_header.version, "%2" SCNd16 "%2" SCNd16,
               &table.version[0], &table.version[1]);
        table.limit_dates[0] = ConvertDate1980(raw_table_ptr.date_range[0]);
        table.limit_dates[1] = ConvertDate1980(raw_table_ptr.date_range[1]);
        FAIL_PARSE_IF(filename, table.limit_dates[1] <= table.limit_dates[0]);

        // Table type
        strncpy(table.raw_type, raw_table_header.name, SIZE(raw_table_header.name));
        table.raw_type[SIZE(table.raw_type) - 1] = '\0';
        table.raw_type[strcspn(table.raw_type, " ")] = '\0';
        if (TestStr(table.raw_type, "ARBREDEC")) {
            table.type = TableType::GhmDecisionTree;
        } else if (TestStr(table.raw_type, "DIAG10CR")) {
            table.type = TableType::DiagnosisTable;
        } else if (TestStr(table.raw_type, "CCAMCARA")) {
            table.type = TableType::ProcedureTable;
        } else if (TestStr(table.raw_type, "RGHMINFO")) {
            table.type = TableType::GhmRootTable;
        } else if (TestStr(table.raw_type, "GHSINFO")) {
            table.type = TableType::GhsAccessTable;
        } else if (TestStr(table.raw_type, "TABCOMBI")) {
            table.type = TableType::SeverityTable;
        } else if (TestStr(table.raw_type, "AUTOREFS")) {
            table.type = TableType::AuthorizationTable;
        } else if (TestStr(table.raw_type, "SRCDGACT")) {
            table.type = TableType::SrcPairTable;
        } else {
            table.type = TableType::UnknownTable;
        }

        // Parse table sections
        table.sections.len = raw_table_header.sections_count;
        for (int j = 0; j < raw_table_header.sections_count; j++) {
            FAIL_PARSE_IF(filename, raw_table_sections[j].raw_len !=
                                        (uint32_t)raw_table_sections[j].values_count *
                                        raw_table_sections[j].value_len);
            table.sections[j].raw_offset = (Size)(raw_table_ptr.raw_offset +
                                                  raw_table_sections[j].raw_offset);
            table.sections[j].raw_len = (Size)raw_table_sections[j].raw_len;
            table.sections[j].values_count = raw_table_sections[j].values_count;
            table.sections[j].value_len = raw_table_sections[j].value_len;
        }

        out_tables->Append(table);
    }

    out_tables_guard.disable();
    return true;
}

bool ParseGhmDecisionTree(const uint8_t *file_data,
                          const TableInfo &table, HeapArray<GhmDecisionNode> *out_nodes)
{
    DEFER_NC(out_nodes_guard, len = out_nodes->len) { out_nodes->RemoveFrom(len); };

#pragma pack(push, 1)
    struct PackedTreeNode {
        uint8_t function;
        uint8_t params[2];
        uint8_t children_count;
        uint16_t children_idx;
	};
#pragma pack(pop)

    FAIL_PARSE_IF(table.filename, table.sections.len != 1);
    FAIL_PARSE_IF(table.filename, table.sections[0].value_len != SIZE(PackedTreeNode));

    for (Size i = 0; i < table.sections[0].values_count; i++) {
        GhmDecisionNode ghm_node = {};

        PackedTreeNode raw_node;
        memcpy(&raw_node, file_data + table.sections[0].raw_offset +
                          (size_t)(i * SIZE(PackedTreeNode)), SIZE(PackedTreeNode));
        raw_node.children_idx = BigEndian(raw_node.children_idx);

        if (raw_node.function != 12) {
            ghm_node.type = GhmDecisionNode::Type::Test;
            ghm_node.u.test.function = raw_node.function;
            ghm_node.u.test.params[0] = raw_node.params[0];
            ghm_node.u.test.params[1] = raw_node.params[1];
            if (raw_node.function == 20) {
                ghm_node.u.test.children_idx = raw_node.children_idx + (raw_node.params[0] << 8) +
                                                                        raw_node.params[1];
                ghm_node.u.test.children_count = 1;
            } else {
                ghm_node.u.test.children_idx = raw_node.children_idx;
                ghm_node.u.test.children_count = raw_node.children_count;
            }

            FAIL_PARSE_IF(table.filename, !ghm_node.u.test.children_count);
            FAIL_PARSE_IF(table.filename, ghm_node.u.test.children_idx > table.sections[0].values_count);
            FAIL_PARSE_IF(table.filename, ghm_node.u.test.children_count > table.sections[0].values_count -
                                                                           ghm_node.u.test.children_idx);
        } else {
            static char chars1[] = {0, 'C', 'H', 'K', 'M', 'Z', ' ', ' ', ' ', ' '};
            static char chars4[] = {0, 'A', 'B', 'C', 'D', 'E', 'J', 'Z', ' ', ' '};

            ghm_node.type = GhmDecisionNode::Type::Ghm;
            ghm_node.u.ghm.ghm.parts.cmd = (int8_t)raw_node.params[1];
            ghm_node.u.ghm.ghm.parts.type = chars1[(raw_node.children_idx / 1000) % 10];
            ghm_node.u.ghm.ghm.parts.seq = (int8_t)((raw_node.children_idx / 10) % 100);
            ghm_node.u.ghm.ghm.parts.mode = chars4[raw_node.children_idx % 10];
            ghm_node.u.ghm.error = raw_node.params[0];
        }

        out_nodes->Append(ghm_node);
    }

    out_nodes_guard.disable();
    return true;
}

bool ParseDiagnosisTable(const uint8_t *file_data,
                          const TableInfo &table, HeapArray<DiagnosisInfo> *out_diags)
{
    DEFER_NC(out_diags_guard, len = out_diags->len) { out_diags->RemoveFrom(len); };

#pragma pack(push, 1)
    struct PackedDiagnosisPtr  {
        uint16_t code456;

        uint16_t section2_idx;
        uint8_t section3_idx;
        uint16_t section4_bit;
        uint16_t section4_idx;
	};
#pragma pack(pop)

    FAIL_PARSE_IF(table.filename, table.sections.len != 5);
    FAIL_PARSE_IF(table.filename, table.sections[0].values_count != 26 * 100 ||
                                  table.sections[0].value_len != 2);
    FAIL_PARSE_IF(table.filename, table.sections[1].value_len != SIZE(PackedDiagnosisPtr));
    FAIL_PARSE_IF(table.filename, !table.sections[2].value_len || table.sections[2].value_len % 2 ||
                                  table.sections[2].value_len / 2 > SIZE(DiagnosisInfo::attributes[0].raw));
    FAIL_PARSE_IF(table.filename, !table.sections[3].value_len ||
                                  table.sections[3].value_len > SIZE(DiagnosisInfo::warnings) * 8);
    FAIL_PARSE_IF(table.filename, !table.sections[4].value_len);

    Size block_end = table.sections[1].raw_offset;
    for (int16_t root_idx = 0; root_idx < table.sections[0].values_count; root_idx++) {
        Size block_start = block_end;

        // Find block end
        {
            const uint8_t *end_idx_ptr = file_data + table.sections[0].raw_offset +
                                         root_idx * 2;
            uint16_t end_idx = (uint16_t)((end_idx_ptr[0] << 8) | end_idx_ptr[1]);
            FAIL_PARSE_IF(table.filename, end_idx > table.sections[1].values_count);
            block_end = table.sections[1].raw_offset + end_idx * SIZE(PackedDiagnosisPtr);
        }
        if (block_end == block_start)
            continue;

        for (Size block_offset = block_start; block_offset < block_end;
             block_offset += SIZE(PackedDiagnosisPtr)) {
            DiagnosisInfo diag = {};

            PackedDiagnosisPtr raw_diag_ptr;
            {
                memcpy(&raw_diag_ptr, file_data + block_offset, SIZE(PackedDiagnosisPtr));
                raw_diag_ptr.code456 = BigEndian(raw_diag_ptr.code456);
                raw_diag_ptr.section2_idx = BigEndian(raw_diag_ptr.section2_idx);
                raw_diag_ptr.section4_bit = BigEndian(raw_diag_ptr.section4_bit);
                raw_diag_ptr.section4_idx = BigEndian(raw_diag_ptr.section4_idx);

                FAIL_PARSE_IF(table.filename,
                              raw_diag_ptr.section2_idx >= table.sections[2].values_count);
                FAIL_PARSE_IF(table.filename,
                              raw_diag_ptr.section3_idx >= table.sections[3].values_count);
                FAIL_PARSE_IF(table.filename,
                              raw_diag_ptr.section4_idx >= table.sections[4].values_count);
            }

            diag.diag = ConvertDiagnosisCode(root_idx, raw_diag_ptr.code456);

            // Flags and warnings
            {
                const uint8_t *sex_data = file_data + table.sections[2].raw_offset +
                                          raw_diag_ptr.section2_idx * table.sections[2].value_len;
                memcpy(diag.attributes[0].raw, sex_data, (size_t)(table.sections[2].value_len / 2));
                memcpy(diag.attributes[1].raw, sex_data + table.sections[2].value_len / 2,
                       (size_t)(table.sections[2].value_len / 2));
                if (memcmp(diag.attributes[0].raw, diag.attributes[1].raw, SIZE(diag.attributes[0].raw))) {
                    diag.flags |= (int)DiagnosisInfo::Flag::SexDifference;
                }

                for (int i = 0; i < 2; i++) {
                    diag.attributes[i].cmd = diag.attributes[i].raw[0];
                    diag.attributes[i].jump = diag.attributes[i].raw[1];

                    if (diag.attributes[i].raw[21] & 0x40) {
                        diag.attributes[i].severity = 3;
                    } else if (diag.attributes[i].raw[21] & 0x80) {
                        diag.attributes[i].severity = 2;
                    } else if (diag.attributes[i].raw[20] & 0x1) {
                        diag.attributes[i].severity = 1;
                    } else {
                        diag.attributes[i].severity = 0;
                    }
                }

                const uint8_t *warn_data = file_data + table.sections[3].raw_offset +
                                           raw_diag_ptr.section3_idx * table.sections[3].value_len;
                for (int i = 0; i < table.sections[3].value_len; i++) {
                    if (warn_data[i]) {
                        diag.warnings = (uint16_t)(diag.warnings | (1 << i));
                    }
                }

                diag.exclusion_set_idx = raw_diag_ptr.section4_idx;
                diag.cma_exclusion_mask.offset = (uint8_t)(raw_diag_ptr.section4_bit >> 3);
                diag.cma_exclusion_mask.value =
                    (uint8_t)(0x80 >> (raw_diag_ptr.section4_bit & 0x7));
            }

            out_diags->Append(diag);
        }
    }

    out_diags_guard.disable();
    return true;
}

bool ParseExclusionTable(const uint8_t *file_data,
                         const TableInfo &table,
                         HeapArray<ExclusionInfo> *out_exclusions)
{
    DEFER_NC(out_exclusions_guard, len = out_exclusions->len) { out_exclusions->RemoveFrom(len); };

    FAIL_PARSE_IF(table.filename, table.sections.len != 5);
    FAIL_PARSE_IF(table.filename, !table.sections[4].value_len);
    FAIL_PARSE_IF(table.filename, table.sections[4].value_len > SIZE(ExclusionInfo::raw));

    for (Size i = 0; i < table.sections[4].values_count; i++) {
        ExclusionInfo *excl = out_exclusions->AppendDefault();
        memcpy(excl->raw, file_data + table.sections[4].raw_offset +
                          i * table.sections[4].value_len, (size_t)table.sections[4].value_len);
        memset(excl->raw + table.sections[4].value_len, 0,
               (size_t)(SIZE(excl->raw) - table.sections[4].value_len));
    }

    out_exclusions_guard.disable();
    return true;
}

bool ParseProcedureTable(const uint8_t *file_data,
                         const TableInfo &table, HeapArray<ProcedureInfo> *out_procs)
{
    DEFER_NC(out_proc_guard, len = out_procs->len) { out_procs->RemoveFrom(len); };

#pragma pack(push, 1)
    struct PackedProcedurePtr  {
        char char4;
        uint16_t seq_phase;

        uint16_t section2_idx;
        uint16_t date_min;
        uint16_t date_max;
	};
#pragma pack(pop)

    FAIL_PARSE_IF(table.filename, table.sections.len != 3);
    FAIL_PARSE_IF(table.filename, table.sections[0].values_count != 26 * 26 * 26 ||
                                  table.sections[0].value_len != 2);
    FAIL_PARSE_IF(table.filename, table.sections[1].value_len != SIZE(PackedProcedurePtr));
    FAIL_PARSE_IF(table.filename, !table.sections[2].value_len ||
                                  table.sections[2].value_len > SIZE(ProcedureInfo::bytes));

    Size block_end = table.sections[1].raw_offset;
    for (int16_t root_idx = 0; root_idx < table.sections[0].values_count; root_idx++) {
        Size block_start = block_end;

        // Find block end
        {
            const uint8_t *end_idx_ptr = file_data + table.sections[0].raw_offset +
                                         root_idx * 2;
            uint16_t end_idx = (uint16_t)((end_idx_ptr[0] << 8) | end_idx_ptr[1]);
            FAIL_PARSE_IF(table.filename, end_idx > table.sections[1].values_count);
            block_end = table.sections[1].raw_offset + end_idx * SIZE(PackedProcedurePtr);
        }
        if (block_end == block_start)
            continue;

        char code123[3];
        {
            int16_t root_idx_remain = root_idx;
            for (int i = 0; i < 3; i++) {
                code123[2 - i] = (char)((root_idx_remain % 26) + 65);
                root_idx_remain /= 26;
            }
        }

        for (Size block_offset = block_start; block_offset < block_end;
             block_offset += SIZE(PackedProcedurePtr)) {
            ProcedureInfo proc = {};

            PackedProcedurePtr raw_proc_ptr;
            {
                memcpy(&raw_proc_ptr, file_data + block_offset, SIZE(PackedProcedurePtr));
                raw_proc_ptr.seq_phase = BigEndian(raw_proc_ptr.seq_phase);
                raw_proc_ptr.section2_idx = BigEndian(raw_proc_ptr.section2_idx);
                raw_proc_ptr.date_min = BigEndian(raw_proc_ptr.date_min);
                raw_proc_ptr.date_max = BigEndian(raw_proc_ptr.date_max);

                FAIL_PARSE_IF(table.filename,
                              raw_proc_ptr.section2_idx >= table.sections[2].values_count);
            }

            // CCAM code and phase
            memcpy(proc.proc.str, code123, 3);
            snprintf(proc.proc.str + 3, SIZE(proc.proc.str) - 3, "%c%03u",
                     (raw_proc_ptr.char4 % 26) + 65, raw_proc_ptr.seq_phase / 10 % 1000);
            proc.phase = (char)(raw_proc_ptr.seq_phase % 10);

            // CCAM information and lists
            {
                proc.limit_dates[0] = ConvertDate1980(raw_proc_ptr.date_min);
                if (raw_proc_ptr.date_max < UINT16_MAX) {
                    proc.limit_dates[1] = ConvertDate1980((uint16_t)(raw_proc_ptr.date_max + 1));
                } else {
                    proc.limit_dates[1] = ConvertDate1980(UINT16_MAX);
                }

                const uint8_t *proc_data = file_data + table.sections[2].raw_offset +
                                           raw_proc_ptr.section2_idx * table.sections[2].value_len;
                memcpy(proc.bytes, proc_data, (size_t)table.sections[2].value_len);
            }

            // CCAM activities
            if (proc.bytes[31] & 0x1) {
                proc.activities |= 1 << 1;
            }
            if (proc.bytes[32] & 0x80) {
                proc.activities |= 1 << 2;
            }
            if (proc.bytes[32] & 0x40) {
                proc.activities |= 1 << 3;
            }
            if (proc.bytes[22] & 0x20) {
                proc.activities |= 1 << 4;
            }
            if (proc.bytes[32] & 0x20) {
                proc.activities |= 1 << 5;
            }

            out_procs->Append(proc);
        }
    }

    out_proc_guard.disable();
    return true;
}

bool ParseGhmRootTable(const uint8_t *file_data,
                       const TableInfo &table, HeapArray<GhmRootInfo> *out_ghm_roots)
{
    DEFER_NC(out_ghm_roots_guard, len = out_ghm_roots->len) { out_ghm_roots->RemoveFrom(len); };

#pragma pack(push, 1)
    struct PackedGhmRoot {
        uint8_t cmd;
        uint16_t type_seq;
        uint8_t young_severity_mode;
        uint8_t old_severity_mode;
        uint8_t duration_severity_mode;
        uint8_t pad1[2];
        uint8_t cma_exclusion_offset;
        uint8_t cma_exclusion_mask;
        uint8_t confirm_duration_treshold;
        uint8_t childbirth_severity_mode; // Appeared in FG 11d
	};
#pragma pack(pop)

    FAIL_PARSE_IF(table.filename, table.sections.len != 1);
    if (table.version[0] > 11 || (table.version[0] == 11 && table.version[1] > 14)) {
        FAIL_PARSE_IF(table.filename, table.sections[0].value_len != SIZE(PackedGhmRoot));
    } else {
        FAIL_PARSE_IF(table.filename, table.sections[0].value_len != SIZE(PackedGhmRoot) - 1);
    }

    for (Size i = 0; i < table.sections[0].values_count; i++) {
        GhmRootInfo ghm_root = {};

        PackedGhmRoot raw_ghm_root;
        memcpy(&raw_ghm_root, file_data + table.sections[0].raw_offset +
                              i * table.sections[0].value_len,
               (size_t)table.sections[0].value_len);
        raw_ghm_root.type_seq = BigEndian(raw_ghm_root.type_seq);

        // GHM root code
        {
            static char chars1[] = {0, 'C', 'H', 'K', 'M', 'Z', ' ', ' ', ' ', ' '};

            ghm_root.ghm_root.parts.cmd = (int8_t)raw_ghm_root.cmd;
            ghm_root.ghm_root.parts.type = chars1[raw_ghm_root.type_seq / 100 % 10];
            ghm_root.ghm_root.parts.seq = (int8_t)(raw_ghm_root.type_seq % 100);
        }

        switch (raw_ghm_root.duration_severity_mode) {
            case 1: {
                ghm_root.allow_ambulatory = true;
            } break;
            case 2: {
                ghm_root.short_duration_treshold = 1;
            } break;
            case 3: {
                ghm_root.short_duration_treshold = 2;
            } break;
            case 4: {
                ghm_root.short_duration_treshold = 3;
            } break;
        }
        ghm_root.confirm_duration_treshold = (int8_t)raw_ghm_root.confirm_duration_treshold;

        if (raw_ghm_root.young_severity_mode == 1) {
            ghm_root.young_age_treshold = 2;
            ghm_root.young_severity_limit = 1;
        }
        switch (raw_ghm_root.old_severity_mode) {
            case 1: {
                ghm_root.old_age_treshold = 70;
                ghm_root.old_severity_limit = 1;
            } break;
            case 2: {
                ghm_root.old_age_treshold = 80;
                ghm_root.old_severity_limit = 1;
            } break;
            case 3: {
                ghm_root.old_age_treshold = 70;
                ghm_root.old_severity_limit = 2;
            } break;
            case 4: {
                ghm_root.old_age_treshold = 80;
                ghm_root.old_severity_limit = 2;
            } break;
            case 5: {
                ghm_root.old_age_treshold = 70;
                ghm_root.old_severity_limit = 3;
            } break;
            case 6: {
                ghm_root.old_age_treshold = 80;
                ghm_root.old_severity_limit = 3;
            } break;
        }

        if (table.sections[0].value_len >= 12 && raw_ghm_root.childbirth_severity_mode) {
            FAIL_PARSE_IF(table.filename, raw_ghm_root.childbirth_severity_mode < 2 ||
                                          raw_ghm_root.childbirth_severity_mode > 4);
            ghm_root.childbirth_severity_list = (int8_t)(raw_ghm_root.childbirth_severity_mode - 1);
        }

        ghm_root.cma_exclusion_mask.offset = raw_ghm_root.cma_exclusion_offset;
        ghm_root.cma_exclusion_mask.value = raw_ghm_root.cma_exclusion_mask;

        out_ghm_roots->Append(ghm_root);
    }

    out_ghm_roots_guard.disable();
    return true;
}

bool ParseSeverityTable(const uint8_t *file_data,
                        const TableInfo &table, int section_idx,
                        HeapArray<ValueRangeCell<2>> *out_cells)
{
    DEFER_NC(out_cells_guard, len = out_cells->len) { out_cells->RemoveFrom(len); };

#pragma pack(push, 1)
    struct PackedCell {
        uint16_t var1_min;
        uint16_t var1_max;
        uint16_t var2_min;
        uint16_t var2_max;
        uint16_t value;
	};
#pragma pack(pop)

    FAIL_PARSE_IF(table.filename, section_idx >= table.sections.len);
    FAIL_PARSE_IF(table.filename, table.sections[section_idx].value_len != SIZE(PackedCell));

    for (Size i = 0; i < table.sections[section_idx].values_count; i++) {
        ValueRangeCell<2> cell = {};

        PackedCell raw_cell;
        memcpy(&raw_cell, file_data + table.sections[section_idx].raw_offset +
                          i * SIZE(PackedCell), SIZE(PackedCell));
        raw_cell.var1_min = BigEndian(raw_cell.var1_min);
        raw_cell.var1_max = BigEndian(raw_cell.var1_max);
        raw_cell.var2_min = BigEndian(raw_cell.var2_min);
        raw_cell.var2_max = BigEndian(raw_cell.var2_max);
        raw_cell.value = BigEndian(raw_cell.value);

        cell.limits[0].min = raw_cell.var1_min;
        cell.limits[0].max = raw_cell.var1_max + 1;
        cell.limits[1].min = raw_cell.var2_min;
        cell.limits[1].max = raw_cell.var2_max + 1;
        cell.value = raw_cell.value;

        out_cells->Append(cell);
    }

    out_cells_guard.disable();
    return true;
}

bool ParseGhsAccessTable(const uint8_t *file_data,
                         const TableInfo &table, HeapArray<GhsAccessInfo> *out_ghs)
{
    Size start_ghs_len = out_ghs->len;
    DEFER_N(out_ghs_guard) { out_ghs->RemoveFrom(start_ghs_len); };

#pragma pack(push, 1)
    struct PackedGhsNode {
        uint8_t cmd;
        uint16_t type_seq;
        uint8_t low_duration_mode;
        uint8_t function;
        uint8_t params[2];
        uint8_t skip_after_failure;
        uint8_t valid_ghs;
        struct {
            uint16_t ghs_code;
             // We get those from the pricing tables, so they're ignored here
            uint16_t high_duration_treshold;
            uint16_t low_duration_treshold;
        } sectors[2];
	};
    StaticAssert(ARRAY_SIZE(PackedGhsNode().sectors) == ARRAY_SIZE(GhsAccessInfo().ghs));
#pragma pack(pop)

    FAIL_PARSE_IF(table.filename, table.sections.len != 1);
    FAIL_PARSE_IF(table.filename, table.sections[0].value_len != SIZE(PackedGhsNode));

    GhsAccessInfo current_ghs = {};
    for (Size i = 0; i < table.sections[0].values_count; i++) {
        PackedGhsNode raw_ghs_node;
        memcpy(&raw_ghs_node, file_data + table.sections[0].raw_offset +
                         i * SIZE(PackedGhsNode), SIZE(PackedGhsNode));
        raw_ghs_node.type_seq = BigEndian(raw_ghs_node.type_seq);
        for (int j = 0; j < 2; j++) {
            raw_ghs_node.sectors[j].ghs_code = BigEndian(raw_ghs_node.sectors[j].ghs_code);
            raw_ghs_node.sectors[j].high_duration_treshold =
                BigEndian(raw_ghs_node.sectors[j].high_duration_treshold);
            raw_ghs_node.sectors[j].low_duration_treshold =
                BigEndian(raw_ghs_node.sectors[j].low_duration_treshold);
        }

        if (!current_ghs.ghm.IsValid()) {
            static char chars1[] = {0, 'C', 'H', 'K', 'M', 'Z'};
            static char chars4[] = {0, 'A', 'B', 'C', 'D', 'E', 'J',
                                    'Z', 'T', '1', '2', '3', '4'};

            current_ghs.ghm.parts.cmd = (int8_t)raw_ghs_node.cmd;
            current_ghs.ghm.parts.type = chars1[raw_ghs_node.type_seq / 10000 % 6];
            current_ghs.ghm.parts.seq = (int8_t)(raw_ghs_node.type_seq / 100 % 100);
            current_ghs.ghm.parts.mode = chars4[raw_ghs_node.type_seq % 100 % 13];
        }

        switch (raw_ghs_node.function) {
            case 0: {
                FAIL_PARSE_IF(table.filename, !raw_ghs_node.valid_ghs);
            } break;

            case 1: {
                ListMask mask;
                mask.offset = raw_ghs_node.params[0];
                mask.value = raw_ghs_node.params[1];
                FAIL_PARSE_IF(table.filename, !current_ghs.procedure_masks.Available());
                current_ghs.procedure_masks.Append(mask);
            } break;

            case 2: {
                FAIL_PARSE_IF(table.filename, raw_ghs_node.params[0]);
                FAIL_PARSE_IF(table.filename, current_ghs.unit_authorization);
                current_ghs.unit_authorization = (int8_t)raw_ghs_node.params[1];
            } break;

            case 3: {
                FAIL_PARSE_IF(table.filename, raw_ghs_node.params[0]);
                FAIL_PARSE_IF(table.filename, current_ghs.bed_authorization);
                current_ghs.bed_authorization = (int8_t)raw_ghs_node.params[1];
            } break;

            case 5: {
                FAIL_PARSE_IF(table.filename, current_ghs.main_diagnosis_mask.offset ||
                                              current_ghs.main_diagnosis_mask.value);
                current_ghs.main_diagnosis_mask.offset = raw_ghs_node.params[0];
                current_ghs.main_diagnosis_mask.value = raw_ghs_node.params[1];
            } break;

            case 6: {
                FAIL_PARSE_IF(table.filename, raw_ghs_node.params[0]);
                FAIL_PARSE_IF(table.filename, current_ghs.minimal_duration);
                current_ghs.minimal_duration = (int8_t)(raw_ghs_node.params[1] + 1);
            } break;

            case 7: {
                FAIL_PARSE_IF(table.filename, current_ghs.diagnosis_mask.offset ||
                                              current_ghs.diagnosis_mask.value);
                current_ghs.diagnosis_mask.offset = raw_ghs_node.params[0];
                current_ghs.diagnosis_mask.value = raw_ghs_node.params[1];
            } break;

            case 8: {
                FAIL_PARSE_IF(table.filename, raw_ghs_node.params[0]);
                FAIL_PARSE_IF(table.filename, current_ghs.minimal_age);
                current_ghs.minimal_age = (int8_t)raw_ghs_node.params[1];
            } break;

            default: {
                FAIL_PARSE_IF(table.filename, true);
            } break;
        }

        if (raw_ghs_node.valid_ghs) {
            for (Size j = 0; j < ARRAY_SIZE(current_ghs.ghs); j++) {
                current_ghs.ghs[j].number = (int16_t)raw_ghs_node.sectors[j].ghs_code;
            }
            out_ghs->Append(current_ghs);
            current_ghs = {};
        }
    }

    std::stable_sort(out_ghs->begin() + start_ghs_len, out_ghs->end(),
                     [](const GhsAccessInfo &ghs_info1, const GhsAccessInfo &ghs_info2) {
        int root_cmp = MultiCmp(ghs_info1.ghm.parts.cmd - ghs_info2.ghm.parts.cmd,
                                ghs_info1.ghm.parts.type - ghs_info2.ghm.parts.type,
                                ghs_info1.ghm.parts.seq - ghs_info2.ghm.parts.seq);
        if (root_cmp) {
            return root_cmp < 0;
        } else if (ghs_info1.ghm.parts.mode >= 'J' && ghs_info2.ghm.parts.mode < 'J') {
            return true;
        } else if (ghs_info2.ghm.parts.mode >= 'J' && ghs_info1.ghm.parts.mode < 'J') {
            return false;
        } else {
            return ghs_info1.ghm.parts.mode < ghs_info2.ghm.parts.mode;
        }
    });

    out_ghs_guard.disable();
    return true;
}

bool ParseAuthorizationTable(const uint8_t *file_data,
                             const TableInfo &table, HeapArray<AuthorizationInfo> *out_auths)
{
    DEFER_NC(out_auths_guard, len = out_auths->len) { out_auths->RemoveFrom(len); };

#pragma pack(push, 1)
    struct PackedAuthorization {
        uint8_t code;
        uint8_t function;
        uint8_t global;
	};
#pragma pack(pop)

    FAIL_PARSE_IF(table.filename, table.sections.len != 2);
    FAIL_PARSE_IF(table.filename, table.sections[0].value_len != 3 ||
                                  table.sections[0].value_len != 3);

    for (Size i = 0; i < 2; i++) {
        for (Size j = 0; j < table.sections[i].values_count; j++) {
            AuthorizationInfo auth = {};

            PackedAuthorization raw_auth;
            memcpy(&raw_auth, file_data + table.sections[i].raw_offset +
                                          j * SIZE(PackedAuthorization),
                   SIZE(PackedAuthorization));

            if (i == 0) {
                auth.type.st.scope = AuthorizationScope::Bed;
            } else if (!raw_auth.global) {
                auth.type.st.scope = AuthorizationScope::Unit;
            } else {
                auth.type.st.scope = AuthorizationScope::Facility;
            }
            auth.type.st.code = (int8_t)raw_auth.code;
            auth.function = (int8_t)raw_auth.function;

            out_auths->Append(auth);
        }
    }

    out_auths_guard.disable();
    return true;
}

bool ParseSrcPairTable(const uint8_t *file_data,
                       const TableInfo &table, int section_idx,
                       HeapArray<SrcPair> *out_pairs)
{
    DEFER_NC(out_pairs_guard, len = out_pairs->len) { out_pairs->RemoveFrom(len); };

#pragma pack(push, 1)
    struct PackedPair {
        uint16_t diag_code123;
        uint16_t diag_code456;
        uint16_t proc_code123;
        uint16_t proc_code456;
	};
#pragma pack(pop)

    FAIL_PARSE_IF(table.filename, section_idx >= table.sections.len);
    FAIL_PARSE_IF(table.filename, table.sections[section_idx].value_len != SIZE(PackedPair));

    for (Size i = 0; i < table.sections[section_idx].values_count; i++) {
        SrcPair pair = {};

        PackedPair raw_pair;
        memcpy(&raw_pair, file_data + table.sections[section_idx].raw_offset +
                          i * SIZE(PackedPair), SIZE(PackedPair));
        raw_pair.diag_code123 = BigEndian(raw_pair.diag_code123);
        raw_pair.diag_code456 = BigEndian(raw_pair.diag_code456);
        raw_pair.proc_code123 = BigEndian(raw_pair.proc_code123);
        raw_pair.proc_code456 = BigEndian(raw_pair.proc_code456);

        pair.diag = ConvertDiagnosisCode((int16_t)raw_pair.diag_code123, raw_pair.diag_code456);
        {
            uint16_t code123_remain = raw_pair.proc_code123;
            for (int j = 0; j < 3; j++) {
                pair.proc.str[2 - j] = (char)((code123_remain % 26) + 65);
                code123_remain /= 26;
            }
            snprintf(pair.proc.str + 3, SIZE(pair.proc.str) - 3, "%c%03u",
                     (raw_pair.proc_code456 / 1000 % 26) + 65,
                     raw_pair.proc_code456 % 1000);
        }

        out_pairs->Append(pair);
    }

    out_pairs_guard.disable();
    return true;
}

class JsonPricesHandler: public BaseJsonHandler<JsonPricesHandler> {
    enum class State {
        Default,
        TableArray,
        TableObject,
        GhsPriceArray,
        GhsPriceObject,
        GhsPriceSectorObject,
        SupplementPriceObject,
        SupplementPriceSectorObject
    };

    State state = State::Default;

    PriceTable price_table;
    int sector;
    GhsPriceInfo price_info[2] = {};

public:
    HeapArray<PriceTable> *out_price_tables;

    JsonPricesHandler(HeapArray<PriceTable> *out_price_tables = nullptr)
        : out_price_tables(out_price_tables) {}

    bool Branch(JsonBranchType type, const char *key)
    {
        switch (state) {
            case State::Default: {
                switch (type) {
                    case JsonBranchType::Array: { state = State::TableArray; } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::TableArray: {
                switch (type) {
                    case JsonBranchType::Object: { state = State::TableObject; } break;
                    case JsonBranchType::EndArray: { state = State::Default; } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::TableObject: {
                switch (type) {
                    case JsonBranchType::Array: {
                        if (TestStr(key, "ghs")) {
                            state = State::GhsPriceArray;
                        } else {
                            return UnexpectedBranch(type);
                        }
                    } break;
                    case JsonBranchType::Object: {
                        if (TestStr(key, "supplements")) {
                            state = State::SupplementPriceObject;
                        } else {
                            return UnexpectedBranch(type);
                        }
                    } break;
                    case JsonBranchType::EndObject: {
                        if (price_table.date.value) {
                            PriceTable *table_it = out_price_tables->AppendDefault();
                            memmove(table_it, &price_table, SIZE(price_table));
                            memset(&price_table, 0, SIZE(price_table));
                        } else {
                            LogError("Missing date attribute");
                        }
                        state = State::TableArray;
                    } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::GhsPriceArray: {
                switch (type) {
                    case JsonBranchType::Object: { state = State::GhsPriceObject; } break;
                    case JsonBranchType::EndArray: { state = State::TableObject; } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::GhsPriceObject: {
                switch (type) {
                    case JsonBranchType::Object: {
                        if (TestStr(key, "public")) {
                            state = State::GhsPriceSectorObject;
                            sector = 0;
                        } else if (TestStr(key, "private")) {
                            state = State::GhsPriceSectorObject;
                            sector = 1;
                        } else {
                            return UnexpectedBranch(type);
                        }
                    } break;
                    case JsonBranchType::EndObject: {
                        for (int i = 0; i < 2; i++) {
                            price_info[i].ghs = price_info[0].ghs;
                            price_table.ghs_prices[i].Append(price_info[i]);
                            price_info[i] = {};
                        }

                        state = State::GhsPriceArray;
                    } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::GhsPriceSectorObject: {
                switch (type) {
                    case JsonBranchType::EndObject: { state = State::GhsPriceObject; } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::SupplementPriceObject: {
                switch (type) {
                    case JsonBranchType::Object: {
                        if (TestStr(key, "public")) {
                            state = State::SupplementPriceSectorObject;
                            sector = 0;
                        } else if (TestStr(key, "private")) {
                            state = State::SupplementPriceSectorObject;
                            sector = 1;
                        } else {
                            return UnexpectedBranch(type);
                        }
                    } break;
                    case JsonBranchType::EndObject: { state = State::TableObject; } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;

            case State::SupplementPriceSectorObject: {
                switch (type) {
                    case JsonBranchType::EndObject: { state = State::SupplementPriceObject; } break;
                    default: { return UnexpectedBranch(type); } break;
                }
            } break;
        }

        return true;
    }

    bool Value(const char *key, const JsonValue &value)
    {
        switch (state) {
            case State::TableObject: {
                if (TestStr(key, "build_date")) {
                    SetDate(value, &price_table.build_date);
                } else if (TestStr(key, "date")) {
                    SetDate(value, &price_table.date);
                } else {
                    return UnknownAttribute(key);
                }
            } break;

            case State::GhsPriceObject: {
                if (TestStr(key, "ghs")) {
                    SetInt(value, &price_info[0].ghs.number);
                } else {
                    return UnknownAttribute(key);
                }
            } break;

            case State::GhsPriceSectorObject: {
                if (TestStr(key, "price_cents")) {
                    SetInt(value, &price_info[sector].price_cents);
                } else if (TestStr(key, "exh_treshold")) {
                    SetInt(value, &price_info[sector].exh_treshold);
                } else if (TestStr(key, "exh_cents")) {
                    SetInt(value, &price_info[sector].exh_cents);
                } else if (TestStr(key, "exb_treshold")) {
                    SetInt(value, &price_info[sector].exb_treshold);
                } else if (TestStr(key, "exb_cents")) {
                    SetInt(value, &price_info[sector].exb_cents);
                } else if (TestStr(key, "exb_once")) {
                    SetFlag(value, &price_info[sector].flags, (int)GhsPriceInfo::Flag::ExbOnce);
                } else {
                    return UnknownAttribute(key);
                }
            } break;

            case State::SupplementPriceSectorObject: {
                if (TestStr(key, "rea_cents")) {
                    SetInt(value, &price_table.supplement_cents[sector].st.rea);
                } else if (TestStr(key, "stf_cents")) {
                    SetInt(value, &price_table.supplement_cents[sector].st.reasi);
                    SetInt(value, &price_table.supplement_cents[sector].st.si);
                } else if (TestStr(key, "src_cents")) {
                    SetInt(value, &price_table.supplement_cents[sector].st.src);
                } else if (TestStr(key, "nn1_cents")) {
                    SetInt(value, &price_table.supplement_cents[sector].st.nn1);
                } else if (TestStr(key, "nn2_cents")) {
                    SetInt(value, &price_table.supplement_cents[sector].st.nn2);
                } else if (TestStr(key, "nn3_cents")) {
                    SetInt(value, &price_table.supplement_cents[sector].st.nn3);
                } else if (TestStr(key, "rep_cents")) {
                    SetInt(value, &price_table.supplement_cents[sector].st.rep);
                } else if (TestStr(key, "ant_cents")) {
                    // Not supported yet
                } else if (TestStr(key, "dip_cents")) {
                    // Not supported yet
                } else if (TestStr(key, "rap_cents")) {
                    // Not supported yet
                } else if (TestStr(key, "sdc_cents")) {
                    // Not supported yet
                } else {
                    return UnknownAttribute(key);
                }
            } break;

            default: { return UnexpectedValue(); } break;
        }

        return true;
    }
};

bool ParsePricesJson(StreamReader &st, HeapArray<PriceTable> *out_tables)
{
    DEFER_NC(out_guard, len = out_tables->len) { out_tables->RemoveFrom(len); };

    {
        JsonPricesHandler json_handler(out_tables);
        if (!ParseJsonFile(st, &json_handler))
            return false;
    }

    out_guard.disable();
    return true;
}

const TableIndex *TableSet::FindIndex(Date date) const
{
    if (date.value) {
        for (Size i = indexes.len; i-- > 0;) {
            if (date >= indexes[i].limit_dates[0] && date < indexes[i].limit_dates[1])
                return &indexes[i];
        }
    } else if (indexes.len) {
        return &indexes[indexes.len - 1];
    }
    return nullptr;
}

bool TableSetBuilder::LoadAtihTab(StreamReader &st)
{
    Span<uint8_t> raw_data;
    {
        HeapArray<uint8_t> raw_buf(&file_alloc);
        if (st.ReadAll(Megabytes(8), &raw_buf) < 0)
            return false;
        raw_data = raw_buf.Leak();
    }

    Size start_len = set.tables.len;
    if (!ParseTableHeaders(raw_data, st.filename, &set.str_alloc, &set.tables))
        return false;

    for (Size i = start_len; i < set.tables.len; i++) {
        if (set.tables[i].type == TableType::UnknownTable)
            return true;

        TableLoadInfo load_info = {};
        load_info.table_idx = i;
        load_info.u.raw_data = raw_data;
        table_loads.Append(load_info);
    }

    return true;
}

bool TableSetBuilder::LoadPriceJson(StreamReader &st)
{
    Size start_len = price_tables.len;

    if (!ParsePricesJson(st, &price_tables))
        return false;

    for (Size i = start_len; i < price_tables.len; i++) {
        const PriceTable &price_table = price_tables[i];

        TableLoadInfo load_info = {};
        load_info.table_idx = set.tables.len;
        load_info.u.price_table_idx = i;
        table_loads.Append(load_info);

        TableInfo table_info = {};
        table_info.filename = DuplicateString(&set.str_alloc, st.filename).ptr;
        table_info.build_date = price_table.build_date;
        table_info.limit_dates[0] = price_table.date;
        table_info.limit_dates[1] = ConvertDate1980(UINT16_MAX);
        strcpy(table_info.raw_type, "PRICESJS");
        table_info.type = TableType::PriceTable;
        set.tables.Append(table_info);
    }

    return true;
}

bool TableSetBuilder::LoadFiles(Span<const char *const> tab_filenames,
                                Span<const char *const> price_filenames)
{
    bool success = true;

    for (const char *filename: tab_filenames) {
        LocalArray<char, 16> extension;
        CompressionType compression_type;
        extension.len = GetPathExtension(filename, extension.data, &compression_type);

        if (!TestStr(extension, ".tab")) {
            LogError("Cannot load table file '%1' with unknown extension '%2'",
                     filename, extension);
            success = false;
            continue;
        }

        StreamReader st(filename, compression_type);
        if (st.error) {
            success = false;
            continue;
        }
        success &= LoadAtihTab(st);
    }

    for (const char *filename: price_filenames) {
        LocalArray<char, 16> extension;
        CompressionType compression_type;
        extension.len = GetPathExtension(filename, extension.data, &compression_type);

        if (!TestStr(extension, ".json")) {
            LogError("Cannot load price file '%1' with unknown extension '%2'",
                     filename, extension);
            success = false;
            continue;
        }

        StreamReader st(filename, compression_type);
        if (st.error) {
            success = false;
            continue;
        }
        success &= LoadPriceJson(st);
    }

    return success;
}

bool TableSetBuilder::Finish(TableSet *out_set)
{
    bool success = true;

    std::sort(table_loads.begin(), table_loads.end(),
              [&](const TableLoadInfo &tab_load_info1, const TableLoadInfo &tab_load_info2) {
        const TableInfo &table_info1 = set.tables[tab_load_info1.table_idx];
        const TableInfo &table_info2 = set.tables[tab_load_info2.table_idx];

        return MultiCmp(table_info1.limit_dates[0] - table_info2.limit_dates[0],
                        table_info1.version[0] - table_info2.version[0],
                        table_info1.version[1] - table_info2.version[1],
                        table_info1.build_date - table_info2.build_date) < 0;
    });

    TableLoadInfo *active_tables[ARRAY_SIZE(TableTypeNames)] = {};
    Date start_date = {}, end_date = {};
    for (TableLoadInfo &load_info: table_loads) {
        const TableInfo &table_info = set.tables[load_info.table_idx];

        while (end_date.value && table_info.limit_dates[0] >= end_date) {
            success &= CommitIndex(start_date, end_date, active_tables);

            start_date = {};
            Date next_end_date = {};
            for (Size i = 0; i < ARRAY_SIZE(active_tables); i++) {
                if (!active_tables[i])
                    continue;

                const TableInfo &active_info = set.tables[active_tables[i]->table_idx];

                if (active_info.limit_dates[1] == end_date) {
                    active_tables[i] = nullptr;
                } else if (!next_end_date.value || active_info.limit_dates[1] < next_end_date) {
                    next_end_date = active_info.limit_dates[1];
                }
            }

            start_date = table_info.limit_dates[0];
            end_date = next_end_date;
        }

        if (start_date.value) {
            if (table_info.limit_dates[0] > start_date) {
                success &= CommitIndex(start_date, table_info.limit_dates[0], active_tables);
                start_date = table_info.limit_dates[0];
            }
        } else {
            start_date = table_info.limit_dates[0];
        }
        if (!end_date.value || table_info.limit_dates[1] < end_date) {
            end_date = table_info.limit_dates[1];
        }

        active_tables[(int)table_info.type] = &load_info;
    }
    success &= CommitIndex(start_date, end_date, active_tables);

    for (Size i = 0 ; i < set.indexes.len; i++) {
        TableIndex &index = set.indexes[i];

#define FIX_SPAN(SpanName) \
            index.SpanName.ptr = set.store.SpanName.ptr + (Size)index.SpanName.ptr
#define BUILD_MAP(IndexName, MapPtrName, MapName, TableType) \
            do { \
                if (!i || index.changed_tables & MaskEnum(TableType)) { \
                    auto map = set.maps.MapName.AppendDefault(); \
                    for (auto &value: index.IndexName) { \
                        map->Append(&value); \
                    } \
                    index.MapPtrName = map; \
                } else { \
                    index.MapPtrName = &set.maps.MapName[set.maps.MapName.len - 1]; \
                } \
            } while (false)

        FIX_SPAN(ghm_nodes);
        FIX_SPAN(diagnoses);
        FIX_SPAN(exclusions);
        FIX_SPAN(procedures);
        FIX_SPAN(ghm_roots);
        FIX_SPAN(gnn_cells);
        FIX_SPAN(cma_cells[0]);
        FIX_SPAN(cma_cells[1]);
        FIX_SPAN(cma_cells[2]);
        FIX_SPAN(ghs);
        FIX_SPAN(ghs_prices[0]);
        FIX_SPAN(ghs_prices[1]);
        FIX_SPAN(authorizations);
        FIX_SPAN(src_pairs[0]);
        FIX_SPAN(src_pairs[1]);

        BUILD_MAP(diagnoses, diagnoses_map, diagnoses, TableType::DiagnosisTable);
        BUILD_MAP(procedures, procedures_map, procedures, TableType::ProcedureTable);
        BUILD_MAP(ghm_roots, ghm_roots_map, ghm_roots, TableType::GhmRootTable);
        BUILD_MAP(ghs, ghm_to_ghs_map, ghm_to_ghs, TableType::GhsAccessTable);
        BUILD_MAP(ghs, ghm_root_to_ghs_map, ghm_root_to_ghs, TableType::GhsAccessTable);
        BUILD_MAP(authorizations, authorizations_map, authorizations, TableType::GhsAccessTable);
        BUILD_MAP(ghs_prices[0], ghs_prices_map[0], ghs_prices[0], TableType::PriceTable);
        BUILD_MAP(ghs_prices[1], ghs_prices_map[1], ghs_prices[1], TableType::PriceTable);

#undef BUILD_MAP
#undef FIX_SPAN
    }

    if (!success)
        return false;

    SwapMemory(out_set, &set, SIZE(set));
    return true;
}

bool TableSetBuilder::CommitIndex(Date start_date, Date end_date,
                                  TableSetBuilder::TableLoadInfo *current_tables[])
{
    bool success = true;

    TableIndex index = {};

    index.limit_dates[0] = start_date;
    index.limit_dates[1] = end_date;

#define CHECK_PIECE(TableType) \
            do { \
                if (!current_tables[(int)(TableType)]) { \
                    pieces.Append(TableTypeNames[(int)(TableType)]); \
                } \
            } while (false)
#define LOAD_TABLE(MemberName, LoadFunc, ...) \
        do { \
            if (!load_info->loaded) { \
                index.MemberName.ptr = (decltype(index.MemberName.ptr))set.store.MemberName.len; \
                success &= LoadFunc(__VA_ARGS__, &set.store.MemberName); \
                index.MemberName.len = set.store.MemberName.len - (Size)index.MemberName.ptr; \
                index.changed_tables |= (uint32_t)(1 << i); \
            } else { \
                index.MemberName = set.indexes[set.indexes.len - 1].MemberName; \
            } \
        } while (false)

    // FIXME: Validate all tables (some were not always needed)
    {
        LocalArray<FmtArg, ARRAY_SIZE(TableTypeNames)> pieces;

        CHECK_PIECE(TableType::GhmDecisionTree);
        CHECK_PIECE(TableType::DiagnosisTable);
        CHECK_PIECE(TableType::ProcedureTable);
        CHECK_PIECE(TableType::GhmRootTable);
        CHECK_PIECE(TableType::GhsAccessTable);

        if (pieces.len) {
            LogDebug("Missing pieces to make index from %1 to %2: %3", start_date, end_date,
                     pieces);
            return true;
        }
    }

    Size active_count = 0;
    for (Size i = 0; i < ARRAY_SIZE(index.tables); i++) {
        if (!current_tables[i])
            continue;

        TableLoadInfo *load_info = current_tables[i];
        const TableInfo &table_info = set.tables[load_info->table_idx];

        switch ((TableType)i) {
            case TableType::GhmDecisionTree: {
                LOAD_TABLE(ghm_nodes, ParseGhmDecisionTree, load_info->u.raw_data.ptr, table_info);
            } break;
            case TableType::DiagnosisTable: {
                LOAD_TABLE(diagnoses, ParseDiagnosisTable, load_info->u.raw_data.ptr, table_info);
                LOAD_TABLE(exclusions, ParseExclusionTable, load_info->u.raw_data.ptr, table_info);
            } break;
            case TableType::ProcedureTable: {
                LOAD_TABLE(procedures, ParseProcedureTable, load_info->u.raw_data.ptr, table_info);
            } break;
            case TableType::GhmRootTable: {
                LOAD_TABLE(ghm_roots, ParseGhmRootTable, load_info->u.raw_data.ptr, table_info);
            } break;
            case TableType::SeverityTable: {
                LOAD_TABLE(gnn_cells, ParseSeverityTable,
                           load_info->u.raw_data.ptr, table_info, 0);
                LOAD_TABLE(cma_cells[0], ParseSeverityTable,
                           load_info->u.raw_data.ptr, table_info, 1);
                LOAD_TABLE(cma_cells[1], ParseSeverityTable,
                           load_info->u.raw_data.ptr, table_info, 2);
                LOAD_TABLE(cma_cells[2], ParseSeverityTable,
                           load_info->u.raw_data.ptr, table_info, 3);
            } break;

            case TableType::GhsAccessTable: {
                LOAD_TABLE(ghs, ParseGhsAccessTable, load_info->u.raw_data.ptr, table_info);
            } break;
            case TableType::AuthorizationTable: {
                LOAD_TABLE(authorizations, ParseAuthorizationTable,
                           load_info->u.raw_data.ptr, table_info);
            } break;
            case TableType::SrcPairTable: {
                LOAD_TABLE(src_pairs[0], ParseSrcPairTable,
                           load_info->u.raw_data.ptr, table_info, 0);
                LOAD_TABLE(src_pairs[1], ParseSrcPairTable,
                           load_info->u.raw_data.ptr, table_info, 1);
            } break;

            case TableType::PriceTable: {
                if (!load_info->loaded) {
                    const PriceTable &price_table = price_tables[load_info->u.price_table_idx];

                    for (int j = 0; j < 2; j++) {
                        index.ghs_prices[j].ptr = (GhsPriceInfo *)set.store.ghs_prices[j].len;
                        set.store.ghs_prices[j].Append(price_table.ghs_prices[j]);
                        index.ghs_prices[j].len = set.store.ghs_prices[j].len - (Size)index.ghs_prices[j].ptr;
                        index.supplement_prices[j] =
                            set.store.supplement_prices[j].Append(price_table.supplement_cents[j]);
                    }
                    index.changed_tables |= (uint32_t)(1 << i);
                } else {
                    for (int j = 0; j < 2; j++) {
                        index.ghs_prices[j] = set.indexes[set.indexes.len - 1].ghs_prices[j];
                        index.supplement_prices[j] = set.indexes[set.indexes.len - 1].supplement_prices[j];
                    }
                }
            } break;

            case TableType::UnknownTable:
                break;
        }
        load_info->loaded = true;
        index.tables[i] = &table_info;

        active_count++;
    }

    if (active_count) {
        set.indexes.Append(index);
    }

#undef LOAD_TABLE
#undef CHECK_PIECE

    return success;
}

template <typename T, typename U, typename Handler>
Span<const T> FindSpan(Span<const T> arr, const HashTable<U, const T *, Handler> *map, U code)
{
    Span<const T> ret = {};

    ret.ptr = map->FindValue(code, nullptr);
    if (ret.ptr) {
        const T *end_it = ret.ptr + 1;
        while (end_it < arr.end() && Handler::CompareKeys(Handler::GetKey(end_it), code)) {
            end_it++;
        }
        ret.len = end_it - ret.ptr;
    }

    return ret;
}

const DiagnosisInfo *TableIndex::FindDiagnosis(DiagnosisCode diag) const
{
    return diagnoses_map->FindValue(diag, nullptr);
}

Span<const ProcedureInfo> TableIndex::FindProcedure(ProcedureCode proc) const
{
    return FindSpan(procedures, procedures_map, proc);
}

const ProcedureInfo *TableIndex::FindProcedure(ProcedureCode proc, int8_t phase, Date date) const
{
    const ProcedureInfo *proc_info = procedures_map->FindValue(proc, nullptr);
    if (!proc_info)
        return nullptr;

    do {
        if (proc_info->phase != phase)
            continue;
        if (date < proc_info->limit_dates[0] || date >= proc_info->limit_dates[1])
            continue;

        return proc_info;
    } while (++proc_info < procedures.end() && proc_info->proc == proc);

    return nullptr;
}

const GhmRootInfo *TableIndex::FindGhmRoot(GhmRootCode ghm_root) const
{
    return ghm_roots_map->FindValue(ghm_root, nullptr);
}

Span<const GhsAccessInfo> TableIndex::FindCompatibleGhs(GhmRootCode ghm_root) const
{
    return FindSpan(ghs, ghm_root_to_ghs_map, ghm_root);
}

Span<const GhsAccessInfo> TableIndex::FindCompatibleGhs(GhmCode ghm) const
{
    return FindSpan(ghs, ghm_to_ghs_map, ghm);
}

const AuthorizationInfo *TableIndex::FindAuthorization(AuthorizationScope scope, int8_t type) const
{
    decltype(AuthorizationInfo::type) key;
    key.st.scope = scope;
    key.st.code = type;

    return authorizations_map->FindValue(key.value, nullptr);
}

const GhsPriceInfo *TableIndex::FindGhsPrice(GhsCode ghs, Sector sector) const
{
    return ghs_prices_map[(int)sector]->FindValue(ghs, nullptr);
}

const SupplementCounters<int32_t> &TableIndex::SupplementPrices(Sector sector) const
{
    return *supplement_prices[(int)sector];
}

#undef FAIL_PARSE_IF
