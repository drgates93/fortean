#ifndef FORTEAN_TOML_H
#define FORTEAN_TOML_H

#include "toml.h"

typedef struct {
    toml_table_t *table;
    char *data;  // Owned buffer of TOML file content
} fortean_toml_t;

int fortean_toml_load(const char *path, fortean_toml_t *cfg);
void fortean_toml_free(fortean_toml_t *cfg);

// Returns a NULL-terminated array of strings (caller must free all)
char **fortean_toml_get_array(fortean_toml_t *cfg, const char *key_path);

// Get a string from key_path, or NULL if not found (do NOT free)
const char *fortean_toml_get_string(fortean_toml_t *cfg, const char *key_path);

#endif // FORTEAN_TOML_H
