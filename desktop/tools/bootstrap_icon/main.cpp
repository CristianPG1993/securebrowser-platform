#include <windows.h>

#include <cstdio>

// Herramienta de compilación. RC ya ha convertido el ICO en un grupo y sus imágenes nativas.
int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::fputs("Usage: sbp_bootstrap_icon <staged-bootstrap>\n", stderr);
        return 1;
    }
    HANDLE update = BeginUpdateResourceW(argv[1], FALSE);
    if (!update) {
        std::fprintf(stderr, "Could not open the bootstrap copy (Win32 %lu).\n", GetLastError());
        return 1;
    }

    // EnumResourceNames llama de forma síncrona para cada imagen y, al final, para su grupo.
    const auto copy_icon = [](HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR context) -> BOOL {
        constexpr WORD language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        const HRSRC entry = FindResourceExW(module, type, name, language);
        if (!entry) {
            return FALSE;
        }
        const DWORD size = SizeofResource(module, entry);
        const HGLOBAL resource = LoadResource(module, entry);
        void* bytes = resource ? LockResource(resource) : nullptr;
        return bytes && size && UpdateResourceW(reinterpret_cast<HANDLE>(context), type, name, language, bytes, size);
    };

    const HMODULE module = GetModuleHandleW(nullptr);
    const LONG_PTR context = reinterpret_cast<LONG_PTR>(update);
    const bool copied = EnumResourceNamesW(module, RT_ICON, copy_icon, context)
                     && EnumResourceNamesW(module, RT_GROUP_ICON, copy_icon, context);
    if (!copied) {
        std::fprintf(stderr, "Could not copy the Windows icon resources (Win32 %lu).\n", GetLastError());
    }
    if (!EndUpdateResourceW(update, !copied)) {
        std::fprintf(stderr, "Could not finish the icon update (Win32 %lu).\n", GetLastError());
        return 1;
    }
    return copied ? 0 : 1;
}
