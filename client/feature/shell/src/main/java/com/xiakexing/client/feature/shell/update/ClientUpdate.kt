package com.xiakexing.client.feature.shell.update

import android.app.Application
import android.content.Intent
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import androidx.core.net.toUri
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.xiakexing.client.core.network.PublicEndpoint
import com.xiakexing.client.core.network.TlsFallbackHttpClient
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.net.URL
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicLong

internal const val UPDATE_DOWNLOAD_COMMAND = "ui://client-update/download"
internal const val CLIENT_UPDATE_SCHEMA_VERSION = 1
internal const val CLIENT_BUILD_IDENTITY_ASSET = "xkx-build-identity.json"
internal const val MAX_UPDATE_APK_BYTES = 50L * 1024L * 1024L

private val SHA256_PATTERN = Regex("[0-9a-f]{64}")
private val COMMIT_PATTERN = Regex("[0-9a-f]{40}")
private val RFC3339_UTC_PATTERN =
    Regex("\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(?:\\.\\d+)?Z")

internal data class InstalledBuildIdentity(
    val schemaVersion: Int,
    val sourceCommit: String,
    val endpointHost: String,
    val endpointPort: Int,
)

internal fun isInstalledBuildIdentityCompatible(
    identity: InstalledBuildIdentity,
    publicEndpoint: PublicEndpoint,
): Boolean =
    identity.schemaVersion == CLIENT_UPDATE_SCHEMA_VERSION &&
        COMMIT_PATTERN.matches(identity.sourceCommit) &&
        identity.endpointHost.lowercase() == publicEndpoint.host.lowercase() &&
        identity.endpointPort == publicEndpoint.httpsPort

internal fun requireSuccessfulUpdateStatus(
    status: Int,
    resource: String,
) {
    if (status !in 200..299) {
        throw IllegalStateException("$resource request failed: HTTP $status")
    }
}

data class ClientUpdateManifest(
    val schemaVersion: Int = 0,
    val versionCode: Int = 0,
    val versionName: String = "",
    val minSupportedVersionCode: Int = 1,
    val force: Boolean = false,
    val apkUrl: String = "",
    val releasePageUrl: String = "",
    val sha256: String = "",
    val size: Long = 0L,
    val sourceCommit: String = "",
    val title: String = "发现新版本",
    val notes: List<String> = emptyList(),
    val publishedAt: String = "",
) {
    fun isUsable(allowedHosts: Set<String>): Boolean {
        val parsedUrl = runCatching { URL(apkUrl) }.getOrNull()
        val host = parsedUrl?.host?.lowercase()
        return schemaVersion == CLIENT_UPDATE_SCHEMA_VERSION &&
            versionCode > 0 &&
            versionName.isNotBlank() &&
            minSupportedVersionCode in 1..versionCode &&
            parsedUrl != null &&
            parsedUrl.protocol.equals("https", ignoreCase = true) &&
            parsedUrl.userInfo == null &&
            parsedUrl.query == null &&
            parsedUrl.ref == null &&
            parsedUrl.port in setOf(-1, 443) &&
            host != null &&
            host in allowedHosts &&
            SHA256_PATTERN.matches(sha256) &&
            size in 1..MAX_UPDATE_APK_BYTES &&
            COMMIT_PATTERN.matches(sourceCommit) &&
            RFC3339_UTC_PATTERN.matches(publishedAt)
    }

    fun isRequiredFor(installed: InstalledAppVersion): Boolean =
        force || installed.versionCode < minSupportedVersionCode
}

data class InstalledAppVersion(
    val versionCode: Int,
    val versionName: String,
)

data class ClientUpdateDownloadProgress(
    val downloadedBytes: Long,
    val totalBytes: Long,
) {
    val fraction: Float?
        get() =
            totalBytes
                .takeIf { it > 0L }
                ?.let { total ->
                    (downloadedBytes.toDouble() / total.toDouble())
                        .toFloat()
                        .coerceIn(0f, 1f)
                }
}

fun ClientUpdateManifest.isNewerThan(
    installed: InstalledAppVersion,
    allowedHosts: Set<String>,
): Boolean {
    return isUsable(allowedHosts) && versionCode > installed.versionCode
}

fun ClientUpdateManifest.isApplicableTo(
    installed: InstalledAppVersion,
    allowedHosts: Set<String>,
): Boolean {
    return isUsable(allowedHosts) && versionCode > installed.versionCode
}

