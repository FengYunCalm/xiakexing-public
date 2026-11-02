package com.xiakexing.client.feature.shell

internal fun formatMoneyAmount(amount: Int): String {
    if (amount <= 0) return ""
    val gold = amount / 10_000
    val silver = (amount % 10_000) / 100
    val copper = amount % 100
    return buildList {
        if (gold > 0) add("${gold}两黄金")
        if (silver > 0) add("${silver}两银")
        if (copper > 0 || isEmpty()) add("${copper}文")
    }.joinToString(" ")
}
