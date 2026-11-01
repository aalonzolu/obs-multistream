import subprocess
import sys
import time
from pathlib import Path

import requests

from harness import Failed, check, launch_obs, quit_obs, session, targets, vendor, wait_for, wait_state

ROOT = Path(__file__).resolve().parent
API_A = "http://127.0.0.1:9997"
API_B = "http://127.0.0.1:9998"


def start_receiver_b() -> subprocess.Popen:
    process = subprocess.Popen(["mediamtx", str(ROOT / "mediamtx-b.yml")], cwd=ROOT / "out",
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wait_for(lambda: requests.get(f"{API_B}/v3/paths/list", timeout=2).ok if _alive(API_B) else False, 10)
    return process


def _alive(api: str) -> bool:
    try:
        return requests.get(f"{api}/v3/paths/list", timeout=2).ok
    except requests.RequestException:
        return False


def path_bytes(api: str, name: str) -> int:
    try:
        items = requests.get(f"{api}/v3/paths/list", timeout=3).json().get("items", [])
    except requests.RequestException:
        return 0
    item = next((i for i in items if i["name"] == f"live/{name}"), {})
    return item.get("inboundBytes", item.get("bytesReceived", 0))


def kick(api: str, name: str) -> bool:
    for _ in range(5):
        conns = requests.get(f"{api}/v3/rtmpconns/list", timeout=3).json().get("items", [])
        conn = next((c for c in conns if c.get("path") == f"live/{name}" and c.get("state") == "publish"), None)
        if conn and requests.post(f"{api}/v3/rtmpconns/kick/{conn['id']}", timeout=3).ok:
            return True
        time.sleep(1)
    return False


def state(client, name: str) -> str:
    return targets(client)[name]["state"]


def still_flowing(api: str, name: str, seconds: float = 3) -> bool:
    before = path_bytes(api, name)
    time.sleep(seconds)
    return path_bytes(api, name) > before


def main() -> int:
    receiver_b = None
    quit_obs()
    try:
        receiver_b = start_receiver_b()
        check(_alive(API_A) and _alive(API_B), "los dos receptores RTMP están arriba")
        client = launch_obs(dev_targets=True)
        vendor(client, "SetSelected", {"selection": {"facebook": False, "youtube": False, "tiktok": False, "local-a": True, "local-b": True}})
        vendor(client, "SetOptions", {"reconnect": {"retries": 3, "delay_seconds": 1}})
        vendor(client, "DevSetBehavior", {"provider": "local-a", "behavior": {}})
        vendor(client, "DevSetBehavior", {"provider": "local-b", "behavior": {"port": 1936}})
        vendor(client, "Start", {"title": "Prueba reconexión"})
        check(wait_for(lambda: state(client, "local-a") in ("SENDING", "LIVE_CONFIRMED") and
                       state(client, "local-b") in ("SENDING", "LIVE_CONFIRMED"), 25), "los dos destinos envían señal")
        id_a = targets(client)["local-a"]["remote_id"]
        id_b = targets(client)["local-b"]["remote_id"]

        print("\n1. Corte breve en A: se reconecta sola al mismo directo")
        check(kick(API_A, "local-a"), "el receptor A corta la conexión de A")
        check(wait_for(lambda: state(client, "local-a") in ("SENDING", "LIVE_CONFIRMED") and
                       targets(client)["local-a"]["transport"] == "sending" and path_bytes(API_A, "local-a") > 0, 25),
              f"A vuelve a enviar (estado {state(client, 'local-a')})")
        check(still_flowing(API_A, "local-a"), "A recibe datos otra vez")
        check(targets(client)["local-a"]["remote_id"] == id_a, "A sigue en el mismo directo")
        check(still_flowing(API_B, "local-b"), "B nunca dejó de recibir")

        print("\n2. B se cae y no logra reconectar; A sigue al aire")
        receiver_b.terminate()
        receiver_b.wait(10)
        check(wait_for(lambda: state(client, "local-b") == "FAILED", 60), "B queda en FAILED tras agotar los reintentos")
        b = targets(client)["local-b"]
        check(b["can_retry"] and b["can_restart"], f"B ofrece Reintentar y Nuevo directo: «{b['message']}»")
        check(session(client)["state"] == "partial", f"la sesión queda parcial: «{session(client)['message']}»")
        check(still_flowing(API_A, "local-a"), "A sigue recibiendo datos")

        print("\n3. Vuelve la red de B: Reintentar recupera el mismo directo")
        receiver_b = start_receiver_b()
        vendor(client, "Retry", {"provider": "local-b"})
        check(wait_for(lambda: state(client, "local-b") in ("SENDING", "LIVE_CONFIRMED"), 20), "B vuelve a enviar")
        check(targets(client)["local-b"]["remote_id"] == id_b, "B sigue en el mismo directo")
        check(wait_state(client, "live", 10), "la sesión vuelve a 'live'")

        print("\n4. B cae otra vez y se crea un directo nuevo solo para B")
        receiver_b.terminate()
        receiver_b.wait(10)
        check(wait_for(lambda: state(client, "local-b") == "FAILED", 60), "B vuelve a fallar")
        receiver_b = start_receiver_b()
        finalized_before = vendor(client, "DevSetBehavior", {"provider": "local-b", "behavior": {"port": 1936}})["finalize_calls"]
        vendor(client, "RestartTarget", {"provider": "local-b"})
        check(wait_for(lambda: state(client, "local-b") in ("SENDING", "LIVE_CONFIRMED"), 20), "B envía en el directo nuevo")
        new_b = targets(client)["local-b"]["remote_id"]
        check(new_b and new_b != id_b, f"B tiene otro directo ({id_b} → {new_b})")
        check(vendor(client, "DevSetBehavior", {"provider": "local-b", "behavior": {"port": 1936}})["finalize_calls"] == finalized_before + 1,
              "se cerró el directo anterior de B")
        check(still_flowing(API_A, "local-a") and targets(client)["local-a"]["remote_id"] == id_a, "A no se tocó")

        print("\n5. La plataforma cierra el directo de A: solo se ofrece uno nuevo")
        vendor(client, "DevSetBehavior", {"provider": "local-a", "behavior": {"inspect": "ended"}})
        check(wait_for(lambda: targets(client)["local-a"]["remote_ended"], 45), "se detecta que A terminó del lado remoto")
        a = targets(client)["local-a"]
        check(a["state"] == "FAILED" and not a["can_retry"] and a["can_restart"], f"A ofrece solo Nuevo directo: «{a['message']}»")
        check(still_flowing(API_B, "local-b"), "B sigue al aire")
        vendor(client, "DevSetBehavior", {"provider": "local-a", "behavior": {}})
        vendor(client, "RestartTarget", {"provider": "local-a"})
        check(wait_for(lambda: state(client, "local-a") in ("SENDING", "LIVE_CONFIRMED"), 20), "A vuelve con un directo nuevo")
        check(targets(client)["local-a"]["remote_id"] != id_a, "A tiene otro directo")

        print("\n6. Finalizar todo")
        vendor(client, "Stop")
        check(wait_state(client, "idle", 30), "la sesión termina limpia")
        vendor(client, "SetOptions", {"reconnect": {"retries": 10, "delay_seconds": 3}})
        return 0
    finally:
        if receiver_b and receiver_b.poll() is None:
            receiver_b.terminate()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        try:
            print("  estado:", {k: (v["state"], v["message"]) for k, v in targets(launch_obs()).items() if k.startswith("local")})
        except Exception:
            pass
        sys.exit(1)
