#include "ui/ui.h"
#include "theme.h"
#include "imgui.h"
#include "texture_manager.h"
#include "ui/components.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

  constexpr float TREE_FRAME_MARGIN = 16.0f;

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

  std::string relative_path_from_root(const fs::path& current, const fs::path& root_path) {
    if (current == root_path) {
      return std::string();
    }

    std::string relative = current.lexically_relative(root_path).generic_u8string();
    if (relative == ".") {
      return std::string();
    }
    return relative;
  }

  bool gather_folder_filters(UIState& ui_state, const fs::path& root_path,
      const fs::path& current, std::vector<std::string>& collected, bool& any_selected) {
    std::string path_id = path_key(current);
    bool checked = get_checkbox_state(ui_state, path_id);

    auto cache_it = ui_state.folder_children_cache.find(path_id);
    bool has_children = cache_it != ui_state.folder_children_cache.end() &&
      !cache_it->second.empty();

    if (!has_children) {
      if (checked) {
        any_selected = true;
        collected.push_back(relative_path_from_root(current, root_path));
        return true;
      }
      return false;
    }

    std::vector<std::string> child_filters;
    bool all_children_selected = true;
    for (const std::string& child_id : cache_it->second) {
      bool child_full = gather_folder_filters(ui_state, root_path, fs::path(child_id),
        child_filters, any_selected);
      if (!child_full) {
        all_children_selected = false;
      }
    }

    if (checked) {
      any_selected = true;
    }

    if (checked && all_children_selected) {
      collected.push_back(relative_path_from_root(current, root_path));
      return true;
    }

    collected.insert(collected.end(), child_filters.begin(), child_filters.end());
    return false;
  }

  // Marks the supplied folder and every cached descendant as checked/unchecked
  // without touching ancestors; does not load children lazily.
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

  // Walks up the tree from dir_path toward root_path ensuring every ancestor
  // remains checked so filters stay minimized correctly.
  void ensure_parents_checked(UIState& ui_state, const fs::path& dir_path,
      const fs::path& root_path) {
    if (dir_path == root_path) {
      return;
    }

    fs::path current = dir_path;
    while (true) {
      current = current.parent_path();
      if (current.empty()) {
        break;
      }

      std::string parent_id = path_key(current);
      ui_state.folder_checkbox_states[parent_id] = true;

      if (current == root_path) {
        break;
      }
    }
  }

  // Checks the requested folder, optionally cascades to already-loaded
  // children, and keeps all ancestors checked. This is the main entry point
  // for user actions that should affect parents and visible descendants.
  void set_folder_checked_with_relatives(UIState& ui_state, const fs::path& dir_path,
      const fs::path& root_path, bool include_loaded_children) {
    if (include_loaded_children) {
      set_folder_subtree_checked(ui_state, dir_path, true);
    } else {
      std::string path_id = path_key(dir_path);
      ui_state.folder_checkbox_states[path_id] = true;
    }

    ensure_parents_checked(ui_state, dir_path, root_path);
  }

  // Convenience helper used by exclusive selection flows to reset all
  // checkboxes without discarding cached nodes.
  void clear_all_folder_checks(UIState& ui_state) {
    for (auto& entry : ui_state.folder_checkbox_states) {
      entry.second = false;
    }
  }

  // Clears every checkbox, collapses the tree, and walks down the requested
  // relative path checking ancestors (and optionally the final subtree).
  void select_path_exclusive(UIState& ui_state, const fs::path& root_path,
      fs::path target_relative, bool include_target_subtree) {
    clear_all_folder_checks(ui_state);
    ui_state.tree_nodes_to_open.clear();
    ui_state.collapse_tree_requested = true;

    if (target_relative == ".") {
      target_relative.clear();
    }

    if (target_relative.empty()) {
      set_folder_checked_with_relatives(ui_state, root_path, root_path,
        include_target_subtree);
      return;
    }

    fs::path current = root_path;
    set_folder_checked_with_relatives(ui_state, root_path, root_path, false);

    for (auto it = target_relative.begin(); it != target_relative.end(); ++it) {
      folder_tree_utils::ensure_children_loaded(ui_state, current, false);
      current /= *it;
      bool is_last = std::next(it) == target_relative.end();
      bool include_children = include_target_subtree && is_last;
      set_folder_checked_with_relatives(ui_state, current, root_path,
        include_children);
      if (!is_last) {
        ui_state.tree_nodes_to_open.insert(path_key(current));
      }
    }
  }

  // Applies preview-panel selections that should mirror the right-click
  // exclusive behavior once the tree has populated its children.
  void apply_pending_tree_selection(UIState& ui_state, const fs::path& root_path) {
    if (!ui_state.pending_tree_selection.has_value()) {
      return;
    }

    std::string pending = *ui_state.pending_tree_selection;
    ui_state.pending_tree_selection.reset();

    fs::path target_relative = fs::path(pending);
    select_path_exclusive(ui_state, root_path, target_relative, true);
  }

  // Renders a single folder entry, handles left/right-click interactions, and
  // recurses into lazily loaded children.
  void render_folder_tree_node(UIState& ui_state, const fs::path& dir_path,
      const fs::path& root_path) {
    std::string path_id = path_key(dir_path);
    bool stored_checked = get_checkbox_state(ui_state, path_id);

    auto cache_it = ui_state.folder_children_cache.find(path_id);
    bool has_known_children = cache_it != ui_state.folder_children_cache.end();
    bool has_children = has_known_children ? !cache_it->second.empty() : true;

    std::string display_name = get_display_name_for_path(dir_path);
    std::string checkbox_id = "##FolderCheck" + path_id;

    ImGui::PushID(checkbox_id.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::COLOR_TRANSPARENT);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::COLOR_TRANSPARENT);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::COLOR_TRANSPARENT);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::ToImU32(Theme::BORDER_LIGHT_BLUE_1));
    bool checkbox_value = stored_checked;
    bool checkbox_clicked = ImGui::Checkbox("", &checkbox_value);
    bool right_click_exclusive = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (right_click_exclusive) {
      fs::path relative = dir_path == root_path ? fs::path() :
        dir_path.lexically_relative(root_path);
      select_path_exclusive(ui_state, root_path, relative, true);
      checkbox_value = true;
      stored_checked = true;
    } else if (checkbox_clicked) {
      if (checkbox_value) {
        set_folder_checked_with_relatives(ui_state, dir_path, root_path, true);
      } else {
        set_folder_subtree_checked(ui_state, dir_path, false);
      }
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
    bool path_should_open = false;
    auto open_it = ui_state.tree_nodes_to_open.find(path_id);
    if (open_it != ui_state.tree_nodes_to_open.end()) {
      ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      ui_state.tree_nodes_to_open.erase(open_it);
      path_should_open = true;
    }
    if (ui_state.collapse_tree_requested && !path_should_open) {
      ImGui::SetNextItemOpen(false, ImGuiCond_Always);
    }
    bool open = ImGui::TreeNodeEx(tree_label.c_str(), node_flags);
    ImGui::PopStyleVar();

    bool should_tree_pop = open && (node_flags & ImGuiTreeNodeFlags_NoTreePushOnOpen) == 0;

    if (open) {
      const auto& children = folder_tree_utils::ensure_children_loaded(ui_state, dir_path);
      for (const std::string& child_id : children) {
        render_folder_tree_node(ui_state, fs::path(child_id), root_path);
      }
    }

    if (should_tree_pop) {
      ImGui::TreePop();
    }
  }
}

