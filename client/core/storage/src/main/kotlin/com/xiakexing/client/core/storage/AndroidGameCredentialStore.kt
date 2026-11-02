package com.xiakexing.client.core.storage

import android.annotation.SuppressLint
import android.content.Context
import android.os.Build
import android.provider.Settings
import android.util.Base64
import androidx.annotation.RequiresApi
import com.xiakexing.client.core.network.CredentialStoreException
import com.xiakexing.client.core.network.CredentialStoreFailureKind
import com.xiakexing.client.core.network.GameCredentialStore
import com.xiakexing.client.core.network.GameCredentials
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.nio.charset.StandardCharsets
import java.security.KeyStore
import java.security.MessageDigest
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * Application-owned credential storage.
 *
 * API 23+ uses an Android Keystore AES-GCM key. On API 21-22 a password is
 * never persisted: saving keeps only the username and removes any old
 * password value. A legacy v1 payload is migrated only after a successful v2
 * write; malformed or undecryptable data is reported instead of being
 * returned as plaintext or silently replaced with empty credentials.
 */
class AndroidGameCredentialStore(context: Context) : GameCredentialStore {
    private val appContext = context.applicationContext
    private val operationMutex = Mutex()
    private val preferences =
        appContext.getSharedPreferences(
            PREFERENCES_NAME,
            Context.MODE_PRIVATE,
        )

