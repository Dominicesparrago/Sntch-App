package com.sntch.app.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * Forest Tech tokens lifted from sntch-visual-design.html: deep forest
 * navigation, emerald/mint accents, light workspace surfaces, white cards with
 * a hairline border. Fonts use the platform sans family because the prototype's
 * Google fonts are not bundled with the app.
 */
object Forest {
    val deepForest = Color(0xFF10281D)
    val forest = Color(0xFF14532D)
    val emerald = Color(0xFF059669)
    val mint = Color(0xFF34D399)
    val pale = Color(0xFFDCECE2)
    val workspace = Color(0xFFF2F8F4)
    val workspaceDark = Color(0xFF0C1812)

    val sidebar = Brush.linearGradient(listOf(Color(0xFF0E2419), Color(0xFF14532D)))
    val primaryButton = Brush.horizontalGradient(listOf(Color(0xFF047857), Color(0xFF34D399)))
    val progress = Brush.horizontalGradient(listOf(Color(0xFF059669), Color(0xFF34D399)))

    val okBg = Color(0xFFD8F4E3)
    val okFg = Color(0xFF047857)
    val warnBg = Color(0xFFFDF1DC)
    val warnFg = Color(0xFF8A4B05)
    val badBg = Color(0xFFFBE7E7)
    val badFg = Color(0xFF9B1C1C)
    val infoBg = Color(0xFFE3EEF9)
    val infoFg = Color(0xFF1E4E79)
    val mutedBg = Color(0xFFDEEEE4)
    val mutedFg = Color(0xFF60766A)

    val onSidebar = Color(0xFFE7FFF0)
    val onSidebarMuted = Color(0xFFACD5BC)
}

private val LightScheme = lightColorScheme(
    primary = Color(0xFF047857),
    onPrimary = Color.White,
    primaryContainer = Forest.okBg,
    onPrimaryContainer = Color(0xFF052E16),
    secondary = Forest.emerald,
    onSecondary = Color.White,
    background = Forest.workspace,
    onBackground = Color(0xFF14291E),
    surface = Color.White,
    onSurface = Color(0xFF14291E),
    surfaceVariant = Forest.workspace,
    onSurfaceVariant = Color(0xFF60766A),
    outline = Color(0xFFDCEBE1),
    error = Color(0xFF9B1C1C),
    errorContainer = Forest.badBg,
    onError = Color.White,
)

private val DarkScheme = darkColorScheme(
    primary = Forest.mint,
    onPrimary = Color(0xFF052E16),
    primaryContainer = Color(0xFF1A3B2A),
    onPrimaryContainer = Color(0xFFE4F5EA),
    secondary = Forest.mint,
    background = Forest.workspaceDark,
    onBackground = Color(0xFFE4F5EA),
    surface = Color(0xFF12241A),
    onSurface = Color(0xFFE4F5EA),
    surfaceVariant = Color(0xFF1A3B2A),
    onSurfaceVariant = Color(0xFFA3BFAF),
    outline = Color(0xFF24402F),
    error = Color(0xFFFBB4B4),
    errorContainer = Color(0xFF3B1717),
    onError = Color(0xFF2A0A0A),
)

private val SntchTypography = Typography(
    headlineLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.ExtraBold,
        fontSize = 26.sp,
        letterSpacing = (-0.2).sp,
    ),
    headlineSmall = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.ExtraBold,
        fontSize = 22.sp,
        letterSpacing = (-0.2).sp,
    ),
    titleLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Bold,
        fontSize = 18.sp,
    ),
    titleMedium = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Bold,
        fontSize = 16.sp,
    ),
    bodyLarge = TextStyle(fontFamily = FontFamily.SansSerif, fontSize = 15.sp),
    bodyMedium = TextStyle(fontFamily = FontFamily.SansSerif, fontSize = 14.sp),
    labelLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.SemiBold,
        fontSize = 14.sp,
    ),
    labelSmall = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.SemiBold,
        fontSize = 11.sp,
        letterSpacing = 0.6.sp,
    ),
)

val MonoStyle = TextStyle(
    fontFamily = FontFamily.Monospace,
    fontFeatureSettings = "tnum",
)

@Composable
fun SntchTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (darkTheme) DarkScheme else LightScheme,
        typography = SntchTypography,
        content = content,
    )
}
