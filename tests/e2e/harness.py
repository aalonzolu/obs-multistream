import json
import logging
import subprocess
import time
from pathlib import Path

import obsws_python as obs
import requests

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "out"
TEST_COLLECTION = "Multistream Pruebas"
TEST_INPUT = "Tarjeta de prueba"
VENDOR = "obs-multistream"
MEDIAMTX_API = "http://127.0.0.1:9997"
LOCAL_RTMP = "rtmp://127.0.0.1:1935/live"

logging.getLogger("obsws_python").setLevel(logging.CRITICAL)


class Failed(AssertionError):
    pass


def check(condition: bool, message: str):
    if not condition:
        raise Failed(message)
    print(f"  ✓ {message}", flush=True)


def obs_running() -> bool:
    return subprocess.run(["pgrep", "-xq", "OBS"]).returncode == 0


def launch_obs(timeout: float = 60, dev_targets: bool = False) -> obs.ReqClient:
    if not obs_running():
        env = ["--env", "MULTISTREAM_DEV_TARGETS=1"] if dev_targets else []
        subprocess.run(["open", "-a", "OBS", *env, "--args", "--disable-shutdown-check"], check=True)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            client = obs.ReqClient(host="127.0.0.1", port=4455, password=(OUT / "ws-password").read_text(), timeout=10)
            client.get_scene_collection_list()
            return client
        except Exception:
            time.sleep(1)
    raise Failed("OBS no abrió obs-websocket a tiempo")


def stop_session_if_active():
    try:
        client = obs.ReqClient(host="127.0.0.1", port=4455, password=(OUT / "ws-password").read_text(), timeout=5)
        state = vendor(client, "GetStatus")["session"]["state"]
        if state in ("checking", "preparing", "live", "partial"):
            vendor(client, "Stop")
            wait_for(lambda: vendor(client, "GetStatus")["session"]["state"] in ("idle", "attention"), 30)
    except Exception:
        pass


def quit_obs(timeout: float = 30):
    if not obs_running():
        return
    stop_session_if_active()
    subprocess.run(["osascript", "-e", 'tell application "OBS" to quit'], check=False, capture_output=True)
    deadline = time.monotonic() + timeout
    while obs_running() and time.monotonic() < deadline:
        time.sleep(0.5)
    if obs_running():
        raise Failed("OBS no se cerró a tiempo")


def vendor(client: obs.ReqClient, request_type: str, data: dict | None = None) -> dict:
    response = client.call_vendor_request(VENDOR, request_type, data or {})
    return response.response_data


def ensure_test_scene(client: obs.ReqClient):
    collections = client.get_scene_collection_list()
    if collections.current_scene_collection_name != TEST_COLLECTION:
        if TEST_COLLECTION in collections.scene_collections:
            client.set_current_scene_collection(TEST_COLLECTION)
        else:
            client.create_scene_collection(TEST_COLLECTION)
        time.sleep(2)
    inputs = [i["inputName"] for i in client.get_input_list().inputs]
    if TEST_INPUT not in inputs:
        scene = client.get_current_program_scene().scene_name
        client.create_input(scene, TEST_INPUT, "ffmpeg_source", {
            "local_file": str(OUT / "testcard.mp4"),
            "looping": True,
            "restart_on_activate": False,
        }, True)


def mediamtx_paths() -> dict[str, dict]:
    items = requests.get(f"{MEDIAMTX_API}/v3/paths/list", timeout=5).json().get("items", [])
    return {item["name"]: item for item in items}


def path_bytes(name: str) -> int:
    item = mediamtx_paths().get(name, {})
    return item.get("inboundBytes", item.get("bytesReceived", 0))


def wait_for(predicate, timeout: float, interval: float = 0.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval)
    return predicate()


def probe(url: str) -> dict:
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-rw_timeout", "8000000", "-show_entries",
         "stream=codec_name,width,height,sample_rate,channels,avg_frame_rate", "-of", "json", url],
        capture_output=True, text=True, timeout=20,
    )
    return json.loads(result.stdout or "{}")


def session(client) -> dict:
    return vendor(client, "GetStatus")["session"]


def targets(client) -> dict[str, dict]:
    return {t["id"]: t for t in session(client)["targets"]}


def wait_state(client, expected: str | set[str], timeout: float = 20) -> bool:
    wanted = {expected} if isinstance(expected, str) else expected
    return bool(wait_for(lambda: session(client)["state"] in wanted, timeout))
