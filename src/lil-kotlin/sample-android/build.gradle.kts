plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.compose.compiler)
}

android {
    namespace = "ru.pluttan.lil.sample"
    compileSdk = libs.versions.android.compile.sdk.get().toInt()

    defaultConfig {
        applicationId = "ru.pluttan.lil.sample"
        minSdk        = libs.versions.android.min.sdk.get().toInt()
        targetSdk     = libs.versions.android.target.sdk.get().toInt()
        versionCode   = 1
        versionName   = "0.1.0"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    buildFeatures {
        compose = true
    }

    packaging {
        resources.excludes += "/META-INF/{AL2.0,LGPL2.1}"
    }
}

dependencies {
    implementation(project(":library"))
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.material3)
    debugImplementation(libs.compose.ui.tooling)
}
