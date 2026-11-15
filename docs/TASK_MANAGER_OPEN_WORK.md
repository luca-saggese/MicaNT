# LuceOS Task Manager — punti aperti e continuità

Aggiornato: 2026-11-15
Documento operativo: registra implementazione, verifiche, dipendenze e prossimo
intervento. La roadmap normativa resta
[`USERMODE_TASK_MANAGER_AND_SYSTEM_MONITOR_SPEC.md`](USERMODE_TASK_MANAGER_AND_SYSTEM_MONITOR_SPEC.md);
questo file ne traccia l'avanzamento senza ridefinire i milestone kernel.

## Stato attuale

Il Task Manager Ring 3 legge uno snapshot reale dal registro processi Ps tramite
`NtQuerySystemInformation(SystemProcessInformation)`, mostra i nomi immagine
catturati da Ps e riceve la memoria disponibile dal database PFN Mm. Il blocco
snapshot/nome/memoria è stato committato e verificato in QEMU nel commit
`cc5be83` (QEMU: `--taskmgr-selftest`, log
`/private/tmp/micant-taskmgr-block-b-final`). CPU e gli altri provider non
disponibili restano esplicitamente non disponibili.

Il blocco D1, fixture di terminazione autorizzata, è **implementato e passato
in QEMU; commit ancora da creare**. Il test inizialmente falliva perché
`taskmgr_probe.exe` rimaneva nello snapshot dopo una richiesta riuscita. Il
percorso Ps ora non include nei risultati i processi con `isTerminating()` e il
bootstrap chiude l'handle kernel del fixture appena avviato. Il self-test
QEMU finale è stato eseguito con
`python3 qemu.py --headless --taskmgr-selftest --diagnostics-dir
/private/tmp/micant-taskmgr-terminate-fix2`; il target
`ntoskrnl_core_baremetal` è stato linkato, Task Manager ha individuato e
terminato il probe con un handle aperto per `PROCESS_TERMINATE`, il successivo
snapshot valido non lo contiene e il seriale è rimasto stabile per 5 secondi.

L'azione End task nella UI non è ancora collegata: questo blocco verifica il
percorso nativo attraverso il self-test. Il commit D1 è
`1eb9b54 feat(taskmgr): verify authorized process termination`. Le modifiche
contemporanee Win32k/compositor e script QEMU restano nel worktree e non sono
state incluse.

## Punti aperti ordinati per dipendenza

