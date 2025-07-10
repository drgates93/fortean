#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

//Fortean files
#include "levenshtein.h"
#include "fortean_build.h"
#include "fortean_cli_args.h"
#include "fortean_helper_fn.h"

#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
    #define MKDIR(path) _mkdir(path)
    #define PATH_SEP '\\'
#else
    #define MKDIR(path) mkdir(path, 0755)
    #define PATH_SEP '/'
    #include <unistd.h>
    #include <libgen.h>
#endif

const char *get_executable_dir(void) {
    static char buffer[4096];
    #if defined(_WIN32)
        DWORD len = GetModuleFileNameA(NULL, buffer, sizeof(buffer));
        if (len == 0 || len == sizeof(buffer)) return NULL;
        for (int i = len - 1; i >= 0; --i) {
            if (buffer[i] == '\\' || buffer[i] == '/') {
                buffer[i] = '\0';
                break;
            }
        }
        return buffer;
    #elif defined(__linux__)
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len == -1 || len >= sizeof(buffer)) return NULL;
        buffer[len] = '\0';
        return dirname(buffer);

    #else
        return NULL;
    #endif
}

// Directory list
const char *dirs[] = { "src", "mod", "obj", "build", "data", "lib"};
const int num_dirs = sizeof(dirs) / sizeof(dirs[0]);

const char *hidden_dirs[] = {".cache"};
const int num_hidden_dirs = 1;

// Create directory helper
int create_dir(const char *path) {
    int result = MKDIR(path);
    if (result == 0) return 0;
    if (errno == EEXIST) return 0; // already exists is fine
    return -1;
}

int create_hidden_dir(const char* dir_name) {
#ifdef _WIN32
    // Create the directory
    if (!CreateDirectoryA(dir_name, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            print_error("Failed to create directory. Error code: %lu\n");
            return -1;
        }
    }

    // Set the directory as hidden
    if (!SetFileAttributesA(dir_name, FILE_ATTRIBUTE_HIDDEN)) {
        print_error("Failed to set hidden attribute.\n");
        return -1;
    }
#else
    // Just create the directory (it's hidden due to the dot prefix)
    if (MKDIR(dir_name) != 0 && errno != EEXIST) {
        print_error("Failed to create directory");
        return -1;
    }
#endif
    return 0;
}

// Create directories inside a base path
void create_directories(const char *base_path) {
    for (int i = 0; i < num_dirs; ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s%c%s", base_path, PATH_SEP, dirs[i]);
        int result = create_dir(path);
        if (result == 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Created directory: %s", path);
            print_ok(msg);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create directory: %s (errno %d)", path, errno);
            print_error(msg);
        }
    }

    for(int i = 0; i < num_hidden_dirs; i++){
        char path[512];
        snprintf(path, sizeof(path), "%s%c%s", base_path, PATH_SEP, hidden_dirs[i]);
        int result = create_hidden_dir(path);
        if (result != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create directory: %s (errno %d)", path, errno);
            print_error(msg);
        }
    }
}



// Modified copy_file to support destination path with base dir
int copy_file(const char *src, const char *dest) {
    FILE *f_src = fopen(src, "rb");
    if (!f_src) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot open source file: %s", src);
        print_error(msg);
        return -1;
    }

    FILE *f_dest = fopen(dest, "wb");
    if (!f_dest) {
        fclose(f_src);
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot open destination file: %s", dest);
        print_error(msg);
        return -1;
    }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), f_src)) > 0) {
        fwrite(buffer, 1, bytes, f_dest);
    }

    fclose(f_src);
    fclose(f_dest);
    return 0;
}

void copy_template_files(const char *base_path) {
    //char dest_makefile[512];
    char dest_exe[512];
    //snprintf(dest_makefile, sizeof(dest_makefile), "%s%cmakefile", base_path, PATH_SEP);
    snprintf(dest_exe, sizeof(dest_exe), "%s%cbuild%cmaketopologicf90.exe", base_path, PATH_SEP, PATH_SEP);


    //Need the absolute install path
    const char *install_dir = get_executable_dir();
    //char src_makefile[1024];
    char src_exe[1024];
    //snprintf(src_makefile, sizeof(src_makefile), "%s%c%s%c%s", install_dir, PATH_SEP, "data",PATH_SEP,"template.mak");
    snprintf(src_exe,      sizeof(src_exe), "%s%c%s%c%s", install_dir, PATH_SEP, "bin",PATH_SEP,"maketopologicf90.exe");

    //Now we can copy the files.
    //copy_file(src_makefile, dest_makefile);
    copy_file(src_exe, dest_exe);
}

