import os
import subprocess
import sys
import time

from harness import OUT, Failed, check, ensure_test_scene, launch_obs, quit_obs, session, vendor, wait_for, wait_state

SHOTS = OUT / "shots"


def window_bounds(window: str | None) -> str | None:
    target = f'window "{window}"' if window else "window 1"
    result = subprocess.run(
        ["osascript", "-e", 'tell application "OBS" to activate', "-e",
         f'tell application "System Events" to tell process "OBS" to get {{position, size}} of {target}'],
        capture_output=True, text=True,
    )
    numbers = [n.strip() for n in result.stdout.split(",")]
    return ",".join(numbers) if len(numbers) == 4 else None


def capture(name: str, window: str | None = None):
    if not os.environ.get("MULTISTREAM_SHOTS"):
        return
    bounds = window_bounds(window)
    if not bounds:
        print(f"  · sin captura de {name}: no encontré la ventana")
        return
    SHOTS.mkdir(parents=True, exist_ok=True)
    time.sleep(0.6)
    subprocess.run(["screencapture", "-x", "-R", bounds, str(SHOTS / f"{name}.png")], check=False)


def button(client) -> dict:
    return vendor(client, "GetStreamButton")


def modal(client) -> str:
    return vendor(client, "GetModal")["name"]


def main() -> int:
    quit_obs()
    client = launch_obs(dev_targets=True)
    ensure_test_scene(client)
    vendor(client, "SetSelected", {"selection": {"facebook": False, "youtube": False, "tiktok": False, "local-a": True, "local-b": True}})
    vendor(client, "SetOptions", {"profile": "economy"})
    if session(client)["state"] != "idle":
        vendor(client, "Stop")
        wait_state(client, {"idle", "attention"}, 20)


    print("\n1. El botón de OBS queda tomado por Multistream")
    idle = button(client)
    check(idle["signals_blocked"], "el botón ya no dispara la transmisión propia de OBS")
    native_text = idle["text"]

    print("\n2. «Iniciar transmisión» abre la ventana")
    vendor(client, "ClickStreamButton")
    check(wait_for(lambda: modal(client) == "multistream-start", 5), "se abre la ventana «Transmitir»")
    check(session(client)["state"] == "idle", "abrirla no inicia nada")

    vendor(client, "DismissModal")
    check(wait_for(lambda: modal(client) == "", 5), "Cancelar cierra la ventana sin transmitir")
    check(session(client)["state"] == "idle", "sigue sin transmitir")

    print("\n3. Al aire, el botón cambia a «Finalizar transmisión»")
    vendor(client, "Start", {"title": "Prueba de interfaz"})
    check(wait_state(client, "live", 20), "la sesión llega a 'live'")
    live = button(client)
    check(live["text"] == "Finalizar transmisión" and live["class"] == "state-active", "el botón muestra el estado activo")


    print("\n4. Finalizar pide confirmación")
    vendor(client, "ClickStreamButton")
    check(wait_for(lambda: modal(client) == "multistream-confirm-stop", 5), "aparece la confirmación")

    vendor(client, "DismissModal")
    check(wait_for(lambda: modal(client) == "", 5), "«Seguir transmitiendo» cierra el aviso")
    check(session(client)["state"] == "live", "la transmisión sigue al aire")

    vendor(client, "Stop")
    check(wait_state(client, "idle", 20), "la sesión vuelve a 'idle'")
    restored = button(client)
    check(restored["text"] == native_text and restored["class"] == "", "el botón recupera su texto original")

    quit_obs()
    print("\nInterfaz: todo bien")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        sys.exit(1)
