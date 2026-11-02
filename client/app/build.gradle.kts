import java.util.Locale

val xkxVersionCode = 389
val xkxVersionName = "1.0.388"

fun xkxIsDnsHost(value: String): Boolean {
    val host = value.trim().lowercase(Locale.ROOT)
    if (host.length !in 1..253 || !host.contains('.') || host.startsWith('.') || host.endsWith('.')) {
        return false
    }
    if (host.all { it.isDigit() || it == '.' }) {
        return false
    }
    return host.split('.').all { label ->
        label.length in 1..63 &&
            Regex("[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?").matches(label)
    }
}

fun xkxJsonEscape(value: String): String =
    buildString {
        value.forEach { char ->
            when (char) {
                '\\' -> append("\\\\")
                '"' -> append("\\\"")
                '\n' -> append("\\n")
                '\r' -> append("\\r")
                '\t' -> append("\\t")
                else -> append(char)
            }
        }
    }

fun xkxBuildIdentityJson(
    buildType: String,
    sourceCommit: String,
    endpointHost: String,
    endpointPort: Int,
): String {
    val escapedBuildType = xkxJsonEscape(buildType)
    val escapedEndpointHost = xkxJsonEscape(endpointHost)
    val escapedSourceCommit = xkxJsonEscape(sourceCommit)
    val escapedVersionName = xkxJsonEscape(xkxVersionName)
    return """{"schemaVersion":1,"sourceCommit":"$escapedSourceCommit","versionCode":$xkxVersionCode,"versionName":"$escapedVersionName","publicEndpointHost":"$escapedEndpointHost","publicEndpointPort":$endpointPort,"buildType":"$escapedBuildType"}"""
}

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.androidx.baselineprofile)
    alias(libs.plugins.kotlin.android)
}

val releaseVariantRequested =
    gradle.startParameter.taskNames.any { task ->
        task.substringAfterLast(':').contains("release", ignoreCase = true)
    }
val debugVariantRequested =
    gradle.startParameter.taskNames.any { task ->
        task.substringAfterLast(':').contains("debug", ignoreCase = true)
    }
if (releaseVariantRequested && debugVariantRequested) {
    throw GradleException("Build identity requires one variant per Gradle invocation")
}
val identityBuildType = if (releaseVariantRequested) "release" else "debug"

