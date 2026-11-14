# M4 — punti ancora aperti

Questa checklist segue la definizione corrente di M4 in
[`KERNEL_MILESTONES.md`](KERNEL_MILESTONES.md). Il ledger
[`KERNEL_INTEGRATION_STATUS.md`](KERNEL_INTEGRATION_STATUS.md) fornisce evidenza
storica e dettagli tecnici; quando i due documenti divergono, prevale la riga
M4 della milestone.

Aggiornare una casella a `[x]` solo dopo aver collegato il percorso al kernel
modulare AMD64 e aver verificato in QEMU il comportamento pertinente. La sola
compilazione o una regressione hosted non chiudono un punto d'accettazione.

## Checklist di chiusura

### APC e continuazione Ring 3

- [x] **Consegnare un APC utente normale al ritorno verso Ring 3.** QEMU ora
  verifica la callback accodata a un thread sospeso, la ripresa, `NtTestAlert`,
  il ritorno tramite `NtContinue` e lo status di uscita. Il failure era nella
  fixture: dopo `SYSCALL` usava il valore clobberato di RCX come puntatore
  all'argomento; il test ora lo conserva in R12 secondo la convenzione AMD64.
- [x] **Consegnare APC durante wait e delay alertable.** Il fixture CPL3 accoda
  callback al thread corrente, attende su evento non segnalato e invoca
  `NtDelayExecution` alertable; verifica `STATUS_USER_APC` e i marker scritti
  dalle callback prima di riprendere il chiamante, senza una successiva
  `NtTestAlert`. QEMU ha trovato che il dispatcher HAL richiedeva IF nel frame
  al ritorno: ora il ritorno con `STATUS_USER_APC` da `NtWaitForSingleObject`,
  `NtWaitForMultipleObjects` o `NtDelayExecution` è un punto di consegna anche
  quando IF non è impostato.
- [x] **Verificare la consegna normale e il rundown APC nei casi coperti.** In
  QEMU due callback utente sono consegnate in ordine FIFO e il test controlla
  `NtContinue` e lo status di uscita; il test di terminazione remota verifica il
  rundown di un APC utente accodato a un waiter e il rilascio dei riferimenti.
  Il thread corrente può usare l'handle pseudo `NtCurrentThread()` per
  `NtAlertThread` e `NtQueueApcThread`; QEMU verifica anche che entrambi
  rifiutino un handle nullo (`STATUS_INVALID_HANDLE`) senza alterare la
  consegna successiva.
- [x] **Verificare il guasto durante una callback utente con altre APC pendenti.**
  Il fixture CPL3 accoda una routine non valida e un APC sentinella nel processo
  figlio, poi verifica la terminazione per `STATUS_ACCESS_VIOLATION`, l'attesa
  e la chiusura di thread/processo. Lo status distinto del sentinella dimostra
  che l'APC successivo non viene eseguito. Il commit `6e28db0` impedisce
  l'iniezione di APC durante entrambe le chance di `NtRaiseException` e mantiene
  la consegna esplicita via `NtTestAlert`; QEMU passa. Restano da ampliare i casi
  di rundown/callback e gli interleaving oltre il fixture verificato.

### Contratti di accesso

- [x] **Chiudere i casi di accesso già verificati.** Il percorso QEMU copre
  `NtOpenProcess`/`NtOpenThread` per CID, rifiuto di diritti insufficienti,
  DACL allow/deny in contesto UserMode, valid-access mask, generic e
  `MAXIMUM_ALLOWED`, duplicate rights e rollback delle callback Ob.