void render_folder_tree_panel(UIState& ui_state, TextureManager& texture_manager,
    float panel_width, float panel_height) {
  SpriteAtlas tree_frame_atlas = texture_manager.get_ui_elements_atlas();
  const NineSliceDefinition tree_frame_definition = make_16px_frame(1, 3.0f);

  ImVec2 frame_pos = ImGui::GetCursorScreenPos();
  if (tree_frame_atlas.texture_id != 0) {
    draw_nine_slice_image(tree_frame_atlas, tree_frame_definition, frame_pos,
      ImVec2(panel_width, panel_height));
  }

  ImVec2 content_pos(
    frame_pos.x + TREE_FRAME_MARGIN,
    frame_pos.y + TREE_FRAME_MARGIN);
  ImVec2 content_size(
    std::max(0.0f, panel_width - TREE_FRAME_MARGIN * 2.0f),
    std::max(0.0f, panel_height - TREE_FRAME_MARGIN * 2.0f));

  ImGuiWindowFlags scroll_flags = ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGuiWindowFlags container_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::COLOR_TRANSPARENT);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::FRAME_LIGHT_BLUE_3);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::FRAME_LIGHT_BLUE_4);
  ImGui::SetCursorScreenPos(content_pos);
  ImGui::BeginChild("FolderTreeRegion", content_size, false, container_flags);

  if (ui_state.assets_directory.empty()) {
    ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "Set an assets directory to view folders.");
    ImGui::EndChild();
    ImGui::PopStyleColor(3);
    return;
  }

  fs::path root_path(ui_state.assets_directory);
  std::error_code ec;
  if (!fs::exists(root_path, ec) || !fs::is_directory(root_path, ec)) {
    ImGui::TextColored(Theme::TEXT_WARNING, "Assets path unavailable: %s", ui_state.assets_directory.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor(3);
    return;
  }

  bool root_checked = get_checkbox_state(ui_state, root_path.u8string());
  ImGui::PushID("RootFolderCheckbox");
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::COLOR_TRANSPARENT);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::COLOR_TRANSPARENT);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::COLOR_TRANSPARENT);
  ImGui::PushStyleColor(ImGuiCol_Border, Theme::ToImU32(Theme::BORDER_LIGHT_BLUE_1));
  bool root_checkbox_value = root_checked;
  bool root_clicked = ImGui::Checkbox("", &root_checkbox_value);
  bool root_right_click = ImGui::IsItemClicked(ImGuiMouseButton_Right);
  if (root_right_click) {
    select_path_exclusive(ui_state, root_path, fs::path(), true);
    root_checkbox_value = true;
  } else if (root_clicked) {
    if (root_checkbox_value) {
      set_folder_checked_with_relatives(ui_state, root_path, root_path, true);
    } else {
      set_folder_subtree_checked(ui_state, root_path, false);
    }
  }
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar();
  ImGui::PopID();
  ImGui::SameLine(0.0f, 4.0f);
  float content_width = content_size.x;
  float wrap_width = std::max(0.0f, content_width - ImGui::GetCursorPos().x - 16.0f);
  ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap_width);
  ImGui::TextColored(Theme::TEXT_LABEL, "%s", ui_state.assets_directory.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Separator();

  ScrollbarStyle scrollbar_style;
  scrollbar_style.pixel_scale = 2.0f;
  ScrollbarState scrollbar_state = begin_scrollbar_child(
    "FolderTreeScrollRegion",
    ImVec2(0, 0),
    scrollbar_style,
    scroll_flags);

  ImGuiIO& tree_io = ImGui::GetIO();
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && tree_io.MouseWheel != 0.0f) {
    float current_scroll = ImGui::GetScrollY();
    ImGui::SetScrollY(current_scroll - tree_io.MouseWheel * 10.0f);
  }

  const auto& root_subdirectories = folder_tree_utils::ensure_children_loaded(ui_state, root_path);
  apply_pending_tree_selection(ui_state, root_path);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
  for (const auto& child : root_subdirectories) {
    render_folder_tree_node(ui_state, fs::path(child), root_path);
  }
  ImGui::PopStyleVar();
  ui_state.collapse_tree_requested = false;

  folder_tree_utils::FilterComputationResult filter_result =
    folder_tree_utils::collect_active_filters(ui_state, root_path);
  std::vector<std::string> new_folder_filters = filter_result.filters;
  bool new_path_filter_active = !filter_result.all_selected;
  bool new_selection_empty = !filter_result.any_selected;

  bool filters_changed = (new_folder_filters != ui_state.path_filters) ||
    (ui_state.path_filter_active != new_path_filter_active) ||
    (ui_state.folder_selection_covers_all != filter_result.all_selected) ||
    (ui_state.folder_selection_empty != new_selection_empty);

  if (filters_changed) {
    ui_state.path_filters = std::move(new_folder_filters);
    ui_state.path_filter_active = new_path_filter_active;
    ui_state.folder_selection_covers_all = filter_result.all_selected;
    ui_state.folder_selection_empty = new_selection_empty;
    ui_state.filters_changed = true;
  }

  ui_state.tree_nodes_to_open.clear();

  end_scrollbar_child(scrollbar_state);

  SpriteAtlas scrollbar_atlas = texture_manager.get_ui_elements_atlas();
  if (scrollbar_atlas.texture_id != 0) {
    ThreeSliceDefinition track_def = make_scrollbar_track_definition(0, scrollbar_style.pixel_scale);
    ThreeSliceDefinition thumb_def = make_scrollbar_thumb_definition(scrollbar_style.pixel_scale);
    draw_scrollbar_overlay(scrollbar_state, scrollbar_atlas, track_def, thumb_def);
  }

  ImGui::EndChild();
  ImGui::PopStyleColor(3);
  ImGui::SetCursorScreenPos(ImVec2(frame_pos.x, frame_pos.y + panel_height));
}

namespace folder_tree_utils {
  const std::vector<std::string>& ensure_children_loaded(UIState& ui_state,
    const std::filesystem::path& dir_path, bool inherit_parent_state) {
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
    if (inserted) {
      bool child_state = inherit_parent_state ? get_checkbox_state(ui_state, parent_id) : false;
      for (const std::string& child_id : inserted_it->second) {
        ui_state.folder_checkbox_states.emplace(child_id, child_state);
      }
    }
    return inserted_it->second;
  }
  FilterComputationResult collect_active_filters(UIState& ui_state,
    const std::filesystem::path& root_path) {
    folder_tree_utils::ensure_children_loaded(ui_state, root_path);

    std::vector<std::string> filters;
    bool any_selected = false;
    bool root_full = gather_folder_filters(ui_state, root_path, root_path, filters, any_selected);

    std::sort(filters.begin(), filters.end());
    filters.erase(std::unique(filters.begin(), filters.end()), filters.end());

    if (root_full) {
      filters.clear();
    }

    FilterComputationResult result;
    result.filters = std::move(filters);
    result.all_selected = root_full;
    result.any_selected = any_selected;
    return result;
  }
}