| ID | Punto | Owner canonico | Stato / criterio di chiusura |
|---|---|---|---|
| D1 | Completamento della terminazione remota e rundown Ps/Ob; processi terminanti non devono restare nei risultati di `SystemProcessInformation`. Verificare handle di processo, thread inizialmente sospeso, reaper e visibilità atomica del registro. | Ps/Ob/Ke | **Passato QEMU e committato** in `1eb9b54`; il probe è scomparso dallo snapshot valido e il seriale è rimasto stabile per 5 secondi. |
| D2 | Azione End task collegata a selezione riga e conferma nell'interfaccia interattiva. | Task Manager UI + Ps/Ob | L'utente seleziona il processo, conferma; access denied, uscita concorrente e successo hanno esiti corretti; no PID come autorità. Dipende da D1. |
| P1 | Contatori CPU reali globali/per processo (tempi 100 ns, monotonicità, processori effettivamente online), delta e grafico storico bounded. | Ke/Ps + performance provider + UI | Stress QEMU controllato, delte coerenti e campionamento con overhead misurato; campo indisponibile se manca il contatore. |
| U1 | Finestra Win32 interattiva, resize, navigazione Processes/Performance/Details, ricerca, ordinamento, selezione e refresh timer con cache. | Win32k/User32/GDI/theme/compositor + UI | Input QMP e screenshot dimostrano layout e azioni; la UI continua senza Explorer quando il servizio grafico/sessione è operativo. Coordinare le modifiche Win32k/compositor in corso. |
| S1 | Hotkey Ctrl+Shift+Esc nel percorso input attendibile, con key-up/modificatori e soppressione repeat; avvio singolo per sessione anche senza Explorer. | HAL/input + session manager/Winlogon | QMP inietta press/release e verifica una sola istanza Ring 3 nel desktop/sessione interattiva. |
| S2 | Ctrl+Alt+Del come SAS verso desktop sicuro Winlogon e scelta Gestione attività. | Winlogon/session/auth + secure desktop | QMP verifica passaggio desktop e avvio con token autorizzato; fail-closed se manca infrastruttura di autenticazione. Non simulare autenticazione. Dipende dall'infrastruttura sessione/S1. |
| P2 | Memoria: distinguere disponibile, working set, commit privato/sistema e limite; completare i campi per-proc solo con accounting Mm/Ps coerente. | Mm/Ps + UI | Valori confrontati con contatori owner e pressione memoria QEMU; ciò che non è contabilizzato appare unavailable. |
| P3 | Storage, rete e GPU: bytes/operazioni/errori/latency solo da provider reali. | Io/storage, TCPIP, GPU/compositor | Scenari di I/O e rete QEMU; GPU unavailable finché il driver non espone metriche reali. Nessuno zero sintetico. |
| F1 | Pagine sessioni utenti, startup e servizi e relativi controlli autorizzati. | Session Manager/Userenv/SCM + Se + UI | Enumerazione e mutazioni collegate a provider canonici; assenza provider dichiarata, niente righe di esempio. |
| G1 | Stress, fault injection e overhead integrati: churn durante snapshot, buffer insufficiente/non valido, access denied, PID reuse, close durante refresh, provider assenti, stall Explorer. | Integratore + owner | Test bare-metal QEMU con log seriali/QMP/screenshot e misure ripetibili; update evidence ledger al confine d'integrazione. |

## Regole di parallelizzazione e commit

- Un solo integratore modifica `main`, integra e crea commit locali; niente push.
- Gli audit paralleli devono avere scope e file distinti. Prima di modificare un
  file condiviso, l'owner annuncia ownership e si serializza l'integrazione.
- Non includere file già modificati da altri owner nei commit Task Manager;
  usare staging selettivo e verificare `git diff --cached`.
- Ogni blocco è chiuso solo con implementazione, gestione errori/cleanup e
  verifica del percorso bare-metal richiesta dal blocco. Build passata da sola
  non chiude un comportamento.
- Aggiornare questa tabella all'inizio e alla fine di ogni macro-blocco, dopo
  ogni cambio del punto di ripresa e quando cambia la verifica. Aggiungere
  dettagli ai log/evidence soltanto ai confini di integrazione rilevanti.

## Sessione corrente — prossimo intervento

1. Avviare P1, contatori CPU reali, verificando prima owner/counter esistenti in
   Ke e il provider `kernel/ex/perf_taskmgr.cpp`; non inventare contatori.
2. Coordinare U1 con gli owner Win32k/compositor già attivi prima di toccare
   `tools/taskmgr_app.cpp` o file GUI condivisi. L'azione End task D2 segue la
   disponibilità di selezione riga/UI e riusa D1.
3. Audit scope: restano UI navigabile, hotkey/SAS,
   sessioni/startup/servizi e integrazione/stress. Il percorso tasti attuale
   è solo PS/2 bootstrap diretto al foreground: non traccia i modificatori per
   chord globali; `winlogon.exe` e `logonui.exe` non sono avviati e le app
   dichiarano non supportati IPC autenticato e desktop sicuro. Dopo D1,
   scegliere CPU o UI in base agli owner/file disponibili; non modificare i
   file Win32k/compositor già attivi senza accordo.

## Non ancora dichiarato supportato

Interfaccia navigabile Windows 11, azioni interattive, CPU e grafici reali,
working set/commit completi, storage/rete/GPU, hotkey/SAS, indipendenza da
Explorer, utenti/sessioni, startup, servizi e overhead misurato. I dati non
coperti da provider devono rimanere unavailable.
