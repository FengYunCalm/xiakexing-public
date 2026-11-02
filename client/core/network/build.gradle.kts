plugins {
    alias(libs.plugins.kotlin.jvm)
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinJvmCompile>().configureEach {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation(libs.kotlinx.coroutines.core)
    implementation(libs.okhttp)
    implementation(project(":core:protocol"))

    // 协议解码由进程连接 owner 唯一持有；测试同样钉死生产 decoder 合同。
    testImplementation(project(":core:protocol"))
    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
}
