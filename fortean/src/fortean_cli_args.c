#include "fortean_cli_args.h"
#include "fortean_helper_fn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long hash_str(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

void hashmap_init(hashmap_t *map) {
    for (int i = 0; i < HASHMAP_SIZE; i++) {
        map->buckets[i] = NULL;
    }
}

int hashmap_put(hashmap_t *map, const char *key, const int idx) {
    if (!map || !key) return -1;

    unsigned long h = hash_str(key) % HASHMAP_SIZE;
    kvpair_t *pair = map->buckets[h];
    while (pair) {
        if (strcmp(pair->key, key) == 0) return 0;
        pair = pair->next;
    }

    pair = (kvpair_t *)malloc(sizeof(kvpair_t));
    if (!pair) {
        char msg[512];
        snprintf(msg,sizeof(msg),"Memory allocation failed for key value pair\n");
        print_error(msg);
        return -1;
    }
    pair->key = strdup(key);
    pair->idx = idx;
    if (!pair->key || !pair->idx) {
        free(pair);
        char msg[512];
        snprintf(msg,sizeof(msg),"Memory copy failed for saving the key for comparison \n");
        print_error(msg);
        return -1;
    }
    pair->next = map->buckets[h];
    map->buckets[h] = pair;
    return 0;
}

int hashmap_contains(hashmap_t *map, const char *key) {
    if (!map || !key) return 0;

    unsigned long h = hash_str(key) % HASHMAP_SIZE;
    kvpair_t *pair = map->buckets[h];
    while (pair) {
        if (strcmp(pair->key, key) == 0) {
            return 1;
        }
        pair = pair->next;
    }
    return 0;
}

//Check if key exists with some index. Preserves the cli order which is important. 
int hashmap_contains_key_and_index(hashmap_t *map, const char *key, const int idx){
    if (!map || !key) return 0;
    unsigned long h = hash_str(key) % HASHMAP_SIZE;
    kvpair_t *pair = map->buckets[h];
    while (pair) {
        if (strcmp(pair->key, key) == 0 && pair->idx == idx) {
            return 1;
        }
        pair = pair->next;
    }
    return 0;
}

//Lookup specific index in the hashmap 
const char* return_key_for_index(hashmap_t *map, const int idx){
    for (int i = 0; i < HASHMAP_SIZE; i++) {
        if(map->buckets[i] == NULL) continue;
        kvpair_t *pair = map->buckets[i];
        while(pair){
            if(pair->idx == idx){
                return pair->key;
            }
            pair = pair->next;
        }
    }
    return NULL;
}

//Lookup specific key in the hashmap that does not match the input and does not contain dashes.
const char* return_key_with_no_dashes(hashmap_t *map, const char* key){
    for (int i = 0; i < HASHMAP_SIZE; i++) {
        if(map->buckets[i] == NULL) continue;
        kvpair_t *pair = map->buckets[i];
        while(pair){
            if(strstr(pair->key,"-") == NULL && strcmp(pair->key,key) != 0){
                return pair->key;
            }
            pair = pair->next;
        }
    }
    return NULL;
}

void hashmap_free(hashmap_t *map) {
    if (!map) return;

    for (int i = 0; i < HASHMAP_SIZE; i++) {
        kvpair_t *pair = map->buckets[i];
        while (pair) {
            kvpair_t *next = pair->next;
            free(pair->key);
            free(pair);
            pair = next;
        }
        map->buckets[i] = NULL;
    }
}

void cli_args_init(cli_args_t *args) {
    if (!args) return;
    hashmap_init(&args->args_map);
}

void cli_args_free(cli_args_t *args) {
    if (!args) return;
    hashmap_free(&args->args_map);
}

int cli_args_parse(cli_args_t *args, int argc, char **argv) {
    if (!args || !argv) return -1;

    for (int i = 1; i < argc; i++) {
        size_t arg_len = strnlen(argv[i], MAX_ARG_LEN + 1);
        if (arg_len > MAX_ARG_LEN) {
            char msg[512];
            snprintf(msg,sizeof(msg),"Argument too long (max %d chars): %s\n", MAX_ARG_LEN, argv[i]);
            print_error(msg);
            return -1;
        }
        if (hashmap_put(&args->args_map, argv[i], i) != 0) {
            return -1;
        }
    }
    return 0;
}
