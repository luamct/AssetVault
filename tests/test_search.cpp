#include <catch2/catch_all.hpp>
#include <map>
#include <unordered_map>
#include <chrono>
#include "search.h"
#include "asset.h"
#include "database.h"
#include "test_helpers.h"
#include "utils.h"
#include "config.h"
#include "services.h"

// Mock global for linking - we're not testing functions that use it
class EventProcessor;
EventProcessor* g_event_processor = nullptr;

TEST_CASE("parse_search_query basic functionality", "[search]") {
    SECTION("Empty query") {
        auto query = parse_search_query("");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.type_filters.empty());
    }

    SECTION("Text only query") {
        auto query = parse_search_query("monster texture");
        REQUIRE(query.text_query == "monster texture");
        REQUIRE(query.type_filters.empty());
    }

    SECTION("Non-ASCII text query") {
        auto query = parse_search_query("mönster× tëxture");
        REQUIRE(query.text_query == "mönster× tëxture");
        REQUIRE(query.type_filters.empty());
    }

    SECTION("Type only query") {
        auto query = parse_search_query("type=2d");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.type_filters.size() == 1);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
    }

    SECTION("Type and text query") {
        auto query = parse_search_query("type=audio monster");
        REQUIRE(query.text_query == "monster");
        REQUIRE(query.type_filters.size() == 1);
        REQUIRE(query.type_filters[0] == AssetType::Audio);
    }
}

TEST_CASE("tokenize_index_terms normalizes separators", "[search][tokens]") {
    SECTION("Hyphenated names produce separate tokens") {
        auto tokens = tokenize_index_terms("door-rotate-square-a.glb");
        std::vector<std::string> expected = {"door", "rotate", "square", "glb"};
        REQUIRE(tokens == expected);
    }

    SECTION("Full asset path with spaces") {
        auto tokens = tokenize_index_terms("Building Kit/Models/GLB format/door-rotate-square-a.glb");
        std::vector<std::string> expected = {
            "building", "kit", "models", "glb", "format", "door", "rotate", "square", "glb"
        };
        REQUIRE(tokens == expected);
    }

    SECTION("Short and numeric tokens are dropped") {
        auto tokens = tokenize_index_terms("ab-cd99-01-1xz");
        std::vector<std::string> expected = {"cd99", "1xz"};
        REQUIRE(tokens == expected);
    }
}

TEST_CASE("parse_search_query multiple types", "[search]") {
    SECTION("Multiple types comma separated") {
        auto query = parse_search_query("type=2d,audio");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.type_filters.size() == 2);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
        REQUIRE(query.type_filters[1] == AssetType::Audio);
    }

    SECTION("Multiple types with text") {
        auto query = parse_search_query("type=2d,3d monster");
        REQUIRE(query.text_query == "monster");
        REQUIRE(query.type_filters.size() == 2);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
        REQUIRE(query.type_filters[1] == AssetType::_3D);
    }
}

TEST_CASE("parse_search_query case insensitive", "[search]") {
    SECTION("Uppercase type names") {
        auto query = parse_search_query("type=2D,AUDIO");
        REQUIRE(query.type_filters.size() == 2);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
        REQUIRE(query.type_filters[1] == AssetType::Audio);
    }

    SECTION("Mixed case type names") {
        auto query = parse_search_query("type=2D,audio,3D");
        REQUIRE(query.type_filters.size() == 3);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
        REQUIRE(query.type_filters[1] == AssetType::Audio);
        REQUIRE(query.type_filters[2] == AssetType::_3D);
    }
}

TEST_CASE("parse_search_query whitespace handling", "[search]") {
    SECTION("Spaces around equals") {
        auto query = parse_search_query("type = 2d");
        REQUIRE(query.type_filters.size() == 1);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
    }

    SECTION("Spaces everywhere") {
        auto query = parse_search_query("  type = 2d , audio   monster  ");
        REQUIRE(query.text_query == "monster");
        REQUIRE(query.type_filters.size() == 2);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
        REQUIRE(query.type_filters[1] == AssetType::Audio);
    }
}

