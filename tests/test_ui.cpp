#include <catch2/catch_all.hpp>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "ui.h"

namespace fs = std::filesystem;

namespace {

void ensure_node(UIState& state, const fs::path& path, bool checked = true) {
  std::string key = path.u8string();
  state.folder_checkbox_states.emplace(key, checked);
}

void set_children(UIState& state, const fs::path& parent, const std::vector<fs::path>& children) {
  std::vector<std::string> encoded;
  encoded.reserve(children.size());
  for (const auto& child : children) {
    encoded.push_back(child.u8string());
    ensure_node(state, child);
  }
  state.folder_children_cache[parent.u8string()] = std::move(encoded);
}

std::vector<std::string> gather_filters(UIState& state, const fs::path& root) {
  std::vector<std::string> filters;
  bool all_selected = true;
  const auto cache_it = state.folder_children_cache.find(root.u8string());
  REQUIRE(cache_it != state.folder_children_cache.end());
  for (const auto& child : cache_it->second) {
    bool full = folder_tree_utils::collect_folder_filters(state, fs::path(child), root, filters);
    if (!full) {
      all_selected = false;
    }
  }
  if (all_selected) {
    filters.clear();
  } else {
    std::sort(filters.begin(), filters.end());
    filters.erase(std::unique(filters.begin(), filters.end()), filters.end());
  }
  return filters;
}

struct StubTree {
  fs::path root;

  StubTree() : root("/assets") {
    ensure_node(state, root);
  }

  UIState state;

  void build(const std::vector<std::pair<fs::path, std::vector<fs::path>>>& spec) {
    for (const auto& entry : spec) {
      ensure_node(state, entry.first);
      set_children(state, entry.first, entry.second);
    }
  }
};
}  // namespace

TEST_CASE("collect_folder_filters produces no filters when everything selected") {
  StubTree tree;
  auto textures = tree.root / "Textures";
  auto meshes = tree.root / "Meshes";
  tree.build({
    { tree.root, { textures, meshes } },
    { textures, { textures / "SubA" } }
  });

  auto filters = gather_filters(tree.state, tree.root);
  REQUIRE(filters.empty());
}

TEST_CASE("collect_folder_filters returns minimal relative paths") {
  StubTree tree;
  auto textures = tree.root / "Textures";
  auto meshes = tree.root / "Meshes";
  auto textures_sub1 = textures / "Sub1";
  auto textures_sub2 = textures / "Sub2";
  tree.build({
    { tree.root, { textures, meshes } },
    { textures, { textures_sub1, textures_sub2 } }
  });

  tree.state.folder_checkbox_states[meshes.u8string()] = false;
  tree.state.folder_checkbox_states[textures_sub1.u8string()] = false;

  auto filters = gather_filters(tree.state, tree.root);
  std::vector<std::string> expected = { "Textures/Sub2" };
  REQUIRE(filters == expected);
}

TEST_CASE("collect_folder_filters reports leaf selections") {
  StubTree tree;
  auto textures = tree.root / "Textures";
  auto leaf = textures / "Sub1" / "Leaf";
  auto meshes = tree.root / "Meshes";
  tree.build({
    { tree.root, { textures, meshes } },
    { textures, { textures / "Sub1" } },
    { textures / "Sub1", { leaf } }
  });

  tree.state.folder_checkbox_states[textures.u8string()] = false;
  tree.state.folder_checkbox_states[meshes.u8string()] = false;
  tree.state.folder_checkbox_states[(textures / "Sub1").u8string()] = false;
  tree.state.folder_checkbox_states[leaf.u8string()] = true;

  auto filters = gather_filters(tree.state, tree.root);
  std::vector<std::string> expected = { "Textures/Sub1/Leaf" };
  REQUIRE(filters == expected);
}
