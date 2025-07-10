## `Fortean`

A cross-platform CLI tool for creating and managing Fortran-based scientific projects. Inspired by build systems like Cargo, Fortean offers a lightweight way to initialize, configure, build, and run modular Fortran projects using TOML config and automation.

Currently only supports building a single target executable, with future support planned for more complicated builds.

---

### Features

* Initializes directory structure (`src`, `mod`, `obj`, etc.)
* Copies template executables and build config files
* Generates a `project.toml` for build configuration
* Supports incremental and parallel builds
* Cross-platform (Linux/Windows)
* Lightweight and Fast 
---

### Installation

#### From Source:

```bash
git clone https://github.com/drgates93/fortean.git
cd fortean
make install
```

> Ensure you have a C compiler and `make` installed. For Windows, use MinGW or WSL.

---

### Usage

```bash
fortean new <project-name>      # Initialize a new Fortran project
fortean build <project-name>    # Build the project
fortean run <project-name>      # Build and run the executable
```

#### Flags:

| Flag              | Description                        |
| ----------------- | ---------------------------------- |
| `-j`              | Enable parallel build              |
| `-r`, `--rebuild` | Disable incremental build          |
| `--bin`           | Skip build and run existing binary |

---

### Directory Structure

```
<project-name>/
├── src/           # Fortran source files
├── mod/           # Fortran modules (.mod)
├── obj/           # Object files (.o)
├── build/         # Output binaries and config
├── data/          # Input or template files
├── lib/           # External libraries
└── .cache/        # Hidden cache directory
```
---

### `project.toml` Example

```toml
# Auto-generated TOML config for project: test

[build]
target = "test"
compiler = "gfortran"

flags = [
  "-cpp", "-fno-align-commons", "-O3",
  "-ffpe-trap=zero,invalid,underflow,overflow",
  "-std=legacy", "-ffixed-line-length-none", "-fall-intrinsics",
  "-Wno-unused-variable", "-Wno-unused-function",
  "-Wno-conversion", "-fopenmp", "-Imod"
]

obj_dir = "obj"
mod_dir = "mod"

[search]
deep = ["src"]
#shallow = ["lib", "include"]

[library]
#source-libs = ["lib/test.lib"]
```

### Search Directories for files

```
[search]
deep = ["src"]
#shallow = ["lib", "include"]
```

| TOML             | Description                        |
| ----------------- | ---------------------------------- |
| `deep    = ["src"]` | Comma separated list of directories to **recursively** search for files and add to the depedency graph|
| `shallow = ["lib"]` | Comma separated list of directories to search for files and add to the depedency graph.|

### License

This project is licensed under the MIT License. See `LICENSE` for more information.

---

Let me know if you want the `README.md` file saved or customized for Doxygen or GitHub Pages!
