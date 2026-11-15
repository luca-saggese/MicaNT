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

Il blocco D1, fixture di terminazione autorizzata, è **implementato, passato
in QEMU e committato**. Il test inizialmente falliva perché
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
| P1a | Contatori CPU globali per processore e percentuale da delta monotoni. | Ke/HAL + `kernel/ex/perf_taskmgr.cpp` + Task Manager | **Implementato, QEMU passato e committato** in `1ab31da`: class NT 8 restituisce tempi idle/kernel/user tick-sampled in unità 100 ns per il BSP online; la UI calcola una percentuale bounded da due letture. Microsoft documenta il layout e le unità di 100 ns [qui](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation). La risoluzione è quella del clock 100 Hz. |
| P1b | Tempi CPU user/kernel per thread e processi, storico/grafico bounded e riconciliazione con il totale. | Ke/Ps + Task Manager | Implementazione Ke/Ps e lettura Task Manager presenti e linkate, **non ancora verificate in QEMU**. La prova con `qemu.py --taskmgr-selftest` compila e linka i target, poi non raggiunge l'idle stabile richiesto entro 90 s; seriale conferma che Explorer crea la prima finestra, ma non contiene marker Task Manager. Non dichiarare completato né committare il blocco finché il self-test non esercita tempi monotoni per thread/processo. |
| U1 | Finestra Win32 interattiva, resize, navigazione Processes/Performance/Details, ricerca, ordinamento, selezione e refresh timer con cache. | Win32k/User32/GDI/theme/compositor + UI | Audit parallelo: primo slice è registrare classe, creare finestra, gestire `WM_PAINT`/`WM_SIZE` e ciclo `GetMessage`/`DispatchMessage`. `tools/dlls/user32.cpp` espone stub nativi, mentre l'API inline `include/micant/um/subsystems/winuser/user32.hpp` usa un manager hosted; il percorso Ring 3 deve invece collegarsi a Win32k/compositor. File primari: `tools/taskmgr_app.cpp`, `tools/dlls/user32.cpp`; coordinare prima le modifiche concorrenti ai file Win32k/compositor. |
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

## Sessione corrente — punto di ripresa

1. P1b è in worktree: l'ISR Ke usa il CS salvato per attribuire il tick al
   thread corrente; Ps somma i tempi dei thread nei record di processo e
   thread; Task Manager verifica crescita monotona dello snapshot del proprio
   processo. Build/link dei target bare-metal completata. `qemu.py` è fallito
   dopo 90 s senza idle stabile e senza marker Task Manager: log
   `/private/tmp/micant-taskmgr-process-times3/serial.log`. Analizzare il
   blocco boot/UI o rendere raggiungibile il self-test senza abbassare i
   criteri; quindi ripetere QEMU e committare solo con evidenza del provider.
2. Il marker Task Manager assente impedisce di concludere se i valori Ke/Ps
   crescono; `totalCyclesExecuted` dello scheduler hosted resta inappropriato.
3. Per U1, integrare il percorso nativo User32→Win32k/compositor, poi finestra
   e message loop; serializzare le modifiche con gli owner già attivi. D2
   dipende da selezione riga e riusa il percorso D1.
4. Restano UI navigabile, hotkey/SAS,
   sessioni/startup/servizi e integrazione/stress. Il percorso tasti attuale
   è solo PS/2 bootstrap diretto al foreground: non traccia i modificatori per
   chord globali; `winlogon.exe` e `logonui.exe` non sono avviati e le app
   dichiarano non supportati IPC autenticato e desktop sicuro. Dopo D1,
   proseguire per dipendenza e mantenere questo documento aggiornato a ogni
   cambio di blocco/verifica.

## Non ancora dichiarato supportato

Interfaccia navigabile Windows 11, azioni interattive, CPU per-processo,
grafici/storico reali, working set/commit completi, storage/rete/GPU,
hotkey/SAS, indipendenza da Explorer, utenti/sessioni, startup, servizi e
overhead misurato. I dati non coperti da provider devono rimanere unavailable.
