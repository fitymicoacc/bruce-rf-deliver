// Root build file. Module-specific config lives in library/build.gradle.kts
// and (later) sample-android/build.gradle.kts.

plugins {
    // Apply false — the actual application happens in subproject blocks so
    // each module picks only the plugins it needs.
    alias(libs.plugins.kotlin.multiplatform) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.serialization) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.compose.compiler) apply false
}
