package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject

data class MapGridPacket(
    val title: String,
    val zone: String,
    val rows: List<String>,
    val width: Int,
    val height: Int,
    val legend: List<String>,
    val paths: Map<String, List<String>> = emptyMap(),
    val rooms: Map<String, String> = emptyMap(),
    val questMarkers: Set<String> = emptySet(),
    val questMarkerDetails: List<QuestMarkerDetailPacket> = emptyList(),
    val projectionId: String = "",
    val projectionRevision: Long = 0L,
    val roomId: String = "",
    val mapRevision: Long = 0L,
    val regionId: String = "",
    val mapContextId: String = "",
    val mapLayer: String = "",
    val submapId: String = "",
) {
    companion object {
        fun fromJson(payload: JsonObject): MapGridPacket {
            val rows = payload.mapRows()
            val width =
                payload.int("width").takeIf { it > 0 } ?: rows.maxOfOrNull {
                        row ->
                    row.length
                } ?: 0
            val height = payload.int("height").takeIf { it > 0 } ?: rows.size
            return MapGridPacket(
                title = payload.string("title"),
                zone = payload.string("zone"),
                rows = rows,
                width = width,
                height = height,
                legend = payload.mapLegend(),
                paths = payload.mapPaths(),
                rooms = payload.stringMap("rooms"),
                questMarkers = payload.stringArray("quest_markers").toSet(),
                questMarkerDetails = payload.questMarkerDetails(),
                projectionId = payload.string("projection_id"),
                projectionRevision = payload.mapLong("projection_revision"),
                roomId = payload.string("room_id"),
                mapRevision = payload.mapLong("map_revision"),
                regionId = payload.string("region_id"),
                mapContextId = payload.string("map_context_id"),
                mapLayer = payload.string("map_layer"),
                submapId = payload.string("submap_id"),
            )
        }
    }
}

data class QuestMarkerDetailPacket(
    val coord: String,
    val entryRoomId: String,
    val targetRoomId: String,
    val entryCommand: String,
    val entryDirection: String,
    val label: String,
    val questIds: List<String>,
)

data class RoutePreviewPacket(
    val protocolVersion: String = "",
    val requestId: String = "",
    val projection: ProjectionIdentity? = null,
    val sourceKind: String = "",
    val objectiveId: String = "",
    val acceptanceId: String = "",
    val acceptanceRevision: Long = 0L,
    val startRoomId: String = "",
    val targetRoomId: String,
    val targetRoomName: String,
    val ok: Boolean,
    val error: String,
    val commands: List<String>,
    val rooms: List<String>,
    val maxSteps: Int,
    val steps: List<RouteStepPacket> = emptyList(),
    val transferCount: Int = 0,
    val topologyGeneration: Long = 0L,
) {
    val isV2: Boolean
        get() = projection?.isV2 == true && requestId.isNotBlank()

    companion object {
        fun fromJson(payload: JsonObject): RoutePreviewPacket {
            return RoutePreviewPacket(
                protocolVersion = payload.string("protocol_version"),
                requestId = payload.string("request_id"),
                projection = ProjectionIdentity.fromJsonOrNull(payload),
                sourceKind = payload.string("source_kind"),
                objectiveId = payload.string("objective_id"),
                acceptanceId = payload.string("acceptance_id"),
                acceptanceRevision = payload.payloadLong("acceptance_revision").coerceAtLeast(0L),
                startRoomId = payload.string("start_room_id"),
                targetRoomId = payload.string("target_room_id"),
                targetRoomName = payload.string("target_room_name"),
                ok = payload.bool("ok"),
                error = payload.string("error"),
                commands = payload.stringArray("commands"),
                rooms = payload.stringArray("rooms"),
                maxSteps = payload.int("max_steps"),
                steps =
                    payload.array("steps").mapNotNull { element ->
                        element.takeIf { it.isJsonObject }?.asJsonObject?.let(
                            RouteStepPacket::fromJson,
                        )
                    },
                transferCount = payload.int("transfer_count"),
                topologyGeneration = payload.mapLong("topology_generation"),
            )
        }
    }
}

data class RouteStepPacket(
    val kind: String = "",
    val edgeType: String = "",
    val from: String = "",
    val to: String = "",
    val direction: String = "",
    val transitionId: String = "",
    val sourceRegion: String = "",
    val targetRegion: String = "",
    val targetRoom: String = "",
    val transferGeneration: Long = 0L,
) {
    companion object {
        fun fromJson(payload: JsonObject): RouteStepPacket {
            val transition =
                payload.get("transition")
                    ?.takeIf { it.isJsonObject }
                    ?.asJsonObject
            return RouteStepPacket(
                kind = payload.string("kind"),
                edgeType = payload.string("edge_type"),
                from = payload.string("from"),
                to = payload.string("to"),
                direction = payload.string("direction"),
                transitionId = transition?.string("id") ?: "",
                sourceRegion = transition?.string("source_region") ?: "",
                targetRegion = transition?.string("target_region") ?: "",
                targetRoom = transition?.string("target_room") ?: "",
                transferGeneration = transition?.mapLong("transfer_generation") ?: 0L,
            )
        }
    }
}

private fun JsonObject.mapLong(key: String): Long {
    return get(key)
        ?.takeUnless { it.isJsonNull }
        ?.takeIf { it.isJsonPrimitive && it.asJsonPrimitive.isNumber }
        ?.asLong
        ?: 0L
}

