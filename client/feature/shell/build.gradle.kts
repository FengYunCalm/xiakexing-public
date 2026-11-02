plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    // ktlint 已上提根 subprojects 统一 apply（S3 共享居所）
}

android {
    namespace = "com.xiakexing.client.feature.shell"
    compileSdk = 34

    defaultConfig {
        minSdk = 21
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
        // M2/F1 取证结论：compose 1.5.14 强跳过使不可跳过组合函数 64→14
        // （剩余全为 getter/transition 非组合体），转常态默认。
        // -PcomposeMetrics=<dir> 仍为 opt-in 稳定性报告开关，常态零开销。
        val metricsDir = project.findProperty("composeMetrics") as String?
        if (metricsDir != null) {
            freeCompilerArgs +=
                listOf(
                    "-P",
                    "plugin:androidx.compose.compiler.plugins.kotlin" +
                        ":metricsDestination=$metricsDir",
                    "-P",
                    "plugin:androidx.compose.compiler.plugins.kotlin" +
                        ":reportsDestination=$metricsDir",
                )
        }
        // strongSkipping 已上提根 subprojects 统一注入（S3 共享居所）
    }

    buildFeatures {
        compose = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = libs.versions.composeCompiler.get()
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }

    testOptions {
        unitTests.isIncludeAndroidResources = true
    }
}

dependencies {
    implementation(project(":core:network"))
    implementation(project(":core:protocol"))

    implementation(libs.androidx.core.ktx)
    implementation(libs.gson)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(libs.kotlinx.coroutines.android)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)

    testImplementation(libs.junit)
    testImplementation(libs.androidx.activity.compose)
    testImplementation(libs.androidx.compose.ui.test.junit4)
    testImplementation(libs.androidx.test.ext.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.robolectric)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
    releaseImplementation(libs.androidx.compose.ui.test.manifest)
}
