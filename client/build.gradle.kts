plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.test) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.androidx.baselineprofile) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.jvm) apply false
    // 版本入库供 subprojects 统一 apply（S3 共享居所）
    alias(libs.plugins.ktlint) apply false
}

// S3 共享居所：ktlint 全模块覆盖＋Compose 编译器旗标单点。
// 两族旗标不再逐模块拷贝（F1 遗留①②收口）：新模块接入即自动生效。
subprojects {
    apply(plugin = "org.jlleitschuh.gradle.ktlint")
    afterEvaluate {
        // 仅 Android 模块（带 Compose compiler 面）注入 strongSkipping；
        // 纯 JVM 模块（core/*）无此编译器插件，跳过
        if (extensions.findByName("android") != null) {
            tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
                kotlinOptions.freeCompilerArgs += listOf(
                    "-P",
                    "plugin:androidx.compose.compiler.plugins.kotlin:strongSkipping=true",
                )
            }
        }
        // warnings-as-errors（清零后启用，避免未用 API/恒真分支重新累积）
        tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
            kotlinOptions.allWarningsAsErrors = true
        }
    }
}
