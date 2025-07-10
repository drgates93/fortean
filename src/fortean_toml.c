#include "fortean_toml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Load toml file from path, parse, and store in cfg
int fortean_toml_load(const char *path, fortean_toml_t *cfg) {
    if (!cfg || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    cfg->data = malloc(size + 1);
    if (!cfg->data) {
        fclose(f);
        return -1;
    }

    if (fread(cfg->data, 1, size, f) != (size_t)size) {
        free(cfg->data);
        fclose(f);
        return -1;
    }
    cfg->data[size] = '\0';
    fclose(f);

    char errbuf[200];
    cfg->table = toml_parse(cfg->data, errbuf, sizeof(errbuf));
    if (!cfg->table) {
        free(cfg->data);
        fprintf(stderr, "TOML parse error: %s\n", errbuf);
        return -1;
    }
    return 0;
}

// Free resources
void fortean_toml_free(fortean_toml_t *cfg) {
    if (!cfg) return;
    if (cfg->table) toml_free(cfg->table);
    if (cfg->data) free(cfg->data);
    cfg->table = NULL;
    cfg->data = NULL;
}

// Helper: traverse tables using dot-separated keys except last part is key
static toml_table_t* fortean_toml_traverse_table(toml_table_t *table, const char *key_path) {
    if (!table || !key_path) return NULL;

    char key_copy[256];
    strncpy(key_copy, key_path, sizeof(key_copy));
    key_copy[sizeof(key_copy)-1] = '\0';

    char *last_dot = strrchr(key_copy, '.');
    if (last_dot) *last_dot = '\0';

    toml_table_t *cur = table;
    if (last_dot) {
        char *token = strtok(key_copy, ".");
        while (token && cur) {
            toml_table_t *next = toml_table_in(cur, token);
            if (!next) return NULL;
            cur = next;
            token = strtok(NULL, ".");
        }
    }
    return cur;
}

// Get string array from key path like "search.shallow"
char **fortean_toml_get_array(fortean_toml_t *cfg, const char *key_path) {
    if (!cfg || !cfg->table || !key_path) return NULL;

    char key_copy[256];
    strncpy(key_copy, key_path, sizeof(key_copy));
    key_copy[sizeof(key_copy)-1] = '\0';

    char *last_dot = strrchr(key_copy, '.');
    const char *array_key = last_dot ? last_dot + 1 : key_copy;

    toml_table_t *tbl = fortean_toml_traverse_table(cfg->table, key_path);
    if (!tbl) return NULL;

    toml_array_t *arr = toml_array_in(tbl, array_key);
    if (!arr) return NULL;

    int n = toml_array_nelem(arr);
    char **result = malloc((n + 1) * sizeof(char *));
    if (!result) return NULL;

    for (int i = 0; i < n; i++) {
        toml_datum_t val = toml_string_at(arr, i);
        if (!val.ok) {
            for (int j = 0; j < i; j++) free(result[j]);
            free(result);
            return NULL;
        }
        result[i] = strdup(val.u.s);
    }
    result[n] = NULL;
    return result;
}

// Get string value from key path like "build.target"
const char *fortean_toml_get_string(fortean_toml_t *cfg, const char *key_path) {
    if (!cfg || !cfg->table || !key_path) return NULL;

    char key_copy[256];
    strncpy(key_copy, key_path, sizeof(key_copy));
    key_copy[sizeof(key_copy)-1] = '\0';

    char *last_dot = strrchr(key_copy, '.');
    const char *key_name = last_dot ? last_dot + 1 : key_copy;

    toml_table_t *tbl = fortean_toml_traverse_table(cfg->table, key_path);
    if (!tbl) return NULL;

    toml_datum_t val = toml_string_in(tbl, key_name);
    if (val.ok) return val.u.s;
    return NULL;
}
