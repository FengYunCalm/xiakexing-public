package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable
import com.xiakexing.client.core.protocol.payload.ProjectionStatePacket

/**
 * 公共作用域运行时状态：只含 REAL 投影身份。
 */
@Immutable
data class ProjectionRuntimeState(
    val projection: ProjectionStatePacket? = null,
)
