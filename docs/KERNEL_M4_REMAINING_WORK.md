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

- [ ] **Ripristinare la consegna di un APC utente normale al ritorno verso Ring 3.**
  Il test corrente crea e riprende un thread, accoda una callback e chiama
  `NtTestAlert`, ma il thread termina con `STATUS_ACCESS_DENIED` senza impostare
  il marker della callback. Individuare perché il contesto preparato non arriva
  al dispatcher NTDLL, correggere il percorso canonico e mantenere il test
  end-to-end.
- [ ] **Verificare gli esiti e il cleanup della consegna APC.**
  Coprire callback riuscita, routine utente non valida/eccezione, APC rimaste in
  coda, rundown durante terminazione e rimozione delle risorse APC. I casi non
  supportati devono restituire uno status esplicito e lasciare stato e
  riferimenti coerenti.

### Contratti di accesso

- [ ] **Completare i controlli di accesso per creazione, apertura e uso di
  processi e thread.** Verificare access mode Kernel/User, diritti specifici,
  mapping dei generic rights, `MAXIMUM_ALLOWED`, token/DACL e handle pseudo nei
  percorsi nativi interessati. Un handle con diritti insufficienti deve fallire
  con lo status previsto senza pubblicare handle o oggetti parziali.
- [ ] **Chiudere i percorsi di duplicazione e inheritance degli handle nei
  processi.** Verificare diritti concessi, callback Ob, handle ereditabili,
  rollback se una callback o la copia dell'output fallisce e rilascio esatto dei
  riferimenti. Riutilizzare le tabelle Ob esistenti.
- [ ] **Verificare gli attributi di handle e gli errori di chiusura.** Coprire
  `OBJ_PROTECT_CLOSE`, chiusura forzata durante teardown e output utente non
  valido senza doppia chiusura o perdita di riferimenti.

### Creazione e lifecycle generali

- [ ] **Generalizzare `NtCreateProcessEx` oltre il fixture bootstrap.** Verificare
  parent e section handle, accesso, attributi, flags supportati, immagine
  `SEC_IMAGE` file-backed, metadata loader, handle inheritance, pubblicazione
  dell'handle e rollback di ogni fase. Gli input o flags non supportati devono
  fallire esplicitamente.
- [ ] **Generalizzare `NtCreateThreadEx` e `NtResumeThread`.** Verificare
  processo target, routine/argomento, stack reserve/commit e guard, attributi
  `CLIENT_ID`/TEB, sospensione iniziale, resume count, context AMD64 e rollback
  dopo errori di allocazione o copyout.
- [ ] **Completare terminazione locale e remota di thread e processi.** Coprire
  thread ready, running, waiting, suspended e mai avviati; processi con zero,
  uno o più thread; APC rundown; segnalazione degli oggetti; cancellazione delle
  attese; reaping differito; chiusura address space; status di uscita e retry
  dopo errori. Un preflight fallito non deve alterare code, attese o stato di
  terminazione.
- [ ] **Completare attese e lifetime degli oggetti di processo/thread.**
  Verificare timeout, alertability/APC, cancellazione, chiusura dell'ultimo
  handle durante l'attesa, riferimenti trattenuti, CID lookup e reaping in ogni
  ordine valido.
- [ ] **Rimuovere la dipendenza BSP-only dai percorsi che M4 dichiara generali.**
  Sostituire l'esclusione locale basata su `cli` con sincronizzazione Ke/Ob/Ps
  corretta dove il percorso deve operare oltre il BSP; proteggere pubblicazione,
  terminazione, attach, handle table e reaper. Includere stress concorrente e
  verifica QEMU multi-CPU quando il backend SMP è disponibile. Non dichiarare
  supporto SMP finché scheduler, lock e teardown non sono coerenti fra loro.
- [ ] **Chiudere o delimitare esplicitamente il namespace degli oggetti Ps.**
  La documentazione storica segnala che il namespace nominato freestanding non
  è supportato. Stabilire se rientra nell'accettazione M4; se resta fuori,
  rifiutare in modo documentato gli attributi/nome non supportati e coprire il
  comportamento con un test, senza fingere un'apertura riuscita.

### Limiti dichiarati e accettazione integrata

- [ ] **Mantenere gli initializer DLL esplicitamente non supportati.**
  Un'immagine con initializer non implementati deve fallire con lo status
  previsto; non saltare callback richieste né restituire successo sintetico.
  Aggiornare questa voce solo se il supporto viene implementato e verificato.
- [ ] **Eseguire la verifica integrata M4 dopo gli ultimi cambiamenti.**
  Usare `python3 qemu.py --headless`; controllare log seriali, status di uscita,
  accessi, lifecycle, APC, `SEC_IMAGE` file-backed e baseline PFN. Registrare
  separatamente build/link, avvio QEMU e test effettivamente completati.
- [ ] **Aggiornare la riga M4 e il ledger d'integrazione.** Farlo quando i punti
  sopra sono chiusi o quando cambiano materialmente i limiti supportati.

## Già verificato secondo la milestone corrente

- [x] Percorso Ps/Ob limitato per processi e thread, APC/rundown e reaper sul
  BSP, inclusa crescita dello stack attraverso guard fault CPL3 e recupero PFN.
- [x] Percorso Io/Mm/Ps file-backed `SEC_IMAGE`: apertura di un PE dal volume
  FAT32 montato, creazione section, mapping rilocato e inizializzazione
  dell'immagine principale con recupero PFN.
- [x] Il fix del control word x87 `0x037f` e dei selettori DS/ES nel contesto
  APC elimina il precedente `STATUS_INVALID_PARAMETER`/`#UD`; commit
  `cf1f4d9`. Il test APC esteso resta aperto come descritto sopra.

## Stato QEMU annotato

- Build/link `ntoskrnl_core_baremetal` e target boot: passati con
  `python3 qemu.py --build-only` nel batch precedente.
- Boot completo: non superato nell'ultima verifica; il test Ring 3 process/thread
  si ferma sul marker APC (`thread exit=STATUS_ACCESS_DENIED`). Il fix di
  `STATUS_INVALID_PARAMETER` quindi non equivale all'accettazione M4.

