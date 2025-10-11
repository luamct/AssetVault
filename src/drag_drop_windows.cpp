#include "drag_drop.h"
#include "logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlobj.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

HGLOBAL duplicate_global_handle(HGLOBAL source) {
  if (!source) {
    return nullptr;
  }

  const SIZE_T size = ::GlobalSize(source);
  if (size == 0) {
    return nullptr;
  }

  HGLOBAL destination = ::GlobalAlloc(GMEM_MOVEABLE, size);
  if (!destination) {
    return nullptr;
  }

  void* source_data = ::GlobalLock(source);
  void* destination_data = ::GlobalLock(destination);

  if (!source_data || !destination_data) {
    if (destination_data) {
      ::GlobalUnlock(destination);
    }
    if (source_data) {
      ::GlobalUnlock(source);
    }
    ::GlobalFree(destination);
    return nullptr;
  }

  std::memcpy(destination_data, source_data, size);

  ::GlobalUnlock(destination);
  ::GlobalUnlock(source);
  return destination;
}

bool format_matches(const FORMATETC& requested, const FORMATETC& available) {
  return requested.cfFormat == available.cfFormat &&
         (requested.tymed & available.tymed) != 0 &&
         requested.dwAspect == available.dwAspect &&
         requested.lindex == available.lindex;
}

class ScopedOleInitializer {
public:
  ScopedOleInitializer()
    : result_(::OleInitialize(nullptr)) {}

  ~ScopedOleInitializer() {
    if (SUCCEEDED(result_)) {
      ::OleUninitialize();
    }
  }

  HRESULT result() const { return result_; }
  bool succeeded() const { return SUCCEEDED(result_); }

private:
  HRESULT result_;
};

class SimpleDropSource final : public IDropSource {
public:
  SimpleDropSource()
    : ref_count_(1) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
    if (!ppvObject) {
      return E_POINTER;
    }

    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *ppvObject = static_cast<IDropSource*>(this);
      AddRef();
      return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(::InterlockedIncrement(&ref_count_));
  }

  ULONG STDMETHODCALLTYPE Release() override {
    ULONG ref = static_cast<ULONG>(::InterlockedDecrement(&ref_count_));
    if (ref == 0) {
      delete this;
    }
    return ref;
  }

  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape_pressed, DWORD key_state) override {
    if (escape_pressed) {
      return DRAGDROP_S_CANCEL;
    }
    if ((key_state & MK_LBUTTON) == 0) {
      return DRAGDROP_S_DROP;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }

private:
  LONG ref_count_;
};

struct StoredData {
  FORMATETC format{};
  STGMEDIUM medium{};
};

class FileDropDataObject final : public IDataObject {
public:
  explicit FileDropDataObject(const std::vector<std::wstring>& files)
    : ref_count_(1) {
    initialize_formats(files);
  }

  ~FileDropDataObject() {
    for (auto& entry : stored_data_) {
      ::ReleaseStgMedium(&entry.medium);
    }
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
    if (!ppvObject) {
      return E_POINTER;
    }

    if (riid == IID_IUnknown || riid == IID_IDataObject) {
      *ppvObject = static_cast<IDataObject*>(this);
      AddRef();
      return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(::InterlockedIncrement(&ref_count_));
  }

  ULONG STDMETHODCALLTYPE Release() override {
    ULONG ref = static_cast<ULONG>(::InterlockedDecrement(&ref_count_));
    if (ref == 0) {
      delete this;
    }
    return ref;
  }

  HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
    if (!format || !medium) {
      return E_INVALIDARG;
    }

    for (const auto& entry : stored_data_) {
      if (format_matches(*format, entry.format)) {
        if (entry.medium.tymed != TYMED_HGLOBAL) {
          return DV_E_TYMED;
        }

        HGLOBAL copy = duplicate_global_handle(entry.medium.hGlobal);
        if (!copy) {
          return STG_E_MEDIUMFULL;
        }

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = copy;
        medium->pUnkForRelease = nullptr;
        return S_OK;
      }
    }

    return DV_E_FORMATETC;
  }

  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
    return DATA_E_FORMATETC;
  }

  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
    if (!format) {
      return E_INVALIDARG;
    }

    for (const auto& entry : stored_data_) {
      if (format_matches(*format, entry.format)) {
        return S_OK;
      }
    }

    return DV_E_FORMATETC;
  }

  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* result) override {
    if (!result) {
      return E_POINTER;
    }
    result->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
  }

  HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** enum_format) override {
    if (!enum_format) {
      return E_POINTER;
    }

    if (direction != DATADIR_GET) {
      return E_NOTIMPL;
    }

    std::vector<FORMATETC> formats;
    formats.reserve(stored_data_.size());
    for (const auto& entry : stored_data_) {
      formats.push_back(entry.format);
    }

    return ::SHCreateStdEnumFmtEtc(static_cast<UINT>(formats.size()), formats.data(), enum_format);
  }

  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