TEST_CASE("parse_search_query unknown types", "[search]") {
    SECTION("Unknown type ignored") {
        auto query = parse_search_query("type=2d,invalidtype,audio");
        REQUIRE(query.type_filters.size() == 2);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
        REQUIRE(query.type_filters[1] == AssetType::Audio);
    }

    SECTION("All unknown types") {
        auto query = parse_search_query("type=invalid1,invalid2");
        REQUIRE(query.type_filters.empty());
    }
}


TEST_CASE("parse_search_query edge cases", "[search]") {
    SECTION("Empty type list") {
        auto query = parse_search_query("type=");
        REQUIRE(query.type_filters.empty());
    }

    SECTION("Only commas") {
        auto query = parse_search_query("type=,,,");
        REQUIRE(query.type_filters.empty());
    }

    SECTION("Type with only spaces") {
        auto query = parse_search_query("type=  ,  ,  ");
        REQUIRE(query.type_filters.empty());
    }
}

TEST_CASE("filter_assets applies text, type, and path filters", "[search][filter]") {
    std::vector<Asset> assets_vector = {
        create_test_asset("textures/monster_texture.png", AssetType::_2D),
        create_test_asset("textures/robot_texture.png", AssetType::_2D),
        create_test_asset("textures/ui/button_texture.png", AssetType::_2D),
        create_test_asset("models/monster_model.fbx", AssetType::_3D),
        create_test_asset("sounds/explosion_sound.wav", AssetType::Audio),
        create_test_asset("audio/background_music.mp3", AssetType::Audio)
    };

    // Assign IDs to assets and convert to map
    uint32_t id = 1;
    for (auto& asset : assets_vector) {
        asset.id = id++;
    }

    SafeAssets test_assets;
    {
        auto [lock, assets_map] = test_assets.write();
        for (const auto& asset : assets_vector) {
            assets_map[asset.path] = asset;
        }
    }

    SearchIndex search_index;
    MockDatabase mock_db;  // Needed for Services
    Services::provide(&mock_db, &search_index, nullptr, nullptr, nullptr, nullptr, nullptr);
    {
        auto [lock, assets_map] = test_assets.read();
        for (const auto& [key, asset] : assets_map) {
            search_index.add_asset(asset.id, asset);
        }
    }

    auto run_query = [&](const std::string& query,
                         const std::vector<std::string>& path_filters = {}) {
        UIState state;
        safe_strcpy(state.buffer, sizeof(state.buffer), query.c_str());
        if (!path_filters.empty()) {
            state.path_filter_active = true;
            state.path_filters = path_filters;
        }
        filter_assets(state, test_assets);
        std::vector<std::string> names;
        for (const auto& asset : state.results) {
            names.push_back(asset.name);
        }
        return names;
    };

    SECTION("Text matching is case-insensitive and path-aware") {
        auto names = run_query("monster");
        std::vector<std::string> expected = {"monster_model", "monster_texture"};
        REQUIRE(names == expected);
    }

    SECTION("Type filters in query are applied") {
        auto names = run_query("type=audio");
        std::vector<std::string> expected = {"background_music", "explosion_sound"};
        REQUIRE(names == expected);
    }

    SECTION("Combined text and type filters narrow results") {
        auto names = run_query("type=2d texture");
        std::vector<std::string> expected = {
            "monster_texture",
            "robot_texture",
            "button_texture"
        };
        REQUIRE(names == expected);
    }

    SECTION("UI path filters narrow the result set") {
        auto names = run_query("", {"textures"});
        std::vector<std::string> expected = {
            "monster_texture",
            "robot_texture",
            "button_texture"
        };
        REQUIRE(names == expected);
    }

    SECTION("UI path filters handle subdirectories") {
        auto names = run_query("", {"textures/ui"});
        std::vector<std::string> expected = {"button_texture"};
        REQUIRE(names == expected);
    }
}

