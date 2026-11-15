# LuceOS Task Manager — punti aperti

Checklist breve; la specifica completa è
[`USERMODE_TASK_MANAGER_AND_SYSTEM_MONITOR_SPEC.md`](USERMODE_TASK_MANAGER_AND_SYSTEM_MONITOR_SPEC.md).

- [x] Snapshot, nomi e memoria globale — QEMU; `cc5be83`.
- [x] D1 terminazione autorizzata — QEMU; `1eb9b54`.
- [x] P1a/P1b CPU globale e per processo/thread — QEMU; `1ab31da`, `9df2acc`.
- [x] D2 End task UI e chiusura finestra — QEMU self-test; handle autorizzato,
  `WM_CLOSE`/`WM_DESTROY`, paint, resize e idle.
- [ ] U1 UI — parziale: finestre, schede, ricerca, selezione e Details attivi;
  restano ordinamento, grafici storici e copertura QEMU completa.
- [ ] P2 memoria per processo — working set corrente/picco live; commit privato
  senza accounting Mm.
- [ ] S1/S2 hotkey e SAS — manca il percorso trusted Input/HAL → sessione,
  Winlogon e desktop sicuro.
- [ ] P3 storage/rete/GPU — mancano provider live Io/NDIS/GPU.
- [ ] F1 sessioni/startup/servizi — mancano Session Manager/Userenv/SCM runtime.
- [ ] G1 stress/fault/overhead — churn, errori/cleanup e misure non coperti.

## Verifica recente

`python3 qemu.py --headless --taskmgr-selftest --cmake-build-dir
build/qemu-taskmgr-isolated --memory-mib 2048 --timeout 90 --stable-seconds 3`
passato il 2026-11-15: process snapshot, CPU, azione D1, controlli UI,
paint/resize, chiusura e seriale stabile. Evidenza in
`/private/tmp/micant-taskmgr-destroy-callback-2/serial.log`.
