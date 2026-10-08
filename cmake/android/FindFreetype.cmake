# Android: FreeType is built from source by cmake/Android.cmake.
if (NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
set(Freetype_FOUND TRUE)
set(FREETYPE_FOUND TRUE)
get_target_property(FREETYPE_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
set(FREETYPE_LIBRARIES Freetype::Freetype)
set(FREETYPE_VERSION_STRING "${BTGA_ANDROID_FREETYPE_VERSION}")
