import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone

import requests

from harness import (CONFIG, ROOT, TEST_INPUT, Failed, check, ensure_test_scene, launch_obs, preserved_config, targets,
                     vendor, wait_for, wait_state)

CAPTURES = ROOT.parent.parent / "docs" / "manual" / "capturas"
DESTINATIONS = ["facebook", "youtube", "tiktok"]
API_B = "http://127.0.0.1:9998"
RADIO_RECEIVER = "http://127.0.0.1:8010"

TEMPLATES = [
    ("Ensayo general · {fecha}", "Prueba de cámaras y sonido."),
    ("Conferencia · {fecha}", "Charla y preguntas del público."),
    ("Transmisión en vivo · {fecha}", "En vivo desde nuestro auditorio. ¡Gracias por acompañarnos!"),
]

RADIO_EXAMPLE = {"host": "radio.ejemplo.com", "port": 8000, "mount": "/en-vivo", "user": "source", "password": "ejemplo",
                 "format": "aac", "bitrate": 192, "sample_rate": 48000, "channels": 2, "track": 1,
                 "station_name": "Radio de ejemplo"}


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


def background(*command) -> subprocess.Popen:
    return subprocess.Popen(list(command), cwd=ROOT / "out", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def alive(url: str, method: str = "GET") -> bool:
    try:
        return requests.request(method, url, timeout=2).ok
    except requests.RequestException:
        return False


def today_title() -> str:
    return "Transmisión en vivo · " + datetime.now().strftime("%d/%m/%Y")


def main() -> int:
    CAPTURES.mkdir(parents=True, exist_ok=True)
    for old in CAPTURES.glob("*.png"):
        old.unlink()
    with preserved_config():
        (CONFIG / "journal.json").unlink(missing_ok=True)
        tiktok_receiver = background("mediamtx", str(ROOT / "mediamtx-b.yml"))
        radio_receiver = background(sys.executable, str(ROOT / "icecast_receiver.py"), "8010")
        try:
            wait_for(lambda: alive(f"{API_B}/v3/paths/list"), 10)
            wait_for(lambda: alive(f"{RADIO_RECEIVER}/state", "CONTROL"), 10)
            return capture(tiktok_receiver)
        finally:
            tiktok_receiver.terminate()
            radio_receiver.terminate()


def capture(tiktok_receiver: subprocess.Popen) -> int:
    client = launch_obs(demo=True)
    ensure_test_scene(client)
    vendor(client, "SetSelected", {"selection": {**{d: True for d in DESTINATIONS}, "radio": True}})
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
    vendor(client, "RadioConfigure", {"config": RADIO_EXAMPLE})
    starts = (datetime.now(timezone.utc) + timedelta(hours=2)).replace(minute=0, second=0, microsecond=0)
    vendor(client, "ScheduleCreate", {"title": "Transmisión en vivo · " + starts.astimezone().strftime("%d/%m/%Y"),
                                      "description": TEMPLATES[2][1], "starts_at": starts.isoformat(),
                                      "providers": "facebook,youtube"})
    check(wait_for(lambda: vendor(client, "ScheduleList")["entries"], 10), "transmisión programada de ejemplo")

    print("\n1. Antes de transmitir")
    status_bar(client, "01-barra-lista")
    vendor(client, "ClickStreamButton")
    check(wait_for(lambda: modal(client) == "multistream-start", 5), "se abre la ventana Transmitir")
    snap(client, "02-ventana-transmitir", "modal")
    vendor(client, "DismissModal")
    wait_for(lambda: modal(client) == "", 5)
    vendor(client, "OpenScheduleWindow", {"title": "Conferencia · {fecha}", "description": TEMPLATES[1][1]})
    check(wait_for(lambda: modal(client) == "multistream-schedule", 5), "se abre Programar transmisión")
    snap(client, "11-programar", "modal")
    vendor(client, "DismissModal")
    wait_for(lambda: modal(client) == "", 5)
    vendor(client, "OpenAddCustom")
    check(wait_for(lambda: modal(client) == "multistream-custom", 5), "se abre Agregar destino RTMP")
    snap(client, "12-destino-rtmp", "modal")
    vendor(client, "DismissModal")
    wait_for(lambda: modal(client) == "", 5)
    vendor(client, "OpenRadioSettings")
    check(wait_for(lambda: modal(client) == "multistream-radio", 5), "se abre la configuración de la radio")
    snap(client, "10-configurar-radio", "modal")
    vendor(client, "DismissModal")
    wait_for(lambda: modal(client) == "", 5)
    vendor(client, "OpenStatusWindow")
    time.sleep(1)
    snap(client, "03-estado-listo", "status-window")

    print("\n2. Al aire")
    vendor(client, "RadioConfigure", {"config": {**RADIO_EXAMPLE, "host": "localhost", "port": 8010}})
    requests.request("CONTROL", f"{RADIO_RECEIVER}/set", json={"user": "source", "password": "ejemplo"}, timeout=5)
    vendor(client, "Start", {"title": today_title(), "description": TEMPLATES[2][1]})
    vendor(client, "RadioStart", {"title": today_title()})
    check(wait_state(client, "live", 30), "la sesión sale al aire")
    check(wait_for(lambda: all(targets(client)[d]["state"] == "LIVE_CONFIRMED" for d in DESTINATIONS), 30), "los tres confirman")
    check(wait_for(lambda: vendor(client, "RadioStatus")["state"] == "on_air", 15), "la radio sale al aire")
    time.sleep(6)
    status_bar(client, "04-barra-al-aire")
    snap(client, "05-estado-al-aire", "status-window")
    geometry = vendor(client, "DevGeometry")
    snap(client, "06-boton-finalizar", "main", geometry.get("controls"))
    obs_window("00-obs-al-aire")
    client.set_input_mute(TEST_INPUT, True)
    check(wait_for(lambda: vendor(client, "AlertStatus")["silent"], 35), "aviso de silencio")
    time.sleep(2)
    status_bar(client, "13-barra-sin-audio")
    client.set_input_mute(TEST_INPUT, False)
    wait_for(lambda: not vendor(client, "AlertStatus")["silent"], 10)

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
    vendor(client, "RadioStop")
    check(wait_state(client, {"idle", "attention"}, 30), "la sesión termina")
    print(f"\nCapturas en {CAPTURES}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        sys.exit(1)
