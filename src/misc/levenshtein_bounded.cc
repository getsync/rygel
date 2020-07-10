#include "../core/libcc/libcc.hh"

using namespace RG;

static bool LoadWords(const char *path, Allocator *alloc, HeapArray<Span<const char>> *out_words)
{
    StreamReader st(path);
    LineReader reader(&st);

    Span<const char> line;
    while (reader.Next(&line)) {
        Span<const char> word = TrimStr(SplitStr(line, ';'));
        if (word.len) {
            out_words->Append(DuplicateString(word, alloc));
        }
    }

    return st.Close();
}

static int ComputeBoundedLevenshtein(Span<const char> str1, Span<const char> str2, int max_dist)
{
    if (str1.len - str2.len >= max_dist || str2.len - str1.len >= max_dist)
        return -1;

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))

    int column[33];
    for (Size y = 1; y <= str1.len; y++)
        column[y] = y;
    for (Size x = 1; x <= str2.len; x++) {
        column[0] = x;

        int last_diag = x - 1;
        int min_diag = INT_MAX;
        for (Size y = 1; y <= str1.len; y++) {
            int old_diag = column[y];
            column[y] = MIN3(column[y] + 1, column[y - 1] + 1, last_diag + (str1[y - 1] == str2[x - 1] ? 0 : 1));
            min_diag = std::min(min_diag, column[y]);
            last_diag = old_diag;
        }

        if (min_diag >= max_dist)
            return -1;
    }

#undef MIN3

    return column[str1.len];
}

void ComputeDistances(Span<Span<const char>> words, HeapArray<int16_t> *out_distances)
{
    Size start_len = out_distances->len;
    out_distances->Grow(words.len * words.len);

    Async async;
    Size task_len = 512;

    for (Size offset = 0; offset < words.len; offset += task_len) {
        Size end = std::min(offset + task_len, words.len);

        async.Run([&, offset, end]() {
            for (Size i = offset; i < end; i++) {
                for (Size j = 0; j < words.len - 1; j++) {
                    Size idx = start_len + i * words.len + j;
                    size_t distance = ComputeBoundedLevenshtein(words[i], words[j], 3);

                    out_distances->ptr[idx] = (int16_t)distance;
                }
            }

            return true;
        });
    }

    async.Sync();
    out_distances->len += words.len * words.len;
}

int main(int argc, char **argv)
{
    HeapArray<Span<const char>> words;
    BlockAllocator words_alloc(Megabytes(1));

    if (!LoadWords("src/misc/nouns.txt", &words_alloc, &words))
        return 1;
    LogInfo("Loaded %1 words", words.len);

    HeapArray<int16_t> distances;
    ComputeDistances(words, &distances);
    LogInfo("Computed %1 distances", distances.len);

    return 0;
}
