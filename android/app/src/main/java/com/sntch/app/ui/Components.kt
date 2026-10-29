package com.sntch.app.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

enum class Tone { OK, WARN, BAD, INFO, MUTED }

private data class ToneColors(val background: Color, val foreground: Color)

private fun toneColors(tone: Tone): ToneColors = when (tone) {
    Tone.OK -> ToneColors(Forest.okBg, Forest.okFg)
    Tone.WARN -> ToneColors(Forest.warnBg, Forest.warnFg)
    Tone.BAD -> ToneColors(Forest.badBg, Forest.badFg)
    Tone.INFO -> ToneColors(Forest.infoBg, Forest.infoFg)
    Tone.MUTED -> ToneColors(Forest.mutedBg, Forest.mutedFg)
}

fun statusTone(status: String): Tone = when (status) {
    "active", "live", "present", "passed", "pass", "reviewed", "submitted", "joined",
    "connected", "ok", "ready" -> Tone.OK
    "paused", "late", "pending", "warn", "scheduled", "syncing", "planned" -> Tone.WARN
    "absent", "failed", "fail", "expired", "disconnected", "unavailable", "bad",
    "storage-error", "pending_delete" -> Tone.BAD
    "draft", "ended", "completed", "unmarked", "waiting", "notjoined", "closed" -> Tone.MUTED
    else -> Tone.INFO
}

fun statusLabel(status: String): String = when (status) {
    "ready" -> "Ready"
    "live" -> "Live"
    "paused" -> "Paused"
    "ended" -> "Ended"
    "expired" -> "Expired"
    "pending_delete" -> "Awaiting backup"
    "active" -> "Active"
    "draft" -> "Draft"
    "completed" -> "Completed"
    "present" -> "Present"
    "late" -> "Late"
    "absent" -> "Absent"
    "unmarked" -> "Not marked"
    "pending" -> "Pending review"
    "reviewed" -> "Reviewed"
    "passed" -> "Passed"
    "failed" -> "Failed"
    "submitted" -> "Submitted"
    "open" -> "Open"
    else -> status.replaceFirstChar { it.uppercase() }
}

@Composable
fun Badge(text: String, tone: Tone = Tone.MUTED, modifier: Modifier = Modifier) {
    val colors = toneColors(tone)
    Surface(
        modifier = modifier,
        color = colors.background,
        shape = RoundedCornerShape(50),
    ) {
        Text(
            text = text,
            color = colors.foreground,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 5.dp),
        )
    }
}

@Composable
fun SntchCard(
    modifier: Modifier = Modifier,
    title: String? = null,
    action: (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
        tonalElevation = 0.dp,
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            if (title != null) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        title,
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.weight(1f),
                    )
                    action?.invoke()
                }
                Spacer(Modifier.height(10.dp))
            }
            content()
        }
    }
}

