plugins {
    id("com.android.application")
}

android {
    namespace = "com.otaviomorais.sheepraider"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    signingConfigs {
        create("sideload") {
            storeFile = file("rechan.keystore")
            storePassword = "rechan"
            keyAlias = "rechan"
            keyPassword = "rechan"
        }
    }

    defaultConfig {
        applicationId = "com.otaviomorais.we2002"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("sideload")
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("sideload")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        checkReleaseBuilds = false
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }
}
