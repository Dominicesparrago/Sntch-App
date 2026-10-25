package com.sntch.app.data

import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** The controller answered with an error envelope. */
class ApiException(
    val code: String,
    override val message: String,
    val status: Int,
) : Exception(message)

/** The controller could not be reached at all (wrong network, wrong address, offline). */
class NetworkException(message: String, cause: Throwable? = null) : Exception(message, cause)

/**
 * Thin transport for the local protocol. One instance per controller address;
 * no retries here - the repositories and UI own retry/reconnect behaviour.
 */
class SntchApi(private val baseUrl: String) {

    fun get(path: String, token: String? = null): JSONObject =
        request("GET", path, body = null, token = token)

    fun post(path: String, body: JSONObject? = null, token: String? = null): JSONObject =
        request("POST", path, body, token)

    fun health(): JSONObject = get("/api/health")

    private fun request(
        method: String,
        path: String,
        body: JSONObject?,
        token: String?,
    ): JSONObject {
        val cleanBase = baseUrl.trim().trimEnd('/')
        if (cleanBase.isEmpty()) throw NetworkException("Set the controller address first.")
        val connection = try {
            URL(cleanBase + path).openConnection() as HttpURLConnection
        } catch (error: Exception) {
            throw NetworkException("Invalid controller address: $cleanBase", error)
        }
        try {
            connection.requestMethod = method
            connection.connectTimeout = 4000
            connection.readTimeout = 8000
            connection.useCaches = false
            connection.setRequestProperty("Accept", "application/json")
            connection.setRequestProperty("Connection", "close")
            if (token != null) connection.setRequestProperty("Authorization", "Bearer $token")
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
            }

            val status = connection.responseCode
            val stream = if (status in 200..299) connection.inputStream else connection.errorStream
            val text = stream?.bufferedReader(Charsets.UTF_8)?.use { it.readText() } ?: ""
            val json = try {
                JSONObject(text)
            } catch (error: Exception) {
                throw NetworkException(
                    if (status in 200..299) "The controller sent an unreadable response."
                    else "The controller returned HTTP $status.",
                    error,
                )
            }
            if (status !in 200..299) {
                val error = json.optJSONObject("error")
                throw ApiException(
                    code = error?.str("code")?.takeIf { it.isNotEmpty() } ?: "HTTP_$status",
                    message = error?.str("message")?.takeIf { it.isNotEmpty() }
                        ?: "The controller rejected the request.",
                    status = status,
                )
            }
            if (!json.optBoolean("ok", false)) {
                throw ApiException("UNEXPECTED", "The controller rejected the request.", status)
            }
            return json
        } catch (error: ApiException) {
            throw error
        } catch (error: NetworkException) {
            throw error
        } catch (error: IOException) {
            throw NetworkException(
                "Cannot reach the SNTCH controller. Check that this device is connected to the SNTCH Wi-Fi network.",
                error,
            )
        } finally {
            connection.disconnect()
        }
    }

    companion object {
        const val DEFAULT_ADDRESS = "http://192.168.4.1"
    }
}

/** Messages shown to the user; never include tokens or PINs. */
fun describeFailure(error: Throwable): String = when (error) {
    is ApiException -> error.message ?: "The controller rejected the request."
    is NetworkException -> error.message ?: "Cannot reach the SNTCH controller."
    else -> "Something went wrong. Try again."
}
