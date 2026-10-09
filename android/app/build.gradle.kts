import com.android.build.api.artifact.SingleArtifact
import java.security.MessageDigest
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

// Testing aid: btga.vulkanValidation=true (local.properties or -P) packs Khronos' Vulkan validation
// layer into debug builds, for the Android GPU debug-layer settings to load (see android/README.md).
val vulkanValidation: Boolean =
    ((findProperty("btga.vulkanValidation") as String?) ?: localProperties.getProperty("btga.vulkanValidation")) == "true"
val validationLayerVersion = "1.4.304.0"
val validationLayerSha256 = "3e67710f93daa7f39823e85b4ba5aaaf6cd44d207747715fa258a6796b9798ed"
val validationLayerDir = layout.buildDirectory.dir("validation-layer/jniLibs")

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

    // The game's fonts, icons and stylesheet (the repo's assets/ folder); BattleTanxActivity
    // copies them to the app's storage on first launch.
    sourceSets {
        getByName("main") {
            assets.srcDir("../../assets")
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

    if (vulkanValidation) {
        sourceSets {
            getByName("debug") {
                jniLibs.srcDir(validationLayerDir.get().asFile)
            }
        }
        // The GPU debug-layer loader looks for the layer as a file in the app's library folder.
        packaging {
            jniLibs {
                useLegacyPackaging = true
            }
        }
    }
}

if (vulkanValidation) {
    val fetchValidationLayer = tasks.register("fetchVulkanValidationLayer") {
        val zip = layout.buildDirectory.file("validation-layer/android-binaries-$validationLayerVersion.zip")
        outputs.dir(validationLayerDir)
        doLast {
            val zipFile = zip.get().asFile
            if (!zipFile.exists()) {
                zipFile.parentFile.mkdirs()
                uri("https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/" +
                    "vulkan-sdk-$validationLayerVersion/android-binaries-$validationLayerVersion.zip")
                    .toURL().openStream().use { input -> zipFile.outputStream().use { input.copyTo(it) } }
            }
            val digest = MessageDigest.getInstance("SHA-256")
                .digest(zipFile.readBytes()).joinToString("") { "%02x".format(it) }
            if (digest != validationLayerSha256) {
                zipFile.delete()
                throw GradleException("Validation layer download has SHA-256 $digest, expected $validationLayerSha256")
            }
            val target = validationLayerDir.get().dir("arm64-v8a").asFile
            target.mkdirs()
            ZipFile(zipFile).use { z ->
                val entry = z.getEntry("android-binaries-$validationLayerVersion/arm64-v8a/libVkLayer_khronos_validation.so")
                z.getInputStream(entry).use { input ->
                    target.resolve("libVkLayer_khronos_validation.so").outputStream().use { input.copyTo(it) }
                }
            }
        }
    }
    tasks.matching { it.name == "mergeDebugJniLibFolders" }.configureEach { dependsOn(fetchValidationLayer) }
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
