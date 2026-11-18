import java.io.FileInputStream
import java.util.Properties

plugins {
    id("com.android.application")
}

val keystorePropertiesFile = rootProject.file("keystore.properties")
val keystoreProperties = Properties()
if (keystorePropertiesFile.exists()) {
    keystoreProperties.load(FileInputStream(keystorePropertiesFile))
}

android {
    namespace = "org.ps2native.recomp"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    signingConfigs {
        if (keystorePropertiesFile.exists()) {
            create("release") {
                storeFile = rootProject.file(
                    keystoreProperties["storeFile"] as String
                )
                storePassword = keystoreProperties["storePassword"] as String
                keyAlias = keystoreProperties["keyAlias"] as String
                keyPassword = keystoreProperties["keyPassword"] as String
            }
        }
    }

    defaultConfig {
        applicationId = "org.ps2native.recomp"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_PLATFORM=android-29",
                    "-DANDROID_ABI=arm64-v8a"
                )
                cppFlags += "-std=c++17"
                if (project.hasProperty("storageSelftest")) {
                    arguments += "-DPORT_STORAGE_SELFTEST=ON"
                } else {
                    arguments += "-DPORT_STORAGE_SELFTEST=OFF"
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            // app/ -> android-project/ -> port/CMakeLists.txt (sus fuentes están en port/src).
            path = file("../../CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            isDebuggable = false
            isJniDebuggable = false
            if (keystorePropertiesFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
            // APK final sin tabla de simbolos. AGP ejecuta
            // stripReleaseDebugSymbols y no genera native-debug-symbols.zip.
            ndk {
                debugSymbolLevel = "NONE"
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            // Las .so van comprimidas en el APK (menos descarga).
            useLegacyPackaging = true
            // Lista vacia: AGP puede eliminar .symtab/.debug_* de libmain.so.
            keepDebugSymbols.clear()
        }
    }
}