- [ ] **Completare i controlli di accesso generali per creazione, apertura e uso
  di processi e thread.** Estendere oltre i casi bootstrap a tutti i modi
  Kernel/User e ai diritti specifici dei percorsi nativi supportati, inclusi
  token/DACL, handle pseudo e attributi di sicurezza. Un handle con diritti
  insufficienti deve fallire con lo status previsto senza pubblicare handle o
  oggetti parziali. Il test kernel UserMode verifica ora `NtOpenProcess` negato
  dalla DACL senza `SeDebugPrivilege` e consentito con il privilegio abilitato,
  controllando i diritti concessi e ripristinando il token. QEMU verifica anche
  che `NtOpenProcess` e `NtOpenThread` in KernelMode ignorino un deny ACE senza
  concedere diritti oltre quelli richiesti. Restano i casi di creazione/uso
  generali e handle pseudo. QEMU verifica inoltre `NtOpenThread` con il solo
  `UniqueThread`, rifiuta un `UniqueProcess` non corrispondente e non pubblica
  handle nel caso negativo. QEMU CPL3 verifica anche
  che un `NtCreateThreadEx` con self-relative security descriptor e DACL vuota
  ritorni `STATUS_ACCESS_DENIED` senza pubblicare l'handle. La fixture CPL3
  verifica anche i pseudo-handle di processo e thread correnti in
  `NtWaitForSingleObject`: con timeout zero entrambi restituiscono
  `STATUS_TIMEOUT` mentre i relativi oggetti restano attivi.
- [ ] **Chiudere i percorsi verificati di duplicazione e inheritance degli handle nei
  processi.** Verificare diritti concessi, callback Ob, handle ereditabili,
  rollback se una callback o la copia dell'output fallisce e rilascio esatto dei
  riferimenti. `NtDuplicateObject` ora è collegato al syscall CPL3; QEMU verifica
  `DUPLICATE_SAME_ACCESS`, rifiuto dei diritti insufficienti, handle sorgente
  invalido e output non mappato. `DUPLICATE_CLOSE_SOURCE` è verificato sia dopo
  una duplicazione riuscita sia quando i diritti target vengono negati; in
  entrambi i casi il source handle è ritirato e l'handle risultante è controllato.
  Il target non pubblicato viene ritirato anche
  per handle `OBJ_PROTECT_CLOSE`, usando il close callback canonico. I test
  kernel coprono il rollback della callback Ob. QEMU CPL3 verifica inoltre la
  creazione di un evento `OBJ_INHERIT`, la clonazione con flag di inheritance,
  la chiusura dell'handle sorgente nel parent e il suo uso da un thread child;
  un secondo evento non inheritable viene rifiutato nel figlio e resta non
  segnalato. Restano il copyout fallito dopo una probe valida e il rollback
  QEMU di tutte le fasi d'inheritance. Il percorso kernel M4 verifica in
  QEMU `NtCreateEvent` con `OBJ_INHERIT`, `NtCreateProcessEx` con inheritance,
  chiusura dell'handle sorgente e wait sul valore-handle dopo attach al figlio.
  `NtCreateEvent` registra l'attributo nella handle table. Resta aperta la prova
  del rollback in caso di fallimento di tutte le fasi d'inheritance.
- [x] **Verificare gli attributi di handle e gli errori di chiusura.** Coprire
  `OBJ_PROTECT_CLOSE`, chiusura forzata durante teardown e output utente non
  valido senza doppia chiusura o perdita di riferimenti; i casi documentati nel
  ledger sono coperti da QEMU.

### Creazione e lifecycle generali

