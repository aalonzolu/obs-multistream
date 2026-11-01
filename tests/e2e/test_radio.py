import json
import subprocess
import sys
import time

import requests

from harness import (ROOT, Failed, check, ensure_test_scene, launch_obs, preserved_config, quit_obs, session, vendor,
                     wait_for, wait_state)

RECEIVER = "http://127.0.0.1:8010"
LOCAL = {"host": "127.0.0.1", "port": 8010, "mount": "/", "user": "source", "password": "prueba", "legacy_source": False}
FORMATS = [
    ("mp3", 192, "mp3", "audio/mpeg"),
    ("aac", 256, "aac", "audio/aac"),
    ("opus", 128, "opus", "audio/ogg"),
    ("vorbis", 192, "vorbis", "audio/ogg"),
    ("flac", 0, "flac", "audio/ogg"),
]


def control(path: str, body: dict | None = None) -> dict:
    response = requests.request("CONTROL", f"{RECEIVER}{path}", data=json.dumps(body or {}), timeout=5)
    return response.json() if response.content else {}


def connections() -> list[dict]:
    return [c for c in control("/state")["connections"] if c["method"] in ("PUT", "SOURCE")]


def metadata() -> list[dict]:
    return control("/state")["metadata"]


def radio(client) -> dict:
    return vendor(client, "RadioStatus")


def configure(client, **overrides):
    result = vendor(client, "RadioConfigure", {"config": {**LOCAL, **overrides}})
    check(result.get("ok"), f"radio configurada: {result.get('summary')}")


def probe(path: str) -> dict:
    result = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=codec_name,sample_rate,channels",
                             "-of", "json", path], capture_output=True, text=True, timeout=20)
    streams = json.loads(result.stdout or "{}").get("streams", [])
    return streams[0] if streams else {}


def run_format(client, format_id: str, bitrate: int, codec: str, content_type: str):
    control("/set", {"reset": True})
    configure(client, format=format_id, bitrate=bitrate or 128)
    vendor(client, "RadioStart", {"title": f"Prueba {format_id}"})
    check(wait_for(lambda: radio(client)["state"] == "on_air", 10), f"{format_id}: al aire")
    check(wait_for(lambda: radio(client)["bytes"] > 20000, 15), f"{format_id}: envía datos ({radio(client)['bytes']} bytes)")
    time.sleep(3)
    vendor(client, "RadioStop")
    check(wait_for(lambda: radio(client)["state"] == "off", 10), f"{format_id}: se detiene")
    received = connections()[-1]
    check(received["content_type"] == content_type, f"{format_id}: Content-Type {received['content_type']}")
    check(received["ice_description"] == f"Prueba {format_id}", f"{format_id}: el título viaja como descripción")
    if format_id == "mp3":
        check(any("song=Prueba" in m["query"] and m["authorization"] for m in metadata()),
              "mp3: el título se publica como metadato (updinfo)")
    stream = probe(received["file"])
    check(stream.get("codec_name") == codec, f"{format_id}: el servidor recibe {stream.get('codec_name')} "
                                             f"{stream.get('sample_rate')} Hz, {stream.get('channels')} canales")


def main() -> int:
    with preserved_config():
        return scenarios()


def scenarios() -> int:
    receiver = subprocess.Popen([sys.executable, str(ROOT / "icecast_receiver.py"), "8010"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_for(lambda: _alive(), 10)
        client = launch_obs(dev_targets=True)
        ensure_test_scene(client)
        vendor(client, "RadioStop")

        print("\n1. Formatos")
        for format_id, bitrate, codec, content_type in FORMATS:
            run_format(client, format_id, bitrate, codec, content_type)

        print("\n2. Servidor que no acepta PUT: cambia solo a SOURCE")
        control("/set", {"reset": True, "reject_put": True})
        configure(client, format="mp3", bitrate=128, legacy_source=False)
        vendor(client, "RadioStart", {"title": "Prueba SOURCE"})
        check(wait_for(lambda: radio(client)["state"] == "on_air", 10), "al aire con SOURCE")
        check(radio(client)["legacy_source"], "recuerda el método que funcionó")
        methods = [c["method"] for c in connections()]
        check(methods[:2] == ["PUT", "SOURCE"], f"intentos: {methods}")
        vendor(client, "RadioStop")
        wait_for(lambda: radio(client)["state"] == "off", 10)
        control("/set", {"reject_put": False})

        print("\n3. Contraseña incorrecta")
        control("/set", {"reset": True})
        configure(client, password="otra")
        vendor(client, "RadioStart", {"title": "Prueba clave"})
        check(wait_for(lambda: radio(client)["state"] == "failed", 10), f"falla: «{radio(client)['message']}»")
        attempts = len(connections())
        time.sleep(6)
        check(len(connections()) == attempts, "no insiste con una contraseña rechazada")
        configure(client, password="prueba")

        print("\n4. Corte de conexión: reconecta solo")
        control("/set", {"reset": True, "drop_after": 4})
        vendor(client, "RadioStart", {"title": "Prueba corte"})
        check(wait_for(lambda: radio(client)["state"] == "on_air", 10), "al aire")
        check(wait_for(lambda: radio(client)["state"] == "reconnecting", 15), f"detecta el corte: «{radio(client)['message']}»")
        control("/set", {"drop_after": 0})
        check(wait_for(lambda: radio(client)["state"] == "on_air", 20), "vuelve al aire")
        check(radio(client)["reconnects"] == 1, "cuenta una reconexión")
        vendor(client, "RadioStop")
        wait_for(lambda: radio(client)["state"] == "off", 10)

        print("\n5. Radio independiente del video")
        control("/set", {"reset": True})
        vendor(client, "SetSelected", {"selection": {"facebook": False, "youtube": False, "tiktok": False,
                                                     "local-a": True, "local-b": False}})
        vendor(client, "RadioStart", {"title": "Culto"})
        check(wait_for(lambda: radio(client)["state"] == "on_air", 10), "la radio sale sola")
        vendor(client, "Start", {"title": "Culto"})
        check(wait_state(client, "live", 20), "el video se suma después")
        vendor(client, "Stop")
        check(wait_state(client, "idle", 20), "el video termina")
        check(radio(client)["state"] == "on_air", "la radio sigue al aire")
        bytes_before = radio(client)["bytes"]
        check(wait_for(lambda: radio(client)["bytes"] > bytes_before, 5), "y sigue enviando")
        vendor(client, "RadioStop")
        check(wait_for(lambda: radio(client)["state"] == "off", 10), "la radio se detiene por separado")
        check(session(client)["state"] == "idle", "sin afectar al video")

        quit_obs()
        print("\nRadio: todo bien")
        return 0
    finally:
        receiver.terminate()


def _alive() -> bool:
    try:
        control("/state")
        return True
    except requests.RequestException:
        return False


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        sys.exit(1)
