// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Darachnid
// Original companion code. Not part of the Dead Packet Society application.

package com.deadpacketsociety.fieldcontroller

import org.json.JSONObject
import java.util.Locale
import kotlin.math.roundToInt

/**
 * Session totals for the watch companion.
 *
 * Unique Wi-Fi identities are BSSIDs. Unique Bluetooth identities are
 * BLE addresses. 2.4 and 5 count Wi-Fi only; BLE stays in the BT total.
 * The per-minute rate is unique hits in the last 15 seconds, times four.
 */
class FieldSnapshot {

    private class NodeFace(val slot: Int) {
        var id: String = "DPS-%02d".format(slot)
        var online: Boolean = false
        var role: Int = 0
        var channels: IntArray = IntArray(0)
        var roleFromNode: Boolean = false
        var scanState: Int = -1
        val wifi = LinkedHashSet<String>()
        val bt = LinkedHashSet<String>()
        val g24 = LinkedHashSet<String>()
        val g5 = LinkedHashSet<String>()
        var lastName: String = ""
    }

    private val lock = Any()
    private val wifiIds = LinkedHashSet<String>()
    private val btIds = LinkedHashSet<String>()
    private val g24Ids = LinkedHashSet<String>()
    private val g5Ids = LinkedHashSet<String>()
    private val hits = ArrayDeque<Long>()
    private val logs = ArrayDeque<String>()
    private val nodes = Array(SLOT_COUNT) { index -> NodeFace(index + 1) }

    private var sessionStart = 0L
    private var sessionEnd = 0L
    private var scanning = false
    private var lastHit = 0L

    fun beginSession() {
        synchronized(lock) {
            wifiIds.clear()
            btIds.clear()
            g24Ids.clear()
            g5Ids.clear()
            hits.clear()
            lastHit = 0L
            sessionStart = System.currentTimeMillis()
            sessionEnd = 0L
            scanning = true
            for (face in nodes) {
                face.wifi.clear()
                face.bt.clear()
                face.g24.clear()
                face.g5.clear()
                face.lastName = ""
            }
        }
    }

    fun setScanning(on: Boolean) {
        synchronized(lock) {
            if (scanning && !on) {
                sessionEnd = System.currentTimeMillis()
            }
            scanning = on
        }
    }

    fun noteWifi(
        node: Int,
        bssid: String,
        frequency: Int,
        channel: Int,
        name: String
    ) {
        val id = bssid.trim()
        if (id.isEmpty()) {
            return
        }
        synchronized(lock) {
            val five = isFiveGhz(node, frequency, channel)
            wifiIds.add(id)
            if (five) {
                g5Ids.add(id)
            } else {
                g24Ids.add(id)
            }
            recordHit(System.currentTimeMillis())
            val face = face(node) ?: return
            face.wifi.add(id)
            if (five) {
                face.g5.add(id)
            } else {
                face.g24.add(id)
            }
            if (name.isNotBlank()) {
                face.lastName = name.trim()
            }
        }
    }

    fun noteBle(node: Int, mac: String, name: String) {
        val id = mac.trim()
        if (id.isEmpty()) {
            return
        }
        synchronized(lock) {
            btIds.add(id)
            recordHit(System.currentTimeMillis())
            val face = face(node) ?: return
            face.bt.add(id)
            if (name.isNotBlank()) {
                face.lastName = name.trim()
            }
        }
    }

    fun noteNodeReport(json: JSONObject) {
        val node = json.optInt("node", json.optInt("slot", 0))
        val face = face(node) ?: return
        val role = roleCode(json.opt("role"))
        val channels = readChannels(json)
        synchronized(lock) {
            if (role in 1..3) {
                face.role = role
                face.roleFromNode = true
                face.channels = channels
            }
            if (json.has("scanning")) {
                face.scanState = if (json.optBoolean("scanning", false)) 1 else 0
            }
        }
    }

    fun syncNode(slot: Int, id: String, online: Boolean) {
        val face = face(slot) ?: return
        synchronized(lock) {
            if (id.isNotBlank()) {
                face.id = clean(id, 12)
            }
            face.online = online
        }
    }

    fun noteLog(line: String) {
        synchronized(lock) {
            logs.addLast(line)
            while (logs.size > LOG_LIMIT) {
                logs.removeFirst()
            }
        }
    }

    fun json(): String {
        synchronized(lock) {
            val now = System.currentTimeMillis()
            prune(now)
            val units = wifiIds.size + btIds.size
            val window = hits.size
            val rate = window * (60_000L / WINDOW_MS).toInt()
            val sum = listOf(
                units.toString(),
                since(lastHit, now),
                average(now),
                g24Ids.size.toString(),
                g5Ids.size.toString(),
                wifiIds.size.toString(),
                btIds.size.toString(),
                rate.toString(),
                window.toString(),
                if (scanning) "1" else "0"
            ).joinToString("|")

            val nodeBody = nodes.joinToString("\n") { face -> face.line() }
            val logBody = logs.takeLast(LOG_WATCH).joinToString("\n") { watchLogLine(it) }

            return JSONObject()
                .put("sum", sum)
                .put("nodes", nodeBody)
                .put("log", logBody)
                .toString()
        }
    }

    private fun NodeFace.line(): String {
        val scan = when (scanState) {
            0 -> "0"
            1 -> "1"
            else -> ""
        }
        return listOf(
            id,
            if (online) "1" else "0",
            clean(roleLabel(), 22),
            wifi.size.toString(),
            bt.size.toString(),
            g24.size.toString(),
            g5.size.toString(),
            clean(lastName, 16).ifBlank { "-" },
            scan
        ).joinToString("^")
    }