- [ ] **Generalizzare `NtCreateProcessEx` oltre i percorsi bootstrap verificati.**
  Verificare parent handle e diritti, attributi e flags supportati, access mode,
  metadata loader, handle inheritance, pubblicazione dell'handle e rollback di
  ogni fase. Gli input o flags non supportati devono fallire esplicitamente.
  Un test QEMU integrato crea ora un processo Ob-owned da `SEC_IMAGE` file-backed
  e verifica immagini finalizzate; il rollback esaustivo delle fasi e gli altri
  access mode supportati restano aperti.
  QEMU CPL3 verifica ora il rifiuto con handle nullo per flag ignoti, debug port,
  exception port e `InJob`, oltre al rifiuto di un parent handle senza
  `PROCESS_CREATE_PROCESS`. Verifica inoltre la cattura syscall di un
  self-relative security descriptor con DACL vuota: `STATUS_ACCESS_DENIED` e
  handle nullo. L'inheritance `OBJ_INHERIT` positiva e negativa ora passa in
  CPL3/QEMU; restano aperti gli altri access mode e il rollback QEMU di tutte le
  fasi di creazione e inheritance. La fixture CPL3 verifica anche
  `STATUS_ACCESS_VIOLATION` con output handle non scrivibile prima di allocare o
  pubblicare il processo. La fixture CPL3 verifica anche che `Flags=1` venga
  rifiutato con `STATUS_NOT_IMPLEMENTED` e output handle nullo.
  La fixture CPL3 prova anche che un puntatore `PreviousSuspendCount` non
  valido fa fallire `NtResumeThread` senza togliere la sospensione; il wait
  immediatamente successivo restituisce `STATUS_TIMEOUT`.
- [ ] **Generalizzare `NtCreateThreadEx` e `NtResumeThread`.** Verificare
  processo target, routine/argomento, stack reserve/commit e guard, attributi
  `CLIENT_ID`/TEB, sospensione iniziale, resume count, context AMD64 e rollback
  dopo errori di allocazione o copyout. QEMU esercita via syscall CPL3 il rifiuto
  `STATUS_INVALID_PARAMETER` per commit maggiore del reserve e reserve oltre il
  limite gestito, verificando handle nullo; il fixture kernel verifica inoltre
  che non siano creati oggetti o ready entry. Un attributo `CLIENT_ID`
  duplicato viene rifiutato senza handle, oggetti o ready entry. QEMU CPL3
  verifica anche il rifiuto `STATUS_NOT_IMPLEMENTED` di flag non supportati con
  handle nullo, e `STATUS_INVALID_PARAMETER` senza handle per dimensioni stack
  che causano overflow nell'arrotondamento. Il contratto generale e il rollback
  di tutte le fasi restano aperti. La fixture CPL3 verifica inoltre
  `STATUS_ACCESS_VIOLATION` con output handle non scrivibile prima di allocare
  stack, TEB o membership del thread. La fixture CPL3 controlla inoltre che
  un copyout non valido di `PreviousSuspendCount` in `NtResumeThread` non
  rimuova la sospensione: il wait a timeout zero successivo resta in timeout.
  La nuova prova CPL3 termina inoltre un thread sospeso con un APC pendente e
  controlla stato di uscita, wait segnalato, mancata esecuzione dell'APC e
  chiusura dell'handle.
  La fixture file-backed avvia anche un thread dal vero entry point PE, ne
  attende l'uscita via syscall e chiude l'handle nel processo proprietario.
- [x] **Collegare il wrapper `NtResumeThread` al servizio Ps verificato.** Il
  wrapper inoltra handle e puntatore al resume count al syscall; QEMU esercita
  il resume count 1→0 sul thread creato sospeso. `NtSuspendThread` resta
  esplicitamente `STATUS_NOT_IMPLEMENTED` finché manca il supporto kernel per
  sospendere thread già avviati.
- [ ] **Completare terminazione locale e remota di thread e processi.** Coprire
  thread ready, running, waiting, suspended e mai avviati; processi con zero,
  uno o più thread; APC rundown; segnalazione degli oggetti; cancellazione delle
  attese; reaping differito; chiusura address space; status di uscita e retry
  dopo errori. Un preflight fallito non deve alterare code, attese o stato di
  terminazione. Il thunk NTDLL `NtTerminateThread` ora usa il servizio syscall
  canonico; QEMU CPL3 termina il thread corrente con `STATUS_ACCESS_DENIED` e il
  waiter osserva lo status previsto. Resta da generalizzare e stressare gli
  altri stati e le combinazioni di thread/processo. QEMU verifica inoltre la
  terminazione del processo file-backed quando l'ultimo thread PE chiama
  `NtTerminateThread`, wait sui due oggetti e recupero PFN dopo il teardown.
