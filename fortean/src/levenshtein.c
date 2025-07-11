#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "levenshtein.h"
#include "fortean_build.h"

// Function to find minimum of three integers
int min(int a, int b, int c) {
    if (a < b && a < c) return a;
    if (b < c) return b;
    return c;
}

// Levenshtein Distance Function
int editDistance(const char *str1, const char *str2) {
    int len1 = strlen(str1);
    int len2 = strlen(str2);
    int dp[len1 + 1][len2 + 1];

    for (int i = 0; i <= len1; i++)
        dp[i][0] = i;
    for (int j = 0; j <= len2; j++)
        dp[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            if (str1[i - 1] == str2[j - 1])
                dp[i][j] = dp[i - 1][j - 1];
            else
                dp[i][j] = 1 + min(dp[i - 1][j],     // Deletion
                                   dp[i][j - 1],     // Insertion
                                   dp[i - 1][j - 1]);// Substitution
        }
    }
    return dp[len1][len2];
}

// Suggest closest word from dictionary
void suggestClosestWord(const char *typo, const char dictionary_pointer[][MAX_WORD_LEN], int dictSize) {
    int minDist = INT_MAX;
    const char *closest = NULL;

    for (int i = 0; i < dictSize; i++) {
        int dist = editDistance(typo, dictionary_pointer[i]);
        if (dist < minDist) {
            minDist = dist;
            closest = dictionary_pointer[i];
        }
    }

    //Add suggestion if it's within 3 of any word in the dictionary.
    if (closest && minDist < 3) {
        printf("Unknown flag: Did you mean: %s?\n", closest);
    }else{
        printf("Unknown command: %s\n", typo);
    }
    return;
}

