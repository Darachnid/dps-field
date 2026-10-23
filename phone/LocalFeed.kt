// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Darachnid
// Original companion code. Not part of the Dead Packet Society application.

package com.deadpacketsociety.fieldcontroller

import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.nio.charset.StandardCharsets

/**
 * Loopback JSON feed for the watch companion.
 * Binds to 127.0.0.1 only, so the monitor log stays on the phone.
 */
class LocalFeed(private val snapshot: FieldSnapshot) {

    private var server: ServerSocket? = null
    @Volatile private var running = false

    fun start() {
        if (running) {
            return
        }
        val socket = ServerSocket()
        socket.reuseAddress = true
        socket.bind(InetSocketAddress(InetAddress.getByName("127.0.0.1"), PORT))
        server = socket
        running = true
        Thread({
            while (running && !socket.isClosed) {
                try {
                    val client = socket.accept()
                    serve(client)
                } catch (_: Exception) {
                    if (!running || socket.isClosed) {
                        break
                    }
                }
            }
        }, "dps-feed").apply {
            isDaemon = true
            start()
        }
    }

    fun stop() {
        running = false
        try {
            server?.close()
        } catch (_: Exception) {
        }
        server = null
    }

    private fun serve(socket: Socket) {
        socket.use { client ->
            client.soTimeout = 2000
            val reader = client.getInputStream().bufferedReader()
            val request = try {
                reader.readLine()
            } catch (_: Exception) {
                null
            } ?: return
            try {
                while (true) {
                    val line = reader.readLine() ?: break
                    if (line.isEmpty()) {
                        break
                    }
                }
            } catch (_: Exception) {
            }
            val ok = request.startsWith("GET ")
            val body = if (ok) snapshot.json() else ""
            val status = if (ok) "200 OK" else "404 Not Found"
            val bytes = body.toByteArray(StandardCharsets.UTF_8)
            val header =
                "HTTP/1.0 $status\r\n" +
                    "Content-Type: application/json\r\n" +
                    "Content-Length: ${bytes.size}\r\n" +
                    "Connection: close\r\n\r\n"
            val out = client.getOutputStream()
            out.write(header.toByteArray(StandardCharsets.US_ASCII))
            out.write(bytes)
            out.flush()
        }
    }

    companion object {
        const val PORT = 4774
    }
}
