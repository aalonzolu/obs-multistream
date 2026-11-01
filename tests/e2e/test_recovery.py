import subprocess
import sys
import time

from harness import Failed, check, launch_obs, obs_running, quit_obs, session, vendor, wait_for, wait_state


def main() -> int:
    quit_obs()
    client = launch_obs(dev_targets=True)
    vendor(client, "SetSelected", {"selection": {"facebook": False, "youtube": False, "tiktok": False, "local-a": True, "local-b": True}})
    vendor(client, "Start", {"title": "Prueba recuperación"})
    check(wait_state(client, "live", 20), "la sesión llega a 'live'")
    time.sleep(3)

    print("\n1. OBS se cierra de golpe con la sesión activa")
    subprocess.run(["pkill", "-9", "-x", "OBS"], check=False)
    check(wait_for(lambda: not obs_running(), 10), "OBS terminó")

    print("\n2. Al reabrir se detecta la emisión pendiente y no se reinicia sola")
    client = launch_obs(dev_targets=True)
    current = session(client)
    check(len(current["recovered"]) == 1 and current["recovered"][0]["title"] == "Prueba recuperación",
          "hay una emisión anterior sin cerrar")
    check(current["state"] == "idle" and all(not t["in_session"] for t in current["targets"]), "no se inició ninguna emisión nueva")
    vendor(client, "Start", {"title": "Otra"})
    check(wait_state(client, "attention", 10), f"iniciar otra queda bloqueado: «{session(client)['message']}»")

    print("\n3. Finalizar la emisión anterior")
    vendor(client, "FinalizeRecovered")
    check(wait_for(lambda: not session(client)["recovered"], 15), "la emisión anterior quedó cerrada")
    check(wait_state(client, "idle", 10), "el panel vuelve a 'idle'")
    quit_obs()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        sys.exit(1)