android {
    namespace = "com.xiakexing.client"
    compileSdk = 34

    val releaseStoreFile =
        providers.gradleProperty("xkx.release.storeFile")
            .orElse(providers.environmentVariable("XKX_RELEASE_STORE_FILE"))
            .orNull
    val releaseStorePassword =
        providers.gradleProperty("xkx.release.storePassword")
            .orElse(providers.environmentVariable("XKX_RELEASE_STORE_PASSWORD"))
            .orNull
    val releaseKeyAlias =
        providers.gradleProperty("xkx.release.keyAlias")
            .orElse(providers.environmentVariable("XKX_RELEASE_KEY_ALIAS"))
            .orNull
    val releaseKeyPassword =
        providers.gradleProperty("xkx.release.keyPassword")
            .orElse(providers.environmentVariable("XKX_RELEASE_KEY_PASSWORD"))
            .orNull
    val releaseSigningConfigured =
        listOf(releaseStoreFile, releaseStorePassword, releaseKeyAlias, releaseKeyPassword)
            .all { !it.isNullOrBlank() }
    val packagingRelease =
        gradle.startParameter.taskNames.any { task ->
            task.contains("assembleRelease", ignoreCase = true) ||
                task.contains("bundleRelease", ignoreCase = true)
        }
    if (packagingRelease && !releaseSigningConfigured) {
        throw GradleException(
            "Release packaging requires XKX_RELEASE_STORE_FILE, " +
                "XKX_RELEASE_STORE_PASSWORD, XKX_RELEASE_KEY_ALIAS, and " +
                "XKX_RELEASE_KEY_PASSWORD",
        )
    }

    signingConfigs {
        create("xkxRelease") {
            if (releaseStoreFile != null) storeFile = file(releaseStoreFile)
            storePassword = releaseStorePassword ?: ""
            keyAlias = releaseKeyAlias ?: ""
            keyPassword = releaseKeyPassword ?: ""
        }
    }

    defaultConfig {
        applicationId = "com.xiakexing.clientnew"
        val configuredPublicEndpointHost =
            providers.gradleProperty("xkx.public.host")
                .orElse(providers.environmentVariable("XKX_PUBLIC_DOMAIN"))
                .orNull
                ?.trim()
                ?.takeIf(String::isNotEmpty)
        val configuredPublicEndpointPort =
            providers.gradleProperty("xkx.public.port")
                .orElse(providers.environmentVariable("XKX_PUBLIC_PORT"))
                .orNull
                ?.trim()
                ?.toIntOrNull()
        val normalizedConfiguredPublicEndpointHost =
            configuredPublicEndpointHost?.lowercase(Locale.ROOT)
        if (releaseVariantRequested) {
            if (normalizedConfiguredPublicEndpointHost == null) {
                throw GradleException(
                    "Release builds require xkx.public.host or XKX_PUBLIC_DOMAIN",
                )
            }
            if (!xkxIsDnsHost(normalizedConfiguredPublicEndpointHost)) {
                throw GradleException("Release endpoint host must be a DNS name, not an IP address")
            }
            if (configuredPublicEndpointPort != 443) {
                throw GradleException("Release endpoint port must be explicitly set to 443")
            }
        }
        val publicEndpointHost = normalizedConfiguredPublicEndpointHost ?: "127.0.0.1"
        val publicEndpointPort = configuredPublicEndpointPort ?: 8080
        require(publicEndpointPort in 1..65535) {
            "xkx.public.port/XKX_PUBLIC_PORT must be between 1 and 65535"
        }
        val configuredSourceCommit =
            providers.gradleProperty("xkx.source.commit")
                .orElse(providers.environmentVariable("XKX_SOURCE_COMMIT"))
                .orNull
                ?.trim()
                ?.lowercase(Locale.ROOT)
        if (releaseVariantRequested && configuredSourceCommit?.matches(Regex("[0-9a-f]{40}")) != true) {
            throw GradleException(
                "Release builds require xkx.source.commit or XKX_SOURCE_COMMIT as a 40-character commit",
            )
        }
        val sourceCommit = configuredSourceCommit ?: "unknown"
        val buildIdentityJson =
            xkxBuildIdentityJson(
                buildType = identityBuildType,
                sourceCommit = sourceCommit,
                endpointHost = publicEndpointHost,
                endpointPort = publicEndpointPort,
            )
        buildConfigField(
            "String",
            "PUBLIC_ENDPOINT_HOST",
            "\"${xkxJsonEscape(publicEndpointHost)}\"",
        )
        buildConfigField(
            "int",
            "PUBLIC_ENDPOINT_PORT",
            publicEndpointPort.toString(),
        )
        buildConfigField(
            "int",
            "BUILD_IDENTITY_SCHEMA",
            "1",
        )
        buildConfigField(
            "String",
            "BUILD_SOURCE_COMMIT",
            "\"${xkxJsonEscape(sourceCommit)}\"",
        )
        buildConfigField(
            "String",
            "BUILD_TYPE",
            "\"${xkxJsonEscape(identityBuildType)}\"",
        )
        buildConfigField(
            "String",
            "BUILD_IDENTITY_JSON",
            "\"${xkxJsonEscape(buildIdentityJson)}\"",
        )
        minSdk = 21
        targetSdk = 34
        versionCode = xkxVersionCode
        versionName = xkxVersionName
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            signingConfig = signingConfigs.getByName("xkxRelease")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }

        debug {
            applicationIdSuffix = ".debug"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    testOptions {
        unitTests.isIncludeAndroidResources = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = libs.versions.composeCompiler.get()
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}

val xkxBuildIdentityDir = layout.buildDirectory.dir("generated/xkx-build-identity")
android.sourceSets["main"].assets.srcDir(xkxBuildIdentityDir)
val generateXkxBuildIdentity =
    tasks.register("generateXkxBuildIdentity") {
        inputs.property("buildType", identityBuildType)
        outputs.dir(xkxBuildIdentityDir)
        outputs.upToDateWhen { false }
        doLast {
            val identityFile = xkxBuildIdentityDir.get().file("xkx-build-identity.json").asFile
            identityFile.parentFile.mkdirs()
            identityFile.writeText(
                xkxBuildIdentityJson(
                    buildType = identityBuildType,
                    sourceCommit =
                        providers.gradleProperty("xkx.source.commit")
                            .orElse(providers.environmentVariable("XKX_SOURCE_COMMIT"))
                            .orNull
                            ?.trim()
                            ?.lowercase(Locale.ROOT)
                            ?: "unknown",
                    endpointHost =
                        providers.gradleProperty("xkx.public.host")
                            .orElse(providers.environmentVariable("XKX_PUBLIC_DOMAIN"))
                            .orNull
                            ?.trim()
                            ?.lowercase(Locale.ROOT)
                            ?: "127.0.0.1",
                    endpointPort =
                        providers.gradleProperty("xkx.public.port")
                            .orElse(providers.environmentVariable("XKX_PUBLIC_PORT"))
                            .orNull
                            ?.trim()
                            ?.toIntOrNull()
                            ?: 8080,
                ),
            )
        }
    }
// Lint vital reads the generated identity asset without going through merge assets.
tasks.matching { task ->
    task.name == "mergeDebugAssets" ||
        task.name == "mergeReleaseAssets" ||
        task.name.contains("LintVital", ignoreCase = true)
}.configureEach { dependsOn(generateXkxBuildIdentity) }

baselineProfile {
    dexLayoutOptimization = true
}

dependencies {
    implementation(project(":feature:shell"))
    implementation(project(":core:network"))
    implementation(project(":core:storage"))
    implementation(libs.android.material)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.profileinstaller)
    implementation(libs.androidx.core.ktx)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    baselineProfile(project(":baselineprofile"))
    debugImplementation(libs.androidx.compose.ui.tooling)
    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.robolectric)
}
