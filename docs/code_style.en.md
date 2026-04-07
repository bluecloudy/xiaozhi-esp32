# Code Style Guide

## Code Formatting Tool

This project uses `clang-format` to enforce a unified code style. A `.clang-format` configuration file is provided at the project root. It is based on the Google C++ style guide with project-specific adjustments.

### Install clang-format

Before use, make sure `clang-format` is installed:

- **Windows**:
  ```powershell
  winget install LLVM
  # or use Chocolatey
  choco install llvm
  ```

- **Linux**:
  ```bash
  sudo apt install clang-format  # Ubuntu/Debian
  sudo dnf install clang-tools-extra  # Fedora
  ```

- **macOS**:
  ```bash
  brew install clang-format
  ```

### Usage

1. **Format a single file**:
   ```bash
   clang-format -i path/to/your/file.cpp
   ```

2. **Format the whole project**:
   ```bash
   # Run at project root
   find main -iname *.h -o -iname *.cc | xargs clang-format -i
   ```

3. **Check formatting before commit**:
   ```bash
   # Check whether formatting matches rules (without modifying files)
   clang-format --dry-run -Werror path/to/your/file.cpp
   ```

### IDE Integration

- **Visual Studio Code**:
  1. Install the C/C++ extension
  2. Enable `C_Cpp.formatting` as `clang-format` in settings
  3. Optionally enable format-on-save: `editor.formatOnSave: true`

- **CLion**:
  1. In settings, go to `Editor > Code Style > C/C++`
  2. Set `Formatter` to `clang-format`
  3. Choose the `.clang-format` file in the project

### Main Formatting Rules

- Use 4 spaces for indentation
- Line width limit: 100 characters
- Braces use Attach style (same line as control statements)
- Pointer and reference symbols are left-aligned
- Automatically sort header includes
- Class access specifier indentation is -4 spaces

### Notes

1. Ensure code is formatted before committing
2. Do not manually re-align already formatted code
3. If a block should not be formatted, wrap it with:
   ```cpp
   // clang-format off
   // your code
   // clang-format on
   ```

### FAQ

1. **Formatting fails**:
   - Check whether the `clang-format` version is too old
   - Confirm file encoding is UTF-8
   - Verify `.clang-format` syntax is valid

2. **Output is different from expectation**:
   - Check whether the `.clang-format` in project root is actually used
   - Confirm no other `.clang-format` file takes precedence

If you have any questions or suggestions, feel free to open an issue or pull request.
