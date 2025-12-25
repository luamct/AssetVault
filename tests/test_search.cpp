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

TEST_CASE("asset_matches_search text matching", "[search]") {
    Asset asset = create_test_asset("monster_texture", ".png", AssetType::_2D, "assets/textures/monster_texture.png");
    Asset non_ascii_asset = create_test_asset("mönster×_tëxture", ".png", AssetType::_2D, "assets/textures/mönster×_tëxture.png");

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

    SECTION("Non-ASCII name matching") {
        SearchQuery query;
        query.text_query = "mönster";
        
        REQUIRE(asset_matches_search(asset, query) == false);          // ASCII asset doesn't match
        REQUIRE(asset_matches_search(non_ascii_asset, query) == true); // Non-ASCII asset matches
    }

    SECTION("Non-ASCII character search with multiplication sign") {
        SearchQuery query;
        query.text_query = "×";
        
        REQUIRE(asset_matches_search(asset, query) == false);          // ASCII asset doesn't have ×
        REQUIRE(asset_matches_search(non_ascii_asset, query) == true); // Non-ASCII asset has ×
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
    // Use absolute paths relative to a configured asset root
    std::string asset_root = "/tmp/test_assets_root";
    Asset texture_in_textures = create_test_asset("monster", ".png", AssetType::_2D, asset_root + "/textures/monster.png", asset_root);
    Asset texture_in_ui = create_test_asset("button", ".png", AssetType::_2D, asset_root + "/textures/ui/button.png", asset_root);
    Asset model_in_models = create_test_asset("character", ".fbx", AssetType::_3D, asset_root + "/models/character.fbx", asset_root);
    Asset sound_in_sounds = create_test_asset("explosion", ".wav", AssetType::Audio, asset_root + "/sounds/explosion.wav", asset_root);

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
        Asset asset_with_spaces = create_test_asset("damage", ".png", AssetType::_2D, asset_root + "/simple damage/folder/damage.png", asset_root);

        SearchQuery query;
        query.path_filters = { "simple damage/folder" };

        REQUIRE(asset_matches_search(asset_with_spaces, query) == true);
        REQUIRE(asset_matches_search(texture_in_textures, query) == false);
    }

    SECTION("Path filter with spaces partial match") {
        Asset asset_with_spaces = create_test_asset("damage", ".png", AssetType::_2D, asset_root + "/simple damage/folder/subfolder/damage.png", asset_root);

        SearchQuery query;
        query.path_filters = { "simple damage" };

        REQUIRE(asset_matches_search(asset_with_spaces, query) == true);
    }
}

TEST_CASE("SearchIndex tokenization and search", "[search][index]") {
    // Create a search index - no database needed anymore
    SearchIndex index;

    SECTION("Tokenization works correctly") {
        Asset asset;
        asset.id = 1;
        asset.name = "MyTexture_diffuse.png";
        asset.extension = "png";
        asset.path = "/assets/textures/MyTexture_diffuse.png";
        asset.relative_path = "textures/MyTexture_diffuse.png";  // Set relative path for tokenization
        asset.size = 1024;
        asset.last_modified = std::chrono::system_clock::now();
        asset.type = AssetType::_2D;

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
        Asset asset1;
        asset1.id = 1;
        asset1.name = "grass_texture.png";
        asset1.extension = "png";
        asset1.path = "/assets/nature/grass_texture.png";
        asset1.relative_path = "nature/grass_texture.png";
        asset1.size = 1024;
        asset1.last_modified = std::chrono::system_clock::now();
        asset1.type = AssetType::_2D;

        Asset asset2;
        asset2.id = 2;
        asset2.name = "rock_texture.jpg";
        asset2.extension = "jpg";
        asset2.path = "/assets/nature/rock_texture.jpg";
        asset2.relative_path = "nature/rock_texture.jpg";
        asset2.size = 2048;
        asset2.last_modified = std::chrono::system_clock::now();
        asset2.type = AssetType::_2D;

        Asset asset3;
        asset3.id = 3;
        asset3.name = "player_model.fbx";
        asset3.extension = "fbx";
        asset3.path = "/assets/models/player_model.fbx";
        asset3.relative_path = "models/player_model.fbx";
        asset3.size = 5120;
        asset3.last_modified = std::chrono::system_clock::now();
        asset3.type = AssetType::_3D;

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
        Asset asset;
        asset.id = 1;
        asset.name = "awesome_background.png";
        asset.extension = "png";
        asset.path = "/assets/ui/awesome_background.png";
        asset.relative_path = "ui/awesome_background.png";
        asset.size = 1024;
        asset.last_modified = std::chrono::system_clock::now();
        asset.type = AssetType::_2D;

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
        Asset asset;
        asset.id = 1;
        asset.name = "blaster_A.fbx";
        asset.extension = "fbx";
        asset.path = "/assets/weapons/blaster_A.fbx";
        asset.relative_path = "weapons/blaster_A.fbx";
        asset.size = 1024;
        asset.last_modified = std::chrono::system_clock::now();
        asset.type = AssetType::_3D;

        index.add_asset(asset.id, asset);

        // Searching for "blaster_A" should split into ["blaster", "a"] (a is ignored as <=2)
        // So it should match on "blaster" alone
        auto results = index.search_prefix("blaster");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
    }

    SECTION("Query splitting with slashes") {
        Asset asset;
        asset.id = 1;
        asset.name = "blaster.fbx";
        asset.extension = "fbx";
        asset.path = "/assets/models/blaster.fbx";
        asset.relative_path = "models/blaster.fbx";
        asset.size = 1024;
        asset.last_modified = std::chrono::system_clock::now();
        asset.type = AssetType::_3D;

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
        Asset asset1;
        asset1.id = 1;
        asset1.name = "test1.png";
        asset1.extension = "png";
        asset1.path = "/assets/test1.png";
        asset1.relative_path = "test1.png";
        asset1.size = 1024;
        asset1.last_modified = std::chrono::system_clock::now();
        asset1.type = AssetType::_2D;
        index.add_asset(asset1.id, asset1);

        Asset asset2;
        asset2.id = 2;
        asset2.name = "test2.jpg";
        asset2.extension = "jpg";
        asset2.path = "/assets/images/test2.jpg";
        asset2.relative_path = "images/test2.jpg";
        asset2.size = 2048;
        asset2.last_modified = std::chrono::system_clock::now();
        asset2.type = AssetType::_2D;
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
                auto extra = create_test_asset(
                    "file_" + std::to_string(i),
                    ".png",
                    AssetType::_2D,
                    "file_" + std::to_string(i) + ".png",
                    "",
                    next_id++);
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
                auto extra = create_test_asset(
                    "file_reset_" + std::to_string(i),
                    ".png",
                    AssetType::_2D,
                    "file_reset_" + std::to_string(i) + ".png",
                    "",
                    next_id++);
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