private:
  static HGLOBAL create_hdrop_payload(const std::vector<std::wstring>& files) {
    SIZE_T total_chars = 0;
    for (const auto& file : files) {
      total_chars += file.length() + 1;
    }

    const SIZE_T payload_bytes = (total_chars + 1) * sizeof(wchar_t);
    const SIZE_T total_size = sizeof(DROPFILES) + payload_bytes;

    HGLOBAL handle = ::GlobalAlloc(GMEM_MOVEABLE, total_size);
    if (!handle) {
      return nullptr;
    }

    auto* drop_files = static_cast<DROPFILES*>(::GlobalLock(handle));
    if (!drop_files) {
      ::GlobalFree(handle);
      return nullptr;
    }

    drop_files->pFiles = sizeof(DROPFILES);
    drop_files->pt = {0, 0};
    drop_files->fNC = FALSE;
    drop_files->fWide = TRUE;

    auto* dest = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop_files) + sizeof(DROPFILES));
    for (const auto& file : files) {
      std::memcpy(dest, file.c_str(), (file.length() + 1) * sizeof(wchar_t));
      dest += file.length() + 1;
    }
    *dest = L'\0';

    ::GlobalUnlock(handle);
    return handle;
  }

  static HGLOBAL create_drop_effect_payload(DWORD effect) {
    HGLOBAL handle = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!handle) {
      return nullptr;
    }

    auto* value = static_cast<DWORD*>(::GlobalLock(handle));
    if (!value) {
      ::GlobalFree(handle);
      return nullptr;
    }

    *value = effect;
    ::GlobalUnlock(handle);
    return handle;
  }

  void initialize_formats(const std::vector<std::wstring>& files) {
    const HGLOBAL file_payload = create_hdrop_payload(files);
    if (!file_payload) {
      LOG_ERROR("[DragDrop] Failed to allocate HDROP payload");
      return;
    }

    StoredData file_data{};
    file_data.format.cfFormat = CF_HDROP;
    file_data.format.ptd = nullptr;
    file_data.format.dwAspect = DVASPECT_CONTENT;
    file_data.format.lindex = -1;
    file_data.format.tymed = TYMED_HGLOBAL;

    file_data.medium.tymed = TYMED_HGLOBAL;
    file_data.medium.hGlobal = file_payload;
    file_data.medium.pUnkForRelease = nullptr;
    stored_data_.push_back(file_data);

    static const CLIPFORMAT preferred_effect_format =
      static_cast<CLIPFORMAT>(::RegisterClipboardFormatW(L"Preferred DropEffect"));

    const HGLOBAL effect_payload = create_drop_effect_payload(DROPEFFECT_COPY);
    if (effect_payload) {
      StoredData effect_data{};
      effect_data.format.cfFormat = preferred_effect_format;
      effect_data.format.ptd = nullptr;
      effect_data.format.dwAspect = DVASPECT_CONTENT;
      effect_data.format.lindex = -1;
      effect_data.format.tymed = TYMED_HGLOBAL;

      effect_data.medium.tymed = TYMED_HGLOBAL;
      effect_data.medium.hGlobal = effect_payload;
      effect_data.medium.pUnkForRelease = nullptr;
      stored_data_.push_back(effect_data);
    } else {
      LOG_WARN("[DragDrop] Failed to allocate preferred drop effect payload");
    }
  }

  LONG ref_count_;
  std::vector<StoredData> stored_data_;
};

