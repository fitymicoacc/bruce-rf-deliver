"""
Bruce-RF Signal Player

Minimal desktop app for signal playback only.
Two modes:
  1. Manual input: select protocol, enter code, frequency
  2. Load from file: JSON with signal parameters

Requires: pip install bleak
"""

import asyncio
import json
import struct
import sys
import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from threading import Thread

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("Install bleak: pip install bleak")
    sys.exit(1)

SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
CMD_UUID     = "12345678-1234-5678-1234-56789abcdef1"
STATUS_UUID  = "12345678-1234-5678-1234-56789abcdef3"

PROTOCOLS = [
    (1,  "PT2262",   350), (2,  "SC5262",   650), (3,  "HX2262",   100),
    (4,  "EV1527",   380), (5,  "HT6P20B",  500), (6,  "HT6P20B~", 450),
    (7,  "HS2303-PT",150), (8,  "Conrad RX", 200), (9,  "Conrad TX", 200),
    (10, "1ByOne",   365), (11, "HT12E",    270), (12, "SM5212",    320),
    (13, "Mumbi",    100), (14, "Blyss",    500), (15, "sc2260R4",  415),
    (16, "HomeNetWerks",250), (17,"ORNO",     80), (18, "CLARUS",    82),
    (19, "NEC",      560), (20, "CAME 12",  250), (21, "FAAC",      330),
    (22, "NICE",     700), (23, "Protocol 23",400),
]


def pack_play(proto_id, key, bits, pulse, freq, continuous=False):
    cmd = 0x07 if continuous else 0x03
    buf = struct.pack("<B B Q B H f", cmd, proto_id, key, bits, pulse, freq)
    return buf


def pack_stop():
    return bytes([0x02])


