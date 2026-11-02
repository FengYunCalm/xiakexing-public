package com.xiakexing.client.feature.shell

/*
 * 游戏内十二时辰时钟。
 * 服务端 TIME_D 权威（1 现实分钟 = 1 时辰），随 STAT 包附载当前值；
 * 客户端按确定速率本地插值刷新，每个到达的 STAT 包重置基准（包校正语义）。
 */

internal val SHICHEN_SEQUENCE: List<String> =
    listOf(
        "子时", "丑时", "寅时", "卯时", "辰时", "巳时",
        "午时", "未时", "申时", "酉时", "戌时", "亥时",
    )

/** 从线缆值求序列 index，未知值返回 -1（调用方应原样显示并等待下一包校正）。 */
internal fun shichenIndexOf(current: String): Int = SHICHEN_SEQUENCE.indexOf(current)

/** 按流逝分钟推进时辰（服务端速率：1 现实分钟 = 1 时辰）。 */
internal fun advanceShichen(
    current: String,
    elapsedMinutes: Long,
): String {
    if (elapsedMinutes <= 0) {
        return current
    }
    val idx = shichenIndexOf(current)
    if (idx < 0) {
        return current
    }
    val advanced = idx + elapsedMinutes
    return SHICHEN_SEQUENCE[((advanced % SHICHEN_SEQUENCE.size) + SHICHEN_SEQUENCE.size).toInt() % SHICHEN_SEQUENCE.size]
}
