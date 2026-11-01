import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

import requests

from harness import (ROOT, Failed, check, ensure_test_scene, launch_obs, quit_obs, session, targets, vendor, wait_for,
                     wait_state)

CAPTURES = ROOT.parent.parent / "docs" / "manual" / "capturas"
CONFIG = Path.home() / "Library/Application Support/obs-studio/plugin_config/obs-multistream"
BACKUP = ROOT / "out" / "config-backup"
DESTINATIONS = ["facebook", "youtube", "tiktok"]
API_B = "http://127.0.0.1:9998"

TEMPLATES = [
    ("Estudio bíblico · {fecha}", "Estudio del libro de Juan, capítulo 3."),
    ("Reunión de jóvenes · {fecha}", "Alabanza y palabra para jóvenes. ¡Te esperamos!"),
    ("Culto dominical · {fecha}", "Culto de adoración en vivo desde Mi página."),
]


def snap(client, name: str, target: str, crop: dict | None = None):
    data = {"target": target, "path": str(CAPTURES / f"{name}.png")}
    if crop:
        data["crop"] = crop
    result = vendor(client, "DevSnapshot", data)
    check(result.get("ok"), f"captura {name}.png")


def status_bar(client, name: str):
    geometry = vendor(client, "DevGeometry")
    snap(client, name, "status-bar", {"x": 0, "y": 0, "width": geometry["strip_right"] + 24, "height": geometry["bar_height"]})


def obs_window(name: str):
    found = subprocess.run(["swift", str(ROOT / "window_id.swift"), "OBS Studio"], capture_output=True, text=True)
    if found.returncode != 0:
        print(f"  · sin captura {name}: no encontré la ventana de OBS")
        return
    subprocess.run(["screencapture", "-x", "-o", f"-l{found.stdout.strip()}", str(CAPTURES / f"{name}.png")], check=False)
    check((CAPTURES / f"{name}.png").exists(), f"captura {name}.png")


def modal(client) -> str:
    return vendor(client, "GetModal")["name"]


def close_obs():
    try:
        quit_obs()
    except Failed:
        subprocess.run(["pkill", "-x", "OBS"], check=False)
        wait_for(lambda: subprocess.run(["pgrep", "-xq", "OBS"]).returncode != 0, 15)


def receiver_b() -> subprocess.Popen:
    process = subprocess.Popen(["mediamtx", str(ROOT / "mediamtx-b.yml")], cwd=ROOT / "out",
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def alive():
        try:
            return requests.get(f"{API_B}/v3/paths/list", timeout=2).ok
        except requests.RequestException:
            return False

    wait_for(alive, 10)
    return process


def today_title() -> str:
    return "Culto dominical · " + datetime.now(ZoneInfo("America/Guatemala")).strftime("%d/%m/%Y")


def main() -> int:
    quit_obs()
    shutil.rmtree(BACKUP, ignore_errors=True)
    shutil.copytree(CONFIG, BACKUP)
    CAPTURES.mkdir(parents=True, exist_ok=True)
    tiktok_receiver = receiver_b()
    try:
        return capture(tiktok_receiver)
    finally:
        tiktok_receiver.terminate()
        close_obs()
        shutil.rmtree(CONFIG)
        shutil.copytree(BACKUP, CONFIG)
        print("  · configuración real restaurada")


def capture(tiktok_receiver: subprocess.Popen) -> int:
    (CONFIG / "journal.json").unlink(missing_ok=True)
    client = launch_obs(demo=True)
    ensure_test_scene(client)
    vendor(client, "SetSelected", {"selection": {d: True for d in DESTINATIONS}})
    vendor(client, "SetOptions", {
        "profile": "balanced",
        "reconnect": {"retries": 2, "delay_seconds": 1},
        "providers": {
            "youtube": {"privacy": "public", "made_for_kids": "no"},
            "facebook": {"hide_after_end": True},
        },
    })
    vendor(client, "DevSetBehavior", {"provider": "tiktok", "behavior": {"port": 1936}})
    for title, description in TEMPLATES:
        vendor(client, "DevRemember", {"title": title, "description": description})

    print("\n1. Antes de transmitir")
    status_bar(client, "01-barra-lista")
    vendor(client, "ClickStreamButton")
    check(wait_for(lambda: modal(client) == "multistream-start", 5), "se abre la ventana Transmitir")
    snap(client, "02-ventana-transmitir", "modal")
    vendor(client, "DismissModal")
    wait_for(lambda: modal(client) == "", 5)
    vendor(client, "OpenStatusWindow")
    time.sleep(1)
    snap(client, "03-estado-listo", "status-window")

    print("\n2. Al aire")
    vendor(client, "Start", {"title": today_title(), "description": TEMPLATES[2][1]})
    check(wait_state(client, "live", 30), "la sesión sale al aire")
    check(wait_for(lambda: all(targets(client)[d]["state"] == "LIVE_CONFIRMED" for d in DESTINATIONS), 30), "los tres confirman")
    wait_for(lambda: all(targets(client)[d].get("stats", {}).get("total_bytes", 1) > 0 for d in DESTINATIONS), 10)
    time.sleep(6)
    status_bar(client, "04-barra-al-aire")
    snap(client, "05-estado-al-aire", "status-window")
    geometry = vendor(client, "DevGeometry")
    snap(client, "06-boton-finalizar", "main", geometry.get("controls"))
    obs_window("00-obs-al-aire")

    print("\n3. Se cae un destino")
    tiktok_receiver.terminate()
    tiktok_receiver.wait(10)
    check(wait_for(lambda: targets(client)["tiktok"]["state"] == "FAILED", 40), "TikTok queda sin señal")
    check(wait_state(client, "partial", 10), "la sesión queda parcial")
    status_bar(client, "07-barra-parcial")
    snap(client, "08-estado-parcial", "status-window")

    print("\n4. Finalizar")
    vendor(client, "ClickStreamButton")
    check(wait_for(lambda: modal(client) == "multistream-confirm-stop", 5), "pide confirmación")
    snap(client, "09-confirmar-fin", "modal")
    vendor(client, "DismissModal")
    wait_for(lambda: modal(client) == "", 5)
    vendor(client, "Stop")
    check(wait_state(client, {"idle", "attention"}, 30), "la sesión termina")
    print(f"\nCapturas en {CAPTURES}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        sys.exit(1)
