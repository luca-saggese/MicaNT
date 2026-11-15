# LuceOS Task Manager — checklist

Aggiornato: 2026-11-15. Stato essenziale dei blocchi e prossimo passo;
requisiti completi in
[`USERMODE_TASK_MANAGER_AND_SYSTEM_MONITOR_SPEC.md`](USERMODE_TASK_MANAGER_AND_SYSTEM_MONITOR_SPEC.md).

- [x] **Snapshot, nomi, memoria globale** — Ps/Mm reali; QEMU passato, `cc5be83`.
- [x] **D1 — Terminazione autorizzata** — handle `PROCESS_TERMINATE`, fixture
  rimosso dallo snapshot; QEMU passato, `1eb9b54`.
- [x] **P1a — CPU globale** — class 8, percentuale da delta; QEMU passato,
  `1ab31da`.
- [x] **P1b — CPU per processo/thread** — Ke conta i tick CPL3/CPL0 per thread,
  Ps somma i tempi di processo, Task Manager calcola e mostra percentuali per
  riga da delta bounded. QEMU passato: `process and thread CPU totals
  reconciled`, `process CPU times validated`, D1 e seriale stabile 5 s in
  `/private/tmp/micant-taskmgr-p1b-percent`. Commit in preparazione.
- [ ] **U1 — UI interattiva** — canvas fissa, senza navigazione/selezione.
  Dipende da export Ring 3 User32 reali collegati a Win32k/compositor; audit
  individua `tools/dlls/user32.cpp` e `tools/taskmgr_app.cpp` come entrypoint.
- [ ] **D2 — End task dalla UI** — dipende da U1; selezione, conferma, errori
  accesso/uscita e azione tramite handle.
- [ ] **S1/S2 — Hotkey e SAS** — nessun chord globale trusted; Winlogon, sessione,
  desktop sicuro e IPC autenticato mancanti. Implementare owner reali prima del
  percorso Ctrl+Shift+Esc/Ctrl+Alt+Del.
- [ ] **P2 — Memoria per processo** — globale disponibile reale; working set e
  commit richiedono ownership/accounting Mm/Ps.
- [ ] **P3 — Storage/rete/GPU** — nessun provider aggregato collegato al query
  Ring 3 bare-metal; metriche unavailable finché non esistono provider reali.
- [ ] **F1 — Sessioni/startup/servizi** — provider runtime assenti; rimuovere
  risposte synthetic e collegare session manager/SCM/Userenv canonici.
- [ ] **G1 — Fault, stress e overhead** — churn, buffer invalidi, access denied,
  PID reuse, refresh concorrente, provider assenti, stall Explorer e misure.

## Punto di ripresa

P1b è verificato in QEMU e pronto per commit selettivo. Il prossimo blocco è
U1: collegare il Task Manager al percorso nativo Win32k/User32; coordinare i
file compositor condivisi già modificati da altri owner. La storia grafica
bounded viene aggiunta con la vista Performance in U1.

Audit paralleli: S1/S2 dipendono da sessione/autenticazione reali; P2 da
accounting Mm/Ps; P3 da provider Io/NDIS/GPU; F1 da SCM/session/Userenv reali.
Nessun valore senza provider va visualizzato come zero o successo.

Commits registro: `cc867e0`, `6278c52`. Aggiornare questa checklist a ogni
avanzamento, risultato QEMU e cambio del blocco attivo.