TEST_CASE("parse_search_query path filtering", "[search]") {
    SECTION("Single path filter") {
        auto query = parse_search_query("path=textures");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.type_filters.empty());
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "textures");
    }

    SECTION("Multiple path filters comma separated") {
        auto query = parse_search_query("path=textures,sounds");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.type_filters.empty());
        REQUIRE(query.path_filters.size() == 2);
        REQUIRE(query.path_filters[0] == "textures");
        REQUIRE(query.path_filters[1] == "sounds");
    }

    SECTION("Path filter with text") {
        auto query = parse_search_query("path=textures monster");
        REQUIRE(query.text_query == "monster");
        REQUIRE(query.type_filters.empty());
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "textures");
    }

    SECTION("Path filter with subdirectory") {
        auto query = parse_search_query("path=models/characters");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "models/characters");
    }

    SECTION("Path filter with backslashes normalized") {
        auto query = parse_search_query("path=models\\characters");
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "models/characters");
    }

    SECTION("Path and type filters combined") {
        auto query = parse_search_query("type=2d path=textures");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.type_filters.size() == 1);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "textures");
    }

    SECTION("Path filter whitespace handling") {
        auto query = parse_search_query("path = textures , sounds   monster");
        REQUIRE(query.text_query == "monster");
        REQUIRE(query.path_filters.size() == 2);
        REQUIRE(query.path_filters[0] == "textures");
        REQUIRE(query.path_filters[1] == "sounds");
    }

    SECTION("Quoted path with spaces") {
        auto query = parse_search_query("path=\"simple damage/folder\"");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "simple damage/folder");
    }

    SECTION("Multiple quoted paths with spaces") {
        auto query = parse_search_query("path=\"simple damage/folder\",\"another path/with spaces\"");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.path_filters.size() == 2);
        REQUIRE(query.path_filters[0] == "simple damage/folder");
        REQUIRE(query.path_filters[1] == "another path/with spaces");
    }

    SECTION("Quoted path with text query") {
        auto query = parse_search_query("path=\"simple damage/folder\" monster");
        REQUIRE(query.text_query == "monster");
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "simple damage/folder");
    }

    SECTION("Quoted path with backslashes normalized") {
        auto query = parse_search_query("path=\"simple damage\\\\folder\"");
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "simple damage/folder");
    }

    SECTION("Quoted path with escaped quotes") {
        auto query = parse_search_query("path=\"folder \\\"with quotes\\\"/subfolder\"");
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "folder \"with quotes\"/subfolder");
    }

    SECTION("Mixed quoted and unquoted paths now supported") {
        // The new token-based parser correctly handles mixed quoted and unquoted paths
        auto query = parse_search_query("path=\"simple damage/folder\",textures");
        REQUIRE(query.path_filters.size() == 2);
        REQUIRE(query.path_filters[0] == "simple damage/folder");
        REQUIRE(query.path_filters[1] == "textures");
    }

    SECTION("Quoted path and type filters combined") {
        auto query = parse_search_query("type=2d path=\"simple damage/folder\"");
        REQUIRE(query.text_query.empty());
        REQUIRE(query.type_filters.size() == 1);
        REQUIRE(query.type_filters[0] == AssetType::_2D);
        REQUIRE(query.path_filters.size() == 1);
        REQUIRE(query.path_filters[0] == "simple damage/folder");
    }
}