class App:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Bruce-RF Player")
        self.root.geometry("420x520")
        self.root.resizable(False, False)

        self.client = None
        self.loop = asyncio.new_event_loop()
        self.thread = Thread(target=self._run_loop, daemon=True)
        self.thread.start()
        self.playing = False

        self._build_ui()

    def _run_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def _async(self, coro):
        return asyncio.run_coroutine_threadsafe(coro, self.loop)

    def _build_ui(self):
        pad = {"padx": 8, "pady": 4}

        # Connection
        conn_frame = ttk.LabelFrame(self.root, text="Connection")
        conn_frame.pack(fill="x", **pad)

        self.status_var = tk.StringVar(value="Disconnected")
        ttk.Label(conn_frame, textvariable=self.status_var).pack(side="left", **pad)
        self.btn_connect = ttk.Button(conn_frame, text="Connect", command=self.on_connect)
        self.btn_connect.pack(side="right", **pad)

        # Manual input
        manual_frame = ttk.LabelFrame(self.root, text="Manual Input")
        manual_frame.pack(fill="x", **pad)

        ttk.Label(manual_frame, text="Protocol:").grid(row=0, column=0, sticky="w", **pad)
        self.proto_var = tk.StringVar()
        proto_names = [f"{p[0]}: {p[1]} (T={p[2]})" for p in PROTOCOLS]
        self.proto_combo = ttk.Combobox(manual_frame, textvariable=self.proto_var,
                                        values=proto_names, width=30, state="readonly")
        self.proto_combo.grid(row=0, column=1, **pad)
        self.proto_combo.current(0)

        ttk.Label(manual_frame, text="Code (hex):").grid(row=1, column=0, sticky="w", **pad)
        self.code_var = tk.StringVar(value="A1B2C5")
        ttk.Entry(manual_frame, textvariable=self.code_var, width=20).grid(row=1, column=1, sticky="w", **pad)

        ttk.Label(manual_frame, text="Bits:").grid(row=2, column=0, sticky="w", **pad)
        self.bits_var = tk.StringVar(value="24")
        ttk.Entry(manual_frame, textvariable=self.bits_var, width=10).grid(row=2, column=1, sticky="w", **pad)

        ttk.Label(manual_frame, text="Frequency MHz:").grid(row=3, column=0, sticky="w", **pad)
        self.freq_var = tk.StringVar(value="433.92")
        ttk.Entry(manual_frame, textvariable=self.freq_var, width=10).grid(row=3, column=1, sticky="w", **pad)

        # File input
        file_frame = ttk.LabelFrame(self.root, text="Load from File")
        file_frame.pack(fill="x", **pad)

        self.file_var = tk.StringVar(value="")
        ttk.Entry(file_frame, textvariable=self.file_var, width=35).pack(side="left", **pad)
        ttk.Button(file_frame, text="Browse", command=self.on_browse).pack(side="right", **pad)

        # Play buttons
        play_frame = ttk.Frame(self.root)
        play_frame.pack(fill="x", **pad)

        self.btn_play = ttk.Button(play_frame, text="Play (single)", command=self.on_play)
        self.btn_play.pack(side="left", expand=True, fill="x", **pad)

        self.btn_hold = ttk.Button(play_frame, text="Hold to Send")
        self.btn_hold.pack(side="right", expand=True, fill="x", **pad)
        self.btn_hold.bind("<ButtonPress-1>", self.on_hold_press)
        self.btn_hold.bind("<ButtonRelease-1>", self.on_hold_release)

        # Log
        log_frame = ttk.LabelFrame(self.root, text="Log")
        log_frame.pack(fill="both", expand=True, **pad)
        self.log_text = tk.Text(log_frame, height=8, state="disabled", font=("Consolas", 9))
        self.log_text.pack(fill="both", expand=True)

    def log(self, msg):
        self.log_text.config(state="normal")
        self.log_text.insert("end", msg + "\n")
        self.log_text.see("end")
        self.log_text.config(state="disabled")

    def get_signal(self):
        filepath = self.file_var.get().strip()
        if filepath and os.path.exists(filepath):
            with open(filepath, "r") as f:
                data = json.load(f)
            return (data["protocol"], data["key"], data["bits"],
                    data.get("pulse_length", PROTOCOLS[data["protocol"]-1][2]),
                    data.get("frequency", 433.92))

        idx = self.proto_combo.current()
        proto_id = PROTOCOLS[idx][0]
        pulse = PROTOCOLS[idx][2]
        key = int(self.code_var.get(), 16)
        bits = int(self.bits_var.get())
        freq = float(self.freq_var.get())
        return (proto_id, key, bits, pulse, freq)

    async def _connect(self):
        self.status_var.set("Scanning...")
        self.log("Scanning for Bruce-RF...")
        devices = await BleakScanner.discover(timeout=5.0)
        target = None
        for d in devices:
            if d.name and "Bruce" in d.name:
                target = d
                break
        if not target:
            self.status_var.set("Not found")
            self.log("Bruce-RF not found")
            return

        self.log(f"Found {target.name} ({target.address})")
        self.status_var.set("Connecting...")
        self.client = BleakClient(target.address)
        await self.client.connect()
        self.status_var.set(f"Connected: {target.name}")
        self.log("Connected!")

    def on_connect(self):
        self._async(self._connect())

    def on_browse(self):
        path = filedialog.askopenfilename(
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")])
        if path:
            self.file_var.set(path)
            self.log(f"Loaded: {path}")

    async def _play(self, continuous=False):
        if not self.client or not self.client.is_connected:
            self.log("Not connected")
            return
        try:
            proto, key, bits, pulse, freq = self.get_signal()
        except Exception as e:
            self.log(f"Error: {e}")
            return

        data = pack_play(proto, key, bits, pulse, freq, continuous)
        await self.client.write_gatt_char(CMD_UUID, data)
        mode = "CONT" if continuous else "SINGLE"
        self.log(f"PLAY {mode}: P{proto} 0x{key:X} {bits}bit {freq}MHz")

    async def _stop(self):
        if self.client and self.client.is_connected:
            await self.client.write_gatt_char(CMD_UUID, pack_stop())
            self.log("STOP")

    def on_play(self):
        self._async(self._play(continuous=False))

    def on_hold_press(self, event):
        self.playing = True
        self._async(self._play(continuous=True))

    def on_hold_release(self, event):
        if self.playing:
            self.playing = False
            self._async(self._stop())

    def run(self):
        self.root.mainloop()
        self.loop.call_soon_threadsafe(self.loop.stop)


if __name__ == "__main__":
    App().run()
