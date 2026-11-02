package com.xiakexing.client.feature.shell.scene

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.R
import com.xiakexing.client.feature.shell.ShellEntrySceneState
import com.xiakexing.client.feature.shell.ShellIntent
import com.xiakexing.client.feature.shell.ShellPhase
import com.xiakexing.client.feature.shell.component.EntryBrandHeader
import com.xiakexing.client.feature.shell.ui.ShellBackgroundColor
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellRadius
import kotlinx.coroutines.delay

@Composable
internal fun LoginScene(
    state: ShellEntrySceneState,
    onIntent: (ShellIntent) -> Unit,
) {
    EntrySceneLayout {
        // 方案 R-17/R-18：品牌区 + 表单卡 + 阶段 chip + 内联错误
        EntryBrandHeader()
        if (state.loginErrorMessage.isNotBlank()) {
            ErrorBanner(text = state.loginErrorMessage)
        }
        EntryTextField(
            label = "姓名",
            value = state.username,
            placeholder = "请输入游戏姓名",
            onValueChange = { onIntent(ShellIntent.UsernameChanged(it)) },
        )
        EntryTextField(
            label = "密码",
            value = state.password,
            placeholder = "请输入密码",
            onValueChange = { onIntent(ShellIntent.PasswordChanged(it)) },
            visualTransformation = entryPasswordVisualTransformation,
        )
        Spacer(modifier = Modifier.height(4.dp))
        EntryPrimaryButton(
            // 方案 R-18：连接中按钮内 pending；点击=取消/断开（沿用 DisconnectClicked 语义）
            text =
                if (state.sessionPhase == ShellPhase.CONNECTING ||
                    state.sessionPhase == ShellPhase.AUTHENTICATING
                ) {
                    "正在连接…点击取消"
                } else {
                    "进入江湖"
                },
            onClick = {
                if (state.sessionPhase == ShellPhase.CONNECTING ||
                    state.sessionPhase == ShellPhase.AUTHENTICATING
                ) {
                    onIntent(ShellIntent.DisconnectClicked)
                } else {
                    onIntent(ShellIntent.ConnectClicked)
                }
            },
            enabled =
                state.canConnect || state.sessionPhase == ShellPhase.CONNECTING ||
                    state.sessionPhase == ShellPhase.AUTHENTICATING,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
private fun EntrySceneLayout(content: @Composable ColumnScope.() -> Unit) {
    Box(modifier = Modifier.fillMaxSize()) {
        EntrySceneBackground()
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(horizontal = 18.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center,
        ) {
            EntryCardSurface(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .widthIn(max = 360.dp)
                        .offset(y = (-24).dp),
            ) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                    content = content,
                )
            }
        }
    }
}

@Composable
internal fun RegisterScene(
    state: ShellEntrySceneState,
    onIntent: (ShellIntent) -> Unit,
) {
    EntrySceneLayout {
        val choosingGender = state.registerOptions.isNotEmpty()
        EntryBrandHeader(title = if (choosingGender) "选择性别" else "输入姓名")
        Box(
            modifier = Modifier.fillMaxWidth().height(112.dp),
            contentAlignment = Alignment.Center,
        ) {
            if (choosingGender) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    state.registerOptions.forEach { option ->
                        EntryPrimaryButton(
                            text = option,
                            onClick = { onIntent(ShellIntent.RegisterOptionClicked(option)) },
                            enabled = true,
                            modifier = Modifier.weight(1f),
                        )
                    }
                }
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    EntryTextField(
                        label = "",
                        value = state.commandInput,
                        placeholder = "",
                        onValueChange = { onIntent(ShellIntent.CommandChanged(it)) },
                    )
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        EntryPrimaryButton(
                            text = "提交",
                            onClick = { onIntent(ShellIntent.SendCommandClicked) },
                            enabled = state.canSend,
                            modifier = Modifier.weight(1f),
                        )
                        EntrySecondaryButton(
                            text = "取消",
                            onClick = { onIntent(ShellIntent.DisconnectClicked) },
                            modifier = Modifier.weight(1f),
                        )
                    }
                }
            }
        }
    }
}

