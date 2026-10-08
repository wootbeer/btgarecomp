// nativefiledialog-extended has no Android backend (its Linux one needs GTK), so the Android
// build links this in its place: a library named nfd with the same API, where every dialog
// reports that it isn't available. Files come from the Java launcher's system document picker
// instead (android/PLAN.md).
#include <nfd.h>

namespace {
    const char* const unavailable = "File dialogs are not available on Android.";
    const char* last_error = nullptr;

    nfdresult_t fail() {
        last_error = unavailable;
        return NFD_ERROR;
    }
}

const char* NFD_GetError(void) { return last_error; }
void NFD_ClearError(void) { last_error = nullptr; }

nfdresult_t NFD_Init(void) { return NFD_OKAY; }
void NFD_Quit(void) {}

void NFD_FreePathN(nfdnchar_t*) {}
void NFD_FreePathU8(nfdu8char_t*) {}

nfdresult_t NFD_OpenDialogN(nfdnchar_t**, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*) { return fail(); }
nfdresult_t NFD_OpenDialogU8(nfdu8char_t**, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*) { return fail(); }
nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t**, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*) { return fail(); }
nfdresult_t NFD_OpenDialogMultipleU8(const nfdpathset_t**, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*) { return fail(); }
nfdresult_t NFD_SaveDialogN(nfdnchar_t**, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*, const nfdnchar_t*) { return fail(); }
nfdresult_t NFD_SaveDialogU8(nfdu8char_t**, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*, const nfdu8char_t*) { return fail(); }
nfdresult_t NFD_PickFolderN(nfdnchar_t**, const nfdnchar_t*) { return fail(); }
nfdresult_t NFD_PickFolderU8(nfdu8char_t**, const nfdu8char_t*) { return fail(); }

// No dialog ever returns a path set, so these only see the null one a failed dialog leaves.
nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t*, nfdpathsetsize_t* count) { *count = 0; return NFD_OKAY; }
nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t*, nfdpathsetsize_t, nfdnchar_t**) { return fail(); }
nfdresult_t NFD_PathSet_GetPathU8(const nfdpathset_t*, nfdpathsetsize_t, nfdu8char_t**) { return fail(); }
void NFD_PathSet_FreePathN(const nfdnchar_t*) {}
void NFD_PathSet_FreePathU8(const nfdu8char_t*) {}
void NFD_PathSet_Free(const nfdpathset_t*) {}
nfdresult_t NFD_PathSet_GetEnum(const nfdpathset_t*, nfdpathsetenum_t*) { return fail(); }
void NFD_PathSet_FreeEnum(nfdpathsetenum_t*) {}
nfdresult_t NFD_PathSet_EnumNextN(nfdpathsetenum_t*, nfdnchar_t** outPath) { *outPath = nullptr; return NFD_OKAY; }
nfdresult_t NFD_PathSet_EnumNextU8(nfdpathsetenum_t*, nfdu8char_t** outPath) { *outPath = nullptr; return NFD_OKAY; }
