import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone

from harness import (OUT, ROOT, Failed, check, ensure_test_scene, launch_obs, mediamtx_paths, preserved_config, session,
                     targets, vendor, wait_for, wait_state, TEST_INPUT)

LOCAL = ["local-a", "local-b"]
RECORDINGS = OUT / "grabaciones"


def select(client, chosen: list[str]):
    selection = {t: t in chosen for t in targets(client)}
    vendor(client, "SetSelected", {"selection": selection})


def stop(client):
    vendor(client, "Stop")
    check(wait_state(client, {"idle", "attention"}, 30), "la sesión termina")
    if session(client)["state"] == "attention":
        vendor(client, "Acknowledge")


def custom_destination(client):
    print("\n1. Destino RTMP personalizado")
    added = vendor(client, "CustomAdd", {"name": "Servidor propio", "server": "rtmp://127.0.0.1:1935/live", "key": "propio"})
    check(added.get("ok"), f"se agrega ({added.get('id')})")
    custom = added["id"]
    check(custom in targets(client) and targets(client)[custom]["state"] == "READY", "aparece listo para transmitir")
    select(client, [custom])
    vendor(client, "StartSession", {"title": "Prueba RTMP"})
    check(wait_state(client, "live", 20), "sale al aire")
    check(wait_for(lambda: mediamtx_paths().get("live/propio", {}).get("ready"), 15), "el servidor recibe la señal con la clave")
    stop(client)
    check(vendor(client, "CustomRemove", {"id": custom}).get("ok"), "se quita")
    check(custom not in targets(client), "deja de aparecer")


def record_along(client):
    print("\n2. Grabar a la vez")
    RECORDINGS.mkdir(parents=True, exist_ok=True)
    original = client.get_record_directory().record_directory
    client.set_record_directory(str(RECORDINGS))
    try:
        before = set(RECORDINGS.iterdir())
        select(client, ["local-a"])
        vendor(client, "StartSession", {"title": "Prueba grabación", "record": True})
        check(wait_state(client, "live", 20), "la transmisión sale al aire")
        check(wait_for(lambda: vendor(client, "RecordingStatus")["active"], 10), "OBS empieza a grabar")
        time.sleep(4)
        stop(client)
        check(wait_for(lambda: not vendor(client, "RecordingStatus")["active"], 15), "la grabación se detiene con la transmisión")
        created = [f for f in set(RECORDINGS.iterdir()) - before if f.stat().st_size > 0]
        check(len(created) == 1, f"queda el archivo {created[0].name if created else '—'}")
    finally:
        client.set_record_directory(original)


def alerts(client):
    print("\n3. Avisos")
    select(client, ["local-a"])
    vendor(client, "StartSession", {"title": "Prueba avisos"})
    check(wait_state(client, "live", 20), "al aire")
    client.set_input_mute(TEST_INPUT, True)
    check(wait_for(lambda: vendor(client, "AlertStatus")["silent"], 35), "detecta el silencio en la pista 1")
    status = vendor(client, "AlertStatus")
    check(status["track"] == 1 and "No hay audio" in status["last"], f"avisa: «{status['last']}»")
    client.set_input_mute(TEST_INPUT, False)
    check(wait_for(lambda: not vendor(client, "AlertStatus")["silent"], 10), "se retira cuando vuelve el audio")
    check(vendor(client, "AlertStatus")["last"] == "Volvió el audio.", "avisa que volvió")
    stop(client)

    receiver = subprocess.Popen(["mediamtx", str(ROOT / "mediamtx-b.yml")], cwd=OUT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(2)
        vendor(client, "SetOptions", {"reconnect": {"retries": 1, "delay_seconds": 1}})
        vendor(client, "DevSetBehavior", {"provider": "local-b", "behavior": {"port": 1936}})
        select(client, LOCAL)
        vendor(client, "StartSession", {"title": "Prueba caída"})
        check(wait_state(client, "live", 20), "dos destinos al aire")
        check(wait_for(lambda: targets(client)["local-b"]["state"] in ("SENDING", "LIVE_CONFIRMED"), 20), "B envía señal")
        receiver.terminate()
        receiver.wait(10)
        check(wait_for(lambda: "Prueba B se cayó" in vendor(client, "AlertStatus")["last"], 40),
              f"avisa la caída: «{vendor(client, 'AlertStatus')['last']}»")
        stop(client)
    finally:
        receiver.terminate()
        vendor(client, "DevSetBehavior", {"provider": "local-b", "behavior": {}})
        vendor(client, "SetOptions", {"reconnect": {"retries": 10, "delay_seconds": 3}})


def scheduling(client):
    print("\n4. Programar con anticipación")
    starts = (datetime.now(timezone.utc) + timedelta(hours=1)).replace(microsecond=0).isoformat()
    vendor(client, "ScheduleCreate", {"title": "Culto programado", "description": "Prueba", "starts_at": starts, "providers": ",".join(LOCAL)})
    listed = wait_for(lambda: vendor(client, "ScheduleList")["entries"], 10)
    check(len(listed) == 1 and set(listed[0]["broadcasts"]) == set(LOCAL), "se crea en los dos destinos")
    entry = listed[0]
    check(vendor(client, "ScheduleList")["due"] == entry["id"], "la ventana Transmitir la ofrece (empieza en menos de 12 h)")
    check("Culto programado" in entry["share"] and "Prueba A:" in entry["share"], "arma el texto para compartir")
    select(client, LOCAL)
    vendor(client, "StartSession", {"title": entry["title"], "scheduled_id": entry["id"]})
    check(wait_state(client, "live", 20), "sale al aire")
    refs = {t: targets(client)[t]["remote_id"] for t in LOCAL}
    check(all(refs[t] == entry["broadcasts"][t]["ref"] for t in LOCAL), f"usa los directos programados ({', '.join(refs.values())})")
    check(wait_for(lambda: not vendor(client, "ScheduleList")["entries"], 5), "deja de figurar como pendiente")
    stop(client)

    vendor(client, "ScheduleCreate", {"title": "Para cancelar", "starts_at": starts, "providers": ",".join(LOCAL)})
    pending = wait_for(lambda: vendor(client, "ScheduleList")["entries"], 10)
    vendor(client, "ScheduleCancel", {"id": pending[0]["id"]})
    check(wait_for(lambda: not vendor(client, "ScheduleList")["entries"], 10), "cancelar la quita de la lista")
    cancelled = vendor(client, "DevCancelledRefs", {"provider": "local-a"})["refs"].split(",")
    check(pending[0]["broadcasts"]["local-a"]["ref"] in cancelled, "y la borra en la plataforma")


def main() -> int:
    with preserved_config():
        client = launch_obs(dev_targets=True)
        ensure_test_scene(client)
        vendor(client, "SetOptions", {"profile": "economy"})
        custom_destination(client)
        record_along(client)
        alerts(client)
        scheduling(client)
        print("\nFunciones nuevas: todo bien")
        return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failed as failure:
        print(f"  ✗ {failure}")
        sys.exit(1)
