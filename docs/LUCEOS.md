# STEERING — MicaNT → LuceOS: rebranding, migrazione residua hosted e consolidamento del kernel canonico

## MISSIONE

Trasforma definitivamente MicaNT in **LuceOS**.

L'obiettivo è ottenere un repository coerente, pulito, privo di implementazioni hosted duplicate o obsolete, con un solo kernel AMD64 modulare di produzione e un'architettura Ring 0/Ring 3 rigorosamente separata.

Repository attuale:
https://github.com/luca-saggese/MicaNT

Nome definitivo del sistema operativo: **LuceOS**

Non limitarti a rinominare file o modificare README. Devi completare la migrazione delle funzionalità hosted residue effettivamente necessarie, eliminare le implementazioni già sostituite, consolidare la build e verificare che il desktop continui a funzionare in QEMU.

## 1. Regole di esecuzione

Lavora direttamente su `main`.

Non creare branch o worktree aggiuntivi.

Sono autorizzati commit locali incrementali per blocchi coerenti e verificati.

Non eseguire push remoto senza autorizzazione separata.

Preserva le modifiche preesistenti e il lavoro degli altri agenti. Prima di modificare file condivisi controlla HEAD, stato Git e differenze presenti.

Non utilizzare comandi Git distruttivi.

Parallelizza l'analisi e le implementazioni indipendenti, assegnando ownership distinta dei file. Un unico coordinatore deve integrare e committare i cambiamenti su main.

Dopo ogni blocco completato continua con il successivo. Un commit, una compilazione o una modifica documentale non terminano l'incarico.

## 2. Prima fase — Specifica completa obbligatoria

Prima di eseguire rinomine o eliminazioni, crea:

`docs/LUCEOS_REBRANDING_AND_HOSTED_CODE_MIGRATION_SPEC.md`

Il documento deve contenere:

- Inventario dei target CMake.
- Inventario degli eseguibili PE e delle DLL.
- Moduli kernel canonici.
- Driver reali e driver hosted.
- Implementazioni duplicate.
- Codice non collegato.
- Codice legacy.
- Codice ancora necessario.
- Test hosted utili.
- Tool e generatori da conservare.
- Dipendenze transitive.
- Dipendenze dal vecchio kernel monolitico.
- Identificatori pubblici e interni da rinominare.
- Formati persistenti da preservare.
- Mappa dei file da modificare, migrare o eliminare.
- Ordine delle migrazioni.
- Piano di regressione.
- Criteri finali di completamento.

Utilizza `docs/FREESTANDING_GAP_MATRIX.md` come riferimento iniziale, ma rivalida ogni voce contro il manifest e le dipendenze correnti.

Non effettuare cancellazioni basandoti soltanto su nomi, directory o vecchi rapporti.

Per ogni elemento utilizza una classificazione:

**KEEP — MIGRATE — REPLACE — DELETE — ARCHIVE**

Per ogni `DELETE` registra il sostituto canonico o la ragione verificata per cui il componente non è più necessario.

Poi inizia immediatamente l'implementazione.

## 3. Rinomina completa in LuceOS

Sostituisci il nome pubblico MicaNT con LuceOS in:

- README.
- Documentazione attiva.
- Script.
- Identità visuale.
- Boot splash.
- Nome visualizzato dal sistema.
- Log di boot.
- Version information.
- Manifest.
- Package names.
- Installer e immagini di distribuzione.
- Configurazione degli strumenti.
- Test e output diagnostici.

Aggiorna CMake da `project(MicaNT ...)` a `project(LuceOS ...)`.

Adotta nuovi identificatori coerenti, per esempio:

- Progetto CMake: `LuceOS`.
- Namespace nuovo: `luceos`.
- Include root finale: `include/luceos/`.
- Macro nuove: `LUCEOS_*`.
- Prefissi dei tool: `luceos_*`.

Non rinominare meccanicamente tutti gli identificatori ABI.

Valuta separatamente:

- Contratti UEFI.
- Handoff bootloader/kernel.
- PE imports/exports.
- Nomi di DLL e driver.
- Device paths.
- Registry paths.
- Formati persistenti.
- Magic numbers.
- GUID.
- Protocolli di comunicazione.
- Identificatori di filesystem.
- Simboli esposti a tool esterni.

Quando un identificatore deve rimanere compatibile, preservalo e documentane la motivazione.

La rinomina del repository remoto GitHub deve essere trattata separatamente: non modificare automaticamente il nome remoto o la configurazione GitHub senza autorizzazione specifica.

## 4. Consolidamento del kernel

LuceOS deve continuare a utilizzare un solo kernel modulare canonico.

Mantieni gli owner:

Ke, Mm, Ob, Ps, Io, Se, Cm, FsRtl, Cc, HAL e gli altri sottosistemi esistenti.

Non introdurre nuove implementazioni parallele durante la migrazione.

Verifica:

- Ownership dei processi.
- Thread e scheduler.
- PFN e memoria virtuale.
- Object Manager.
- Handle.
- Security tokens.
- Filesystem.
- I/O.
- Driver lifecycle.
- Syscall ABI.
- Win32k.
- Compositor.
- Servizi Ring 3.