- [ ] **Completare attese e lifetime degli oggetti di processo/thread.**
  Verificare timeout, alertability/APC, cancellazione, chiusura dell'ultimo
  handle durante l'attesa, riferimenti trattenuti, CID lookup e reaping in ogni
  ordine valido. Il test QEMU kernel chiude entrambi gli handle di un thread
  mentre un waiter è bloccato sul relativo oggetto; l'esecuzione successiva
  segnala l'oggetto, risveglia il waiter e consente il reaping con il riferimento
  esplicito di verifica ancora valido. Gli altri ordini e le varianti timeout/
  alertability restano aperti. QEMU CPL3 verifica inoltre che il wait a timeout
  zero su un thread appena creato sospeso restituisca `STATUS_TIMEOUT` e lasci
  il thread disponibile per setup APC e resume. Il test processo ora blocca un
  waiter, chiude entrambi gli handle pubblici, verifica che gli handle ritirati
  non siano più usabili, poi termina il processo da un suo thread e controlla il
  wake/status. Un riferimento esplicito mantiene l'oggetto vivo fino al controllo
  finale, dopo il quale il PFN/object count torna al baseline.
- [ ] **Portare i percorsi M4 oltre il solo bootstrap BSP.** Collegare accesso,
  lifecycle e cleanup alle entry native Ps/Ob e ai caller reali; mantenere
  capture, pubblicazione, rollback e sincronizzazione coerenti con il contesto
  Ke corrente. La copertura QEMU deve usare i percorsi syscall e non solo fixture
  interne di bootstrap.
- [x] **Delimitare esplicitamente il namespace degli oggetti Ps nel percorso
  nativo coperto.** Il namespace nominato freestanding resta non supportato;
  `NtCreateProcessEx` e `NtCreateThreadEx` rifiutano `ObjectAttributes` con nome
  con `STATUS_NOT_IMPLEMENTED`. Il fixture CPL3 verifica entrambi i rifiuti e
  controlla che non venga pubblicato un handle.

### Limiti dichiarati e accettazione integrata

- [x] **Mantenere gli initializer DLL esplicitamente non supportati.**
  Un'immagine con initializer non implementati deve fallire con lo status
  previsto; il fixture QEMU copre il rifiuto dei TLS callback DLL con
  `STATUS_NOT_IMPLEMENTED`. Non saltare callback richieste né restituire
  successo sintetico.
- [x] **Eseguire la verifica integrata M4 dopo gli ultimi cambiamenti.**
  Usare `python3 qemu.py --headless`; controllare log seriali, status di uscita,
  accessi, lifecycle, APC, `SEC_IMAGE` file-backed e baseline PFN. L'ultimo
  `python3 qemu.py --headless` ha completato il boot modulare e mantenuto la
  seriale stabile per 5 secondi.
- [ ] **Aggiornare la riga M4 e il ledger d'integrazione.** Farlo quando i punti
  sopra sono chiusi o quando cambiano materialmente i limiti supportati.

## Già verificato secondo la milestone corrente

- [x] Percorso Ps/Ob limitato per processi e thread, APC/rundown e reaper sul
  BSP, inclusa crescita dello stack attraverso guard fault CPL3 e recupero PFN.
- [x] Controlli di accesso limitati QEMU/Ob: aperture per CID, diritti concessi
  e negati, DACL UserMode, generic/`MAXIMUM_ALLOWED` e rollback delle callback.
- [x] Percorso Io/Mm/Ps file-backed `SEC_IMAGE`: apertura di un PE dal volume
  FAT32 montato, creazione section, mapping rilocato e inizializzazione
  dell'immagine principale con recupero PFN.