open class ClientUpdateManager(
    private val application: Application,
    private val publicEndpoint: PublicEndpoint,
    private val manifestUrls: List<String>? = null,
    private val enforceBuildIdentity: Boolean = false,
) {
    private val allowedUpdateHosts: Set<String> = setOf(publicEndpoint.host.lowercase())
    private val updateHttpClient: TlsFallbackHttpClient = TlsFallbackHttpClient.shared
    private val installedBuildIdentity: InstalledBuildIdentity? by lazy(
        ::readInstalledBuildIdentity,
    )

    private fun readInstalledBuildIdentity(): InstalledBuildIdentity? =
        runCatching {
            application.assets
                .open(CLIENT_BUILD_IDENTITY_ASSET)
                .bufferedReader(Charsets.UTF_8)
                .use { reader ->
                    val payload = JsonParser.parseString(reader.readText()).asJsonObject
                    InstalledBuildIdentity(
                        schemaVersion = payload.requireInt("schemaVersion"),
                        sourceCommit = payload.requireString("sourceCommit"),
                        endpointHost = payload.requireString("publicEndpointHost"),
                        endpointPort = payload.requireInt("publicEndpointPort"),
                    )
                }
        }.getOrNull()

    open fun installedVersion(): InstalledAppVersion {
        val packageInfo = application.packageManager.getPackageInfo(application.packageName, 0)
        val code =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                packageInfo.longVersionCode.toInt()
            } else {
                @Suppress("DEPRECATION")
                packageInfo.versionCode
            }
        return InstalledAppVersion(
            versionCode = code,
            versionName = packageInfo.versionName.orEmpty(),
        )
    }

    open suspend fun fetchManifest(): ClientUpdateManifest =
        withContext(Dispatchers.IO) {
            var lastError: Throwable? = null
            var bestManifest: ClientUpdateManifest? = null
            (manifestUrls ?: listOf("${publicEndpoint.httpsOrigin()}/update.json")).asSequence()
                .map(String::trim)
                .filter(String::isNotBlank)
                .forEach { url ->
                    try {
                        val manifest = fetchManifestFrom(url)
                        val currentBest = bestManifest
                        if (currentBest == null || manifest.versionCode > currentBest.versionCode) {
                            bestManifest = manifest
                        }
                    } catch (error: CancellationException) {
                        throw error
                    } catch (error: Throwable) {
                        lastError = error
                    }
                }
            bestManifest?.let { return@withContext it }
            throw lastError ?: IllegalStateException("没有可用的更新地址")
        }

    private fun fetchManifestFrom(url: String): ClientUpdateManifest =
        updateHttpClient.executeGet(
            url = secureUpdateUrl(url),
            allowedHost = publicEndpoint.host,
            connectTimeoutMillis = NETWORK_TIMEOUT_MS,
            readTimeoutMillis = NETWORK_TIMEOUT_MS,
        ).use { response ->
            requireSuccessfulUpdateStatus(response.statusCode, "更新清单")
            val body = response.body ?: throw IllegalStateException("更新清单响应为空")
            body.use { input ->
                parseManifest(readUtf8WithinLimit(input, MAX_UPDATE_MANIFEST_BYTES))
                    .also(::validateManifest)
            }
        }

    internal fun validateManifest(manifest: ClientUpdateManifest) {
        if (!manifest.isUsable(allowedUpdateHosts)) {
            throw IllegalArgumentException("update manifest is missing required trusted fields")
        }
        secureUpdateUrl(manifest.apkUrl)
        if (enforceBuildIdentity) {
            val identity =
                installedBuildIdentity
                    ?: throw IllegalStateException("installed APK build identity is missing")
            if (!isInstalledBuildIdentityCompatible(identity, publicEndpoint)) {
                throw IllegalArgumentException(
                    "installed APK build identity does not match the configured endpoint",
                )
            }
        }
    }

    final fun isTrustedManifest(manifest: ClientUpdateManifest): Boolean =
        runCatching { validateManifest(manifest) }.isSuccess

    final fun isNewerTrusted(
        manifest: ClientUpdateManifest,
        installed: InstalledAppVersion,
    ): Boolean =
        runCatching {
            validateManifest(manifest)
            manifest.isNewerThan(installed, allowedUpdateHosts)
        }.getOrDefault(false)

    final fun isApplicableTrusted(
        manifest: ClientUpdateManifest,
        installed: InstalledAppVersion,
    ): Boolean =
        runCatching {
            validateManifest(manifest)
            manifest.isApplicableTo(installed, allowedUpdateHosts)
        }.getOrDefault(false)

    private fun secureUpdateUrl(raw: String): URL {
        val parsed = URL(raw)
        if (parsed.protocol.lowercase() != "https") {
            throw IllegalArgumentException("update URL must use HTTPS")
        }
        if (parsed.userInfo != null || parsed.query != null || parsed.ref != null) {
            throw IllegalArgumentException(
                "update URL must not contain credentials or query parameters",
            )
        }
        if (parsed.port !in setOf(-1, 443)) {
            throw IllegalArgumentException("update URL must use port 443")
        }
        if (parsed.host.lowercase() !in allowedUpdateHosts) {
            throw IllegalArgumentException("update URL host is not the configured update host")
        }
        return parsed
    }

    internal fun parseManifest(raw: String): ClientUpdateManifest {
        val payload = JsonParser.parseString(raw).asJsonObject
        return ClientUpdateManifest(
            schemaVersion = payload.requireInt("schemaVersion"),
            versionCode = payload.requireInt("versionCode"),
            versionName = payload.requireString("versionName"),
            minSupportedVersionCode = payload.requireInt("minSupportedVersionCode"),
            force = payload.requireBoolean("force"),
            apkUrl = payload.requireString("apkUrl"),
            releasePageUrl = payload.optionalString("releasePageUrl"),
            sha256 = payload.requireString("sha256"),
            size = payload.requireLong("size"),
            sourceCommit = payload.requireString("sourceCommit"),
            title = payload.optionalString("title").ifBlank { "发现新版本" },
            notes = payload.stringListOrEmpty("notes"),
            publishedAt = payload.requireString("publishedAt"),
        )
    }

    open suspend fun downloadAndVerify(
        manifest: ClientUpdateManifest,
        onProgress: suspend (ClientUpdateDownloadProgress) -> Unit = {},
    ): File =
        withContext(Dispatchers.IO) {
            validateManifest(manifest)
            val artifactId = DOWNLOAD_ARTIFACT_SERIAL.incrementAndGet()
            val target = File(application.cacheDir, "$UPDATE_APK_FILE_NAME.$artifactId")
            val temporary = File(application.cacheDir, "$UPDATE_APK_FILE_NAME.$artifactId.part")
            target.delete()
            temporary.delete()
            try {
                updateHttpClient.executeGet(
                    url = secureUpdateUrl(manifest.apkUrl),
                    allowedHost = publicEndpoint.host,
                    connectTimeoutMillis = NETWORK_TIMEOUT_MS,
                    readTimeoutMillis = DOWNLOAD_TIMEOUT_MS,
                ).use { response ->
                    requireSuccessfulUpdateStatus(response.statusCode, "APK 下载")
                    if (response.contentLength > manifest.size) {
                        throw IllegalStateException("APK 下载超过清单大小")
                    }
                    val totalBytes = manifest.size
                    var downloadedBytes = 0L
                    var lastReportedBucket = Long.MIN_VALUE
                    var lastReportedBytes = -1L

                    suspend fun reportProgress(force: Boolean = false) {
                        val bucket =
                            ((downloadedBytes * PROGRESS_PERCENT_SCALE) / totalBytes)
                                .coerceIn(0L, PROGRESS_PERCENT_SCALE)
                        if (bucket == lastReportedBucket && (!force || downloadedBytes == lastReportedBytes)) {
                            return
                        }
                        lastReportedBucket = bucket
                        lastReportedBytes = downloadedBytes
                        onProgress(ClientUpdateDownloadProgress(downloadedBytes, totalBytes))
                    }

                    reportProgress(force = true)
                    val input = response.body ?: throw IllegalStateException("APK 下载响应为空")
                    input.use {
                        temporary.outputStream().use { output ->
                            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                            while (true) {
                                val read = input.read(buffer)
                                if (read <= 0) break
                                val nextSize = downloadedBytes + read.toLong()
                                if (nextSize > manifest.size || nextSize > MAX_UPDATE_APK_BYTES) {
                                    throw IllegalStateException("APK 下载超过允许大小")
                                }
                                output.write(buffer, 0, read)
                                downloadedBytes = nextSize
                                reportProgress()
                            }
                        }
                    }
                    reportProgress(force = true)
                }
                validateDownloadedArtifact(temporary, manifest)
                if (!temporary.renameTo(target)) {
                    throw IllegalStateException("APK 临时文件提交失败")
                }
                target
            } catch (error: Throwable) {
                temporary.delete()
                target.delete()
                throw error
            }
        }

    open fun openInstall(file: File): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && !application.packageManager.canRequestPackageInstalls()) {
            val settingsIntent =
                Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES).apply {
                    data = "package:${application.packageName}".toUri()
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
            application.startActivity(settingsIntent)
            return false
        }
        val uri =
            FileProvider.getUriForFile(
                application,
                "${application.packageName}.fileprovider",
                file,
            )
        val installIntent =
            Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, APK_MIME_TYPE)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
        application.startActivity(installIntent)
        return true
    }
}

