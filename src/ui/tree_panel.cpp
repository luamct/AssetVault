#include "ui.h"
#include "theme.h"
#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

  std::string get_display_name_for_path(const fs::path& path) {
    std::string name = path.filename().u8string();
    if (name.empty()) {
      name = path.u8string();
    }
    return name;
  }

  std::string path_key(const fs::path& path) {
    return path.u8string();
  }

  bool get_checkbox_state(UIState& ui_state, const std::string& path_id) {
    auto it = ui_state.folder_checkbox_states.find(path_id);
    if (it == ui_state.folder_checkbox_states.end()) {
      ui_state.folder_checkbox_states[path_id] = true;
      return true;
    }
    return it->second;
  }

  const std::vector<std::string>& ensure_children_loaded_impl(UIState& ui_state, const fs::path& dir_path) {
    std::string parent_id = path_key(dir_path);
    auto cache_it = ui_state.folder_children_cache.find(parent_id);
    if (cache_it != ui_state.folder_children_cache.end()) {
      return cache_it->second;
    }

    std::vector<std::string> children;
    std::error_code ec;
    for (fs::directory_iterator it(dir_path, ec); it != fs::directory_iterator(); it.increment(ec)) {
      if (ec) {
        break;
      }
      if (it->is_directory(ec)) {
        children.push_back(it->path().u8string());
      }
    }

    std::sort(children.begin(), children.end(), [](const std::string& a, const std::string& b) {
      return fs::path(a).filename().u8string() < fs::path(b).filename().u8string();
    });

    auto [inserted_it, inserted] = ui_state.folder_children_cache.emplace(parent_id, std::move(children));
    bool parent_checked = get_checkbox_state(ui_state, parent_id);
    for (const std::string& child_id : inserted_it->second) {
      ui_state.folder_checkbox_states.emplace(child_id, parent_checked);
    }
    return inserted_it->second;
  }

  void set_folder_subtree_checked(UIState& ui_state, const fs::path& dir_path, bool checked) {
    std::string path_id = path_key(dir_path);
    ui_state.folder_checkbox_states[path_id] = checked;

    auto cache_it = ui_state.folder_children_cache.find(path_id);
    if (cache_it == ui_state.folder_children_cache.end()) {
      return;
    }

    for (const std::string& child_id : cache_it->second) {
      set_folder_subtree_checked(ui_state, fs::path(child_id), checked);
    }
  }

  bool collect_folder_filters_impl(UIState& ui_state, const fs::path& dir_path,
    const fs::path& root_path, std::vector<std::string>& filters) {
    std::string path_id = path_key(dir_path);
    bool checked = get_checkbox_state(ui_state, path_id);

    auto cache_it = ui_state.folder_children_cache.find(path_id);
    bool has_cached_children = cache_it != ui_state.folder_children_cache.end();
    bool has_children = has_cached_children && !cache_it->second.empty();

    if (!has_cached_children) {
      // If the folder was never expanded we treat it as a leaf from the cache perspective.
      has_children = false;
    }

    if (!has_children) {
      if (checked) {
        std::string relative = dir_path.lexically_relative(root_path).generic_u8string();
        if (!relative.empty()) {
          filters.push_back(relative);
        }
      }
      return checked;
    }

    std::vector<std::string> child_filters;
    bool all_children_selected = true;
    for (const std::string& child_id : cache_it->second) {
      bool child_full = collect_folder_filters_impl(ui_state, fs::path(child_id), root_path, child_filters);
      if (!child_full) {
        all_children_selected = false;
      }
    }

    if (checked && all_children_selected) {
      std::string relative = dir_path.lexically_relative(root_path).generic_u8string();
      if (!relative.empty()) {
        filters.push_back(relative);
      }
      return true;
    }

    filters.insert(filters.end(), child_filters.begin(), child_filters.end());
    return false;
  }

  void render_folder_tree_node(UIState& ui_state, const fs::path& dir_path) {
    std::string path_id = path_key(dir_path);
    bool stored_checked = get_checkbox_state(ui_state, path_id);

    auto cache_it = ui_state.folder_children_cache.find(path_id);
    bool has_known_children = cache_it != ui_state.folder_children_cache.end();
    bool has_children = has_known_children ? !cache_it->second.empty() : true;

    std::string display_name = get_display_name_for_path(dir_path);
    std::string checkbox_id = "##FolderCheck" + path_id;

    ImGui::PushID(checkbox_id.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::FRAME_LIGHT_BLUE_5);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::FRAME_LIGHT_BLUE_6);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::ACCENT_BLUE_1_ALPHA_80);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::ToImU32(Theme::ACCENT_BLUE_1));
    bool checkbox_value = stored_checked;
    if (ImGui::Checkbox("", &checkbox_value)) {
      set_folder_subtree_checked(ui_state, dir_path, checkbox_value);
      stored_checked = checkbox_value;
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopID();
    ImGui::SameLine(0.0f, 4.0f);

    std::string tree_label = display_name + "##FolderNode" + path_id;
    ImGuiTreeNodeFlags node_flags = 0;
    if (has_known_children && !has_children) {
      node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
    bool open = ImGui::TreeNodeEx(tree_label.c_str(), node_flags);
    ImGui::PopStyleVar();

    bool should_tree_pop = open && (node_flags & ImGuiTreeNodeFlags_NoTreePushOnOpen) == 0;

    if (open) {
      const auto& children = folder_tree_utils::ensure_children_loaded(ui_state, dir_path);
      for (const std::string& child_id : children) {
        render_folder_tree_node(ui_state, fs::path(child_id));
      }
    }

    if (should_tree_pop) {
      ImGui::TreePop();
    }
  }
}

