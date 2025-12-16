# Test Files Directory

This directory contains pre-created test files used by the file watcher unit tests.

## Directory Structure

```
tests/files/
├── README.md                    # This file
├── single_file.png              # Single file for copy tests
├── test_modify.png              # File for modification tests
├── source_dir/                  # Directory with multiple files for copy/move tests
│   ├── file1.png
│   ├── file2.png
│   ├── file3.png
│   └── subdir/
│       └── subfile.png
├── external_dir/                # External directory for move-in tests
│   └── external_file.txt
├── delete_test_dir/             # Directory structure for deletion tests
│   ├── file1.png
│   ├── file2.obj
│   ├── subdir1/
│   │   ├── nested1.obj
│   │   └── nested2.fbx
│   └── subdir2/
│       └── deep.wav
└── move_test_dir/               # Directory structure for move-out tests
    ├── move1.txt
    ├── move2.png
    └── subdir/
        └── nested.obj
```

## Usage

Tests copy these files to temporary directories to avoid modifying the original test assets. This approach:

1. **Reduces test complexity** - No need to create files in code
2. **Improves readability** - Clear file structure visible in filesystem
3. **Enables version control** - Test assets are tracked and consistent
4. **Simplifies debugging** - Can manually inspect test files
5. **Prevents test pollution** - Original files remain unchanged

## File Types

The test files use extensions that are recognized by the AssetVault system:
- `.png` files are detected as `AssetType::Texture`
- `.txt` files are detected as `AssetType::Document`

This ensures the file watcher tests cover realistic asset types.