    private fun NodeFace.roleLabel(): String {
        val useRole = if (role == 0) defaultRole(slot) else role
        val useChannels = when {
            useRole == ROLE_BLE -> IntArray(0)
            roleFromNode -> channels
            else -> defaultChannels(slot)
        }
        val name = when (useRole) {
            ROLE_WIFI_24 -> "2.4 GHz"
            ROLE_WIFI_5 -> "5 GHz"
            ROLE_BLE -> "BLE"
            else -> "—"
        }
        val span = channelSpan(useChannels)
        return if (span.isEmpty()) name else "$name $span"
    }

    private fun face(node: Int): NodeFace? {
        if (node < 1 || node > nodes.size) {
            return null
        }
        return nodes[node - 1]
    }

    private fun isFiveGhz(node: Int, frequency: Int, channel: Int): Boolean {
        if (frequency >= 3000) {
            return true
        }
        if (frequency in 1..2999) {
            return false
        }
        if (channel >= 32) {
            return true
        }
        if (channel in 1..14) {
            return false
        }
        val face = face(node)
        val role = when {
            face == null -> ROLE_WIFI_24
            face.role == 0 -> defaultRole(face.slot)
            else -> face.role
        }
        return role == ROLE_WIFI_5
    }

    private fun recordHit(now: Long) {
        if (sessionStart == 0L) {
            sessionStart = now
        }
        lastHit = now
        hits.addLast(now)
        prune(now)
    }

    private fun prune(now: Long) {
        while (hits.isNotEmpty() && now - hits.first() > WINDOW_MS) {
            hits.removeFirst()
        }
    }

    private fun average(now: Long): String {
        val units = wifiIds.size + btIds.size
        if (sessionStart == 0L || units == 0) {
            return "0"
        }
        val end = if (scanning || sessionEnd == 0L) now else sessionEnd
        val elapsed = (end - sessionStart).coerceAtLeast(1000L)
        return formatRate(units * 60_000.0 / elapsed)
    }

    private fun since(at: Long, now: Long): String {
        if (at <= 0L) {
            return "--"
        }
        val mins = ((now - at) / 60_000L).toInt().coerceAtLeast(0)
        if (mins < 1) {
            return "<1m"
        }
        if (mins < 60) {
            return "${mins}m"
        }
        val hours = mins / 60
        val rem = mins % 60
        if (hours < 48) {
            return if (rem == 0) "${hours}h" else "${hours}h ${rem}m"
        }
        val days = hours / 24
        val rest = hours % 24
        return if (rest == 0) "${days}d" else "${days}d ${rest}h"
    }

    private fun roleCode(raw: Any?): Int {
        return when (raw) {
            is Int -> raw
            is Number -> raw.toInt()
            is String -> when (raw.trim().lowercase(Locale.US)) {
                "wifi24", "wifi_24", "2.4" -> ROLE_WIFI_24
                "wifi5", "wifi_5", "5" -> ROLE_WIFI_5
                "ble", "bt" -> ROLE_BLE
                else -> raw.toIntOrNull() ?: 0
            }
            else -> 0
        }
    }

    private fun readChannels(json: JSONObject): IntArray {
        val arr = json.optJSONArray("channels") ?: return IntArray(0)
        val out = ArrayList<Int>(arr.length())
        for (index in 0 until arr.length()) {
            val value = arr.optInt(index, 0)
            if (value > 0) {
                out.add(value)
            }
            if (out.size >= 64) {
                break
            }
        }
        return out.toIntArray()
    }

    private fun watchLogLine(raw: String): String {
        val text = raw.trim()
        val body = if (text.length >= 11 && text[0] == '[' && text[9] == ']') {
            text.substring(1, 9) + " " + text.substring(11).trim()
        } else {
            text
        }
        return clean(body, 40)
    }

    private fun clean(value: String, max: Int): String {
        val out = StringBuilder(max)
        for (ch in value.trim()) {
            if (out.length >= max) {
                break
            }
            val safe = if (ch == '^' || ch == '|' || ch == '\n' || ch == '\r') ' ' else ch
            out.append(safe)
        }
        return out.toString().trim()
    }

    companion object {
        const val SLOT_COUNT = 10
        private const val LOG_LIMIT = 80
        private const val LOG_WATCH = 11
        private const val WINDOW_MS = 15_000L
        private const val ROLE_WIFI_24 = 1
        private const val ROLE_WIFI_5 = 2
        private const val ROLE_BLE = 3

        private fun defaultRole(slot: Int): Int {
            return when (slot) {
                1, 2 -> ROLE_WIFI_24
                3 -> ROLE_WIFI_5
                else -> ROLE_BLE
            }
        }

        private fun defaultChannels(slot: Int): IntArray {
            return when (slot) {
                1 -> (1..7).toList().toIntArray()
                2 -> (8..13).toList().toIntArray()
                3 -> intArrayOf(
                    36, 40, 44, 48,
                    52, 56, 60, 64,
                    100, 104, 108, 112,
                    116, 120, 124, 128,
                    132, 136, 140, 144,
                    149, 153, 157, 161, 165
                )
                else -> IntArray(0)
            }
        }

        private fun channelSpan(channels: IntArray): String {
            val sorted = channels.filter { it > 0 }.distinct().sorted()
            if (sorted.isEmpty()) {
                return ""
            }
            val contiguous = sorted.last() - sorted.first() + 1 == sorted.size
            return if (contiguous) {
                "${sorted.first()}-${sorted.last()}"
            } else {
                sorted.take(4).joinToString(",")
            }
        }

        private fun formatRate(perMin: Double): String {
            if (perMin < 10.0) {
                val text = String.format(Locale.US, "%.1f", perMin)
                return if (text.endsWith(".0")) text.dropLast(2) else text
            }
            return perMin.roundToInt().toString()
        }
    }
}