@Composable
internal fun WorldLoadingScene(
    state: ShellEntrySceneState,
    onIntent: (ShellIntent) -> Unit,
) {
    // 方案 R-19：失败态留在加载页，当前阶段行变朱砂，提供「重试 / 返回登录」
    val failed =
        state.sessionPhase == ShellPhase.FAILED ||
            state.sessionPhase == ShellPhase.DISCONNECTED ||
            state.sessionPhase == ShellPhase.SUSPENDED

    EntrySceneLayout {
        LoadingVerse()
        LinearProgressIndicator(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(6.dp),
            color = ShellPalette.Accent,
            trackColor = ShellPalette.InkLine,
        )
        if (failed) {
            Text(
                text = state.sessionDetail.ifBlank { "进入江湖失败" },
                color = ShellPalette.Cinnabar,
                style = MaterialTheme.typography.bodyMedium,
                textAlign = TextAlign.Center,
            )
            EntryPrimaryButton(
                text = "重试",
                onClick = { onIntent(ShellIntent.ConnectClicked) },
                enabled = true,
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

@Composable
private fun LoadingVerse() {
    val verses =
        remember {
            listOf(
                "长风破浪会有时，直挂云帆济沧海",
                "醉里挑灯看剑，梦回吹角连营",
                "莫愁前路无知己，天下谁人不识君",
                "仰天大笑出门去，我辈岂是蓬蒿人",
                "会当凌绝顶，一览众山小",
            )
        }
    var index by remember { mutableIntStateOf(0) }
    val currentVerse by rememberUpdatedState(verses[index])
    LaunchedEffect(Unit) {
        while (true) {
            delay(2600)
            index = (index + 1) % verses.size
        }
    }
    Text(
        text = currentVerse,
        color = ShellPalette.PaperMuted,
        style = MaterialTheme.typography.bodyMedium,
        textAlign = TextAlign.Center,
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun EntrySceneBackground() {
    Box(modifier = Modifier.fillMaxSize().background(ShellBackgroundColor)) {
        Image(
            painter = painterResource(id = R.drawable.scene_background_xkx),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            alpha = 0.92f,
            modifier = Modifier.fillMaxSize(),
        )
        Box(modifier = Modifier.fillMaxSize().background(entrySceneAtmosphereScrim))
        Box(modifier = Modifier.fillMaxSize().background(entrySceneLowerScrim))
    }
}

@Composable
private fun EntryHero(
    title: String,
    subtitle: String,
) {
    Text(
        text = title,
        color = ShellPalette.Paper,
        textAlign = TextAlign.Center,
        style =
            MaterialTheme.typography.displayMedium.copy(
                fontWeight = FontWeight.Normal,
                lineHeight = 56.sp,
                letterSpacing = 0.sp,
            ),
    )
    Text(
        text = subtitle,
        color = ShellPalette.PaperMuted,
        textAlign = TextAlign.Center,
        style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 26.sp),
    )
}

@Composable
private fun EntryCardSurface(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    Surface(
        modifier = modifier,
        color = ShellPalette.InkSurface.copy(alpha = 0.78f),
        shape = ShellRadius.M,
        border = BorderStroke(1.dp, ShellLines.SurfaceBoundary),
        shadowElevation = 0.dp,
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(14.dp),
            content = content,
        )
    }
}

@Composable
private fun EntryCardHeading(
    title: String,
    subtitle: String,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(
            text = title,
            color = ShellPalette.Paper,
            style =
                MaterialTheme.typography.headlineSmall.copy(
                    fontWeight = FontWeight.Medium,
                    letterSpacing = 0.sp,
                ),
            textAlign = TextAlign.Center,
        )
        Text(
            text = subtitle,
            color = ShellPalette.PaperMuted,
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
private fun EntryTextField(
    label: String,
    value: String,
    placeholder: String,
    onValueChange: (String) -> Unit,
    visualTransformation: VisualTransformation = VisualTransformation.None,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(if (label.isBlank()) 0.dp else 7.dp),
    ) {
        if (label.isNotBlank()) {
            Text(
                text = label,
                color = ShellPalette.PaperMuted,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Medium),
            )
        }
        OutlinedTextField(
            value = value,
            onValueChange = onValueChange,
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            placeholder = {
                Text(
                    text = placeholder,
                    color = ShellPalette.PaperMuted,
                )
            },
            visualTransformation = visualTransformation,
            shape = ShellRadius.S,
            colors =
                OutlinedTextFieldDefaults.colors(
                    focusedContainerColor = ShellPalette.CanvasInset.copy(alpha = 0.74f),
                    unfocusedContainerColor = ShellPalette.CanvasInset.copy(alpha = 0.58f),
                    disabledContainerColor = ShellPalette.CanvasInset.copy(alpha = 0.42f),
                    focusedBorderColor =
                        ShellPalette.DividerLine.copy(
                            alpha = ShellChrome.DividerLineAlpha,
                        ),
                    unfocusedBorderColor =
                        ShellPalette.DividerLine.copy(
                            alpha = ShellChrome.DividerLineAlpha,
                        ),
                    disabledBorderColor = ShellPalette.DividerLine.copy(alpha = 0.22f),
                    focusedTextColor = ShellPalette.Paper,
                    unfocusedTextColor = ShellPalette.Paper,
                    focusedPlaceholderColor = ShellPalette.PaperMuted,
                    unfocusedPlaceholderColor = ShellPalette.PaperMuted,
                    focusedLabelColor = ShellPalette.PaperMuted,
                    unfocusedLabelColor = ShellPalette.PaperMuted,
                    cursorColor = ShellPalette.Accent,
                ),
        )
    }
}

@Composable
private fun EntryPrimaryButton(
    text: String,
    onClick: () -> Unit,
    enabled: Boolean,
    modifier: Modifier = Modifier,
) {
    OutlinedButton(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.height(48.dp),
        shape = ShellRadius.S,
        border =
            BorderStroke(
                1.dp,
                if (enabled) {
                    ShellPalette.Accent.copy(
                        alpha = 0.92f,
                    )
                } else {
                    ShellPalette.DividerLine.copy(alpha = 0.62f)
                },
            ),
        colors =
            ButtonDefaults.outlinedButtonColors(
                containerColor = ShellPalette.Ink3.copy(alpha = 0.92f),
                contentColor = ShellPalette.Paper,
                disabledContainerColor = ShellPalette.CanvasInset.copy(alpha = 0.22f),
                disabledContentColor = ShellPalette.PaperMuted.copy(alpha = 0.80f),
            ),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 12.dp),
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = text,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium),
                textAlign = TextAlign.Center,
            )
        }
    }
}