TEST_CASE("filter_assets path filters cover case and spaces", "[search][filter]") {
    std::vector<Asset> assets_vector = {
        create_test_asset("textures/monster.png", AssetType::_2D),
        create_test_asset("textures/ui/button.png", AssetType::_2D),
        create_test_asset("simple damage/folder/damage.png", AssetType::_2D),
        create_test_asset("sounds/explosion.wav", AssetType::Audio)
    };

    uint32_t id = 1;
    for (auto& asset : assets_vector) {
        asset.id = id++;
    }

    SafeAssets test_assets;
    {
        auto [lock, assets_map] = test_assets.write();
        for (const auto& asset : assets_vector) {
            assets_map[asset.path] = asset;
        }
    }

    SearchIndex search_index;
    MockDatabase mock_db;
    Services::provide(&mock_db, &search_index, nullptr, nullptr, nullptr, nullptr, nullptr);
    {
        auto [lock, assets_map] = test_assets.read();
        for (const auto& [key, asset] : assets_map) {
            search_index.add_asset(asset.id, asset);
        }
    }

    auto run_path_filter = [&](const std::vector<std::string>& filters) {
        UIState state;
        state.path_filter_active = true;
        state.path_filters = filters;
        safe_strcpy(state.buffer, sizeof(state.buffer), "");
        filter_assets(state, test_assets);
        std::vector<std::string> names;
        for (const auto& asset : state.results) {
            names.push_back(asset.name);
        }
        return names;
    };

    SECTION("Case-insensitive path filters") {
        auto names = run_path_filter({"TEXTURES"});
        std::vector<std::string> expected = {"monster", "button"};
        REQUIRE(names == expected);
    }

    SECTION("Path filters with spaces match exact segments") {
        auto names = run_path_filter({"simple damage/folder"});
        std::vector<std::string> expected = {"damage"};
        REQUIRE(names == expected);
    }

    SECTION("Path filters can target subdirectories") {
        auto names = run_path_filter({"textures/ui"});
        std::vector<std::string> expected = {"button"};
        REQUIRE(names == expected);
    }
}

TEST_CASE("SearchIndex tokenization and search", "[search][index]") {
    // Create a search index - no database needed anymore
    SearchIndex index;

    SECTION("Tokenization works correctly") {
        Asset asset = create_test_asset("textures/MyTexture_diffuse.png", AssetType::_2D, 1);

        // Build index from assets
        std::vector<Asset> assets = {asset};
        REQUIRE(index.build_from_assets(assets));

        // Test searches for different tokens
        auto results = index.search_prefix("mytexture");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);

        results = index.search_prefix("diffuse");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);

        results = index.search_prefix("png");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);

        results = index.search_prefix("texture");  // Should match "textures" directory
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);

        // Test short queries are ignored
        results = index.search_prefix("my");  // <= 2 chars
        REQUIRE(results.empty());
    }

    SECTION("Multi-term search works correctly") {
        // Add multiple assets
        Asset asset1 = create_test_asset("nature/grass_texture.png", AssetType::_2D, 1);
        Asset asset2 = create_test_asset("nature/rock_texture.jpg", AssetType::_2D, 2);
        Asset asset3 = create_test_asset("models/player_model.fbx", AssetType::_3D, 3);

        // Build index from all assets
        std::vector<Asset> assets = {asset1, asset2, asset3};
        REQUIRE(index.build_from_assets(assets));

        // Test single term searches
        auto results = index.search_prefix("texture");
        REQUIRE(results.size() == 2);  // grass_texture and rock_texture

        results = index.search_prefix("nature");
        REQUIRE(results.size() == 2);  // Both in nature directory

        results = index.search_prefix("player");
        REQUIRE(results.size() == 1);  // Only player_model

        // Test multi-term searches (AND logic)
        std::vector<std::string> terms = { "texture", "nature" };
        results = index.search_terms(terms);
        REQUIRE(results.size() == 2);  // Both nature textures have both terms

        terms = { "grass", "texture" };
        results = index.search_terms(terms);
        REQUIRE(results.size() == 1);  // Only grass_texture

        terms = { "player", "texture" };
        results = index.search_terms(terms);
        REQUIRE(results.empty());  // No asset has both terms
    }

    SECTION("Prefix matching works correctly") {
        Asset asset = create_test_asset("ui/awesome_background.png", AssetType::_2D, 1);

        index.add_asset(asset.id, asset);

        // Test prefix matching
        auto results = index.search_prefix("awe");  // Prefix of "awesome"
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);

        results = index.search_prefix("awesome");  // Exact match
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);

        results = index.search_prefix("back");  // Prefix of "background"
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);

        results = index.search_prefix("xyz");  // No match
        REQUIRE(results.empty());
    }

    SECTION("Query splitting with underscores") {
        Asset asset = create_test_asset("weapons/blaster_A.fbx", AssetType::_3D, 1);

        index.add_asset(asset.id, asset);

        // Searching for "blaster_A" should split into ["blaster", "a"] (a is ignored as <=2)
        // So it should match on "blaster" alone
        auto results = index.search_prefix("blaster");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
    }

    SECTION("Query splitting with slashes") {
        Asset asset = create_test_asset("models/blaster.fbx", AssetType::_3D, 1);

        index.add_asset(asset.id, asset);

        // Should match individual tokens
        auto results = index.search_prefix("models");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);

        results = index.search_prefix("blaster");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
    }

    SECTION("Index statistics work correctly") {
        // Add a few assets
        Asset asset1 = create_test_asset("test1.png", AssetType::_2D, 1);
        index.add_asset(asset1.id, asset1);

        Asset asset2 = create_test_asset("images/test2.jpg", AssetType::_2D, 2);
        index.add_asset(asset2.id, asset2);

        // Check statistics
        REQUIRE(index.get_token_count() > 0);
        REQUIRE(index.get_memory_usage() > 0);

        // Clear and verify
        index.clear();
        REQUIRE(index.get_token_count() == 0);
    }
}

