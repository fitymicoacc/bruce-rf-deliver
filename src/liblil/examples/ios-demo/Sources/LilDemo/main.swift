import Foundation
import CLil

func printHex(_ data: [UInt8]) {
    print(data.map { String(format: "%02X", $0) }.joined(separator: " "))
}

print("liblil iOS demo")
print("================================================\n")

// --- Pack / Unpack ---
print("=== Pack / Unpack ===\n")

var buf = [UInt8](repeating: 0, count: 32)
var len: Int = 0

lil_pack_start_listen(433.92, &buf, buf.count, &len)
print("START_LISTEN 433.92: ", terminator: "")
printHex(Array(buf[0..<len]))

lil_pack_stop(&buf, buf.count, &len)
print("STOP:                ", terminator: "")
printHex(Array(buf[0..<len]))

lil_pack_ping(&buf, buf.count, &len)
print("PING:                ", terminator: "")
printHex(Array(buf[0..<len]))

var sig = lil_signal_t()
sig.protocol = 1
sig.key = 0xA1B2C5
sig.bits = 24
sig.pulse_length = 350
sig.freq = 433.92

lil_pack_play(&sig, &buf, buf.count, &len)
print("PLAY P1 0xA1B2C5:    ", terminator: "")
printHex(Array(buf[0..<len]))

// Round-trip: unpack
var notify = Array(buf[0..<17])
notify[0] = UInt8(LIL_SIGNAL_HEADER)
var decoded = lil_signal_t()
let st = lil_unpack_signal(notify, notify.count, &decoded)
let ok = st == LIL_OK && decoded.protocol == 1 && decoded.key == 0xA1B2C5 && decoded.bits == 24
print("Unpack round-trip:   proto=\(decoded.protocol) key=0x\(String(decoded.key, radix: 16, uppercase: true)) bits=\(decoded.bits) [\(ok ? "PASS" : "FAIL")]")

// --- Protocols ---
print("\n=== Protocol Table (\(lil_protocol_count()) entries) ===\n")
for i in 0..<lil_protocol_count() {
    guard let p = lil_protocol_at(i) else { continue }
    let pp = p.pointee
    let name = String(cString: pp.name)
    print(String(format: "  %2d  %-14s  T=%4d  sync=[%d,%d]", pp.id, name, pp.pulse_length, pp.sync_h, pp.sync_l))
}

// --- Decoder ---
print("\n=== Decoder ===\n")

guard let pt = lil_protocol_by_id(1) else {
    print("PT2262 not found!")
    exit(1)
}

let T = Int(pt.pointee.pulse_length)
var raw = [UInt16]()
raw.append(UInt16(T))
raw.append(UInt16(T * Int(pt.pointee.sync_l)))

let code: UInt64 = 0xA1B2C5
for b in stride(from: 23, through: 0, by: -1) {
    let bit = (code >> b) & 1
    if bit == 1 {
        raw.append(UInt16(T * Int(pt.pointee.one_h)))
        raw.append(UInt16(T * Int(pt.pointee.one_l)))
    } else {
        raw.append(UInt16(T * Int(pt.pointee.zero_h)))
        raw.append(UInt16(T * Int(pt.pointee.zero_l)))
    }
}

var out = lil_signal_t()
let decSt = raw.withUnsafeBufferPointer { ptr in
    lil_decode_raw_timings(ptr.baseAddress, raw.count, 1, 433.92, &out)
}

if decSt == LIL_OK {
    let pass = out.protocol == 1 && out.key == 0xA1B2C5 && out.bits == 24
    print("  PT2262 0xA1B2C5 -> P\(out.protocol) key=0x\(String(out.key, radix: 16, uppercase: true)) \(out.bits)bit [\(pass ? "PASS" : "MISMATCH")]")
} else {
    print("  PT2262 0xA1B2C5 -> NO MATCH [FAIL]")
}

// --- Status ---
print("\n=== Status ===\n")
var statusPkt: [UInt8] = [0x01, 0x00]
var state = LIL_STATE_IDLE
var error = LIL_DEV_ERR_NONE
let stSt = lil_unpack_status(statusPkt, 2, &state, &error)
let stOk = stSt == LIL_OK && state == LIL_STATE_LISTENING && error == LIL_DEV_ERR_NONE
print("  Status [0x01,0x00] -> state=\(state.rawValue) error=\(error.rawValue) [\(stOk ? "PASS" : "FAIL")]")

print("\n================================================")
print("All checks passed.")
