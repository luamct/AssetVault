# Database System

SQLite-based persistent storage for asset metadata with fast indexing and search capabilities.

## Features

- **Persistent Storage**: All asset information is stored in a local SQLite database
- **Fast Queries**: Indexed queries for quick asset lookups
- **Batch Operations**: Efficient bulk insert/update operations
- **Search Capabilities**: Search assets by name, type, or directory
- **Statistics**: Get counts and sizes by asset type
- **Transaction Support**: ACID compliance for data integrity

## Database Schema

The main table `assets` contains the following columns:

| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER | Primary key, auto-increment |
| name | TEXT | File name (without path) |
| extension | TEXT | File extension (lowercase) |
| full_path | TEXT | Full absolute path to the file |
| relative_path | TEXT | Path relative to the scanned directory |
| size | INTEGER | File size in bytes |
| last_modified | TEXT | Last modification time (ISO format) |
| is_directory | INTEGER | Boolean flag (0/1) |
| asset_type | TEXT | Asset type (Texture, Model, etc.) |
| created_at | TEXT | When record was created |
| updated_at | TEXT | When record was last updated |

### Indexes

The following indexes are created for optimal query performance:

- `idx_assets_full_path`: For exact path lookups
- `idx_assets_relative_path`: For directory-based queries
- `idx_assets_asset_type`: For type-based filtering
- `idx_assets_extension`: For extension-based queries

## Usage Examples

### Basic Database Operations

```cpp
#include "database.h"
#include "index.h"

// Initialize database
AssetDatabase db;
if (!db.initialize("asset_inventory.db")) {
    std::cerr << "Failed to initialize database!" << std::endl;
    return;
}

// Scan and store assets
std::vector<FileInfo> files = scan_directory("assets");
db.insert_assets_batch(files);

// Query all assets
std::vector<FileInfo> all_assets = db.get_all_assets();

// Get statistics
int total_count = db.get_total_asset_count();
uint64_t total_size = db.get_total_size();
```

### Filtering and Searching

```cpp
// Get all textures
std::vector<FileInfo> textures = db.get_assets_by_type(AssetType::Texture);

// Search by name
std::vector<FileInfo> results = db.search_assets_by_name("texture");

// Get assets in specific directory
std::vector<FileInfo> icon_files = db.get_assets_by_directory("icons");

// Get specific asset
FileInfo asset = db.get_asset_by_path("/path/to/asset.png");
```

### Statistics and Reporting

```cpp
// Get counts by type
std::vector<AssetType> types = {
    AssetType::Texture, AssetType::Model, AssetType::Sound,
    AssetType::Font, AssetType::Shader, AssetType::Document,
    AssetType::Archive, AssetType::Directory, AssetType::Unknown
};

for (auto type : types) {
    int count = db.get_asset_count_by_type(type);
    uint64_t size = db.get_size_by_type(type);
    if (count > 0) {
        std::cout << get_asset_type_string(type)
                  << ": " << count << " files, " << size << " bytes" << std::endl;
    }
}
```

### Updating and Deleting

```cpp
// Update an asset (e.g., after file modification)
FileInfo updated_file = /* ... */;
db.update_asset(updated_file);

// Delete specific asset
db.delete_asset("/path/to/deleted/file.png");

// Delete all assets in a directory
db.delete_assets_by_directory("old_assets");

// Clear all data
db.clear_all_assets();
```

## Performance Considerations

### Batch Operations

For large numbers of files, always use batch operations:

```cpp
// Good: Batch insert
std::vector<FileInfo> files = scan_directory("large_assets_folder");
db.insert_assets_batch(files);

// Avoid: Individual inserts for large datasets
for (const auto& file : files) {
    db.insert_asset(file); // Slower for large datasets
}
```

### Transaction Management

The database automatically uses transactions for batch operations. For custom operations, you can manually control transactions:

```cpp
db.execute_sql("BEGIN TRANSACTION");
// ... perform multiple operations ...
db.execute_sql("COMMIT");
// or db.execute_sql("ROLLBACK"); on error
```

### Query Optimization

- Use indexed columns in WHERE clauses
- Limit result sets when possible
- Use appropriate data types (INTEGER for booleans, TEXT for timestamps)

## Database File Management

### Location

By default, the database file is created in the current working directory as `asset_inventory.db`. You can specify a custom path:

```cpp
db.initialize("/path/to/custom/asset_inventory.db");
```

### Backup and Migration

The database file is a standard SQLite file that can be:

- **Backed up**: Simply copy the `.db` file
- **Migrated**: Move the file to another location
- **Inspected**: Use any SQLite browser (DB Browser for SQLite, etc.)
- **Exported**: Use SQLite command line tools

### Manual Inspection

You can inspect the database using SQLite command line tools:

```bash
# Open database
sqlite3 asset_inventory.db

# View schema
.schema

# Query all assets
SELECT name, asset_type, size FROM assets LIMIT 10;

# Get statistics
SELECT asset_type, COUNT(*), SUM(size)
FROM assets
WHERE is_directory = 0
GROUP BY asset_type;
```

## Error Handling

The database class provides comprehensive error handling:

```cpp
if (!db.insert_asset(file)) {
    std::cerr << "Failed to insert asset: " << file.name << std::endl;
    // Handle error appropriately
}
```

Common error scenarios:
- Database file permissions
- Disk space issues
- Corrupted database file
- Invalid file paths

## Integration with File Watcher

The database integrates seamlessly with the file watcher system:

```cpp
// When files are added/modified/deleted
void on_file_changed(const std::string& path, FileChangeType change_type) {
    switch (change_type) {
        case FileChangeType::Added:
        case FileChangeType::Modified:
            FileInfo file = get_file_info(path);
            db.insert_asset(file); // INSERT OR REPLACE
            break;
        case FileChangeType::Deleted:
            db.delete_asset(path);
            break;
    }
}
```

## Testing

Run the database test to verify functionality:

```bash
# Build the test
cmake --build build --target DatabaseTest

# Run the test
./build/DatabaseTest
```

The test will:
1. Initialize a test database
2. Scan the assets directory
3. Insert all found files
4. Demonstrate various query operations
5. Show performance metrics

## Troubleshooting

### Common Issues

1. **SQLite not found**: Install SQLite development libraries
2. **Permission errors**: Ensure write access to database directory
3. **Performance issues**: Check if indexes are created properly
4. **Corrupted database**: Delete the `.db` file and re-scan

### Debug Information

Enable debug output by checking return values:

```cpp
if (!db.initialize("test.db")) {
    std::cerr << "Database initialization failed" << std::endl;
    return;
}

if (!db.create_tables()) {
    std::cerr << "Table creation failed" << std::endl;
    return;
}
```

## Future Enhancements

Potential improvements for the database system:

- **Full-text search**: For searching file contents
- **Metadata extraction**: Store additional file metadata
- **Versioning**: Track file changes over time
- **Compression**: Compress large text-based assets
- **Remote sync**: Sync with remote asset repositories
- **Advanced queries**: Complex filtering and sorting options