- [x] `NtCreateProcessEx` da section file-backed `SEC_IMAGE` verificato sul
  percorso reale AHCI/FAT32, come riportato nella riga M4 della milestone.
- [x] Processo file-backed `SEC_IMAGE` eseguito in CPL3: QEMU verifica thread
  d'entry PE, terminazione syscall, wait/close nelle handle table corrette,
  attach/restore e recupero PFN dopo il teardown.
- [x] Il fix del control word x87 `0x037f` e dei selettori DS/ES nel contesto
  APC elimina il precedente `STATUS_INVALID_PARAMETER`/`#UD`; commit
  `cf1f4d9`. Il test APC esteso resta aperto come descritto sopra.

## Stato QEMU annotato

- Verifica end-to-end processo/thread da PE file-backed: `TMPDIR=/private/tmp
  python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-fileimage-lifecycle-final` è passata. Anche i test
  bootstrap a due/tre thread, timer-preemption Ring 0/Ring 3, APC/DPC, reaper,
  PS/2 e seriale stabile per 5 secondi sono verdi.
- Verifica integrata del test CPL3 di terminazione del thread sospeso con APC
  pendente: `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-suspended-apc-retry` è passata. Sono verdi i marker di
  creazione/ripresa user, i self-test bootstrap a due e tre thread, SEC_IMAGE,
  timer-preemption Ring 0/Ring 3, DPC/APC, reaper, PS/2 e seriale stabile per 5s.
- Verifica integrata dei pseudo-handle current-process/current-thread in
  `NtWaitForSingleObject`, insieme ai test process flags e resume copyout:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-pseudo-waits-batch` è passata: build/link bare-metal,
  fixture CPL3, input PS/2 e seriale stabile per 5 secondi.
- Verifica integrata dei test CPL3 `NtCreateProcessEx(Flags=1)` e rollback
  `NtResumeThread` su copyout invalido:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-resume-copyout-retry` è passata. Il primo tentativo
  dello stesso batch ha raggiunto i marker CPL3 Ps ma è terminato con `#UD`
  dopo `SEC_IMAGE`; la ripetizione ha completato boot, PS/2 e seriale stabile
  per 5 secondi.
- Verifica integrata del rifiuto `NtCreateProcessEx(Flags=1)` CPL3:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-process-flags-final` è passata. La build/link
  `ntoskrnl_core_baremetal`, il boot modulare, l'input PS/2 e la seriale stabile
  per 5 secondi sono verdi.
- Build/link `ntoskrnl_core_baremetal` e target boot: passati con
  `python3 qemu.py --build-only` nel batch precedente.
- Ultimo boot completo riuscito: `python3 qemu.py --headless` nel batch
  precedente; seriale stabile per 5 secondi, con test Ring 3 APC e rifiuti
  espliciti degli attributi nominati.
- Dopo il ripristino del fixture sperimentale alertable non verificato,
  `python3 qemu.py --headless` ha ricompilato e linkato il kernel, ma due avvii
  consecutivi si sono fermati nel controllo d'input preliminare:
  `PS/2 IRQ route was enabled but no ISR byte reached the consumer`. Il boot
  aveva raggiunto la superficie CPL3 prima dell'invio dell'input. Un avvio con
  `--display none` senza `--headless` è stato rifiutato dal runner perché
  `--interactive` richiede un backend visibile. Questi tentativi non verificano
  i punti M4; il risultato positivo precedente rimane l'ultima verifica
  integrata riuscita. M4 resta aperto per i contratti generali elencati sopra.
- Verifica del fixture esteso APC/wait: la seriale QEMU ha raggiunto
  `[MicaNT Ps] Two CPL3 threads, TLS slots and event waits verified` e i test
  successivi del reaper. Il processo QEMU è poi terminato con errore nel solo
  controllo d'input PS/2 (`PS/2 IRQ route was enabled but no ISR byte reached
  the consumer`) dopo che il click era stato ricevuto dal desktop. Il caso M4
  APC/wait è passato; l'accettazione complessiva del runner resta non verde.
- Verifica successiva dopo l'estensione al delay alertable: `python3 qemu.py
  --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-alertable-waits8` è terminato con esito positivo; la
  build/link del kernel bare-metal, il boot, il controllo input, la seriale
  stabile per 5 secondi e i test CPL3 sono passati.
- Il run con la prova CPL3 del security descriptor ha raggiunto i marker di
  completamento dei test Ps, compreso `[MicaNT Ps] Two CPL3 threads, TLS slots
  and event waits verified`, e il click desktop; il runner ha poi fallito sul
  controllo IRQ PS/2. Dunque il caso DACL è stato eseguito in QEMU, ma questo
  run non è una verifica integrata completa.
- Verifica integrata completa dopo il supporto `OBJ_INHERIT` di
  `NtCreateEvent` e il test UserMode di `SeDebugPrivilege`:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-open-debug-privilege` è passato. Build/link bare-metal,
  test CPL3, input PS/2 e stabilità seriale per 5 secondi sono verdi. I tentativi
  sperimentali di inheritance CPL3 non hanno superato il fixture e sono stati
  rimossi; la prova end-to-end resta aperta.
