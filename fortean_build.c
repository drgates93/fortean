#include "fortean_build.h"
#include "fortean_toml.h"
#include "fortean_hash.h"
#include "fortean_threads.h"
#include "fortean_helper_fn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#include <limits.h>
#endif



static int dir_exists(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}


//Figure out the path options.
#ifdef _WIN32
    #define PATH_SEP '\\'
    //Generate the hash file cache. 
    const char* hash_cache_file = ".cache\\hash.dep";

    //Depenency list 
    const char* deps_file = ".cache\\topo.dep";
#else
    #define PATH_SEP '/'
    //Generate the hash file cache. 
    const char* hash_cache_file = ".cache/hash.dep";

    //Depenency list 
    const char* deps_file = ".cache/topo.dep";
#endif

char *get_last_path_segment(const char *path) {
    const char *end = path + strlen(path);
    const char *p = end;
    while (p > path && *(p - 1) != '/' && *(p - 1) != '\\') p--;
    return strdup(p); 
}

// Worker thread: runs system() on a single command string
void compile_system_worker(void *arg) {
    char *cmd = (char *)arg;
    int ret = system(cmd);
    if (ret != 0) {
        print_error("Compilation failed: %s\n");
        return;
    }
    return;
}

int file_exists(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file) {
        fclose(file);
        return 1;  // File exists
    }
    return 0;  // File does not exist
}

static char *run_command_capture(const char *cmd) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        print_error("Failed to run command.");
        return NULL;
    }
    char *buffer = NULL;
    size_t size = 0;
    char chunk[512];

    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t len = strlen(chunk);
        char *newbuf = realloc(buffer, size + len + 1);
        if (!newbuf) {
            free(buffer);
            pclose(pipe);
            print_error("Memory allocation error.");
            return NULL;
        }
        buffer = newbuf;
        memcpy(buffer + size, chunk, len);
        size += len;
        buffer[size] = '\0';
    }
    pclose(pipe);
    return buffer;
}

// Helper: add flag to unique list if not already there
static int add_unique_flag(char ***list, int *count, const char *flag) {
    for (int i = 0; i < *count; i++) {
        if (strcmp((*list)[i], flag) == 0) {
            return 0; // already present
        }
    }
    char **newlist = realloc(*list, sizeof(char*) * (*count + 1));
    if (!newlist) return -1;
    *list = newlist;
    (*list)[*count] = strdup(flag);
    if (!(*list)[*count]) return -1;
    (*count)++;
    return 0;
}

// Helper: case-insensitive string compare for extension match
int strcmp_case_insensitive(const char *ext, const char *target) {
    while (*ext && *target) {
        if (tolower((unsigned char)*ext) != tolower((unsigned char)*target)) {
            return 1; // not equal
        }
        ext++;
        target++;
    }
    return (*ext == '\0' && *target == '\0') ? 0 : 1;
}

