import sys
import time

from harness import LOCAL_RTMP, Failed, check, ensure_test_scene, launch_obs, mediamtx_paths, path_bytes, probe, vendor, wait_for

TARGETS = ["facebook", "youtube", "tiktok"]


def target(name: str, port: int = 1935) -> dict:
    return {"id": name, "server": f"rtmp://127.0.0.1:{port}/live", "key": name}


def outputs(client) -> dict[str, dict]:
    status = vendor(client, "GetStatus")
    return {o["id"]: o for o in status["engine"]["outputs"]}


def bytes_received(name: str) -> int:
    return path_bytes(f"live/{name}")


def all_sending(client, names) -> bool:
    current = outputs(client)
    return all(current.get(n, {}).get("transport") == "sending" for n in names)


def main() -> int:
    client = launch_obs()
    ensure_test_scene(client)
    status = vendor(client, "GetStatus")
    print(f"Codificadores H.264 disponibles: {status['encoders_available']}")

    print("\n1. Tres salidas con codificador compartido")
    started = vendor(client, "EngineStart", {"profile": "economy", "targets": [target(n) for n in TARGETS]})
    check(started["ok"] and not started["failed"], "las tres salidas arrancan")
    check(wait_for(lambda: all_sending(client, TARGETS), 15), "las tres reportan 'sending'")
    check(wait_for(lambda: all(mediamtx_paths().get(f"live/{n}", {}).get("ready") for n in TARGETS), 10),
          "mediamtx recibe las tres rutas")
    print(f"  encoders: {vendor(client, 'GetStatus')['engine']['encoders']}")

    streams = probe(f"{LOCAL_RTMP}/youtube").get("streams", [])
    video = next((s for s in streams if s.get("codec_name") == "h264"), {})
    audio = next((s for s in streams if s.get("codec_name") == "aac"), {})
    check(video.get("width") == 1280 and video.get("height") == 720, f"video h264 {video.get('width')}x{video.get('height')} @ {video.get('avg_frame_rate')}")
    check(audio.get("sample_rate") == "48000" and audio.get("channels") == 2, "audio AAC 48 kHz estéreo")

    print("\n2. Detener una salida no afecta a las otras")
    vendor(client, "EngineStopTarget", {"id": "tiktok"})
    check(wait_for(lambda: outputs(client)["tiktok"]["transport"] == "stopped", 10), "tiktok queda 'stopped'")
    before = {n: bytes_received(n) for n in ("facebook", "youtube")}
    time.sleep(4)
    check(all(bytes_received(n) > before[n] for n in before), "facebook y youtube siguen recibiendo datos")
    check(vendor(client, "GetStatus")["engine"]["encoders_active"], "el codificador compartido sigue activo")

    print("\n3. Reiniciar la salida detenida con el codificador ya activo")
    restarted = vendor(client, "EngineStart", {"profile": "economy", "targets": [target("tiktok")]})
    check(restarted["ok"] and not restarted["failed"], "tiktok vuelve a arrancar")
    check(wait_for(lambda: all_sending(client, TARGETS), 15), "las tres vuelven a 'sending'")

    print("\n4. Un destino inalcanzable no tumba a los demás")
    vendor(client, "EngineStart", {"profile": "economy", "targets": [target("caido", port=1936)]})
    check(wait_for(lambda: outputs(client)["caido"]["transport"] in ("failed", "reconnecting"), 25),
          f"el destino caído falla ({outputs(client)['caido']['transport']}: {outputs(client)['caido']['last_error']})")
    before = {n: bytes_received(n) for n in TARGETS}
    time.sleep(4)
    check(all(bytes_received(n) > before[n] for n in TARGETS), "las tres salidas sanas siguen recibiendo datos")
    vendor(client, "EngineStopTarget", {"id": "caido"})

    print("\n5. Detener todo")
    vendor(client, "EngineStopAll")
    check(wait_for(lambda: all(o["transport"] in ("stopped", "failed") for o in outputs(client).values()), 15),
          "todas las salidas se detienen")
    check(wait_for(lambda: not vendor(client, "GetStatus")["engine"]["encoders_active"], 10), "los codificadores quedan inactivos")
    dropped = {o["id"]: o["dropped_frames"] for o in outputs(client).values()}
    print(f"  frames descartados por salida: {dropped}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        sys.exit(1)