int directory_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

int generate_project_toml(const char *project_name) {
    char build_dir[256];
    snprintf(build_dir, sizeof(build_dir), "%s/build", project_name);

    // Ensure build directory exists
    struct stat st = {0};
    if (stat(build_dir, &st) == -1) {
        if (MKDIR(project_name) != 0 && errno != EEXIST) {
            print_error("Failed to create project directory.");
            return -1;
        }
        if (MKDIR(build_dir) != 0 && errno != EEXIST) {
            print_error("Failed to create build directory.");
            return -1;
        }
    }

    // Path to the project.toml file
    char toml_path[512];
    snprintf(toml_path, sizeof(toml_path), "%s/project.toml", build_dir);

    FILE *f = fopen(toml_path, "w");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to create TOML file: %s (%s)", toml_path, strerror(errno));
        print_error(msg);
        return -1;
    }

    fprintf(f,
        "# Auto-generated TOML config for project: %s\n\n"
        "[build]\n"
        "target = \"%s\"\n"
        "compiler = \"gfortran\"\n\n"
        "flags = [\n"
        "  \"-cpp\", \"-fno-align-commons\", \"-O3\",\n"
        "  \"-ffpe-trap=zero,invalid,underflow,overflow\",\n"
        "  \"-std=legacy\", \"-ffixed-line-length-none\", \"-fall-intrinsics\",\n"
        "  \"-Wno-unused-variable\", \"-Wno-unused-function\",\n"
        "  \"-Wno-conversion\", \"-fopenmp\", \"-Imod\"\n"
        "]\n\n"
        "obj_dir = \"obj\"\n"
        "mod_dir = \"mod\"\n\n"
        "[search]\n"
        "deep = [\"src\"]\n"
        "#shallow = [\"lib\", \"include\"]\n\n"
        "[library]\n"
        "#source-libs = [\"lib/test.lib\"]\n",
        project_name, project_name
    );

    fclose(f);
    print_ok("Generated project.toml file successfully.");
    return 0;
}

// int replace_program_name_in_makefile(const char *new_program_name) {
//     int n = strlen(new_program_name);
//     char* makefile_path = (char*)calloc((n+1024),sizeof(char));
//     strcpy(makefile_path,new_program_name);
//     strcat(makefile_path,"/makefile");
//     FILE *f = fopen(makefile_path, "r");
//     if (!f) {
//         char msg[256];
//         snprintf(msg, sizeof(msg), "Error opening file %s: %s", makefile_path, strerror(errno));
//         print_error(msg);
//         return -1;
//     }

//     // Read all lines into memory (assuming small file)
//     char lines[1024][512];
//     int line_count = 0;
//     while (fgets(lines[line_count], sizeof(lines[0]), f)) {
//         line_count++;
//         if (line_count >= 1024) break;
//     }
//     fclose(f);

//     // Find PROGRAM line and replace
//     int replaced = 0;
//     for (int i = 0; i < line_count; i++) {
//         if (strncmp(lines[i], "PROGRAM =", 8) == 0) {
//             snprintf(lines[i], sizeof(lines[0]), "PROGRAM = %s\n", new_program_name);
//             replaced = 1;
//             break;
//         }
//     }

//     if (!replaced) {
//         print_error("PROGRAM variable not found in Makefile.");
//         return -2;
//     }

//     // Write back the modified content
//     f = fopen(makefile_path, "w");
//     if (!f) {
//         char msg[256];
//         snprintf(msg, sizeof(msg), "Error opening file %s for writing: %s", makefile_path, strerror(errno));
//         print_error(msg);
//         return -1;
//     }

//     for (int i = 0; i < line_count; i++) {
//         fputs(lines[i], f);
//     }

//     fclose(f);
//     return 0;
// }

int create_main_f90(const char *project_dir) {
    char src_dir[512];
    snprintf(src_dir, sizeof(src_dir), "%s/src", project_dir);

    // Make sure the src directory exists
    struct stat st = {0};
    if (stat(src_dir, &st) == -1) {
        if (mkdir(src_dir) != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create src directory: %s", strerror(errno));
            print_error(msg);
            return -1;
        }
    }

    // Path to main.f90
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/main.f90", src_dir);

    FILE *f = fopen(filepath, "w");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to create %s: %s", filepath, strerror(errno));
        print_error(msg);
        return -1;
    }

    fprintf(f,
        "program main\n"
        "    print*, \"Hello World\"\n"
        "end program main\n"
    );
    fclose(f);
    return 0;
}

// int run_make_in_build(const char *project_name) {
//     char build_dir[512];
//     snprintf(build_dir, sizeof(build_dir), "%s", project_name);