private fun JsonObject.mapRows(): List<String> {
    val directRows = stringArray("rows")
    if (directRows.isNotEmpty()) {
        return directRows
    }

    val directGrid = stringArray("grid")
    if (directGrid.isNotEmpty()) {
        return directGrid
    }

    val gridRows = array("grid").mapNotNull { row -> row.toMapRowOrNull() }
    if (gridRows.isNotEmpty()) {
        return gridRows
    }

    val cellRows = array("cells").mapNotNull { row -> row.toMapRowOrNull() }
    return cellRows
}

private fun JsonObject.mapLegend(): List<String> {
    val legendEntryObject =
        if (has("legend") && get("legend").isJsonObject) {
            getAsJsonObject(
                "legend",
            )
        } else {
            JsonObject()
        }
    if (legendEntryObject.size() > 0) {
        return legendEntryObject.entrySet()
            .mapNotNull { (key, value) ->
                value.takeIf { !it.isJsonNull }?.asString?.trim()?.takeIf {
                        text ->
                    text.isNotBlank()
                }?.let { text -> "$key: $text" }
            }
    }
    return stringArray("legend")
}

private fun JsonObject.mapPaths(): Map<String, List<String>> {
    val pathsObject =
        if (has("paths") && get("paths").isJsonObject) {
            getAsJsonObject(
                "paths",
            )
        } else {
            JsonObject()
        }
    if (pathsObject.size() == 0) {
        return emptyMap()
    }
    return pathsObject.entrySet().mapNotNull { (key, value) ->
        val path = value.toDirectionList()
        if (key.isBlank() || path.isEmpty()) null else key to path
    }.toMap()
}

private fun JsonObject.questMarkerDetails(): List<QuestMarkerDetailPacket> {
    return array("quest_marker_details").mapNotNull { element ->
        if (!element.isJsonObject) {
            return@mapNotNull null
        }
        val entry = element.asJsonObject
        val coord = entry.string("coord")
        if (coord.isBlank()) {
            null
        } else {
            QuestMarkerDetailPacket(
                coord = coord,
                entryRoomId = entry.string("entry_room_id"),
                targetRoomId = entry.string("target_room_id"),
                entryCommand = entry.string("entry_command"),
                entryDirection = entry.string("entry_direction"),
                label = entry.string("label"),
                questIds = entry.stringArray("quest_ids"),
            )
        }
    }
}

private fun JsonElement.toDirectionList(): List<String> {
    return when {
        isJsonNull -> emptyList()
        isJsonArray ->
            asJsonArray.mapNotNull { element ->
                element.takeIf { it.isJsonPrimitive && !it.isJsonNull }
                    ?.asString
                    ?.trim()
                    ?.takeIf { direction -> direction.isNotBlank() }
            }
        isJsonPrimitive ->
            asString
                .split(';', ',', ' ')
                .map { it.trim() }
                .filter { it.isNotBlank() }
        else -> emptyList()
    }
}

private fun JsonObject.stringMap(key: String): Map<String, String> {
    val objectValue = if (has(key) && get(key).isJsonObject) getAsJsonObject(key) else JsonObject()
    if (objectValue.size() == 0) {
        return emptyMap()
    }
    return objectValue.entrySet().mapNotNull { (entryKey, value) ->
        val text =
            value.takeIf { !it.isJsonNull && it.isJsonPrimitive }
                ?.asString
                ?.trim()
                .orEmpty()
        if (entryKey.isBlank() || text.isBlank()) null else entryKey to text
    }.toMap()
}

private fun JsonElement.toMapRowOrNull(): String? {
    return when {
        isJsonNull -> null
        isJsonPrimitive -> asString.trim().takeIf { it.isNotBlank() }
        isJsonObject -> {
            val row =
                asJsonObject.string("row")
                    .ifBlank { asJsonObject.string("text") }
                    .ifBlank { asJsonObject.string("line") }
                    .trim()
            row.takeIf { it.isNotBlank() }
        }
        isJsonArray -> {
            val parts =
                asJsonArray.mapNotNull { cell ->
                    when {
                        cell.isJsonNull -> null
                        cell.isJsonPrimitive -> cell.asString
                        cell.isJsonObject ->
                            cell.asJsonObject.string("char")
                                .ifBlank { cell.asJsonObject.string("text") }
                                .ifBlank { cell.asJsonObject.string("label") }
                                .ifBlank { cell.asJsonObject.string("tile") }
                        else -> null
                    }
                }
            parts.joinToString(separator = "").trimEnd().takeIf { it.isNotBlank() }
        }
        else -> null
    }
}

private fun JsonObject.array(key: String): JsonArray {
    return payloadArray(key)
}

private fun JsonObject.string(key: String): String {
    return payloadString(key)
}

private fun JsonObject.int(key: String): Int {
    return payloadInt(key)
}

private fun JsonObject.stringArray(key: String): List<String> {
    return array(key).mapNotNull { element ->
        element.takeIf { it.isJsonPrimitive && !it.isJsonNull }?.asString?.trim()?.takeIf {
                value ->
            value.isNotBlank()
        }
    }
}

private fun JsonObject.bool(key: String): Boolean {
    if (!has(key) || get(key).isJsonNull) {
        return false
    }
    val value = get(key).asJsonPrimitive
    return if (value.isBoolean) value.asBoolean else payloadLong(key) != 0L
}
