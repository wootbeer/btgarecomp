#pragma once

#if defined(__ANDROID__)
namespace btga::android {
    // Called first thing in main(): sends stdout/stderr to logcat (tag "BTGA") and makes the
    // app's private storage folder the working directory, which is where the activity has
    // copied assets/ and where relative paths such as crash_log.txt land.
    void startup();
}
#endif