@Composable
private fun EntrySecondaryButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    OutlinedButton(
        onClick = onClick,
        modifier = modifier.height(48.dp),
        shape = ShellRadius.S,
        border = BorderStroke(1.dp, ShellLines.ControlStateBorder),
        colors =
            ButtonDefaults.outlinedButtonColors(
                containerColor = ShellPalette.Ink1.copy(alpha = 0.72f),
                contentColor = ShellPalette.PaperMuted,
            ),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 12.dp),
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium),
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
private fun HeroHeader(
    title: String,
    eyebrow: String = "",
    body: String = "",
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        if (eyebrow.isNotBlank()) {
            Text(
                text = eyebrow,
                color = ShellPalette.Accent,
                style = MaterialTheme.typography.labelLarge,
            )
        }
        Text(
            text = title,
            color = ShellPalette.Paper,
            style = MaterialTheme.typography.displayMedium,
        )
        if (body.isNotBlank()) {
            Text(
                text = body,
                color = ShellPalette.PaperMuted,
                style = MaterialTheme.typography.bodyLarge,
            )
        }
    }
}

@Composable
private fun ErrorBanner(text: String) {
    Box(
        modifier =
            Modifier
                .fillMaxWidth()
                .background(ShellPalette.Scarlet.copy(alpha = 0.14f), RoundedCornerShape(0.dp))
                .border(1.dp, ShellPalette.Scarlet.copy(alpha = 0.72f), RoundedCornerShape(0.dp))
                .padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Text(
            text = text,
            color = ShellPalette.Paper,
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

private val entryPasswordVisualTransformation = PasswordVisualTransformation()

private val entrySceneAtmosphereScrim =
    Brush.verticalGradient(
        listOf(
            ShellBackgroundColor.copy(alpha = 0.16f),
            ShellPalette.InkBlack.copy(alpha = 0.34f),
            ShellBackgroundColor.copy(alpha = 0.38f),
        ),
    )

private val entrySceneLowerScrim =
    Brush.verticalGradient(
        listOf(
            Color.Transparent,
            ShellPalette.InkBlack.copy(alpha = 0.44f),
        ),
    )