void render_folder_tree_panel(UIState& ui_state, float panel_width, float panel_height) {
  ImGuiWindowFlags folder_flags = ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::FRAME_LIGHT_BLUE_3);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::FRAME_LIGHT_BLUE_4);
  ImGui::BeginChild("FolderTreeRegion", ImVec2(panel_width, panel_height), true, folder_flags);

  ImGuiIO& tree_io = ImGui::GetIO();
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && tree_io.MouseWheel != 0.0f) {
    float current_scroll = ImGui::GetScrollY();
    ImGui::SetScrollY(current_scroll - tree_io.MouseWheel * 10.0f);
  }

  if (ui_state.assets_directory.empty()) {
    ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "Set an assets directory to view folders.");
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    return;
  }

  fs::path root_path(ui_state.assets_directory);
  std::error_code ec;
  if (!fs::exists(root_path, ec) || !fs::is_directory(root_path, ec)) {
    ImGui::TextColored(Theme::TEXT_WARNING, "Assets path unavailable: %s", ui_state.assets_directory.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    return;
  }

  bool root_checked = get_checkbox_state(ui_state, root_path.u8string());
  ImGui::PushID("RootFolderCheckbox");
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::FRAME_LIGHT_BLUE_5);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::FRAME_LIGHT_BLUE_6);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::ACCENT_BLUE_1_ALPHA_80);
  ImGui::PushStyleColor(ImGuiCol_Border, Theme::ToImU32(Theme::ACCENT_BLUE_1));
  bool root_checkbox_value = root_checked;
  if (ImGui::Checkbox("", &root_checkbox_value)) {
    set_folder_subtree_checked(ui_state, root_path, root_checkbox_value);
  }
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar();
  ImGui::PopID();
  ImGui::SameLine(0.0f, 4.0f);
  ImGui::TextUnformatted(" ");
  ImGui::SameLine(0.0f, 4.0f);
  ImGui::TextColored(Theme::TEXT_LABEL, "%s", ui_state.assets_directory.c_str());
  ImGui::Separator();

  const auto& root_subdirectories = folder_tree_utils::ensure_children_loaded(ui_state, root_path);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
  for (const auto& child : root_subdirectories) {
    render_folder_tree_node(ui_state, fs::path(child));
  }
  ImGui::PopStyleVar();

  std::vector<std::string> new_folder_filters;
  bool all_selected = true;
  for (const auto& child : root_subdirectories) {
    bool full = collect_folder_filters_impl(ui_state, fs::path(child), root_path, new_folder_filters);
    if (!full) {
      all_selected = false;
    }
  }

  if (!all_selected) {
    std::sort(new_folder_filters.begin(), new_folder_filters.end());
    new_folder_filters.erase(std::unique(new_folder_filters.begin(), new_folder_filters.end()), new_folder_filters.end());
  }
  else {
    new_folder_filters.clear();
  }

  bool filters_changed = (new_folder_filters != ui_state.path_filters) ||
    (ui_state.path_filter_active != !new_folder_filters.empty());

  if (filters_changed) {
    ui_state.path_filters = new_folder_filters;
    ui_state.path_filter_active = !new_folder_filters.empty();
    ui_state.update_needed = true;
  }

  ImGui::EndChild();
  ImGui::PopStyleColor(2);
}

namespace folder_tree_utils {
  const std::vector<std::string>& ensure_children_loaded(UIState& ui_state,
    const std::filesystem::path& dir_path) {
    return ::ensure_children_loaded_impl(ui_state, dir_path);
  }

  bool collect_folder_filters(UIState& ui_state, const std::filesystem::path& dir_path,
    const std::filesystem::path& root_path, std::vector<std::string>& filters) {
    return ::collect_folder_filters_impl(ui_state, dir_path, root_path, filters);
  }
}
