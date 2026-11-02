package com.xiakexing.client.feature.shell.component

import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.xiakexing.client.feature.shell.ui.ShellLines

/** Draws overlay edges inside the measured bounds so screen-edge strokes are never clipped. */
internal fun Modifier.narrativeOverlayEdges(
    drawLeft: Boolean = true,
    drawTop: Boolean = false,
    drawBottom: Boolean = true,
    strokeWidth: Dp = 1.dp,
): Modifier =
    drawBehind {
        val strokePx = strokeWidth.toPx()
        val inset = strokePx / 2f
        val color = ShellLines.SurfaceBoundary
        if (drawLeft) {
            drawLine(
                color,
                Offset(inset, inset),
                Offset(inset, size.height - inset),
                strokePx,
                StrokeCap.Square,
            )
        }
        drawLine(
            color,
            Offset(size.width - inset, inset),
            Offset(size.width - inset, size.height - inset),
            strokePx,
            StrokeCap.Square,
        )
        if (drawBottom) {
            drawLine(
                color,
                Offset(inset, size.height - inset),
                Offset(size.width - inset, size.height - inset),
                strokePx,
                StrokeCap.Square,
            )
        }
        if (drawTop) {
            drawLine(
                color,
                Offset(inset, inset),
                Offset(size.width - inset, inset),
                strokePx,
                StrokeCap.Square,
            )
        }
    }
