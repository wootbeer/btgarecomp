// nativefiledialog-extended has no Android backend (its Linux one needs GTK), so the Android
// build links this in its place: a library named nfd with the same API. Opening files shows the
// system document picker (BattleTanxActivity.pickFilesForNative), which copies the chosen
// documents into the app's storage and hands back the copies' paths; the call waits for them, so
// call it from a thread other than Android's UI thread (the game's UI thread is SDL's). Saving
// and folder dialogs report that they aren't available.
#include <nfd.h>

#include <jni.h>

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "SDL.h"

namespace {
    const char* const unavailable = "This file dialog is not available on Android.";
    const char* last_error = nullptr;

    nfdresult_t fail() {
        last_error = unavailable;
        return NFD_ERROR;
    }

    std::mutex pick_mutex;
    std::condition_variable pick_done_cv;
    bool pick_done = false;
    bool pick_chosen = false;
    std::vector<std::string> picked_paths;

    // The chosen files' copies, or false if nothing was chosen (or the picker failed).
    bool pick_files(bool multiple, std::vector<std::string>& out_paths) {
        last_error = nullptr;
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        jobject activity = (jobject)SDL_AndroidGetActivity();
        if (env == nullptr || activity == nullptr) {
            fail();
            return false;
        }
        jclass activity_class = env->GetObjectClass(activity);
        jmethodID pick = env->GetMethodID(activity_class, "pickFilesForNative", "(Z)V");
        if (pick == nullptr) {
            env->ExceptionClear();
            env->DeleteLocalRef(activity_class);
            env->DeleteLocalRef(activity);
            fail();
            return false;
        }

        std::unique_lock lock{ pick_mutex };
        pick_done = false;
        lock.unlock();
        env->CallVoidMethod(activity, pick, (jboolean)multiple);
        env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);

        lock.lock();
        pick_done_cv.wait(lock, [] { return pick_done; });
        if (!pick_chosen) {
            return false;
        }
        out_paths = picked_paths;
        return true;
    }

    nfdresult_t pick_one(char** out_path) {
        std::vector<std::string> paths;
        if (!pick_files(false, paths) || paths.empty()) {
            return last_error != nullptr ? NFD_ERROR : NFD_CANCEL;
        }
        *out_path = strdup(paths[0].c_str());
        return NFD_OKAY;
    }

    struct PathSet {
        std::vector<std::string> paths;
    };

    nfdresult_t pick_many(const nfdpathset_t** out_paths) {
        std::vector<std::string> paths;
        if (!pick_files(true, paths) || paths.empty()) {
            return last_error != nullptr ? NFD_ERROR : NFD_CANCEL;
        }
        *out_paths = new PathSet{ std::move(paths) };
        return NFD_OKAY;
    }

    const PathSet* as_set(const nfdpathset_t* set) {
        return static_cast<const PathSet*>(set);
    }

    nfdresult_t path_at(const nfdpathset_t* set, nfdpathsetsize_t index, char** out_path) {
        if (set == nullptr || index >= as_set(set)->paths.size()) {
            return fail();
        }
        *out_path = strdup(as_set(set)->paths[index].c_str());
        return NFD_OKAY;
    }

    // An enumeration keeps the set and the next index.
    struct PathSetEnum {
        const PathSet* set;
        size_t next;
    };

    nfdresult_t enum_next(nfdpathsetenum_t* e, char** out_path) {
        PathSetEnum* state = static_cast<PathSetEnum*>(e->ptr);
        if (state == nullptr || state->next >= state->set->paths.size()) {
            *out_path = nullptr;
            return NFD_OKAY;
        }
        *out_path = strdup(state->set->paths[state->next++].c_str());
        return NFD_OKAY;
    }
}

// From BattleTanxActivity once the picker closes: the copied files' paths, or null if nothing was
// chosen or the copies failed.
extern "C" JNIEXPORT void JNICALL Java_io_github_wootbeer_btgarecomp_BattleTanxActivity_nativeFilesPicked(
    JNIEnv* env, jclass, jobjectArray paths) {
    std::lock_guard lock{ pick_mutex };
    picked_paths.clear();
    pick_chosen = (paths != nullptr);
    if (pick_chosen) {
        const jsize count = env->GetArrayLength(paths);
        for (jsize i = 0; i < count; i++) {
            jstring path = (jstring)env->GetObjectArrayElement(paths, i);
            const char* chars = env->GetStringUTFChars(path, nullptr);
            picked_paths.emplace_back(chars);
            env->ReleaseStringUTFChars(path, chars);
            env->DeleteLocalRef(path);
        }
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

nfdresult_t NFD_OpenDialogN(nfdnchar_t** out_path, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*) { return pick_one(out_path); }
nfdresult_t NFD_OpenDialogU8(nfdu8char_t** out_path, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*) { return pick_one(out_path); }
nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t** out_paths, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*) { return pick_many(out_paths); }
nfdresult_t NFD_OpenDialogMultipleU8(const nfdpathset_t** out_paths, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*) { return pick_many(out_paths); }
nfdresult_t NFD_SaveDialogN(nfdnchar_t**, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*, const nfdnchar_t*) { return fail(); }
nfdresult_t NFD_SaveDialogU8(nfdu8char_t**, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*, const nfdu8char_t*) { return fail(); }
nfdresult_t NFD_PickFolderN(nfdnchar_t**, const nfdnchar_t*) { return fail(); }
nfdresult_t NFD_PickFolderU8(nfdu8char_t**, const nfdu8char_t*) { return fail(); }

nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t* set, nfdpathsetsize_t* count) {
    *count = (set != nullptr) ? (nfdpathsetsize_t)as_set(set)->paths.size() : 0;
    return NFD_OKAY;
}
nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t* set, nfdpathsetsize_t index, nfdnchar_t** out_path) { return path_at(set, index, out_path); }
nfdresult_t NFD_PathSet_GetPathU8(const nfdpathset_t* set, nfdpathsetsize_t index, nfdu8char_t** out_path) { return path_at(set, index, out_path); }
void NFD_PathSet_FreePathN(const nfdnchar_t* path) { free((void*)path); }
void NFD_PathSet_FreePathU8(const nfdu8char_t* path) { free((void*)path); }
void NFD_PathSet_Free(const nfdpathset_t* set) { delete as_set(set); }
nfdresult_t NFD_PathSet_GetEnum(const nfdpathset_t* set, nfdpathsetenum_t* e) {
    if (set == nullptr) {
        return fail();
    }
    e->ptr = new PathSetEnum{ as_set(set), 0 };
    return NFD_OKAY;
}
void NFD_PathSet_FreeEnum(nfdpathsetenum_t* e) {
    delete static_cast<PathSetEnum*>(e->ptr);
    e->ptr = nullptr;
}
nfdresult_t NFD_PathSet_EnumNextN(nfdpathsetenum_t* e, nfdnchar_t** out_path) { return enum_next(e, out_path); }
nfdresult_t NFD_PathSet_EnumNextU8(nfdpathsetenum_t* e, nfdu8char_t** out_path) { return enum_next(e, out_path); }