internal fun readUtf8WithinLimit(
    input: java.io.InputStream,
    limitBytes: Int,
): String {
    require(limitBytes > 0) { "manifest byte limit must be positive" }
    val bytes = ByteArray(limitBytes + 1)
    var count = 0
    while (count < bytes.size) {
        val read = input.read(bytes, count, bytes.size - count)
        if (read < 0) break
        if (read == 0) continue
        count += read
    }
    if (count > limitBytes) {
        throw IllegalArgumentException("更新清单超过允许大小")
    }
    return String(bytes, 0, count, Charsets.UTF_8)
}

internal fun validateDownloadedArtifact(
    file: File,
    manifest: ClientUpdateManifest,
) {
    if (!file.isFile || file.length() != manifest.size) {
        throw IllegalStateException("APK 大小校验失败")
    }
    if (file.sha256() != manifest.sha256) {
        throw IllegalStateException("APK 完整性校验失败")
    }
}

internal fun File.sha256(): String {
    val digest = MessageDigest.getInstance("SHA-256")
    inputStream().use { input ->
        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
        while (true) {
            val read = input.read(buffer)
            if (read <= 0) break
            digest.update(buffer, 0, read)
        }
    }
    return digest.digest().joinToString(separator = "") { byte -> "%02x".format(byte) }
}