Qualsiasi funzionalità necessaria ancora implementata soltanto in hosted deve essere migrata nel sottosistema canonico appropriato.

Non importare STL hosted o primitive del sistema operativo host dentro il kernel freestanding.

Non utilizzare output statici o successi fittizi per sostituire funzionalità reali.

## 5. Eliminazione del codice hosted migrato

Per ciascuna implementazione hosted:

1. Identifica i call site.
2. Verifica se è compilata.
3. Verifica se è utilizzata da tool o test.
4. Identifica il sostituto canonico.
5. Verifica che il sostituto sia realmente funzionante.
6. Migra eventuali test ancora utili.
7. Aggiorna le dipendenze.
8. Elimina il codice obsoleto.
9. Ricompila.
10. Esegui la regressione pertinente.

Non mantenere due versioni della medesima funzionalità soltanto per compatibilità con test obsoleti.

Non eliminare però test hosted che esercitano correttamente il codice canonico.

La separazione richiesta è:

**Codice di produzione unico, adattatori di test minimi e indipendenti.**

## 6. Migrazione del codice hosted non ancora sostituito

Per ogni componente ancora necessario ma non migrato:

- Identifica l'owner canonico.
- Definisci l'ABI necessaria.
- Rimuovi dipendenze dal runtime host.
- Collega i servizi reali.
- Gestisci errori e cleanup.
- Valida sincronizzazione e IRQL.
- Completa i percorsi di lifecycle.
- Collega il codice alla build effettiva.
- Verifica in QEMU.

Privilegia i componenti necessari a boot, Explorer, desktop, networking, storage e sicurezza.

Non trasformare il lavoro in un porting indiscriminato di ogni modello o API sperimentale.

Se una funzionalità non è necessaria al sistema attuale e non ha un backend reale, classificala esplicitamente come futura o obsoleta.

Non mantenere implementazioni finte spacciandole per supporto reale.

## 7. Driver e modelli importati

Analizza ogni directory in `drivers/`.

Distingui:

- Driver freestanding effettivamente collegati.
- Driver con test hardware QEMU.
- Driver supportati su hardware fisico.
- Modelli hosted non collegati.
- Driver parziali.
- Driver duplicati.
- Codice non utilizzato.

Per i driver effettivamente migrati elimina le vecchie implementazioni hosted duplicate.

Per quelli non ancora migrati decidi se:

- Migrare immediatamente perché necessari.
- Conservare come specifica o prototipo chiaramente separato.
- Eliminare perché obsoleti.

Non cancellare automaticamente una famiglia di driver soltanto perché è `EXCLUDE_FROM_ALL`.

Le dipendenze dei driver devono rimanere compatibili con HAL, Io/PnP, Mm e i contratti DMA del kernel canonico.

## 8. Pulizia Ring 0 / Ring 3

Esegui un audit degli include.

Le DLL Ring 3 non devono importare header privati del kernel.

Sposta i contratti binari condivisi nella directory condivisa canonica.

Elimina:

- Dipendenze improprie da `km/` nei componenti user-mode.
- Accessi diretti user-mode a oggetti Executive.
- Trampolini hosted.
- Import sintetici.
- Stub con successo fittizio.
- Manager duplicati.
- Adapter non più utilizzati.

Conserva la compatibilità NT/Win32 dove effettivamente implementata.

Non dichiarare complete API che rimangono parziali.

## 9. Pulizia dei target CMake e della build

Riorganizza il sistema di build affinché distingua chiaramente:

- Bootloader UEFI.
- Kernel freestanding.
- Driver di produzione.
- DLL Ring 3.
- Applicazioni di sistema.
- Test unitari portabili.
- Test QEMU.
- Strumenti host di sviluppo.

Rimuovi target obsoleti, dipendenze inutilizzate, librerie duplicate e sorgenti hosted non più richiesti.

Preserva i target canonici funzionali, rinominandoli soltanto dove non vengono compromesse dipendenze o compatibilità.

La build non deve collegare accidentalmente implementazioni hosted dentro gli artefatti bare-metal.

Non modificare i risultati dei test per mascherare errori di linking.

## 10. Eliminazione del vecchio codice

Dopo avere verificato che non esistono dipendenze attive, rimuovi i sorgenti legacy effettivamente obsoleti dal progetto corrente.

Questo vale anche per `old/`, ma soltanto dopo un audit delle funzioni ancora mancanti e dei riferimenti di documentazione.

Non ricopiare codice monolitico nel nuovo kernel senza integrare correttamente ownership e runtime.

La cronologia Git costituisce un riferimento storico per i file già tracciati; la rimozione dal repository corrente non deve essere confusa con una cancellazione della cronologia.

Non eliminare licenze, attribuzioni o documenti di provenienza necessari per i componenti rimasti.

## 11. Ripulitura della struttura delle directory

Semplifica le directory dove esistono duplicazioni reali.

Obiettivo indicativo:

