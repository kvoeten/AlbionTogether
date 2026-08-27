#include "FolderPicker.h"

#include <ShObjIdl.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace fable::launcher::platform
{
    bool PickFolder(
        HWND owner,
        const std::filesystem::path& initialFolder,
        std::filesystem::path& selectedFolder)
    {
        selectedFolder.clear();
        const HRESULT initialized = CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const bool uninitialize = SUCCEEDED(initialized);

        IFileDialog* dialog = nullptr;
        HRESULT result = CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (FAILED(result) || dialog == nullptr)
        {
            if (uninitialize) CoUninitialize();
            return false;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"Select the Fable Anniversary folder");

        IShellItem* initialItem = nullptr;
        if (!initialFolder.empty() && SUCCEEDED(SHCreateItemFromParsingName(
                initialFolder.c_str(), nullptr, IID_PPV_ARGS(&initialItem))))
        {
            dialog->SetFolder(initialItem);
            initialItem->Release();
        }

        result = dialog->Show(owner);
        if (SUCCEEDED(result))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
                    path != nullptr)
                {
                    selectedFolder = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
        if (uninitialize) CoUninitialize();
        return !selectedFolder.empty();
    }
}
