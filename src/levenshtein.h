#ifndef LEVENSHTEIN
#define LEVENSHTEIN

#define MAX_WORDS 100
#define MAX_WORD_LEN 50

static const char dictionary[MAX_WORDS][MAX_WORD_LEN] = {
    "build",
    "-m",
    "new",
    "run",
    "--bin"
};
static const int dictionary_size = 5;

void suggestClosestWord(const char *typo, const char dictionary[][MAX_WORD_LEN], int dictSize);

#endif
