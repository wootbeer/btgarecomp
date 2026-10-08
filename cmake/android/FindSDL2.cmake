# Android: SDL2 is built from source by cmake/Android.cmake.
set(SDL2_FOUND TRUE)
set(SDL2_INCLUDE_DIRS
    "${sdl2_BINARY_DIR}/include"
    "${sdl2_BINARY_DIR}/include/SDL2"
    "${sdl2_BINARY_DIR}/include-config-$<LOWER_CASE:$<CONFIG>>/SDL2")
set(SDL2_LIBRARIES SDL2::SDL2)
