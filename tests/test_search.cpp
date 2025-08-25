#include <catch2/catch_all.hpp>
#include <map>
#include <unordered_map>
#include "search.h"
#include "asset.h"
#include "test_helpers.h"
#include "utils.h"
#include "config.h"

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

TEST_CASE("asset_matches_search text matching", "[search]") {
    Asset asset = create_test_asset("monster_texture", ".png", AssetType::_2D, "assets/textures/monster_texture.png");

    SECTION("Name matching") {
        SearchQuery query;
        query.text_query = "monster";
        REQUIRE(asset_matches_search(asset, query) == true);

        query.text_query = "texture";
        REQUIRE(asset_matches_search(asset, query) == true);

        query.text_query = "robot";
        REQUIRE(asset_matches_search(asset, query) == false);
    }

    SECTION("Extension matching") {
        SearchQuery query;
        query.text_query = "png";
        REQUIRE(asset_matches_search(asset, query) == true);

        query.text_query = "jpg";
        REQUIRE(asset_matches_search(asset, query) == false);
    }

    SECTION("Path matching") {
        SearchQuery query;
        query.text_query = "textures";
        REQUIRE(asset_matches_search(asset, query) == true);

        query.text_query = "models";
        REQUIRE(asset_matches_search(asset, query) == false);
    }

    SECTION("Case insensitive matching") {
        SearchQuery query;
        query.text_query = "MONSTER";
        REQUIRE(asset_matches_search(asset, query) == true);

        query.text_query = "PNG";
        REQUIRE(asset_matches_search(asset, query) == true);

        query.text_query = "TEXTURES";
        REQUIRE(asset_matches_search(asset, query) == true);
    }
}

TEST_CASE("asset_matches_search type filtering", "[search]") {
    Asset texture_asset = create_test_asset("texture", ".png", AssetType::_2D);
    Asset model_asset = create_test_asset("model", ".fbx", AssetType::_3D);
    Asset audio_asset = create_test_asset("sound", ".wav", AssetType::Audio);

    SECTION("Single type filter") {
        SearchQuery query;
        query.type_filters = { AssetType::_2D };

        REQUIRE(asset_matches_search(texture_asset, query) == true);
        REQUIRE(asset_matches_search(model_asset, query) == false);
        REQUIRE(asset_matches_search(audio_asset, query) == false);
    }

    SECTION("Multiple type filter (OR condition)") {
        SearchQuery query;
        query.type_filters = { AssetType::_2D, AssetType::Audio };

        REQUIRE(asset_matches_search(texture_asset, query) == true);
        REQUIRE(asset_matches_search(model_asset, query) == false);
        REQUIRE(asset_matches_search(audio_asset, query) == true);
    }

    SECTION("No type filter matches all") {
        SearchQuery query;
        // Empty type_filters means no type restriction

        REQUIRE(asset_matches_search(texture_asset, query) == true);
        REQUIRE(asset_matches_search(model_asset, query) == true);
        REQUIRE(asset_matches_search(audio_asset, query) == true);
    }
}