// Free a list of strings
static void free_string_list(char **list, int count) {
    if (!list) return;
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

int fortean_build_project_incremental(const char *project_dir, const int parallel_build, const int incremental_build_override) {
    if (!project_dir) {
        print_error("Project directory is NULL.");
        return -1;
    }

    char build_dir[512];
    snprintf(build_dir, sizeof(build_dir), "%s%cbuild", project_dir, PATH_SEP);

    if (!dir_exists(build_dir)) {
        snprintf(build_dir, sizeof(build_dir), "build");
        if (!dir_exists(build_dir)) {
            print_error("Build directory does not exist.");
            return -1;
        }
    }

    int incremental_build  = 0;
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s%c%s",project_dir,PATH_SEP,hash_cache_file);
    if(!file_exists(cache_path)){
        //Check if it's local instead
        incremental_build = file_exists(hash_cache_file);
    }else{
        incremental_build = 1;
    }

    //If we allow the override, then we want to rebuild all, so incremental build is disabled.
    if(incremental_build_override == 0) incremental_build = 0;

    char toml_path[512];
    snprintf(toml_path, sizeof(toml_path), "%s%cproject.toml", build_dir,PATH_SEP);

    fortean_toml_t cfg = {0};
    if (fortean_toml_load(toml_path, &cfg) != 0) {
        print_error("Failed to load project.toml.");
        return -1;
    }

    const char *target = fortean_toml_get_string(&cfg, "build.target");
    if (!target) {
        print_error("Missing 'build.target' in config.");
        fortean_toml_free(&cfg);
        return -1;
    }

    char *compiler = (char *)fortean_toml_get_string(&cfg, "build.compiler");
    if (!compiler) compiler = "gfortran";

    char **flags_array = fortean_toml_get_array(&cfg, "build.flags");
    if (!flags_array) {
        print_error("Missing or empty 'build.flags' in config.");
        fortean_toml_free(&cfg);
        return -1;
    }

    // Combine unique flags once into a single string
    char **unique_flags = NULL;
    int unique_count = 0;

    for (int i = 0; flags_array[i]; i++) {
        if (add_unique_flag(&unique_flags, &unique_count, flags_array[i]) != 0) {
            print_error("Memory error adding flag.");
            goto cleanup_arrays;
        }
    }

    // Build single string of all flags (space separated)
    size_t flags_len = 0;
    for (int i = 0; i < unique_count; i++) {
        flags_len += strlen(unique_flags[i]) + 1; // +1 for space or null terminator
    }

    char *flags_str = malloc(flags_len + 1);
    if (!flags_str) {
        print_error("Memory allocation error for flags string.");
        goto cleanup_arrays;
    }
    flags_str[0] = '\0';

    for (int i = 0; i < unique_count; i++) {
        strcat(flags_str, unique_flags[i]);
        if (i < unique_count - 1) strcat(flags_str, " ");
    }

    char old_dir[PATH_MAX];
    getcwd(old_dir, sizeof(old_dir));
    chdir(project_dir);

    const char *obj_dir = fortean_toml_get_string(&cfg, "build.obj_dir");
    const char *mod_dir = fortean_toml_get_string(&cfg, "build.mod_dir");

    if (!obj_dir || !mod_dir) {
        print_error("Missing directory settings in config.");
        goto cleanup_flags_str;
    }

    if (!dir_exists(obj_dir)) {
        print_error("Object directory does not exist.");
        goto cleanup_flags_str;
    }
    if (!dir_exists(mod_dir)) {
        print_error("Module directory does not exist.");
        goto cleanup_flags_str;
    }

    char **deep_dirs    = fortean_toml_get_array(&cfg, "search.deep");
    char **shallow_dirs = fortean_toml_get_array(&cfg, "search.shallow");

    if (!deep_dirs) deep_dirs       = NULL;
    if (!shallow_dirs) shallow_dirs = NULL;

    char maketop_cmd[1024] = {0};

#ifdef _WIN32
    strcat(maketop_cmd, "build\\maketopologicf90.exe");
#else
    strcat(maketop_cmd, "./build/maketopologicf90.exe");
#endif
    if (deep_dirs) {
        strcat(maketop_cmd, " -D ");
        for (int i = 0; deep_dirs[i]; i++) {
            strcat(maketop_cmd, deep_dirs[i]);
            if (deep_dirs[i + 1]) strcat(maketop_cmd, ",");
        }
    }
    if (shallow_dirs) {
        strcat(maketop_cmd, " -d ");
        for (int i = 0; shallow_dirs[i]; i++) {
            strcat(maketop_cmd, shallow_dirs[i]);
            if (shallow_dirs[i + 1]) strcat(maketop_cmd, ",");
        }
    }
    //Still need the list of source files to link against.
    char *topo_src= run_command_capture(maketop_cmd);

    //Allocate the hashmaps.
    FileNode*  cur_map[HASH_TABLE_SIZE]  = {NULL};
    HashEntry* prev_map[HASH_TABLE_SIZE] = {NULL};
    
    //Need the list of everything for linking. 
    if (!topo_src) {
        print_error("Failed to get topologically sorted sources.");
        goto cleanup_search_arrays;
    }

    char *line     = strtok(topo_src, "\n");
    char **sources = NULL;
    int src_count  = 0;
    char **tmp     = NULL;
    while (line) {
        tmp = realloc(sources, sizeof(char *) * (src_count + 1));
        if (!tmp) {
            print_error("Memory allocation error.");
            free(topo_src);
            goto cleanup_sources;
        }
        sources = tmp;
        sources[src_count] = strdup(line);
        src_count++;
        line = strtok(NULL, "\n");
    }


    //For the incremental build, we parse the files!
    if(incremental_build){
        strcat(maketop_cmd," -m");
        char *topo_make = run_command_capture(maketop_cmd);

        //Open the depedency list.
        FILE* depedency_chain = fopen(deps_file ,"w+");
        fprintf(depedency_chain,"%s",topo_make);
        fclose(depedency_chain);

        //Parse the dependency file first (we always need it)
        int res = parse_dependency_file(deps_file,cur_map);
        if(!res){
            print_error("Failed to make hash table of dependency\n");
            return -1;
        }
            
        //If hash file exists, load it and compare
        if (file_exists(hash_cache_file)) {
            load_prev_hashes(hash_cache_file,prev_map);
            save_hashes(hash_cache_file,cur_map);
            prune_obsolete_cached_entries(prev_map,cur_map);
        }else{
            print_error("Cannot do an incremental build with no history!");
            print_error("Check that the .cache/hash.dep file exists.\n");
            return -1;
        }


        //Check the hash table for what we need to build.
        FileNode *rebuild_list = NULL;
        int rebuild_cnt = 0;
        for (int i = 0; i < HASH_TABLE_SIZE; i++) {
            FileNode *node = cur_map[i];
            while (node) {
                // Check if this node has changed
                if (!file_is_unchanged(node->filename, node->file_hash, prev_map)) {
                    // It changed — mark its dependents
                    mark_dependents_for_rebuild(node->filename, cur_map, &rebuild_list, &rebuild_cnt);
                }
                node = node->next;
            }
        }

        //Compile each source only if it changed and needs to be rebuilt. 
        //If we want to support parallel build, this is the place to modify. 
        //Need to convert this linked list of rebuild items to an array and then parse the 
        //work out. Trivial for the base case since it's already an array. 
        FileNode *curr = rebuild_list;
        while(curr){
            const char *src      = curr->filename;
            const char *rel_path = get_last_path_segment(src);
            char *ext = strrchr(rel_path, '.');
            if (ext && (strcmp_case_insensitive(ext, ".f90") == 0 
                    ||  strcmp_case_insensitive(ext, ".for") == 0
                    ||  strcmp_case_insensitive(ext, ".f")   == 0
                    ||  strcmp_case_insensitive(ext, ".f77") == 0)){
                *ext = '\0';
            }

            char obj_file[1024];
            snprintf(obj_file, sizeof(obj_file), "%s%c%s.o", obj_dir, PATH_SEP, rel_path);

            char compile_cmd[2048];
            snprintf(compile_cmd, sizeof(compile_cmd), "%s %s -J%s -c %s -o %s",
                     compiler, flags_str, mod_dir, src, obj_file);

            print_info(compile_cmd);
            int ret = system(compile_cmd);
            if (ret != 0) {
                print_error("Compilation failed.");
                goto cleanup_sources;
            }

            //Next node
            curr = curr->next;
        }

        //Free the topo make and rebuild list.
        free(topo_make);
        free(rebuild_list);
    }else{
        // Compile each source
        thread_t *threads;
        if(parallel_build) threads = (thread_t*)malloc(src_count*sizeof(thread_t));
        for (int i = 0; i < src_count; i++) {
            const char *src      = sources[i];
            const char *rel_path = get_last_path_segment(src);
            char *ext = strrchr(rel_path, '.');
            if (ext && (strcmp_case_insensitive(ext, ".f90") == 0 
                    ||  strcmp_case_insensitive(ext, ".for") == 0
                    ||  strcmp_case_insensitive(ext, ".f")   == 0
                    ||  strcmp_case_insensitive(ext, ".f77") == 0)){
                *ext = '\0';
            }

            char obj_file[1024];
            snprintf(obj_file, sizeof(obj_file), "%s%c%s.o", obj_dir, PATH_SEP, rel_path);

            char compile_cmd[2048];
            snprintf(compile_cmd, sizeof(compile_cmd), "%s %s -J%s -c %s -o %s",
                     compiler, flags_str, mod_dir, src, obj_file);

            if(!parallel_build){
                print_info(compile_cmd);
                int ret = system(compile_cmd);
                if (ret != 0) {
                    print_error("Compilation failed.");
                    goto cleanup_sources;
                }
            }else{
                print_info(compile_cmd);
                if (thread_create(&threads[i], compile_system_worker, compile_cmd) != 0) {
                    print_error("Failed to create thread");
                    goto cleanup_sources;
                }
            }
        }

        if(parallel_build){
            // Wait for all threads to finish
            for (int i = 0; i < src_count; i++) {
                thread_join(threads[i]);
            }
        }
    }

    // Link
    char link_cmd[4096] = {0};
    size_t link_pos = 0;

    link_pos += snprintf(link_cmd + link_pos, sizeof(link_cmd) - link_pos, "%s %s", compiler, flags_str);

    for (int i = 0; i < src_count; i++) {
        const char *src      = sources[i];
        const char *rel_path = get_last_path_segment(src);
        char *ext = strrchr(rel_path, '.');
        if (ext && (strcmp_case_insensitive(ext, ".f90") == 0 
                ||  strcmp_case_insensitive(ext, ".for") == 0
                ||  strcmp_case_insensitive(ext, ".f")   == 0
                ||  strcmp_case_insensitive(ext, ".f77") == 0)){
            *ext = '\0';
        }

        //Write the "object" to the obj directory. For simpliifed building, 
        //we eliminate the relative path to the src in the obj dir and link against
        //just a list of all .o files. This is much cleaner. 
        char obj_path[512];
        snprintf(obj_path, sizeof(obj_path), "%s%c%s.o", obj_dir, PATH_SEP, rel_path);
        link_pos += snprintf(link_cmd + link_pos, sizeof(link_cmd) - link_pos, " %s", obj_path);
    }

    //Link with the libraries (if they exist).
    char **source_libs = fortean_toml_get_array(&cfg, "library.source-libs");
    if (!source_libs) source_libs = NULL;
    if (source_libs ) {
        for (int i = 0; source_libs[i]; i++) {
            link_pos += snprintf(link_cmd + link_pos, sizeof(link_cmd) - link_pos, " %s", source_libs[i]);
        }
    }

    //Build the final link command
    link_pos += snprintf(link_cmd + link_pos, sizeof(link_cmd) - link_pos, " -o %s", target);

    print_info(link_cmd);
    int ret = system(link_cmd);
    if (ret != 0) {
        print_error("Linking failed.");
        goto cleanup_sources;
    }

    print_ok("Built Successfully");

    if(!incremental_build){
         //For the incremental build, we parse the files!
        strcat(maketop_cmd," -m");
        char *topo_make = run_command_capture(maketop_cmd);

        //Always rebuild the hash table when we are done. 
        FILE* depedency_chain = fopen(deps_file ,"w+");
        fprintf(depedency_chain,"%s",topo_make);
        fclose(depedency_chain);

        //Parse the dependency file first (we always need it)
        //It clears the existing hashmap by default. 
        parse_dependency_file(deps_file,cur_map);

        //Save updated hash list for future runs
        save_hashes(hash_cache_file,cur_map);

    }

cleanup_sources:
    if (sources) {
        for (int i = 0; i < src_count; i++) free(sources[i]);
        free(sources);
    }
    free(topo_src);

cleanup_search_arrays:
    if (deep_dirs) {
        for (int i = 0; deep_dirs[i]; i++) free(deep_dirs[i]);
        free(deep_dirs);
    }
    if (shallow_dirs) {
        for (int i = 0; shallow_dirs[i]; i++) free(shallow_dirs[i]);
        free(shallow_dirs);
    }

cleanup_flags_str:
    free(flags_str);

cleanup_arrays:
    for (int i = 0; flags_array[i]; i++) free(flags_array[i]);
    free(flags_array);

    free_string_list(unique_flags, unique_count);

    fortean_toml_free(&cfg);
    chdir(old_dir);

    
    free_all(cur_map);
    free_prev_hash_table(prev_map);

    return 0;
}





// Deprecated Code for single increment every time. Saved here because it's useful for debugging. 
// int fortean_build_project(const char *project_dir) {
//     if (!project_dir) {
//         print_error("Project directory is NULL.");
//         return -1;
//     }

//     char build_dir[512];
//     snprintf(build_dir, sizeof(build_dir), "%s/build", project_dir);

//     if (!dir_exists(build_dir)) {
//         snprintf(build_dir, sizeof(build_dir), "build");
//         if (!dir_exists(build_dir)) {
//             print_error("Build directory does not exist.");
//             return -1;
//         }
//     }

    
//     char cache_path[512];
//     snprintf(cache_path, sizeof(cache_path), "%s/incremental_cache.txt",build_dir);
//     if(file_exists(cache_path)){
//         int res = fortean_build_project_incremental(project_dir);
//         return res;
//     }

//     char toml_path[512];
//     snprintf(toml_path, sizeof(toml_path), "%s/project.toml", build_dir);

//     fortean_toml_t cfg = {0};
//     if (fortean_toml_load(toml_path, &cfg) != 0) {
//         print_error("Failed to load project.toml.");
//         return -1;
//     }

//     const char *target = fortean_toml_get_string(&cfg, "build.target");
//     if (!target) {
//         print_error("Missing 'build.target' in config.");
//         fortean_toml_free(&cfg);
//         return -1;
//     }

//     char *compiler = (char *)fortean_toml_get_string(&cfg, "build.compiler");
//     if (!compiler) compiler = "gfortran";

//     char **flags_array = fortean_toml_get_array(&cfg, "build.flags");
//     if (!flags_array) {
//         print_error("Missing or empty 'build.flags' in config.");
//         fortean_toml_free(&cfg);
//         return -1;
//     }

//     // Combine unique flags once into a single string
//     char **unique_flags = NULL;
//     int unique_count = 0;

//     for (int i = 0; flags_array[i]; i++) {
//         if (add_unique_flag(&unique_flags, &unique_count, flags_array[i]) != 0) {
//             print_error("Memory error adding flag.");
//             goto cleanup_arrays;
//         }
//     }

//     // Build single string of all flags (space separated)
//     size_t flags_len = 0;
//     for (int i = 0; i < unique_count; i++) {
//         flags_len += strlen(unique_flags[i]) + 1; // +1 for space or null terminator
//     }

//     char *flags_str = malloc(flags_len + 1);
//     if (!flags_str) {
//         print_error("Memory allocation error for flags string.");
//         goto cleanup_arrays;
//     }
//     flags_str[0] = '\0';

//     for (int i = 0; i < unique_count; i++) {
//         strcat(flags_str, unique_flags[i]);
//         if (i < unique_count - 1) strcat(flags_str, " ");
//     }

//     char old_dir[PATH_MAX];
//     getcwd(old_dir, sizeof(old_dir));
//     chdir(project_dir);

//     const char *obj_dir = fortean_toml_get_string(&cfg, "build.obj_dir");
//     const char *mod_dir = fortean_toml_get_string(&cfg, "build.mod_dir");

//     if (!obj_dir || !mod_dir) {
//         print_error("Missing directory settings in config.");
//         goto cleanup_flags_str;
//     }

//     if (!dir_exists(obj_dir)) {
//         print_error("Object directory does not exist.");
//         goto cleanup_flags_str;
//     }
//     if (!dir_exists(mod_dir)) {
//         print_error("Module directory does not exist.");
//         goto cleanup_flags_str;
//     }

//     char **deep_dirs = fortean_toml_get_array(&cfg, "search.deep");
//     char **shallow_dirs = fortean_toml_get_array(&cfg, "search.shallow");

//     if (!deep_dirs) deep_dirs = NULL;
//     if (!shallow_dirs) shallow_dirs = NULL;

//     char maketop_cmd[1024] = {0};

// #ifdef _WIN32
//     strcat(maketop_cmd, "build\\maketopologicf90.exe");
// #else
//     strcat(maketop_cmd, "./build/maketopologicf90.exe");
// #endif
//     if (deep_dirs) {
//         strcat(maketop_cmd, " -D ");
//         for (int i = 0; deep_dirs[i]; i++) {
//             strcat(maketop_cmd, deep_dirs[i]);
//             if (deep_dirs[i + 1]) strcat(maketop_cmd, ",");
//         }
//     }
//     if (shallow_dirs) {
//         strcat(maketop_cmd, " -d ");
//         for (int i = 0; shallow_dirs[i]; i++) {
//             strcat(maketop_cmd, shallow_dirs[i]);
//             if (shallow_dirs[i + 1]) strcat(maketop_cmd, ",");
//         }
//     }

//     //Get the file list. 
//     char *topo_src= run_command_capture(maketop_cmd);
//     if (!topo_src) {
//         print_error("Failed to get topologically sorted sources.");
//         goto cleanup_search_arrays;
//     }

//     char *line = strtok(topo_src, "\n");
//     char **sources = NULL;
//     int src_count = 0;

//     while (line) {
//         char **tmp = realloc(sources, sizeof(char *) * (src_count + 1));
//         if (!tmp) {
//             print_error("Memory allocation error.");
//             free(topo_src);
//             goto cleanup_sources;
//         }
//         sources = tmp;
//         sources[src_count] = strdup(line);
//         src_count++;
//         line = strtok(NULL, "\n");
//     }

//     // Compile each source
//     for (int i = 0; i < src_count; i++) {
//         const char *src = sources[i];
//         const char *rel_path = src + strlen("src") + 1;

//         char rel_path_no_ext[512];
//         strncpy(rel_path_no_ext, rel_path, sizeof(rel_path_no_ext));
//         rel_path_no_ext[sizeof(rel_path_no_ext) - 1] = '\0';

//         char *ext = strrchr(rel_path_no_ext, '.');
//         if (ext && (strcmp(ext, ".f90") == 0 || strcmp(ext, ".for") == 0)) {
//             *ext = '\0';
//         }

//         char obj_file[1024];
//         snprintf(obj_file, sizeof(obj_file), "%s/%s.o", obj_dir, rel_path_no_ext);

//         char compile_cmd[2048];

//         snprintf(compile_cmd, sizeof(compile_cmd), "%s %s -J%s -c %s -o %s",
//             compiler, flags_str, mod_dir, src, obj_file);

//         print_info(compile_cmd);
//         int ret = system(compile_cmd);
//         if (ret != 0) {
//             print_error("Compilation failed.");
//             goto cleanup_sources;
//         }
//     }

//     // Link
//     char link_cmd[4096] = {0};
//     size_t link_pos = 0;

//     link_pos += snprintf(link_cmd + link_pos, sizeof(link_cmd) - link_pos, "%s %s", compiler, flags_str);

//     for (int i = 0; i < src_count; i++) {
//         const char *src = sources[i];
//         const char *rel_path = src + strlen("src") + 1;

//         char rel_path_no_ext[512];
//         strncpy(rel_path_no_ext, rel_path, sizeof(rel_path_no_ext));
//         rel_path_no_ext[sizeof(rel_path_no_ext) - 1] = '\0';

//         char *ext = strrchr(rel_path_no_ext, '.');
//         if (ext && (strcmp(ext, ".f90") == 0 || strcmp(ext, ".for") == 0)) {
//             *ext = '\0';
//         }

//         char obj_path[512];
//         snprintf(obj_path, sizeof(obj_path), "%s/%s.o", obj_dir, rel_path_no_ext);

//         link_pos += snprintf(link_cmd + link_pos, sizeof(link_cmd) - link_pos, " %s", obj_path);
//     }

//     link_pos += snprintf(link_cmd + link_pos, sizeof(link_cmd) - link_pos, " -o %s", target);

//     print_info(link_cmd);
//     int ret = system(link_cmd);
//     if (ret != 0) {
//         print_error("Linking failed.");
//         goto cleanup_sources;
//     }

//     print_ok("Built Successfully");

//     //For the incremental build, we parse the files!
//     strcat(maketop_cmd," -m");
//     char *topo_make = run_command_capture(maketop_cmd);

//     //Always rebuild the hash table when we are done. 
//     const char* deps_file = "build/topo.txt";
//     FILE* depedency_chain = fopen(deps_file ,"w+");
//     fprintf(depedency_chain,"%s",topo_make);
//     fclose(depedency_chain);

//     //Allocate the hashmaps.
//     FileNode*  cur_map[HASH_TABLE_SIZE]  = {NULL};

//     //Parse the dependency file first (we always need it)
//     parse_dependency_file(deps_file,cur_map);

//     //If hash file exists, load it and compare
//     const char* hash_cache_file = "build/incremental_cache.txt";

//     //Save updated hash list for future runs
//     save_hashes(hash_cache_file,cur_map);

// cleanup_sources:
//     if (sources) {
//         for (int i = 0; i < src_count; i++) free(sources[i]);
//         free(sources);
//     }
//     free(topo_src);

// cleanup_search_arrays:
//     if (deep_dirs) {
//         for (int i = 0; deep_dirs[i]; i++) free(deep_dirs[i]);
//         free(deep_dirs);
//     }
//     if (shallow_dirs) {
//         for (int i = 0; shallow_dirs[i]; i++) free(shallow_dirs[i]);
//         free(shallow_dirs);
//     }

// cleanup_flags_str:
//     free(flags_str);

// cleanup_arrays:
//     for (int i = 0; flags_array[i]; i++) free(flags_array[i]);
//     free(flags_array);

//     free_string_list(unique_flags, unique_count);

//     fortean_toml_free(&cfg);
//     chdir(old_dir);

//     free_all(cur_map);
    
//     return 0;
// }


