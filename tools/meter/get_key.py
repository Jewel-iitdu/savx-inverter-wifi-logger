"""Read the grid meter's local key from the owner's Smart Life account (QR login) and check it on the LAN.

Run it through get-key.sh. Read-only: it changes nothing on the meter or in Smart Life (no re-pairing, so the
key stays valid and the app keeps working). The sign-in is the same public Smart Life QR login that the Home
Assistant Tuya integration uses; the session is not stored.

Writes (both git-ignored, in <repo>/.meter/):
  secret.json  device id, local key, protocol version, LAN IP   (secret: never commit or paste it)
  info.json    data-point map and current values                (no secrets)
"""
import json
import os
import socket
from concurrent.futures import ThreadPoolExecutor
import subprocess
import sys
import time

import qrcode
import tinytuya
from tuya_sharing import LoginControl, Manager

CLIENT_ID = "HA_3y9q4ak7g4ephrvke"  # Home Assistant's public Smart Life app registration
SCHEMA = "haauthorize"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, ".meter")


def tuya_hosts() -> list:
    """Hosts in the Mac's /24 with the Tuya LAN port 6668 open."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("10.255.255.255", 1))  # no packet is sent; picks the LAN interface
    me = s.getsockname()[0]
    s.close()
    net = me.rsplit(".", 1)[0]

    def probe(h):
        ip = f"{net}.{h}"
        try:
            with socket.create_connection((ip, 6668), timeout=0.5):
                return ip
        except OSError:
            return None

    with ThreadPoolExecutor(64) as ex:
        return [ip for ip in ex.map(probe, range(1, 255)) if ip and ip != me]


def detect(dev_id: str, ip: str, key: str):
    for ver in (3.5, 3.4, 3.3):
        d = tinytuya.Device(dev_id, address=ip, local_key=key, version=ver)
        d.set_socketTimeout(4)
        d.set_socketRetryLimit(1)
        r = d.status()
        if isinstance(r, dict) and "dps" in r:
            return ver, r
    return None, None


def main():
    print("Smart Life: Me > Settings (gear) > Account and Security > User Code.")
    user_code = input("User code: ").strip()
    lc = LoginControl()
    r = lc.qr_code(CLIENT_ID, SCHEMA, user_code)
    if not r.get("success"):
        sys.exit(f"QR request failed: {r.get('msg') or r}")
    token = r["result"]["qrcode"]
    img = os.path.join(OUT, "login-qr.png")
    os.makedirs(OUT, exist_ok=True)
    q = qrcode.QRCode(border=2)
    q.add_data(f"tuyaSmart--qrLogin?token={token}")
    q.make_image().save(img)
    subprocess.run(["open", img])
    print("\nScan the QR code that just opened with Smart Life (Home tab, scan icon top right), then confirm.")
    info = None
    for _ in range(60):
        time.sleep(3)
        ok, info = lc.login_result(token, CLIENT_ID, user_code)
        if ok:
            break
    else:
        sys.exit("No login within 3 minutes. Run it again.")
    os.remove(img)

    tok = {k: info[k] for k in ("t", "uid", "expire_time", "access_token", "refresh_token")}
    mgr = Manager(CLIENT_ID, user_code, info["terminal_id"], info["endpoint"], tok, None)
    mgr.update_device_cache()
    devs = list(mgr.device_map.values())
    if not devs:
        sys.exit("No devices in this Smart Life account.")
    print("\nDevices:")
    for i, d in enumerate(devs):
        print(f"  {i + 1}. {d.name}  ({d.category}, {d.product_name})")
    pick = devs[0] if len(devs) == 1 else devs[int(input("Number of the grid meter: ")) - 1]

    cands = sys.argv[1:] or tuya_hosts()
    print(f"\nChecking the meter on the LAN ({', '.join(cands) or 'no host with port 6668'}) ...")
    ip, ver, st = "", None, None
    for c in cands:
        ver, st = detect(pick.id, c, pick.local_key)
        if ver:
            ip = c
            break
    print(f"Meter at {ip}, local protocol {ver}" if ver else "Meter NOT reached on the LAN")

    strat = getattr(pick, "local_strategy", {}) or {}
    dp_map = {str(k): {"code": v.get("status_code"), "item": v.get("config_item")} for k, v in strat.items()}
    with open(os.path.join(OUT, "info.json"), "w") as f:
        json.dump({
            "name": pick.name, "category": pick.category, "product_id": pick.product_id,
            "product_name": pick.product_name, "version": ver, "ip": ip,
            "local_dps": (st or {}).get("dps"), "cloud_status": pick.status, "dp_map": dp_map,
            "attrs": sorted(k for k in vars(pick) if k != "local_key"),
        }, f, indent=1, default=str)
    sec = os.path.join(OUT, "secret.json")
    with open(sec, "w") as f:
        json.dump({"id": pick.id, "key": pick.local_key, "ver": ver, "ip": ip}, f)
    os.chmod(sec, 0o600)
    print(f"\nSaved .meter/secret.json and .meter/info.json. Do not paste the key in chat.")
    print("Tell Claude: done" + ("" if ver else " (meter not reached on the LAN)"))


if __name__ == "__main__":
    main()