@Composable
fun PrimaryButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    busy: Boolean = false,
) {
    Box(
        modifier = modifier
            .heightIn(min = 48.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(
                if (enabled) Forest.primaryButton
                else androidx.compose.ui.graphics.SolidColor(Forest.pale),
            )
            .clickable(enabled = enabled && !busy) { onClick() }
            .padding(horizontal = 18.dp),
        contentAlignment = Alignment.Center,
    ) {
        if (busy) {
            CircularProgressIndicator(
                modifier = Modifier.size(18.dp),
                strokeWidth = 2.dp,
                color = Forest.onSidebar,
            )
        } else {
            Text(
                text,
                color = if (enabled) Color(0xFF052E16) else Forest.mutedFg,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                maxLines = 1,
            )
        }
    }
}

@Composable
fun QuietButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    Surface(
        modifier = modifier.heightIn(min = 44.dp).clickable(enabled = enabled) { onClick() },
        shape = RoundedCornerShape(12.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
    ) {
        Box(contentAlignment = Alignment.Center, modifier = Modifier.padding(horizontal = 14.dp)) {
            Text(
                text,
                style = MaterialTheme.typography.labelLarge,
                color = if (enabled) MaterialTheme.colorScheme.onSurface
                else MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
            )
        }
    }
}

@Composable
fun Field(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    modifier: Modifier = Modifier,
    placeholder: String? = null,
    singleLine: Boolean = true,
    keyboardType: KeyboardType = KeyboardType.Text,
    minLines: Int = 1,
    maxLines: Int = if (singleLine) 1 else 4,
    enabled: Boolean = true,
    supporting: String? = null,
) {
    OutlinedTextField(
        value = value,
        onValueChange = onValueChange,
        label = { Text(label) },
        placeholder = placeholder?.let { { Text(it) } },
        modifier = modifier.fillMaxWidth(),
        singleLine = singleLine,
        shape = RoundedCornerShape(12.dp),
        enabled = enabled,
        minLines = minLines,
        maxLines = if (singleLine) 1 else maxLines,
        keyboardOptions = KeyboardOptions(keyboardType = keyboardType),
        supportingText = supporting?.let { { Text(it) } },
    )
}

@Composable
fun StatTile(
    label: String,
    value: String,
    sub: String? = null,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
    ) {
        Column(Modifier.padding(14.dp)) {
            Text(
                label.uppercase(),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Spacer(Modifier.height(6.dp))
            Text(value, style = MaterialTheme.typography.headlineSmall)
            if (sub != null) {
                Text(
                    sub,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

@Composable
fun SectionHeading(text: String, trailing: String? = null) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
        if (trailing != null) {
            Text(
                trailing,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
fun InfoBanner(text: String, tone: Tone = Tone.INFO, modifier: Modifier = Modifier) {
    val colors = toneColors(tone)
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        color = colors.background,
    ) {
        Row(
            Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                imageVector = when (tone) {
                    Tone.BAD -> Icons.Filled.Warning
                    Tone.WARN -> Icons.Filled.Warning
                    Tone.OK -> Icons.Filled.Check
                    else -> Icons.Filled.Refresh
                },
                contentDescription = null,
                tint = colors.foreground,
                modifier = Modifier.size(16.dp),
            )
            Spacer(Modifier.width(8.dp))
            Text(
                text,
                color = colors.foreground,
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}

@Composable
fun ProgressBar(fraction: Float, modifier: Modifier = Modifier) {
    Box(
        modifier
            .fillMaxWidth()
            .height(8.dp)
            .clip(RoundedCornerShape(50))
            .background(MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Box(
            Modifier
                .fillMaxWidth(fraction.coerceIn(0f, 1f))
                .fillMaxHeight()
                .background(Forest.progress),
        )
    }
}

@Composable
fun EmptyState(
    title: String,
    text: String,
    actionText: String? = null,
    onAction: (() -> Unit)? = null,
) {
    Column(
        modifier = Modifier.fillMaxWidth().padding(vertical = 18.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(title, style = MaterialTheme.typography.titleMedium, textAlign = TextAlign.Center)
        Spacer(Modifier.height(4.dp))
        Text(
            text,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )
        if (actionText != null && onAction != null) {
            Spacer(Modifier.height(12.dp))
            PrimaryButton(actionText, onAction)
        }
    }
}

@Composable
fun CodePill(code: String, big: Boolean = false) {
    Surface(
        color = MaterialTheme.colorScheme.surfaceVariant,
        shape = RoundedCornerShape(10.dp),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
    ) {
        Text(
            code,
            style = if (big) MaterialTheme.typography.headlineSmall else MaterialTheme.typography.titleMedium,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
        )
    }
}

@Composable
fun SelectChip(text: String, selected: Boolean, onClick: () -> Unit) {
    val colors = if (selected) {
        ToneColors(Forest.okBg, Forest.okFg)
    } else {
        ToneColors(MaterialTheme.colorScheme.surface, MaterialTheme.colorScheme.onSurfaceVariant)
    }
    Surface(
        shape = RoundedCornerShape(50),
        color = colors.background,
        border = BorderStroke(1.dp, if (selected) Forest.okFg else MaterialTheme.colorScheme.outline),
        modifier = Modifier.clickable { onClick() },
    ) {
        Text(
            text,
            color = colors.foreground,
            style = MaterialTheme.typography.labelLarge,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
        )
    }
}

@Composable
fun ConnectionChip(connected: Boolean, label: String) {
    Badge(
        text = label,
        tone = if (connected) Tone.OK else Tone.BAD,
    )
}

@Composable
fun SntchDialog(
    title: String,
    onDismiss: () -> Unit,
    confirmText: String? = null,
    onConfirm: (() -> Unit)? = null,
    confirmEnabled: Boolean = true,
    dismissText: String = "Cancel",
    width: androidx.compose.ui.unit.Dp = 340.dp,
    content: @Composable ColumnScope.() -> Unit,
) {
    Dialog(onDismissRequest = onDismiss) {
        Surface(
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.surface,
            modifier = Modifier.width(width),
        ) {
            Column(Modifier.padding(18.dp)) {
                Text(title, style = MaterialTheme.typography.titleLarge)
                Spacer(Modifier.height(12.dp))
                Column(
                    Modifier
                        .heightIn(max = 460.dp)
                        .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    content()
                }
                Spacer(Modifier.height(16.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (confirmText != null && onConfirm != null) {
                        PrimaryButton(
                            text = confirmText,
                            onClick = onConfirm,
                            modifier = Modifier.weight(1f),
                            enabled = confirmEnabled,
                        )
                    }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onDismiss) { Text(dismissText) }
                }
            }
        }
    }
}

@Composable
fun ConfirmDialog(
    title: String,
    message: String,
    confirmText: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(message) },
        confirmButton = { TextButton(onClick = onConfirm) { Text(confirmText) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun DismissNote(onClose: () -> Unit, text: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(text, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Surface(shape = RoundedCornerShape(50), modifier = Modifier.clickable { onClose() }) {
            Icon(Icons.Filled.Close, contentDescription = "Dismiss", modifier = Modifier.padding(4.dp).size(16.dp))
        }
    }
}

// ------------------------------------------------------------------ helpers --

fun todayIso(): String {
    val calendar = Calendar.getInstance()
    return String.format(
        Locale.US,
        "%04d-%02d-%02d",
        calendar.get(Calendar.YEAR),
        calendar.get(Calendar.MONTH) + 1,
        calendar.get(Calendar.DAY_OF_MONTH),
    )
}

fun clockText(seconds: Int): String {
    val safe = seconds.coerceAtLeast(0)
    return String.format(Locale.US, "%02d:%02d", safe / 60, safe % 60)
}

fun dateText(iso: String): String {
    return try {
        val parsed: Date = SimpleDateFormat("yyyy-MM-dd", Locale.US).parse(iso)!!
        SimpleDateFormat("MMM d, yyyy", Locale.US).format(parsed)
    } catch (error: Exception) {
        iso
    }
}

/** Times are monotonic controller uptime, not wall clock; only show plausible deltas. */
fun uptimeAgo(nowMs: Long, atMs: Long): String {
    if (atMs <= 0 || nowMs <= 0 || atMs > nowMs) return "—"
    val delta = nowMs - atMs
    if (delta > 12L * 60 * 60 * 1000) return "—"
    val minutes = delta / 60000
    if (minutes < 1) return "just now"
    if (minutes < 60) return "$minutes min ago"
    return "${minutes / 60} h ago"
}