std::wstring utf8_to_wide(const std::string& input) {
  if (input.empty()) {
    return std::wstring();
  }

  const int required = ::MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
  if (required <= 0) {
    return std::wstring();
  }

  std::wstring output(static_cast<size_t>(required - 1), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, output.data(), required);
  return output;
}

class WindowsDragDropManager final : public DragDropManager {
public:
  WindowsDragDropManager()
    : hwnd_(nullptr)
    , initialized_(false) {}

  bool initialize(GLFWwindow* window) override {
    if (!window) {
      LOG_ERROR("[DragDrop] Invalid GLFW window");
      return false;
    }

    hwnd_ = glfwGetWin32Window(window);
    if (!hwnd_) {
      LOG_ERROR("[DragDrop] Failed to retrieve HWND from GLFW window");
      return false;
    }

    initialized_ = true;
    LOG_INFO("[DragDrop] Windows drag-and-drop initialized successfully");
    return true;
  }

  bool begin_file_drag(const std::vector<std::string>& file_paths, const ImVec2& /*drag_origin*/) override {
    if (!initialized_) {
      LOG_WARN("[DragDrop] DragDropManager not initialized");
      return false;
    }

    if (file_paths.empty()) {
      LOG_WARN("[DragDrop] No files to drag");
      return false;
    }

    std::vector<std::wstring> wide_paths;
    wide_paths.reserve(file_paths.size());

    for (const auto& path : file_paths) {
      std::wstring wide = utf8_to_wide(path);
      if (wide.empty()) {
        LOG_WARN("[DragDrop] Failed to convert path to wide string: {}", path);
        continue;
      }
      wide_paths.push_back(wide);
    }

    if (wide_paths.empty()) {
      LOG_ERROR("[DragDrop] No valid file paths to drag");
      return false;
    }

    ScopedOleInitializer ole;
    if (!ole.succeeded()) {
      if (ole.result() == RPC_E_CHANGED_MODE) {
        LOG_ERROR("[DragDrop] COM already initialized with incompatible threading model");
      } else {
        LOG_ERROR("[DragDrop] OleInitialize failed: HRESULT=0x{:08X}", static_cast<unsigned int>(ole.result()));
      }
      return false;
    }

    auto* data_object = new FileDropDataObject(wide_paths);
    if (!data_object) {
      LOG_ERROR("[DragDrop] Failed to allocate data object");
      return false;
    }

    if (wide_paths.size() != file_paths.size()) {
      LOG_DEBUG("[DragDrop] Dragging {} of {} requested path(s)", wide_paths.size(), file_paths.size());
    }

    auto* drop_source = new SimpleDropSource();
    if (!drop_source) {
      data_object->Release();
      LOG_ERROR("[DragDrop] Failed to allocate drop source");
      return false;
    }

    FORMATETC format_check{};
    format_check.cfFormat = CF_HDROP;
    format_check.ptd = nullptr;
    format_check.dwAspect = DVASPECT_CONTENT;
    format_check.lindex = -1;
    format_check.tymed = TYMED_HGLOBAL;

    if (data_object->QueryGetData(&format_check) != S_OK) {
      LOG_ERROR("[DragDrop] Failed to prepare drag payload");
      drop_source->Release();
      data_object->Release();
      return false;
    }

    DWORD effect = DROPEFFECT_COPY;
    HRESULT drag_result = ::DoDragDrop(data_object, drop_source, DROPEFFECT_COPY, &effect);
    drop_source->Release();
    data_object->Release();

    if (drag_result == DRAGDROP_S_DROP) {
      LOG_DEBUG("[DragDrop] Drag completed with drop");
      return true;
    }
    if (drag_result == DRAGDROP_S_CANCEL) {
      LOG_DEBUG("[DragDrop] Drag cancelled by user");
      return true;
    }

    LOG_ERROR("[DragDrop] DoDragDrop failed: HRESULT=0x{:08X}", static_cast<unsigned int>(drag_result));
    return false;
  }

  bool is_supported() const override {
    return true;
  }

private:
  HWND hwnd_;
  bool initialized_;
};

}  // namespace

DragDropManager* create_drag_drop_manager() {
  return new WindowsDragDropManager();
}
