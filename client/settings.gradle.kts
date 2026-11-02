pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "client"

include(":app")
include(":baselineprofile")
include(":core:protocol")
include(":core:network")
include(":core:storage")
include(":feature:shell")