private fun JsonObject.requiredValue(name: String) =
    get(name)?.takeUnless { it.isJsonNull }
        ?: throw IllegalArgumentException("update manifest field $name is missing")

private fun JsonObject.requireString(name: String): String {
    val value = requiredValue(name)
    require(value.isJsonPrimitive && value.asJsonPrimitive.isString) {
        "update manifest field $name must be a string"
    }
    return value.asString
}

private fun JsonObject.optionalString(name: String): String {
    if (!has(name)) return ""
    return requireString(name)
}

private fun JsonObject.requireLong(name: String): Long {
    val value = requiredValue(name)
    require(value.isJsonPrimitive && value.asJsonPrimitive.isNumber) {
        "update manifest field $name must be an integer"
    }
    return value.asString.toLongOrNull()
        ?: throw IllegalArgumentException("update manifest field $name must be an integer")
}

private fun JsonObject.requireInt(name: String): Int {
    val value = requireLong(name)
    require(value in Int.MIN_VALUE.toLong()..Int.MAX_VALUE.toLong()) {
        "update manifest field $name is outside the integer range"
    }
    return value.toInt()
}

private fun JsonObject.requireBoolean(name: String): Boolean {
    val value = requiredValue(name)
    require(value.isJsonPrimitive && value.asJsonPrimitive.isBoolean) {
        "update manifest field $name must be a boolean"
    }
    return value.asBoolean
}

private fun JsonObject.stringListOrEmpty(name: String): List<String> {
    val value = get(name) ?: return emptyList()
    require(value.isJsonArray) { "update manifest field $name must be an array" }
    return value.asJsonArray.mapIndexed { index, item ->
        require(item.isJsonPrimitive && item.asJsonPrimitive.isString) {
            "update manifest field $name[$index] must be a string"
        }
        item.asString.trim()
    }.filter(String::isNotBlank)
}

private const val UPDATE_APK_FILE_NAME = "xiakexing-update.apk"
private const val MAX_UPDATE_MANIFEST_BYTES = 64 * 1024
private val DOWNLOAD_ARTIFACT_SERIAL = AtomicLong(0L)
private const val APK_MIME_TYPE = "application/vnd.android.package-archive"
private const val NETWORK_TIMEOUT_MS = 10_000
private const val DOWNLOAD_TIMEOUT_MS = 60_000
private const val PROGRESS_PERCENT_SCALE = 100L
