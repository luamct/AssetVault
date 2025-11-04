#include "builder/embedded_assets.h"

namespace embedded_assets {

const EmbeddedAsset* begin() {
  return detail::kAssets;
}

const EmbeddedAsset* end() {
  return detail::kAssets + detail::kAssetCount;
}

std::size_t count() {
  return detail::kAssetCount;
}

const EmbeddedAsset* find_asset(std::string_view path) {
  for (std::size_t index = 0; index < detail::kAssetCount; ++index) {
    const EmbeddedAsset& asset = detail::kAssets[index];
    if (asset.path == path) {
      return &asset;
    }
  }
  return nullptr;
}

bool contains(std::string_view path) {
  return find_asset(path) != nullptr;
}

std::optional<AssetView> get(std::string_view path) {
  const EmbeddedAsset* asset = find_asset(path);
  if (asset == nullptr) {
    return std::nullopt;
  }
  return AssetView{asset->data, asset->size};
}

}  // namespace embedded_assets
