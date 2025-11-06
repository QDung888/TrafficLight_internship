#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
esp32_pkt_tool.py — Build JSON packets (with MD5 auth) for your ESP32 protocol,
optionally SEND over a serial port, and verify response MD5.

New:
- build: thêm --port/--baud/--newline/--read-seconds để tự mở COM và gửi ngay
- JSON output: sắp xếp key theo thứ tự id_src,id_des,opcode,time,data,auth (auth ở cuối)
"""

import argparse, json, time, hashlib, sys
from collections import OrderedDict

DEFAULT_KEY = "my_secret_key"
DEFAULT_ID_SRC = 1
DEFAULT_ID_DES = 2

def md5_hex(s: str) -> str:
    return hashlib.md5(s.encode('utf-8')).hexdigest()

def json_min(obj) -> str:
    # Compact form giống ArduinoJson serializeJson (không khoảng trắng)
    return json.dumps(obj, separators=(',',':'), ensure_ascii=False)

def make_auth(id_src:int, id_des:int, opcode:int, data_obj, t:int, key:str) -> str:
    data_str = json_min(data_obj) if data_obj is not None else "null"
    raw = f"{id_src}{id_des}{opcode}{data_str}{t}{key}"
    return md5_hex(raw)

# ---------- Serial helpers ----------
def send_over_serial(payload: str, port: str, baud: int = 115200, newline: str = "LF", read_seconds: float = 0.0):
    try:
        import serial   # pyserial
        import serial.tools.list_ports
    except Exception:
        sys.exit("[ERROR] pyserial chưa được cài. Cài bằng: pip install pyserial")

    # Newline mapping
    if newline.upper() == "LF":
        end = "\n"
    elif newline.upper() == "CRLF":
        end = "\r\n"
    elif newline.upper() == "CR":
        end = "\r"
    else:
        sys.exit(f"[ERROR] --newline chỉ nhận LF | CRLF | CR (bạn đưa: {newline})")

    try:
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.1)
    except Exception as e:
        sys.exit(f"[ERROR] Không mở được cổng {port} @ {baud} bps: {e}")

    try:
        # Xoá input buffer để không lẫn dữ liệu cũ
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Gửi
        to_send = payload.rstrip("\r\n") + end
        ser.write(to_send.encode("utf-8"))
        ser.flush()
        print(f"[INFO] ĐÃ GỬI -> {port} @ {baud}:")
        print(to_send, end='')

        # Đọc phản hồi trong khoảng thời gian cho phép
        if read_seconds and read_seconds > 0:
            print(f"[INFO] Đang chờ phản hồi trong {read_seconds:.1f}s ...")
            t0 = time.time()
            buf = bytearray()
            while time.time() - t0 < read_seconds:
                chunk = ser.read(1024)
                if chunk:
                    buf.extend(chunk)
            if buf:
                try:
                    txt = buf.decode("utf-8", errors="replace")
                except Exception:
                    txt = str(buf)
                print("\n[RESP] ----------------")
                print(txt)
                print("[RESP] ----------------")
            else:
                print("[INFO] Không nhận được phản hồi trong thời gian chờ.")
    finally:
        ser.close()

# ---------- Build ----------
def build_packet(args):
    id_src = args.id_src
    id_des = args.id_des
    opcode = args.opcode
    key    = args.key
    t      = int(args.time) if args.time is not None else int(time.time())

    # Construct data per opcode
    data = None
    if opcode == 1:
        if args.lamp is None or args.color is None:
            sys.exit("Opcode 1 requires --lamp and --color (R|Y|G)")
        data = {"lamp": int(args.lamp), "color": str(args.color).upper()}
    elif opcode == 2:
        if not args.pairs:
            sys.exit("Opcode 2 requires --pairs like: 1:R 2:Y 3:G")
        cmds = []
        for pair in args.pairs:
            try:
                lamp_str, col = pair.split(':', 1)
                cmds.append({"lamp": int(lamp_str), "color": col.upper()})
            except Exception:
                sys.exit(f"Bad --pairs item: {pair}. Expected lamp:color")
        data = {"commands": cmds}
    elif opcode == 3:
        data = {}
    elif opcode == 4:
        data = {}
    elif opcode == 5:
        if args.set not in ("auto", "manual"):
            sys.exit("Opcode 5 requires --set auto|manual")
        data = {"set": args.set}
    else:
        sys.exit("opcode must be 1..5")

    # Tính MD5
    auth = make_auth(id_src, id_des, opcode, data, t, key)

    # Sắp xếp khóa theo thứ tự mong muốn (auth ở CUỐI)
    pkt = OrderedDict()
    pkt["id_src"] = id_src
    pkt["id_des"] = id_des
    pkt["opcode"] = opcode
    pkt["time"]   = t
    pkt["data"]   = data
    pkt["auth"]   = auth

    out = json_min(pkt)

    # In ra STDOUT (để bạn có thể pipe hoặc xem lại)
    if not args.port:
        sys.stdout.write(out + "\n")
    else:
        # Gửi qua Serial nếu chỉ định --port
        send_over_serial(out, port=args.port, baud=args.baud, newline=args.newline, read_seconds=args.read_seconds)

# ---------- Verify ----------
def verify_response(args):
    try:
        req = json.loads(args.request_json)
        resp = json.loads(args.response_json)
    except Exception as e:
        sys.exit(f"JSON parse error: {e}")

    # Extract request parts
    try:
        id_src_req = int(req["id_src"])
        id_des_req = int(req["id_des"])
        opcode_req = int(req["opcode"])
        data_req   = req["data"]
    except Exception as e:
        sys.exit(f"Bad request JSON fields: {e}")

    key = args.key

    # Extract response time and auth
    try:
        time_resp = int(resp["time"])
        auth_resp = str(resp["auth"])
    except Exception as e:
        sys.exit(f"Bad response JSON fields: {e}")

    # ESP32 response MD5 is computed with original opcode & data of the *request*
    expected = make_auth(id_src_req, id_des_req, opcode_req, data_req, time_resp, key)

    ok = (expected.lower() == auth_resp.lower())
    status = resp.get("status", None)
    print(json.dumps({
        "md5_match": ok,
        "expected_auth": expected,
        "response_auth": auth_resp,
        "response_status": status,
        "note": "Match=True means response integrity OK and key/opcode/data/time formula consistent."
    }, indent=2, ensure_ascii=False))

# ---------- CLI ----------
def main():
    p = argparse.ArgumentParser(description="ESP32 JSON packet builder & serial sender & response MD5 verifier")
    sub = p.add_subparsers(dest="cmd", required=True)

    pb = sub.add_parser("build", help="Build a request packet (and optionally SEND over Serial)")
    pb.add_argument("--opcode", type=int, required=True, help="1..5")
    pb.add_argument("--lamp", type=int, help="For opcode 1")
    pb.add_argument("--color", type=str, help="R|Y|G for opcode 1 or inside --pairs")
    pb.add_argument("--pairs", nargs="*", help="For opcode 2: list like 1:R 2:Y 3:G")
    pb.add_argument("--set", type=str, choices=["auto","manual"], help="For opcode 5")
    pb.add_argument("--id-src", dest="id_src", type=int, default=DEFAULT_ID_SRC)
    pb.add_argument("--id-des", dest="id_des", type=int, default=DEFAULT_ID_DES)
    pb.add_argument("--key", type=str, default=DEFAULT_KEY)
    pb.add_argument("--time", type=int, help="Epoch seconds; default now()")

    # Serial options (optional). Nếu khai báo --port thì tool sẽ mở COM và gửi ngay.
    pb.add_argument("--port", type=str, help="VD: COM3 (Windows) hoặc /dev/ttyUSB0 (Linux)")
    pb.add_argument("--baud", type=int, default=115200, help="Baudrate (default 115200)")
    pb.add_argument("--newline", type=str, default="LF", help="LF | CRLF | CR (default LF)")
    pb.add_argument("--read-seconds", type=float, default=0.0, help="Sau khi gửi, chờ đọc phản hồi N giây (0 = không đọc)")

    pb.set_defaults(func=build_packet)

    pv = sub.add_parser("verify", help="Verify a response MD5 using the original request JSON")
    pv.add_argument("--request-json", required=True, help="Minified request JSON string you sent")
    pv.add_argument("--response-json", required=True, help="Response JSON string from ESP32")
    pv.add_argument("--key", type=str, default=DEFAULT_KEY)
    pv.set_defaults(func=verify_response)

    args = p.parse_args()
    args.func(args)

if __name__ == "__main__":
    main()