TEST_CASE("asset_matches_search combined filtering", "[search]") {
    Asset monster_texture = create_test_asset("monster_texture", ".png", AssetType::_2D);
    Asset monster_model = create_test_asset("monster_model", ".fbx", AssetType::_3D);
    Asset robot_texture = create_test_asset("robot_texture", ".png", AssetType::_2D);

    SECTION("Type and text both must match") {
        SearchQuery query;
        query.text_query = "monster";
        query.type_filters = { AssetType::_2D };

        REQUIRE(asset_matches_search(monster_texture, query) == true);  // Both match
        REQUIRE(asset_matches_search(monster_model, query) == false);   // Text matches, type doesn't
        REQUIRE(asset_matches_search(robot_texture, query) == false);   // Type matches, text doesn't
    }

    SECTION("Multiple search terms (AND logic)") {
        SearchQuery query;
        query.text_query = "monster texture";

        REQUIRE(asset_matches_search(monster_texture, query) == true);  // Both "monster" and "texture" match
        REQUIRE(asset_matches_search(monster_model, query) == false);   // "texture" doesn't match
        REQUIRE(asset_matches_search(robot_texture, query) == false);   // "monster" doesn't match
    }

    SECTION("Empty text query with type filter") {
        SearchQuery query;
        query.text_query = "";
        query.type_filters = { AssetType::_2D };

        REQUIRE(asset_matches_search(monster_texture, query) == true);
        REQUIRE(asset_matches_search(monster_model, query) == false);
        REQUIRE(asset_matches_search(robot_texture, query) == true);
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

TEST_CASE("asset_matches_search path filtering", "[search]") {
    // Use absolute paths that match the expected ASSET_ROOT_DIRECTORY structure
    std::string asset_root = Config::ASSET_ROOT_DIRECTORY;
    Asset texture_in_textures = create_test_asset("monster", ".png", AssetType::_2D, asset_root + "/textures/monster.png");
    Asset texture_in_ui = create_test_asset("button", ".png", AssetType::_2D, asset_root + "/textures/ui/button.png");
    Asset model_in_models = create_test_asset("character", ".fbx", AssetType::_3D, asset_root + "/models/character.fbx");
    Asset sound_in_sounds = create_test_asset("explosion", ".wav", AssetType::Audio, asset_root + "/sounds/explosion.wav");

    SECTION("Single path filter matches") {
        SearchQuery query;
        query.path_filters = { "textures" };

        REQUIRE(asset_matches_search(texture_in_textures, query) == true);
        REQUIRE(asset_matches_search(texture_in_ui, query) == true);  // textures/ui should match textures
        REQUIRE(asset_matches_search(model_in_models, query) == false);
        REQUIRE(asset_matches_search(sound_in_sounds, query) == false);
    }

    SECTION("Specific subdirectory path filter") {
        SearchQuery query;
        query.path_filters = { "textures/ui" };

        REQUIRE(asset_matches_search(texture_in_textures, query) == false);  // textures doesn't match textures/ui
        REQUIRE(asset_matches_search(texture_in_ui, query) == true);
        REQUIRE(asset_matches_search(model_in_models, query) == false);
        REQUIRE(asset_matches_search(sound_in_sounds, query) == false);
    }

    SECTION("Multiple path filters (OR condition)") {
        SearchQuery query;
        query.path_filters = { "textures", "sounds" };

        REQUIRE(asset_matches_search(texture_in_textures, query) == true);
        REQUIRE(asset_matches_search(texture_in_ui, query) == true);
        REQUIRE(asset_matches_search(model_in_models, query) == false);
        REQUIRE(asset_matches_search(sound_in_sounds, query) == true);
    }

    SECTION("Path and type filters combined") {
        SearchQuery query;
        query.type_filters = { AssetType::_2D };
        query.path_filters = { "textures" };

        REQUIRE(asset_matches_search(texture_in_textures, query) == true);   // Both match
        REQUIRE(asset_matches_search(texture_in_ui, query) == true);         // Both match
        REQUIRE(asset_matches_search(model_in_models, query) == false);      // Path doesn't match
        REQUIRE(asset_matches_search(sound_in_sounds, query) == false);      // Type doesn't match
    }

    SECTION("Path filter case insensitive") {
        SearchQuery query;
        query.path_filters = { "TEXTURES" };

        REQUIRE(asset_matches_search(texture_in_textures, query) == true);
        REQUIRE(asset_matches_search(texture_in_ui, query) == true);
    }

    SECTION("Path filter with spaces matches correctly") {
        Asset asset_with_spaces = create_test_asset("damage", ".png", AssetType::_2D, asset_root + "/simple damage/folder/damage.png");
        
        SearchQuery query;
        query.path_filters = { "simple damage/folder" };

        REQUIRE(asset_matches_search(asset_with_spaces, query) == true);
        REQUIRE(asset_matches_search(texture_in_textures, query) == false);
    }

    SECTION("Path filter with spaces partial match") {
        Asset asset_with_spaces = create_test_asset("damage", ".png", AssetType::_2D, asset_root + "/simple damage/folder/subfolder/damage.png");
        
        SearchQuery query;
        query.path_filters = { "simple damage" };

        REQUIRE(asset_matches_search(asset_with_spaces, query) == true);
    }
}

TEST_CASE("filter_assets functionality", "[search]") {
    // Create test assets map
    std::map<std::string, Asset> test_assets;
    auto assets_vector = std::vector<Asset>{
        create_test_asset("monster_texture", ".png", AssetType::_2D),
        create_test_asset("robot_texture", ".jpg", AssetType::_2D),
        create_test_asset("monster_model", ".fbx", AssetType::_3D),
        create_test_asset("explosion_sound", ".wav", AssetType::Audio),
        create_test_asset("background_music", ".mp3", AssetType::Audio),
        create_test_asset("shader", ".hlsl", AssetType::Shader)
    };
    
    // Assign IDs to assets and convert to map
    uint32_t id = 1;
    for (auto& asset : assets_vector) {
        asset.id = id++;
        test_assets[asset.full_path.u8string()] = asset;
    }

    std::mutex test_mutex;
    SearchState search_state;
    
    // Create a real SearchIndex for testing (without database dependency)
    SearchIndex search_index(nullptr);  // Pass nullptr for database
    
    // Manually populate the search index for testing
    for (const auto& [key, asset] : test_assets) {
        search_index.add_asset(asset.id, asset);
    }

    SECTION("Filter by text") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "monster");
        filter_assets(search_state, test_assets, test_mutex, search_index);

        REQUIRE(search_state.filtered_assets.size() == 2);
        // Check that both monster assets are in results (order may vary with unordered_map)
        bool has_monster_texture = false;
        bool has_monster_model = false;
        for (const auto& asset : search_state.filtered_assets) {
            if (asset.name == "monster_texture") has_monster_texture = true;
            if (asset.name == "monster_model") has_monster_model = true;
        }
        REQUIRE(has_monster_texture);
        REQUIRE(has_monster_model);
    }

    SECTION("Filter by type") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "type=2d");
        filter_assets(search_state, test_assets, test_mutex, search_index);

        REQUIRE(search_state.filtered_assets.size() == 2);
        REQUIRE(search_state.filtered_assets[0].type == AssetType::_2D);
        REQUIRE(search_state.filtered_assets[1].type == AssetType::_2D);
    }

    SECTION("Filter by multiple types") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "type=2d,audio");
        filter_assets(search_state, test_assets, test_mutex, search_index);

        REQUIRE(search_state.filtered_assets.size() == 4);
        // Should include both 2D textures and both audio files
    }

    SECTION("Combined type and text filter") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "type=2d texture");
        filter_assets(search_state, test_assets, test_mutex, search_index);

        REQUIRE(search_state.filtered_assets.size() == 2);
        // Check that both texture assets are in results (order may vary with unordered_map)
        bool has_monster_texture = false;
        bool has_robot_texture = false;
        for (const auto& asset : search_state.filtered_assets) {
            if (asset.name == "monster_texture") has_monster_texture = true;
            if (asset.name == "robot_texture") has_robot_texture = true;
        }
        REQUIRE(has_monster_texture);
        REQUIRE(has_robot_texture);
    }

    SECTION("No matches") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "nonexistent");
        filter_assets(search_state, test_assets, test_mutex, search_index);

        REQUIRE(search_state.filtered_assets.empty());
    }

    SECTION("Empty query returns all") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "");
        filter_assets(search_state, test_assets, test_mutex, search_index);

        REQUIRE(search_state.filtered_assets.size() == test_assets.size());
    }

    SECTION("Search state initialization") {
        safe_strcpy(search_state.buffer, sizeof(search_state.buffer), "monster");
        search_state.selected_asset_index = 5;
        search_state.model_preview_row = 3;

        filter_assets(search_state, test_assets, test_mutex, search_index);

        // Should reset selection and preview state
        REQUIRE(search_state.selected_asset_index == -1);
        REQUIRE(search_state.model_preview_row == -1);
        // Should initialize loaded range
        REQUIRE(search_state.loaded_start_index == 0);
        REQUIRE(search_state.loaded_end_index <= SearchState::LOAD_BATCH_SIZE);
    }
}
