package io.github.wootbeer.btgarecomp;

import org.libsdl.app.SDLActivity;

/**
 * Hosts the native game (libmain.so, built from the repo-root CMakeLists.txt) through SDL.
 */
public class BattleTanxActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    // src/main/main.cpp keeps its own main() (SDL_MAIN_HANDLED) rather than SDL_main.
    @Override
    protected String getMainFunction() {
        return "main";
    }
}
