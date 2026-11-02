package com.xiakexing.client.feature.shell.popup

/**
 * Pure map-atlas geometry shared by the map panel rendering and its unit tests.
 *
 * Coordinate conventions:
 * - [MapPoint] uses relative world coordinates with the player at (0, 0).
 * - [MapConnectionSegment] stores grid coordinates (row = y + radius, column = x + radius).
 */
internal data class MapPoint(
    val x: Int,
    val y: Int,
) {
    operator fun plus(other: MapPoint): MapPoint {
        return MapPoint(x + other.x, y + other.y)
    }

    fun toPathKey(): String {
        return "$x,$y"
    }
}

internal data class MapConnectionSegment(
    val fromRow: Int,
    val fromColumn: Int,
    val toRow: Int,
    val toColumn: Int,
)

internal fun MapPoint.isWithinMapRadius(radius: Int): Boolean {
    return x in -radius..radius && y in -radius..radius
}

internal fun parsePointKey(key: String): MapPoint? {
    val parts = key.trim().split(',', limit = 2)
    if (parts.size != 2) {
        return null
    }
    val x = parts[0].trim().toIntOrNull() ?: return null
    val y = parts[1].trim().toIntOrNull() ?: return null
    return MapPoint(x, y)
}

internal fun directionOffset(direction: String): Pair<Int, Int> {
    return when (direction.trim().lowercase()) {
        "north", "n" -> 0 to -1
        "south", "s" -> 0 to 1
        "east", "e" -> 1 to 0
        "west", "w" -> -1 to 0
        "northeast", "ne" -> 1 to -1
        "northwest", "nw" -> -1 to -1
        "southeast", "se" -> 1 to 1
        "southwest", "sw" -> -1 to 1
        else -> Int.MIN_VALUE to Int.MIN_VALUE
    }
}

internal fun directionMapPointOrNull(direction: String): MapPoint? {
    val offset = directionOffset(direction)
    return if (offset.first == Int.MIN_VALUE) {
        null
    } else {
        MapPoint(offset.first, offset.second)
    }
}

/**
 * Validates one explicit map path against its destination key. Returns the
 * trimmed direction list in original order when the whole path folds exactly
 * to the destination; returns null when the destination key is unparseable,
 * the path is empty, any direction is unknown, or the final point differs.
 * This is the single truth for both connection drawing and clickable actions.
 */
internal fun validateMapPath(
    destinationKey: String,
    path: List<String>,
): List<String>? {
    val destination = parsePointKey(destinationKey) ?: return null
    if (path.isEmpty()) {
        return null
    }
    var current = MapPoint(0, 0)
    val trimmed = mutableListOf<String>()
    for (direction in path) {
        val clean = direction.trim()
        val offset = directionMapPointOrNull(clean) ?: return null
        trimmed += clean
        current += offset
    }
    if (trimmed.isEmpty() || current != destination) {
        return null
    }
    return trimmed
}

/**
 * Builds connection segments for one-step direct exits and explicit multi-step
 * paths. Each path is folded from the center; a path whose final folded point
 * does not match its destination key, or that contains an unresolvable
 * direction, is dropped entirely instead of drawing a guessed line.
 */
internal fun buildMapConnectionSegments(
    explicitPaths: Map<String, List<String>>,
    directExitPoints: Set<MapPoint>,
    occupiedPoints: Set<MapPoint> = emptySet(),
    radius: Int,
): List<MapConnectionSegment> {
    val segments = linkedSetOf<MapConnectionSegment>()

    fun addSegment(
        from: MapPoint,
        to: MapPoint,
    ) {
        if (!from.isWithinMapRadius(radius) || !to.isWithinMapRadius(radius)) {
            return
        }
        val fromRow = from.y + radius
        val fromColumn = from.x + radius
        val toRow = to.y + radius
        val toColumn = to.x + radius
        val normalized =
            if (fromRow < toRow || (fromRow == toRow && fromColumn <= toColumn)) {
                MapConnectionSegment(fromRow, fromColumn, toRow, toColumn)
            } else {
                MapConnectionSegment(toRow, toColumn, fromRow, fromColumn)
            }
        segments += normalized
    }

    directExitPoints.forEach { point -> addSegment(MapPoint(0, 0), point) }
    explicitPaths.forEach { (destinationKey, path) ->
        val validatedPath = validateMapPath(destinationKey, path) ?: return@forEach
        var current = MapPoint(0, 0)
        val pathSegments = mutableListOf<Pair<MapPoint, MapPoint>>()
        for (direction in validatedPath) {
            val next = current + requireNotNull(directionMapPointOrNull(direction))
            pathSegments += current to next
            current = next
        }
        pathSegments.forEach { (from, to) -> addSegment(from, to) }
    }
    // 快照/后端未提供完整 paths 时，仅以已占用的上下左右相邻格补足主干线；
    // 斜向连接必须由后端显式路径提供，避免把城市地图画成八方向网格。
    // 每个点只探东、南邻居，既覆盖每条无向边一次，也避免 O(n²) 的全点配对。
    occupiedPoints.forEach { from ->
        val east = MapPoint(from.x + 1, from.y)
        if (east in occupiedPoints) {
            addSegment(from, east)
        }
        val south = MapPoint(from.x, from.y + 1)
        if (south in occupiedPoints) {
            addSegment(from, south)
        }
    }
    return segments.toList()
}
