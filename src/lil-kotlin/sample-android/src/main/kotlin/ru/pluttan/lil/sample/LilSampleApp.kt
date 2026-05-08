package ru.pluttan.lil.sample

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import ru.pluttan.lil.LilSignal

/**
 * Three screens, rendered inline by a single state machine:
 *   1. [Screen.Scan]      — shows discovered Bruce-RF peripherals, tap to
 *                           connect, a single action button starts/stops scan.
 *   2. [Screen.Signals]   — live list of captured signals; button starts /
 *                           stops listening on 433.92 MHz; tap a row to play
 *                           the signal back.
 *   3. [Screen.Detail]    — a single signal view with Play + Delete buttons.
 *
 * Actual Kable / LilDevice wiring is behind [SampleViewModel] (A3.9
 * follow-up). The UI below uses flat mock state so it renders in preview
 * and on a device without the peripheral being present.
 */
sealed interface Screen {
    data object Scan                       : Screen
    data class  Signals(val deviceId: String) : Screen
    data class  Detail (val signal: LilSignal) : Screen
}

@Composable
fun LilSampleApp() {
    var screen by remember { mutableStateOf<Screen>(Screen.Scan) }
    when (val s = screen) {
        Screen.Scan         -> ScanScreen(
            onConnected = { id -> screen = Screen.Signals(id) }
        )
        is Screen.Signals   -> SignalsScreen(
            deviceId = s.deviceId,
            onBack   = { screen = Screen.Scan },
            onSignal = { sig -> screen = Screen.Detail(sig) }
        )
        is Screen.Detail    -> DetailScreen(
            signal = s.signal,
            onBack = { screen = Screen.Signals(deviceId = "(unknown)") }
        )
    }
}

@Composable
private fun ScanScreen(onConnected: (String) -> Unit) {
    var scanning by remember { mutableStateOf(false) }
    val mockDevices = remember {
        listOf("AA:BB:CC:DD:EE:01" to "Bruce-RF",
               "AA:BB:CC:DD:EE:02" to "Bruce-RF (spare)")
    }
    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Text("Scan", style = androidx.compose.material3.MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))
        Button(onClick = { scanning = !scanning }, modifier = Modifier.fillMaxWidth()) {
            Text(if (scanning) "Stop scan" else "Start scan")
        }
        Spacer(Modifier.height(16.dp))
        if (scanning) {
            CircularProgressIndicator()
            Spacer(Modifier.height(8.dp))
        }
        LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(mockDevices) { (id, name) ->
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
                ) {
                    Column(Modifier.padding(12.dp)) {
                        Text(name, style = androidx.compose.material3.MaterialTheme.typography.titleMedium)
                        Text(id,   style = androidx.compose.material3.MaterialTheme.typography.bodySmall)
                        Spacer(Modifier.height(8.dp))
                        Button(onClick = { onConnected(id) }) { Text("Connect") }
                    }
                }
            }
        }
    }
}

@Composable
private fun SignalsScreen(
    deviceId: String,
    onBack: () -> Unit,
    onSignal: (LilSignal) -> Unit
) {
    var listening by remember { mutableStateOf(false) }
    val mockSignals = remember {
        listOf(
            LilSignal(1, 0xA1B2C5uL, 24, 349, 433.92f),
            LilSignal(4, 0x55AA33uL, 24, 380, 433.92f)
        )
    }
    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Text("Device $deviceId", style = androidx.compose.material3.MaterialTheme.typography.titleMedium)
        Spacer(Modifier.height(8.dp))
        Button(onClick = { listening = !listening }, modifier = Modifier.fillMaxWidth()) {
            Text(if (listening) "Stop listening" else "Start listening 433.92 MHz")
        }
        Spacer(Modifier.height(16.dp))
        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(mockSignals) { sig ->
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
                ) {
                    Column(Modifier.padding(12.dp)) {
                        Text("P${sig.protocol} ${sig.bits}bit 0x${sig.key.toString(16).uppercase()}")
                        Text("${sig.freqMHz} MHz · T=${sig.pulseLength}µs",
                             style = androidx.compose.material3.MaterialTheme.typography.bodySmall)
                        Spacer(Modifier.height(8.dp))
                        Button(onClick = { onSignal(sig) }) { Text("Open") }
                    }
                }
            }
        }
        Spacer(Modifier.height(16.dp))
        OutlinedButton(onClick = onBack) { Text("Back") }
    }
}

@Composable
private fun DetailScreen(signal: LilSignal, onBack: () -> Unit) {
    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Text("Signal", style = androidx.compose.material3.MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))
        Text("Protocol P${signal.protocol} · ${signal.bits} bits")
        Text("Key  0x${signal.key.toString(16).uppercase()}")
        Text("T    ${signal.pulseLength} µs")
        Text("Freq ${signal.freqMHz} MHz")
        Spacer(Modifier.height(16.dp))
        Button(onClick = { /* TODO: device.play(signal) via a ViewModel */ },
               modifier = Modifier.fillMaxWidth()) { Text("Play") }
        Spacer(Modifier.height(8.dp))
        OutlinedButton(onClick = onBack, modifier = Modifier.fillMaxWidth()) { Text("Back") }
    }
}
