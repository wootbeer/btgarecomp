import com.android.build.api.artifact.SingleArtifact
import java.util.Properties
import java.util.zip.ZipFile

plugins {
    id("com.android.application")
}

// The game's build runs file_to_c from a desktop build of this project (see android/README.md):
// btga.hostToolsDir in android/local.properties, or -Pbtga.hostToolsDir=... on the command line.
// Without it the CMake configure stops and says so.
val localProperties = Properties().apply {
    val file = rootProject.file("local.properties")
    if (file.exists()) file.inputStream().use { load(it) }
}
val hostToolsDir: String? =
    (findProperty("btga.hostToolsDir") as String?) ?: localProperties.getProperty("btga.hostToolsDir")

android {
    namespace = "io.github.wootbeer.btgarecomp"
    compileSdk = 35
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "io.github.wootbeer.btgarecomp"
        minSdk = 28
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.1"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_shared"
                if (hostToolsDir != null) {
                    arguments += "-DBTGA_HOST_TOOLS_DIR=$hostToolsDir"
                }
                // libmain.so (the game) and libSDL2.so; nothing else the root project defines.
                targets += listOf("BattleTanxGARecompiled", "SDL2")
            }
        }
    }

    buildTypes {
        debug {
            // Android Studio's Run builds debug. Unoptimised recompiled code is far too slow to
            // play, so the native side is optimised and keeps its debug info.
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
                }
            }
        }
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.31.6"
        }
    }

    lint {
        abortOnError = false
    }
}

// The APK carries the compiled game code but never a ROM. Fails the build if a file in it is
// named like one or starts with an N64 ROM header in any byte order.
abstract class CheckApkHasNoRom : DefaultTask() {
    @get:InputFiles
    abstract val apkDir: DirectoryProperty

    @TaskAction
    fun check() {
        val romExtensions = listOf(".z64", ".n64", ".v64")
        val romHeaders = listOf(
            byteArrayOf(0x80.toByte(), 0x37, 0x12, 0x40), // .z64 (big-endian)
            byteArrayOf(0x37, 0x80.toByte(), 0x40, 0x12), // .v64 (byte-swapped)
            byteArrayOf(0x40, 0x12, 0x37, 0x80.toByte()), // .n64 (little-endian)
        )
        val found = mutableListOf<String>()
        apkDir.get().asFileTree.matching { include("**/*.apk") }.forEach { apk ->
            ZipFile(apk).use { zip ->
                for (entry in zip.entries()) {
                    if (entry.isDirectory) continue
                    val named = romExtensions.any { entry.name.lowercase().endsWith(it) }
                    val header = ByteArray(4)
                    val read = zip.getInputStream(entry).use { it.readNBytes(header, 0, 4) }
                    val looksLikeRom = read == 4 && romHeaders.any { it.contentEquals(header) }
                    if (named || looksLikeRom) found += "${apk.name}: ${entry.name}"
                }
            }
        }
        if (found.isNotEmpty()) {
            throw GradleException(
                "The APK must not contain a ROM, but has:\n  " + found.joinToString("\n  ")
            )
        }
    }
}

androidComponents {
    onVariants { variant ->
        val name = variant.name.replaceFirstChar { it.uppercase() }
        val check = tasks.register<CheckApkHasNoRom>("check${name}ApkHasNoRom") {
            apkDir.set(variant.artifacts.get(SingleArtifact.APK))
        }
        tasks.matching { it.name == "assemble$name" }.configureEach { dependsOn(check) }
    }
}