//     // Save the current (top-level) working directory
//     char original_dir[512];
//     if (!getcwd(original_dir, sizeof(original_dir))) {
//         print_error("Failed to get current working directory");
//         return -1;
//     }

//     // Change to build directory
//     if (chdir(build_dir) != 0) {

//         //Check if we are in the directory
//         if(!strcmp(original_dir,build_dir)){
//             char msg[256];
//             snprintf(msg, sizeof(msg), "Failed to change directory to %s: %s", build_dir, strerror(errno));
//             print_error(msg);
//             return -1;
//         }

//     }

//     int result = system("make");
//     if (result != 0) {
//         print_error("Build failed");
//         return -1;
//     }

//     print_ok("Build completed successfully");
//     return 0;
// }

int main(int argc, char *argv[]) {

    //parse the cli arguments into the table.
    cli_args_t args;
    cli_args_init(&args);

    if (cli_args_parse(&args, argc, argv) != 0) {
        print_error("Failed to cli parse arguments\n");
        return 1;
    }

    if(argc < 3){
        printf("Not enough cli arguments detected\n");
        return 0;
    }

    //Parallel build flag.
    int parallel_build = 0;

    //Incremental build flag
    int incremental_build = 1;

    //Project dir
    const char *project_dir;

    //New Command
    if (hashmap_contains_key_and_index(&args.args_map, "new", 1)) {
        project_dir = return_key_with_no_dashes(&args.args_map,"new");
        if(project_dir == NULL){
            print_error("No valid project directory chosen with the new flag.");
            print_error("Syntax is \"fortean new project\"");
            return 1;
        }
        printf("Initializing new project in '%s'...\n", project_dir);

         // Create the main project directory first
        if (create_dir(project_dir) != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create project directory: %s", project_dir);
            print_error(msg);
            return 1;
        } else {
            print_ok("Created project root directory");
        }

        // Create subdirectories inside project_dir
        create_directories(project_dir);

        // Copy template files into project_dir/build
        copy_template_files(project_dir);

        //Replace the name
        //replace_program_name_in_makefile(project_dir);
        
        //Build a program
        create_main_f90(project_dir);

        //Make teh generic toml file
        generate_project_toml(project_dir);

        //Safely exit
        return 0;
    }

    //Build command
    if (hashmap_contains_key_and_index(&args.args_map, "build", 1)) {
        project_dir = return_key_with_no_dashes(&args.args_map,"build");
        if(project_dir == NULL){
            print_error("No valid project directory chosen with the build flag.");
            print_error("Syntax is \"fortean build project\"");
            return 1;
        }

        //Check if we are doing a parallel build.
        if(hashmap_contains(&args.args_map, "-j")) parallel_build = 1;

        //Check if we are allowing an incremental build.
        if(hashmap_contains(&args.args_map, "-r") || hashmap_contains(&args.args_map, "--rebuild") ){
            incremental_build = 0;
        }

        //Run the build
        fortean_build_project_incremental(project_dir,parallel_build,incremental_build);

        //Check if we want to go through a makefile. This is deprecated now that everything works? 
        // if(hashmap_contains(&args.args_map, "-m")){
        //     run_make_in_build(project_dir);
        // }else{
        //     fortean_build_project_incremental(project_dir,parallel_build,incremental_build );
        // }


        //Safely exit
        return 0;
    }

    //Run command
    if (hashmap_contains_key_and_index(&args.args_map, "run", 1)) {
        project_dir = return_key_with_no_dashes(&args.args_map,"run");
        if(project_dir == NULL){
            print_error("No valid project directory chosen with the run flag.");
            print_error("Syntax is \"fortean run project\"");
            return 1;
        }

            //Check if we are doing a parallel build.
        if(hashmap_contains(&args.args_map, "-j")) parallel_build = 1;

        //Check if we are allowing an incremental build.
        if(hashmap_contains(&args.args_map, "-r") || hashmap_contains(&args.args_map, "--rebuild") ){
            incremental_build = 0;
        }

        if(!hashmap_contains(&args.args_map, "--bin")){
            fortean_build_project_incremental(project_dir,parallel_build,incremental_build);
        }
        chdir(project_dir);
        char exe[512];
        #ifdef _WIN32
            snprintf(exe,sizeof(exe),"%s.exe",project_dir);
        #else
            snprintf(exe,sizeof(exe),"%s",project_dir);
        #endif
        system(exe);

        //Safely exit
        return 0;
    }

    suggestClosestWord(argv[1],dictionary,dictionary_size);
    return 1;
}
