package com.xiakexing.client.baselineprofile

import android.os.Build
import androidx.benchmark.macro.junit4.BaselineProfileRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class BaselineProfileGenerator {
    @get:Rule
    val baselineProfileRule = BaselineProfileRule()

    @Test
    fun generate() {
        assumeTrue(Build.VERSION.SDK_INT >= 28)

        baselineProfileRule.collect(
            packageName = "com.xiakexing.client",
            maxIterations = 5,
            stableIterations = 2,
            includeInStartupProfile = true,
        ) {
            // Keep the profile deterministic by covering app startup only.
            // Entering the real game world still depends on a live gateway session.
            pressHome()
            startActivityAndWait()
            device.waitForIdle()
        }
    }
}
