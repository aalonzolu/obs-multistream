import sys
import time

from harness import (Failed, check, ensure_test_scene, launch_obs, mediamtx_paths, path_bytes, quit_obs, session, targets,
                     vendor, wait_for, wait_state)

LOCAL = ["local-a", "local-b"]


def behave(client, provider, **behavior):
    return vendor(client, "DevSetBehavior", {"provider": provider, "behavior": behavior})


def finalize_calls(client, provider) -> int:
    return behave(client, provider, **current.get(provider, {}))["finalize_calls"]


current: dict[str, dict] = {}


def set_behavior(client, provider, **behavior):
    current[provider] = behavior
    behave(client, provider, **behavior)


def reset(client):
    for provider in LOCAL:
        set_behavior(client, provider)
    state = session(client)["state"]
    if state in ("live", "partial"):
        vendor(client, "Stop")
        wait_state(client, {"idle", "attention"}, 20)
    if session(client)["state"] == "attention":
        vendor(client, "Acknowledge")
        wait_state(client, "idle", 10)


def main() -> int:
    quit_obs()
    client = launch_obs(dev_targets=True)
    ensure_test_scene(client)
    vendor(client, "SetSelected", {"selection": {"facebook": False, "youtube": False, "tiktok": False, "local-a": True, "local-b": True}})
    vendor(client, "SetOptions", {"profile": "economy"})
    reset(client)

    print("\n1. Caso normal: iniciar y finalizar dos destinos")
    vendor(client, "Start", {"title": "Prueba coordinador"})
    check(wait_state(client, "live", 20), "la sesión llega a 'live'")
    check(wait_for(lambda: all(targets(client)[t]["state"] in ("SENDING", "LIVE_CONFIRMED") for t in LOCAL), 15),
          "ambos destinos envían señal")
    check(wait_for(lambda: all(mediamtx_paths().get(f"live/{t}", {}).get("ready") for t in LOCAL), 10), "mediamtx recibe ambos")
    check(wait_for(lambda: all(targets(client)[t]["state"] == "LIVE_CONFIRMED" for t in LOCAL), 20),
          "la verificación remota confirma LIVE")
    before = {t: finalize_calls(client, t) for t in LOCAL}
    vendor(client, "Stop")
    check(wait_state(client, "idle", 20), "la sesión vuelve a 'idle'")
    check(all(finalize_calls(client, t) == before[t] + 1 for t in LOCAL), "se cerró cada emisión remota exactamente una vez")

    print("\n2. Falla la comprobación previa: no se publica nada")
    set_behavior(client, "local-b", preflight="auth")
    before = {t: finalize_calls(client, t) for t in LOCAL}
    vendor(client, "Start", {"title": "Prueba preflight"})
    check(wait_state(client, "attention", 15), "la sesión pide atención")
    current_targets = targets(client)
    check(current_targets["local-b"]["state"] == "AUTH_REQUIRED", f"Prueba B pide reconectar: «{current_targets['local-b']['message']}»")
    check(not mediamtx_paths().get("live/local-a", {}).get("ready"), "Prueba A no se publicó")
    check(all(finalize_calls(client, t) == before[t] for t in LOCAL), "no se creó ni cerró ninguna emisión")
    print(f"  mensaje: {session(client)['message']}")
    reset(client)

    print("\n3. Preparación incierta: se limpia lo creado y no se reintenta a ciegas")
    set_behavior(client, "local-b", prepare="uncertain")
    before = finalize_calls(client, "local-a")
    vendor(client, "Start", {"title": "Prueba incierta"})
    check(wait_state(client, "attention", 20), "la sesión pide atención")
    current_targets = targets(client)
    check(current_targets["local-b"]["state"] == "PREPARE_UNKNOWN", "Prueba B queda en PREPARE_UNKNOWN")
    check(current_targets["local-a"]["state"] == "FINISHED", "Prueba A ya creada se cerró")
    check(finalize_calls(client, "local-a") == before + 1, "se pidió el cierre de Prueba A una sola vez")
    check(not mediamtx_paths().get("live/local-a", {}).get("ready"), "no se envió señal a Prueba A")
    print(f"  mensaje: {session(client)['message']}")
    reset(client)

    print("\n4. Un destino cae durante la emisión y el otro sigue")
    set_behavior(client, "local-b", port=1936)
    vendor(client, "Start", {"title": "Prueba parcial"})
    check(wait_state(client, "partial", 30), "la sesión pasa a 'partial'")
    current_targets = targets(client)
    check(current_targets["local-a"]["state"] in ("SENDING", "LIVE_CONFIRMED"), "Prueba A sigue enviando")
    check(current_targets["local-b"]["state"] == "FAILED" and current_targets["local-b"]["can_retry"],
          f"Prueba B falla y ofrece reintentar: «{current_targets['local-b']['message']}»")
    print(f"  mensaje: {session(client)['message']}")
    check(wait_for(lambda: mediamtx_paths().get("live/local-a", {}).get("ready"), 10), "mediamtx recibe Prueba A")
    bytes_before = path_bytes("live/local-a")
    time.sleep(3)
    check(path_bytes("live/local-a") > bytes_before, "Prueba A sigue recibiendo datos")
    vendor(client, "Stop")
    check(wait_state(client, "idle", 25), "finalizar cierra ambas emisiones")
    reset(client)

    print("\n5. Cierre pendiente y reintento")
    set_behavior(client, "local-a", finalize="reject")
    vendor(client, "Start", {"title": "Prueba cierre pendiente"})
    check(wait_state(client, "live", 20), "la sesión llega a 'live'")
    vendor(client, "Stop")
    check(wait_state(client, "attention", 20), "la sesión pide atención al finalizar")
    current_targets = targets(client)
    check(current_targets["local-a"]["state"] == "CLOSE_PENDING", f"Prueba A queda con cierre pendiente: «{current_targets['local-a']['message']}»")
    check(current_targets["local-b"]["state"] == "FINISHED", "Prueba B se cerró")
    set_behavior(client, "local-a")
    vendor(client, "Retry", {"provider": "local-a"})
    check(wait_state(client, "idle", 15), "reintentar el cierre resuelve la sesión")

    print("\n6. Doble clic no crea dos sesiones")
    before = finalize_calls(client, "local-a")
    vendor(client, "Start", {"title": "Prueba doble clic"})
    vendor(client, "Start", {"title": "Prueba doble clic"})
    check(wait_state(client, "live", 20), "la sesión llega a 'live'")
    vendor(client, "Stop")
    check(wait_state(client, "idle", 20), "la sesión termina")
    check(finalize_calls(client, "local-a") == before + 1, "solo se creó y cerró una emisión")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        try:
            client = launch_obs()
            print(f"  estado: {session(client)}")
        except Exception:
            pass
        sys.exit(1)