- Verifica integrata completa della chiusura degli handle di thread durante un
  wait pendente: `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-wait-close-last-pass` è passata, inclusi build/link,
  seriale stabile per 5 secondi e controllo PS/2.
- Verifica integrata completa dell'inheritance di un evento nel processo figlio:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-event-inherit-kernel` è passata. Il test ha chiuso
  l'handle parent, fatto attach al child e completato il wait; build/link, PS/2
  e seriale stabile per 5 secondi sono verdi. L'esercizio CPL3 rimane aperto.
- Verifica integrata completa della DACL CPL3 su `NtCreateThreadEx`:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-thread-sd-deny2` è passata, con `STATUS_ACCESS_DENIED`
  e handle nullo per la DACL vuota, più build/link, PS/2 e seriale stabile per
  5 secondi.
- Verifica integrata completa dopo il caso di attributo `CLIENT_ID` duplicato:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-duplicate-attributes` è passata. Il rifiuto
  `STATUS_INVALID_PARAMETER` senza handle/oggetto/ready entry, build/link,
  controllo input PS/2 e seriale stabile per 5 secondi sono verdi.
- Verifica integrata completa del rifiuto CPL3 dei flag non supportati in
  `NtCreateThreadEx`: `python3 qemu.py --headless --timeout 120
  --diagnostics-dir /private/tmp/micant-m4-thread-unsupported-flags` è passata,
  incluso handle nullo, build/link, controllo PS/2 e seriale stabile per 5
  secondi.
- Verifica integrata completa del controllo overflow stack CPL3 di
  `NtCreateThreadEx`: `python3 qemu.py --headless --timeout 120
  --diagnostics-dir /private/tmp/micant-m4-thread-stack-overflow` è passata,
  con `STATUS_INVALID_PARAMETER` e handle nullo, build/link, controllo PS/2 e
  seriale stabile per 5 secondi.
- Verifica integrata completa del wait CPL3 a timeout zero su thread sospeso:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-suspended-zero-wait2` è passata. Il syscall ha
  restituito `STATUS_TIMEOUT`, il successivo setup APC/resume è proseguito,
  build/link, PS/2 e seriale stabile per 5 secondi sono verdi.
- Verifica integrata completa dopo i test del lotto wait/lifetime processo e
  preflight degli output `NtCreateProcessEx`/`NtCreateThreadEx`:
  `python3 qemu.py --headless --timeout 120 --diagnostics-dir
  /private/tmp/micant-m4-batch-lifecycle-debug4` è passata. Build/link
  `ntoskrnl_core_baremetal`, due thread CPL3, terminazione, APC, file-backed
  `SEC_IMAGE`, input PS/2 e seriale stabile per 5 secondi sono verdi.

