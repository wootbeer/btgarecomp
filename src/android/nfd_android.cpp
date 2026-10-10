// nativefiledialog-extended has no Android backend (its Linux one needs GTK), so the Android
// build links this in its place: a library named nfd with the same API. Opening one file shows
// the system document picker (BattleTanxActivity.pickFileForNative), which copies the chosen
// document into the app's storage and hands back that copy's path; the call waits for it, so
// call it from a thread other than Android's UI thread (the game's UI thread is SDL's). Every
// other dialog reports that it isn't available.
#include <nfd.h>

#include <jni.h>

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "SDL.h"

namespace {
    const char* const unavailable = "File dialogs are not available on Android.";
    const char* last_error = nullptr;

    nfdresult_t fail() {
        last_error = unavailable;
        return NFD_ERROR;
    }

    std::mutex pick_mutex;
    std::condition_variable pick_done_cv;
    bool pick_done = false;
    bool pick_chosen = false;
    std::string picked_path;

    nfdresult_t pick_file(char** out_path) {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        jobject activity = (jobject)SDL_AndroidGetActivity();
        if (env == nullptr || activity == nullptr) {
            return fail();
        }
        jclass activity_class = env->GetObjectClass(activity);
        jmethodID pick = env->GetMethodID(activity_class, "pickFileForNative", "()V");
        if (pick == nullptr) {
            env->ExceptionClear();
            env->DeleteLocalRef(activity_class);
            env->DeleteLocalRef(activity);
            return fail();
        }

        std::unique_lock lock{ pick_mutex };
        pick_done = false;
        lock.unlock();
        env->CallVoidMethod(activity, pick);
        env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);

        lock.lock();
        pick_done_cv.wait(lock, [] { return pick_done; });
        if (!pick_chosen) {
            return NFD_CANCEL;
        }
        *out_path = strdup(picked_path.c_str());
        return NFD_OKAY;
    }
}

// From BattleTanxActivity once the picker closes: the copied file's path, or null if nothing
// was chosen or the copy failed.
extern "C" JNIEXPORT void JNICALL Java_io_github_wootbeer_btgarecomp_BattleTanxActivity_nativeFilePicked(
    JNIEnv* env, jclass, jstring path) {
    std::lock_guard lock{ pick_mutex };
    pick_chosen = (path != nullptr);
    if (pick_chosen) {
        const char* chars = env->GetStringUTFChars(path, nullptr);
        picked_path = chars;
        env->ReleaseStringUTFChars(path, chars);
    }
    pick_done = true;
    pick_done_cv.notify_all();
}

const char* NFD_GetError(void) { return last_error; }
void NFD_ClearError(void) { last_error = nullptr; }

nfdresult_t NFD_Init(void) { return NFD_OKAY; }
void NFD_Quit(void) {}

void NFD_FreePathN(nfdnchar_t* path) { free(path); }
void NFD_FreePathU8(nfdu8char_t* path) { free(path); }

nfdresult_t NFD_OpenDialogN(nfdnchar_t** out_path, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*) { return pick_file(out_path); }
nfdresult_t NFD_OpenDialogU8(nfdu8char_t** out_path, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*) { return pick_file(out_path); }
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
