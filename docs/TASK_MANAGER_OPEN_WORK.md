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
  `/private/tmp/micant-taskmgr-p1b-percent`; commit `9df2acc`.
- [ ] **U1 — UI interattiva** — in corso: Task Manager sta passando al percorso
  nativo User32/GDI32 → Win32k, finestra HWND, `WM_PAINT`/`WM_SIZE` e pump input.
  Primo boot ha fallito in fase SEC_IMAGE: l'import di Win32k incontrava un
  DIB arena statico da 16 MiB per processo; ora limitato a 4 MiB. Build/link
  bare-metal passati. QEMU 2 GiB completa il test Ps `finalizeNativeImages`,
  ma scade prima dell'ingresso Task Manager (nessun marker `_start`); trovare
  perché il thread non raggiunge l'entry. Restano resize adattivo, schede,
  ricerca, selezione e grafico storico.
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

P1b è chiuso. U1 in corso: il codice HWND e GDI32 compila e linka; il QEMU più
recente completa i self-test Ps ma il processo Task Manager non stampa il suo
marker d'ingresso entro 90 s. Localizzare scheduling/entry prima di ampliare la
UI. Le modifiche Win32k/compositor condivise restano escluse dal commit Task
Manager.

Audit paralleli: S1/S2 dipendono da sessione/autenticazione reali; P2 da
accounting Mm/Ps; P3 da provider Io/NDIS/GPU; F1 da SCM/session/Userenv reali.
Nessun valore senza provider va visualizzato come zero o successo.

Commits registro: `cc867e0`, `6278c52`, `9df2acc`. Aggiornare questa checklist a ogni
avanzamento, risultato QEMU e cambio del blocco attivo.
