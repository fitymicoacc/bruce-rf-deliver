plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)

    androidTarget {
        publishLibraryVariants("release")
    }

    jvm()

    js(IR) {
        browser()
        nodejs()
    }

    // iOS: both device and simulator. cinterop pulls liblil.xcframework
    // produced by liblil/scripts/build-ios.sh. The def file lives at
    // library/nativeInterop/cinterop/liblil.def; the paths below plug in
    // the include directory and xcframework slice per target.
    val liblilRoot = rootProject.projectDir.parentFile.resolve("liblil")

    listOf(
        iosX64()             to "ios-arm64_x86_64-simulator",
        iosArm64()           to "ios-arm64",
        iosSimulatorArm64()  to "ios-arm64_x86_64-simulator"
    ).forEach { (target, slice) ->
        target.binaries.framework {
            baseName = "lil"
            isStatic = true
        }
        target.compilations.getByName("main").cinterops.create("liblil") {
            defFile(project.file("src/nativeInterop/cinterop/liblil.def"))
            packageName("ru.pluttan.lil.native")
            compilerOpts(
                "-I", liblilRoot.resolve("include").absolutePath
            )
            extraOpts(
                "-libraryPath",
                liblilRoot.resolve("dist/ios/liblil.xcframework/$slice").absolutePath
            )
        }
    }

    sourceSets {
        val commonMain by getting {
            dependencies {
                implementation(libs.kable.core)
                implementation(libs.kotlinx.coroutines.core)
                implementation(libs.kotlinx.serialization.core)
            }
        }
        val commonTest by getting {
            dependencies {
                implementation(kotlin("test"))
            }
        }

        val androidMain by getting {
            dependsOn(commonMain)
        }
        val jvmMain by getting {
            dependsOn(commonMain)
        }
        val jsMain by getting {
            dependsOn(commonMain)
        }
        val iosMain by creating {
            dependsOn(commonMain)
        }
        val iosX64Main by getting { dependsOn(iosMain) }
        val iosArm64Main by getting { dependsOn(iosMain) }
        val iosSimulatorArm64Main by getting { dependsOn(iosMain) }
    }
}

android {
    namespace = "ru.pluttan.lil"
    compileSdk = libs.versions.android.compile.sdk.get().toInt()
    defaultConfig {
        minSdk = libs.versions.android.min.sdk.get().toInt()
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
}
