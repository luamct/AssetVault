#include "drag_drop.h"
#include "logger.h"

#include <windows.h>
#include <shlobj.h>
#include <ole2.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <imgui.h>
#include <vector>
#include <string>

// Helper function to convert UTF-8 string to wide string (UTF-16)
static std::wstring utf8_to_wstring(const std::string& utf8_str) {
    if (utf8_str.empty()) return std::wstring();

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), (int)utf8_str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), (int)utf8_str.size(), &wstr[0], size_needed);
    return wstr;
}

// Simple IDataObject implementation for file drag-and-drop
class FileDataObject : public IDataObject {
private:
    LONG ref_count_;
    FORMATETC format_;
    STGMEDIUM medium_;

public:
    FileDataObject(FORMATETC* fmt, STGMEDIUM* med) : ref_count_(1) {
        format_ = *fmt;
        medium_ = *med;
    }

    // IUnknown methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IDataObject methods
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetc, STGMEDIUM* pmedium) override {
        if (pformatetc->cfFormat == format_.cfFormat &&
            (pformatetc->tymed & format_.tymed)) {
            pmedium->tymed = format_.tymed;
            pmedium->hGlobal = medium_.hGlobal;
            pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) override {
        if (pformatetc->cfFormat == format_.cfFormat) {
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pformatetcOut) override {
        pformatetcOut->ptd = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override {
        return E_NOTIMPL;
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
};

// IDropSource implementation - provides feedback during drag operation
class DropSource : public IDropSource {
private:
    LONG ref_count_;

public:
    DropSource() : ref_count_(1) {}

    // IUnknown methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IDropSource methods
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) {
            return DRAGDROP_S_CANCEL;
        }

        // Continue drag if left mouse button is pressed
        if (!(grfKeyState & MK_LBUTTON)) {
            return DRAGDROP_S_DROP;
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) override {
        // Use default cursor feedback
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// Windows implementation of DragDropManager
class WindowsDragDropManager : public DragDropManager {
private:
    HWND hwnd_;
    bool initialized_;
    bool com_initialized_;

public:
    WindowsDragDropManager() : hwnd_(nullptr), initialized_(false), com_initialized_(false) {}

    ~WindowsDragDropManager() override {
        if (com_initialized_) {
            CoUninitialize();
        }
    }

    bool initialize(GLFWwindow* window) override {
        if (!window) {
            LOG_ERROR("[DragDrop] Invalid GLFW window");
            return false;
        }

        // Initialize COM library
        // Use CoInitializeEx with COINIT_APARTMENTTHREADED for better compatibility
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
            LOG_ERROR("[DragDrop] Failed to initialize COM: 0x{:08X}", hr);
            return false;
        }
        // Only mark as initialized if we actually initialized it (not if it was already initialized)
        com_initialized_ = (hr == S_OK || hr == S_FALSE);

        // Get native Windows window from GLFW
        hwnd_ = glfwGetWin32Window(window);
        if (!hwnd_) {
            LOG_ERROR("[DragDrop] Failed to get HWND from GLFW window");
            return false;
        }

        initialized_ = true;
        LOG_INFO("[DragDrop] Windows drag-and-drop initialized successfully");
        return true;
    }

    bool begin_file_drag(const std::vector<std::string>& file_paths, const ImVec2& drag_origin) override {
        if (!initialized_) {
            LOG_WARN("[DragDrop] DragDropManager not initialized");
            return false;
        }

        if (file_paths.empty()) {
            LOG_WARN("[DragDrop] No files to drag");
            return false;
        }

        // Calculate total size needed for DROPFILES structure
        // DROPFILES header + (path + null terminator) for each file + final null terminator
        size_t total_size = sizeof(DROPFILES);
        for (const auto& path : file_paths) {
            std::wstring wpath = utf8_to_wstring(path);
            total_size += (wpath.length() + 1) * sizeof(wchar_t);
        }
        total_size += sizeof(wchar_t); // Final null terminator

        // Allocate global memory for file list
        HGLOBAL hglobal = GlobalAlloc(GHND, total_size);
        if (!hglobal) {
            LOG_ERROR("[DragDrop] Failed to allocate global memory");
            return false;
        }

        // Lock memory and fill in DROPFILES structure
        DROPFILES* drop_files = (DROPFILES*)GlobalLock(hglobal);
        if (!drop_files) {
            GlobalFree(hglobal);
            LOG_ERROR("[DragDrop] Failed to lock global memory");
            return false;
        }

        // Initialize DROPFILES structure
        drop_files->pFiles = sizeof(DROPFILES);
        drop_files->pt.x = (LONG)drag_origin.x;
        drop_files->pt.y = (LONG)drag_origin.y;
        drop_files->fNC = FALSE;
        drop_files->fWide = TRUE; // Use Unicode (UTF-16)

        // Copy file paths after DROPFILES structure
        wchar_t* file_list = (wchar_t*)((BYTE*)drop_files + sizeof(DROPFILES));
        for (const auto& path : file_paths) {
            std::wstring wpath = utf8_to_wstring(path);
            wcscpy_s(file_list, wpath.length() + 1, wpath.c_str());
            file_list += wpath.length() + 1;
        }
        *file_list = L'\0'; // Double null terminator

        GlobalUnlock(hglobal);

        // Set up format and medium for CF_HDROP
        FORMATETC format = {0};
        format.cfFormat = CF_HDROP;
        format.ptd = NULL;
        format.dwAspect = DVASPECT_CONTENT;
        format.lindex = -1;
        format.tymed = TYMED_HGLOBAL;

        STGMEDIUM medium = {0};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = hglobal;
        medium.pUnkForRelease = NULL;

        // Create our custom data object
        IDataObject* data_object = new FileDataObject(&format, &medium);

        // Create drop source
        IDropSource* drop_source = new DropSource();

        // Perform drag-and-drop operation
        DWORD effect;
        HRESULT hr = DoDragDrop(data_object, drop_source, DROPEFFECT_COPY, &effect);

        // Cleanup
        drop_source->Release();
        data_object->Release();

        // Check result - DRAGDROP_S_CANCEL and DRAGDROP_S_DROP are both success codes
        if (hr == DRAGDROP_S_DROP) {
            LOG_DEBUG("[DragDrop] Drag completed - file(s) dropped successfully");
            return true;
        } else if (hr == DRAGDROP_S_CANCEL) {
            LOG_DEBUG("[DragDrop] Drag cancelled by user");
            return true; // Not an error, user just cancelled
        } else if (FAILED(hr)) {
            LOG_ERROR("[DragDrop] DoDragDrop failed: 0x{:X}", static_cast<unsigned int>(hr));
            return false;
        }

        LOG_DEBUG("[DragDrop] Started drag for {} file(s)", file_paths.size());
        return true;
    }

    bool is_supported() const override {
        return true; // Windows always supports drag-and-drop
    }
};

// Factory function implementation for Windows
DragDropManager* create_drag_drop_manager() {
    return new WindowsDragDropManager();
}
