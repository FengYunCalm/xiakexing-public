package com.xiakexing.client.connect

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.xiakexing.client.core.network.ConnectionAttachment
import com.xiakexing.client.core.network.ProcessConnectionOwnerProvider

/** Android host for the process-owned connection graph. */
class GameConnectionService : Service() {
    private var serviceAttachment: ConnectionAttachment? = null

    private val processOwner
        get() =
            (application as? ProcessConnectionOwnerProvider)?.processConnectionOwner
                ?: error("Application must provide ProcessConnectionOwner")

    override fun onCreate() {
        super.onCreate()
        ensureChannel()
        // The Service only hosts the already-created process owner. It does not
        // create a repository, credential store, scope, or endpoint.
        serviceAttachment = processOwner.attachService()
    }

    override fun onDestroy() {
        serviceAttachment?.close()
        serviceAttachment = null
        super.onDestroy()
    }

    override fun onStartCommand(
        intent: Intent?,
        flags: Int,
        startId: Int,
    ): Int {
        startForeground(NOTIFICATION_ID, buildNotification())
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun ensureChannel() {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.O) {
            return
        }
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (manager.getNotificationChannel(CHANNEL_ID) == null) {
            val channel =
                NotificationChannel(
                    CHANNEL_ID,
                    "游戏连接保活",
                    NotificationManager.IMPORTANCE_MIN,
                )
            channel.description = "保持在后台接收游戏消息"
            manager.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(): Notification {
        val builder = NotificationCompat.Builder(this, CHANNEL_ID)
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.O) {
            builder.setPriority(NotificationCompat.PRIORITY_MIN)
        }
        return builder
            .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
            .setContentTitle("侠客行")
            .setContentText("连接保持中，后台持续接收消息")
            .setOngoing(true)
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "game_connection_keepalive"
        private const val NOTIFICATION_ID = 1001

        fun start(context: Context) {
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                context.startForegroundService(Intent(context, GameConnectionService::class.java))
            } else {
                context.startService(Intent(context, GameConnectionService::class.java))
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, GameConnectionService::class.java))
        }
    }
}