    override suspend fun load(): GameCredentials =
        operationMutex.withLock {
            withContext(Dispatchers.IO) {
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
                    loadLegacyPlatformCredentials()
                } else {
                    loadKeystoreCredentials()
                }
            }
        }

    override suspend fun save(
        username: String,
        password: String,
    ) = operationMutex.withLock {
        withContext(Dispatchers.IO) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
                val saved =
                    preferences.edit()
                        .putString(KEY_USERNAME, username.trim())
                        .remove(KEY_PASSWORD)
                        .commit()
                if (!saved) {
                    throw CredentialStoreException(
                        "unable to persist API 21-22 credential policy",
                        kind = CredentialStoreFailureKind.COMMIT_REJECTED,
                    )
                }
            } else {
                val usernamePayload = encryptV2(username.trim())
                val passwordPayload = encryptV2(password)
                val saved =
                    preferences.edit()
                        .putString(KEY_USERNAME, usernamePayload)
                        .putString(KEY_PASSWORD, passwordPayload)
                        .remove(KEY_LEGACY_USERNAME)
                        .remove(KEY_LEGACY_PASSWORD)
                        .commit()
                if (!saved) {
                    throw CredentialStoreException(
                        "unable to persist encrypted credentials",
                        kind = CredentialStoreFailureKind.COMMIT_REJECTED,
                    )
                }
            }
        }
    }

    private fun loadLegacyPlatformCredentials(): GameCredentials {
        val storedUsername = preferences.getString(KEY_USERNAME, "").orEmpty()
        val username =
            if (storedUsername.isBlank()) {
                ""
            } else if (storedUsername.startsWith(VERSION_LEGACY + PAYLOAD_SEPARATOR)) {
                decryptLegacy(storedUsername)
            } else {
                storedUsername
            }
        val hadPassword = !preferences.getString(KEY_PASSWORD, "").orEmpty().isNullOrBlank()
        if (hadPassword) {
            val removed = preferences.edit().remove(KEY_PASSWORD).commit()
            if (!removed) {
                throw CredentialStoreException(
                    "unable to clear unsupported API 21-22 password",
                    kind = CredentialStoreFailureKind.COMMIT_REJECTED,
                )
            }
        }
        return GameCredentials(username = username, password = "")
    }

    @RequiresApi(Build.VERSION_CODES.M)
    private fun loadKeystoreCredentials(): GameCredentials {
        val usernamePayload = preferences.getString(KEY_USERNAME, "").orEmpty()
        val passwordPayload = preferences.getString(KEY_PASSWORD, "").orEmpty()
        val username = decryptPayload(usernamePayload, KEY_USERNAME)
        val password = decryptPayload(passwordPayload, KEY_PASSWORD)
        if (username.migrated || password.migrated) {
            migrateLegacyPayloads(username.value, password.value)
        }
        return GameCredentials(username.value, password.value)
    }

    @RequiresApi(Build.VERSION_CODES.M)
    private fun migrateLegacyPayloads(
        username: String,
        password: String,
    ) {
        val saved =
            preferences.edit()
                .putString(KEY_USERNAME, encryptV2(username))
                .putString(KEY_PASSWORD, encryptV2(password))
                .remove(KEY_LEGACY_USERNAME)
                .remove(KEY_LEGACY_PASSWORD)
                .commit()
        if (!saved) {
            throw CredentialStoreException(
                "unable to commit legacy credential migration",
                kind = CredentialStoreFailureKind.COMMIT_REJECTED,
            )
        }
    }

    private data class DecryptedPayload(
        val value: String,
        val migrated: Boolean,
    )

    @RequiresApi(Build.VERSION_CODES.M)
    private fun decryptPayload(
        storedValue: String,
        keyName: String,
    ): DecryptedPayload {
        if (storedValue.isBlank()) return DecryptedPayload("", false)
        return when {
            storedValue.startsWith(VERSION_KEYSTORE + PAYLOAD_SEPARATOR) ->
                DecryptedPayload(decryptV2(storedValue), false)
            storedValue.startsWith(VERSION_LEGACY + PAYLOAD_SEPARATOR) ->
                DecryptedPayload(decryptLegacy(storedValue), true)
            else ->
                throw CredentialStoreException(
                    "unknown credential payload version for $keyName",
                    kind = CredentialStoreFailureKind.CORRUPT_PAYLOAD,
                )
        }
    }

    @RequiresApi(Build.VERSION_CODES.M)
    private fun encryptV2(value: String): String {
        if (value.isEmpty()) return ""
        return try {
            val cipher = Cipher.getInstance(KEYSTORE_TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKeystoreKey())
            encodePayload(VERSION_KEYSTORE, cipher.iv, cipher.doFinal(value.toByteArray(UTF_8)))
        } catch (error: Exception) {
            throw CredentialStoreException(
                "unable to encrypt credential payload",
                error,
                CredentialStoreFailureKind.STORAGE_UNAVAILABLE,
            )
        }
    }

    @RequiresApi(Build.VERSION_CODES.M)
    private fun decryptV2(storedValue: String): String {
        return try {
            val parts = splitPayload(storedValue)
            val cipher = Cipher.getInstance(KEYSTORE_TRANSFORMATION)
            cipher.init(
                Cipher.DECRYPT_MODE,
                getOrCreateKeystoreKey(),
                GCMParameterSpec(
                    GCM_TAG_LENGTH_BITS,
                    decode(parts[1]),
                ),
            )
            String(cipher.doFinal(decode(parts[2])), UTF_8)
        } catch (error: Exception) {
            throw CredentialStoreException(
                "unable to decrypt credential payload",
                error,
                CredentialStoreFailureKind.CORRUPT_PAYLOAD,
            )
        }
    }

    private fun decryptLegacy(storedValue: String): String {
        return try {
            val parts = splitPayload(storedValue)
            val cipher = Cipher.getInstance(LEGACY_TRANSFORMATION)
            cipher.init(
                Cipher.DECRYPT_MODE,
                legacySecretKey(),
                IvParameterSpec(decode(parts[1])),
            )
            String(cipher.doFinal(decode(parts[2])), UTF_8)
        } catch (error: Exception) {
            throw CredentialStoreException(
                "unable to decrypt legacy credential payload",
                error,
                CredentialStoreFailureKind.CORRUPT_PAYLOAD,
            )
        }
    }

    private fun splitPayload(storedValue: String): List<String> {
        val parts = storedValue.split(PAYLOAD_SEPARATOR)
        if (parts.size != 3 || parts.any(String::isBlank)) {
            throw CredentialStoreException(
                "malformed credential payload",
                kind = CredentialStoreFailureKind.CORRUPT_PAYLOAD,
            )
        }
        return parts
    }

    private fun decode(value: String): ByteArray =
        try {
            Base64.decode(value, Base64.NO_WRAP)
        } catch (error: IllegalArgumentException) {
            throw CredentialStoreException(
                "invalid credential payload",
                error,
                CredentialStoreFailureKind.CORRUPT_PAYLOAD,
            )
        }

    @RequiresApi(Build.VERSION_CODES.M)
    private fun getOrCreateKeystoreKey(): SecretKey {
        val keyStore = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        val existingKey =
            (keyStore.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.secretKey
        if (existingKey != null) return existingKey

        val keyGenerator =
            KeyGenerator.getInstance(
                KEY_ALGORITHM_AES,
                ANDROID_KEYSTORE,
            )
        keyGenerator.init(
            android.security.keystore.KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                android.security.keystore.KeyProperties.PURPOSE_ENCRYPT or
                    android.security.keystore.KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(android.security.keystore.KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(
                    android.security.keystore.KeyProperties.ENCRYPTION_PADDING_NONE,
                )
                .build(),
        )
        return keyGenerator.generateKey()
    }

    @SuppressLint("HardwareIds")
    private fun legacySecretKey(): SecretKey {
        val androidId =
            Settings.Secure.getString(
                appContext.contentResolver,
                Settings.Secure.ANDROID_ID,
            ).orEmpty()
        val seed = "$androidId:${appContext.packageName}:xkx-shell"
        val hash = MessageDigest.getInstance("SHA-256").digest(seed.toByteArray(UTF_8))
        return SecretKeySpec(hash.copyOf(16), KEY_ALGORITHM_AES)
    }

    private fun encodePayload(
        version: String,
        iv: ByteArray,
        payload: ByteArray,
    ): String {
        return listOf(version, encode(iv), encode(payload)).joinToString(PAYLOAD_SEPARATOR)
    }

    private fun encode(bytes: ByteArray): String {
        return Base64.encodeToString(bytes, Base64.NO_WRAP)
    }

    private companion object {
        const val PREFERENCES_NAME = "shell_credentials"
        const val KEY_USERNAME = "username"
        const val KEY_PASSWORD = "password"
        const val KEY_LEGACY_USERNAME = "legacy_username"
        const val KEY_LEGACY_PASSWORD = "legacy_password"
        const val KEY_ALIAS = "xkx_shell_credentials"
        const val KEY_ALGORITHM_AES = "AES"
        const val ANDROID_KEYSTORE = "AndroidKeyStore"
        const val KEYSTORE_TRANSFORMATION = "AES/GCM/NoPadding"
        const val LEGACY_TRANSFORMATION = "AES/CBC/PKCS5Padding"
        const val GCM_TAG_LENGTH_BITS = 128
        const val PAYLOAD_SEPARATOR = ":"
        const val VERSION_KEYSTORE = "v2"
        const val VERSION_LEGACY = "v1"
        val UTF_8 = StandardCharsets.UTF_8
    }
}