```text
LuceOS/
├── boot/
├── kernel/
├── include/
│   └── luceos/
│       ├── km/
│       ├── shared/
│       └── um/
├── drivers/
├── user/
├── tools/
├── tests/
├── System/
├── docs/
├── scripts/
└── CMakeLists.txt
```

Questa struttura è indicativa.

Non spostare file soltanto per ottenere un'organizzazione esteticamente più ordinata.

Ogni spostamento deve preservare include, build, manifest e test.

## 12. Parallelizzazione

Dividi il lavoro in blocchi:

**R0 — Inventario e specifica**

Dipendenze, classification matrix, roadmap.

**R1 — Identità LuceOS**

Rinomina, manifest, CMake, build scripts, documentazione.

**R2 — Kernel hosted cleanup**

Rimozione dei duplicati e completamento delle dipendenze canoniche.

**R3 — Driver cleanup**

Driver reali, modelli hosted, test e packaging.

**R4 — Ring 3 cleanup**

DLL, runtime, Win32k, Explorer e servizi.

**R5 — Test e build**

Verifiche di compilazione, architettura, QEMU e regressione.

**R6 — Pulizia finale**

File inutilizzati, documentazione storica, directory, nomi residui e report conclusivo.

I blocchi indipendenti possono procedere parallelamente.

Le modifiche agli stessi file devono essere coordinate da un unico integratore.

I commit devono essere serializzati su `main`.

## 13. Test e regressioni

Prima delle rimozioni raccogli una baseline riproducibile.

Dopo ogni blocco verifica:

- Compilazione.
- Linking.
- Confini Ring 0/Ring 3.
- Boot UEFI.
- Allocazione memoria.
- Scheduling.
- Processi CPL3.
- Syscall.
- Storage.
- Driver VirtIO.
- Compositor.
- Explorer.
- Input.
- Temi, icone, cursori e font, per i percorsi attivi.

Esegui QEMU ai checkpoint significativi.

La verifica finale deve avviare LuceOS attraverso il kernel canonico, mostrare il desktop e utilizzare le applicazioni Ring 3 disponibili.

Registra gli errori preesistenti separatamente dalle regressioni introdotte dalla migrazione.

Non dichiarare una funzionalità verificata in QEMU sulla base della sola compilazione.

## 14. Commit incrementali

Effettua commit locali selettivi dopo ogni blocco coerente.

Esempi:

- `docs: define LuceOS migration inventory and acceptance criteria`
- `refactor: rename project identity to LuceOS`
- `refactor(build): consolidate freestanding targets`
- `refactor(kernel): remove replaced hosted implementations`
- `refactor(drivers): remove obsolete driver models`
- `refactor(user): consolidate canonical user-mode components`
- `test: verify LuceOS QEMU integration`
- `chore: remove dead sources and stale references`

Non effettuare un unico commit che mescola migliaia di rinomine, cancellazioni e modifiche funzionali impossibili da revisionare.

Prima di ogni commit controlla i file staged e la provenienza delle modifiche.

Dopo ogni commit continua con i requisiti aperti.

## 15. Criteri finali

Il lavoro è completo quando:

1. Il nome pubblico del progetto è LuceOS.
2. Build, boot e documentazione attiva utilizzano l'identità coerente.
3. Gli identificatori interni nuovi rispettano la convenzione stabilita.
4. I vecchi identificatori conservati sono documentati e giustificati.
5. Il kernel di produzione rimane unico e modulare.
6. Il codice hosted già sostituito è stato rimosso.
7. Il codice hosted ancora necessario è stato migrato oppure classificato esplicitamente con motivazione.
8. Non esistono implementazioni concorrenti attive degli stessi sottosistemi.
9. I test portabili utili sono stati conservati.
10. Le DLL Ring 3 rispettano le separazioni architetturali.
11. Il sistema di build non include per errore codice legacy.
12. Le dipendenze dal kernel monolitico sono assenti.
13. I driver realmente utilizzati continuano a funzionare.
14. QEMU avvia il sistema canonico.
15. Il percorso Explorer/compositor non presenta regressioni introdotte.
16. I commit sono incrementali e verificabili.
17. Le documentazioni descrivono lo stato effettivo.
18. Il repository contiene un inventario finale delle funzionalità mantenute, migrate, eliminate e rinviate.

## ISTRUZIONE CONCLUSIVA

Prima crea la specifica dettagliata e la matrice delle dipendenze.

Poi esegui immediatamente la rinomina e il consolidamento, procedendo per blocchi indipendenti, con commit incrementali e verifiche QEMU.

Non fermarti dopo il solo rebranding.

Non cancellare componenti senza aver verificato cosa li utilizza.

Non confondere la rimozione del codice hosted con la rimozione dei test eseguiti su host.

Non ripristinare il kernel monolitico.

**Il risultato richiesto è LuceOS: un sistema operativo AMD64 NT-compatible con un solo kernel canonico, codice di produzione consolidato, componenti legacy superflui rimossi e desktop funzionante.**

Continua fino al completamento dei requisiti raggiungibili nell'ambiente operativo, documentando precisamente eventuali blocchi reali e i criteri ancora aperti.