TEST_CASE("filter_assets functionality", "[search]") {
    // Create test assets map
    SafeAssets test_assets;
    auto assets_vector = std::vector<Asset>{
        create_test_asset("monster_texture.png", AssetType::_2D),
        create_test_asset("robot_texture.jpg", AssetType::_2D),
        create_test_asset("monster_model.fbx", AssetType::_3D),
        create_test_asset("explosion_sound.wav", AssetType::Audio),
        create_test_asset("background_music.mp3", AssetType::Audio),
        create_test_asset("shader.hlsl", AssetType::Shader)
    };
    
    // Assign IDs to assets and convert to map
    uint32_t id = 1;
    for (auto& asset : assets_vector) {
        asset.id = id++;
    }

    // Populate test_assets
    {
        auto [lock, assets_map] = test_assets.write();
        for (const auto& asset : assets_vector) {
            assets_map[asset.path] = asset;
        }
    }

    UIState search_state;

    // Create a real SearchIndex for testing (no database dependency)
    SearchIndex search_index;
    MockDatabase mock_db;  // Needed for Services

    // Register services for testing (nullptr for services not needed for search tests)
    Services::provide(&mock_db, &search_index, nullptr, nullptr, nullptr, nullptr, nullptr);

    // Manually populate the search index for testing
    {
        auto [lock, assets_map] = test_assets.read();
        for (const auto& [key, asset] : assets_map) {
            search_index.add_asset(asset.id, asset);
        }
    }

    SECTION("Filter by text") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "monster");
        filter_assets(search_state, test_assets);

        std::vector<std::string> names;
        for (const auto& asset : search_state.results) {
            names.push_back(asset.name);
        }
        std::vector<std::string> expected = {"monster_model", "monster_texture"};
        REQUIRE(names == expected);
    }

    SECTION("Filter by type") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "type=2d");
        filter_assets(search_state, test_assets);

        std::vector<std::string> names;
        for (const auto& asset : search_state.results) {
            names.push_back(asset.name);
            REQUIRE(asset.type == AssetType::_2D);
        }
        std::vector<std::string> expected = {"monster_texture", "robot_texture"};
        REQUIRE(names == expected);
    }

    SECTION("Filter by multiple types") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "type=2d,audio");
        filter_assets(search_state, test_assets);

        std::vector<std::string> names;
        for (const auto& asset : search_state.results) {
            names.push_back(asset.name);
        }
        std::vector<std::string> expected = {
            "background_music",
            "explosion_sound",
            "monster_texture",
            "robot_texture"
        };
        REQUIRE(names == expected);
    }

    SECTION("Combined type and text filter") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "type=2d texture");
        filter_assets(search_state, test_assets);

        std::vector<std::string> names;
        for (const auto& asset : search_state.results) {
            names.push_back(asset.name);
        }
        std::vector<std::string> expected = {"monster_texture", "robot_texture"};
        REQUIRE(names == expected);
    }

    SECTION("No matches") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "nonexistent");
        filter_assets(search_state, test_assets);

        REQUIRE(search_state.results.empty());
    }

    SECTION("Empty query returns all") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "");
        filter_assets(search_state, test_assets);

        std::vector<std::string> names;
        for (const auto& asset : search_state.results) {
            names.push_back(asset.name);
        }
        std::vector<std::string> expected = {
            "background_music",
            "explosion_sound",
            "monster_model",
            "monster_texture",
            "robot_texture",
            "shader"
        };
        REQUIRE(names == expected);
    }

    SECTION("Folder selection empty returns no results") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "");
        search_state.path_filter_active = true;
        search_state.folder_selection_empty = true;
        search_state.folder_selection_covers_all = false;
        search_state.path_filters.clear();

        filter_assets(search_state, test_assets);

        REQUIRE(search_state.results.empty());
        REQUIRE(search_state.loaded_end_index == 0);
    }

    SECTION("Search state initialization") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "monster");
        search_state.selected_asset_index = 5;
        search_state.model_preview_row = 3;

        filter_assets(search_state, test_assets);

        // Selection should be preserved across filtering; preview row resets
        REQUIRE(search_state.selected_asset_index == 5);
        REQUIRE(search_state.model_preview_row == -1);
        // Should initialize loaded range
        REQUIRE(search_state.loaded_start_index == 0);
        REQUIRE(search_state.loaded_end_index <= UIState::LOAD_BATCH_SIZE);
    }

    SECTION("Query splitting on slashes and underscores") {
        // Search for "monster_texture" - should split into "monster" and "texture"
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "monster_texture");
        filter_assets(search_state, test_assets);

        // Should find the asset with both "monster" and "texture" in its path
        REQUIRE(search_state.results.size() == 1);
        REQUIRE(search_state.results[0].name == "monster_texture");

        // Search for "monster/model" - should split into "monster" and "model"
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "monster/model");
        filter_assets(search_state, test_assets);

        // Should find the asset with both "monster" and "model" in its path
        REQUIRE(search_state.results.size() == 1);
        REQUIRE(search_state.results[0].name == "monster_model");
    }

    SECTION("Preserve loaded range when requested") {
        // Add enough assets to exceed the default batch size
        {
            auto [lock, assets_map] = test_assets.write();
            uint32_t next_id = static_cast<uint32_t>(assets_map.size() + 1);
            for (int i = 0; i < 120; ++i) {
                auto extra = create_test_asset("file_" + std::to_string(i) + ".png", AssetType::_2D, next_id++);
                assets_map[extra.path] = extra;
                search_index.add_asset(extra.id, extra);
            }
        }

        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "");
        search_state.loaded_end_index = 80;

        filter_assets(search_state, test_assets, true);

        REQUIRE(search_state.results.size() > UIState::LOAD_BATCH_SIZE);
        REQUIRE(search_state.loaded_end_index == 80);
        REQUIRE(search_state.new_search_finished == false);
    }

    SECTION("Loaded range resets without preserve flag") {
        {
            auto [lock, assets_map] = test_assets.write();
            uint32_t next_id = static_cast<uint32_t>(assets_map.size() + 1);
            for (int i = 0; i < 120; ++i) {
                auto extra = create_test_asset("file_reset_" + std::to_string(i) + ".png", AssetType::_2D, next_id++);
                assets_map[extra.path] = extra;
                search_index.add_asset(extra.id, extra);
            }
        }

        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "");
        search_state.loaded_end_index = 80;

        filter_assets(search_state, test_assets);

        REQUIRE(search_state.results.size() > UIState::LOAD_BATCH_SIZE);
        REQUIRE(search_state.loaded_end_index == UIState::LOAD_BATCH_SIZE);
        REQUIRE(search_state.new_search_finished == true);
    }
}
