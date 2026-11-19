z# LuceOS — Compositor Engine Consolidation & Optimization

**Documento esecutivo per coding agent**

**Target:** kernel modulare AMD64, compositor Ring 3 canonico  
**Baseline verificata:** `tools/compositor_app.cpp`, blob `ff2b9632`  
**Priorità:** correttezza → stabilità → efficienza → prestazioni  
**Obiettivo:** consolidare e ottimizzare il compositor esistente, non sostituirlo.

---

## 1. Missione

Rivedi, consolida e ottimizza il compositor software canonico di LuceOS, mantenendo la compatibilità con le superfici Win32, il sistema di temi, le decorazioni, il cursore, le animazioni, il backend display e gli effetti già implementati.

Il compositor deve diventare un motore grafico deterministico, robusto e performante, capace di gestire numerose finestre, sovrapposizioni, trasparenze ed effetti senza artefatti e senza elaborare inutilmente pixel.

**Non riscrivere il compositor da zero. Non introdurre un altro compositor, un secondo Surface Manager o un framework grafico alternativo.**

L'intervento deve migliorare l'implementazione esistente con modifiche incrementali, misurabili e verificabili.

### Risultati obbligatori

1. Rendering full e partial pixel-equivalente.
2. Assenza di artefatti al movimento del mouse.
3. Damage tracking corretto e preciso.
4. Pipeline alpha coerente.
5. Lifetime e validazione delle superfici robusti.
6. Liquid Glass e blur compatibili con il repaint parziale.
7. Preparazione della scena senza lavoro duplicato.
8. Occlusion culling per superfici opache.
9. Cursor overlay indipendente dal framebuffer pulito.
10. Presentazioni parziali e frame scheduling efficienti.
11. Consumo CPU misurabile e inattività reale.
12. Budget RAM complessivo del desktop entro 500 MiB.
13. Regressioni grafiche coperte da test deterministici e QEMU.

Il lavoro non è completato dalla sola compilazione o dal miglioramento di uno screenshot.

---

## 2. Regole di esecuzione

Leggi integralmente `AGENTS.md`.

Verifica `git status`, HEAD e modifiche concorrenti prima di iniziare.

Lavora direttamente su `main`, senza nuovi branch né worktree.

Preserva le modifiche esistenti e non sovrascrivere lavoro di altri agenti.

Effettua commit locali selettivi di modifiche attribuibili e verificate, senza push.

Utilizza esclusivamente il kernel modulare canonico e il compositor Ring 3 di produzione.

Non collegare codice da `old/`.

Non introdurre dipendenze hosted nel processo bare-metal.

Non spostare la composizione grafica in Ring 0.

### Limiti di scope

Il file principale è:

`tools/compositor_app.cpp`

Sono consentite modifiche ai suoi helper grafici diretti, alle interfacce condivise e ai chiamanti strettamente necessari per correggere una violazione dimostrata.

Esamina altri sottosistemi soltanto quando un contratto specifico lo richiede, indicando funzione, chiamante e problema.

Non condurre un audit generale di Mm, Ps, Ke, User32, Explorer, filesystem, rete o driver estranei.

Non implementare nuovi temi, widget, menu, API grafiche, animazioni o funzionalità desktop.

---

## 3. Architettura di riferimento

Il codice attuale presenta caratteristiche valide da preservare:

- Processo compositor Ring 3 dedicato.
- Attesa event-driven mediante servizio `0x83`.
- Display mode ottenuto a runtime.
- Superfici e descrittori dinamici.
- Metadata snapshot con sequenza.
- Dirty rectangles e full redraw fallback.
- Presentazione parziale tramite servizio `0x81`.
- Fast path per pixel opachi.
- Cursor overlay separato.
- Rendering di decorazioni WhiteSur.
- Animazioni di finestre.
- Cache Liquid Glass.
- Contatori diagnostici.

Non rimuovere queste capacità per semplificare artificialmente il problema.

### Organizzazione interna desiderata

Conservando un unico compositor, separa logicamente quattro fasi:

**Scene preparation**

Prepara una descrizione coerente della scena: superfici visibili, geometrie, Z-order, clip, opacità e proprietà necessarie al rendering.

**Damage resolution**

Determina quali pixel dell'output devono essere ricomposti, includendo le dipendenze spaziali di ombre, blur e Glass.

**Rasterization**

Disegna soltanto le superfici visibili nelle regioni necessarie, applicando decorazioni, blending ed effetti.

**Presentation**

Invia i rettangoli modificati al display e sovrappone il cursore senza alterare il framebuffer pulito.

Questa separazione può essere realizzata mediante funzioni, strutture leggere e moduli interni. Non creare nuovi manager globali.

---

# FASE P0 — Baseline e verifica di correttezza

## 4. Congelare una baseline misurabile

Prima di ottimizzare:

1. Verifica la compilazione del compositor canonico.
2. Avvia LuceOS in QEMU.
3. Conserva serial log e screenshot QMP.
4. Registra risoluzione, backend, RAM assegnata e configurazione.
5. Abilita soltanto i contatori diagnostici necessari.
6. Raccogli il comportamento iniziale.

Usa la stessa configurazione per i confronti prima/dopo.

I test fondamentali devono funzionare su VirtIO-GPU 2D senza richiedere VirGL.

### Scenari della baseline

| ID | Scena |
|---|---|
| S0 | Desktop fermo |
| S1 | Movimento del solo cursore |
| S2 | Finestra opaca ferma |
| S3 | Due finestre opache sovrapposte |
| S4 | Finestra traslucida |
| S5 | Finestra con angoli arrotondati e ombra |
| S6 | Finestra con child HWND |
| S7 | Drag e resize |
| S8 | Hover dei caption buttons |
| S9 | Card Liquid Glass |
| S10 | Animazione minimize/restore |
| S11 | Cambio tema Light/Dark |

Non implementare nuovi controlli dentro Explorer per produrre queste prove. Usa `theme_test.exe` o un harness grafico strettamente dedicato.

## 5. Verifica full vs partial repaint

Questa è la prima correzione obbligatoria.

Per una scena identica:

1. Produci il framebuffer con composizione completa.
2. Ripristina lo stesso stato iniziale.
3. Produci il framebuffer usando dirty rectangles.
4. Confronta i pixel delle aree interessate.
5. Confronta anche il framebuffer finale dopo tutti gli aggiornamenti.
6. Ripeti con clipping parziale e rettangoli che iniziano a coordinate non nulle.

Per contenuti statici e identici, il risultato deve essere pixel-identico.

Gli effetti temporali o animati devono essere confrontati congelando lo stesso istante logico.

### Casi obbligatori

- Clip con origine X=0/Y=0.
- Clip con origine X=1/Y=1.
- Clip che attraversa un bordo.
- Clip che attraversa un angolo arrotondato.
- Clip che attraversa due superfici sovrapposte.
- Clip che comprende soltanto parte di un child HWND.
- Clip che attraversa l'ombra.
- Clip che attraversa una card Liquid Glass.
- Due dirty rectangles adiacenti.
- Due dirty rectangles sovrapposti.
- Dirty rectangle parzialmente fuori schermo.

### Invarianti

I rettangoli devono seguire ovunque la convenzione:

`left/top inclusivi, right/bottom esclusivi`.

La geometria della finestra deve essere indipendente dall'origine del clip.

Il pitch non deve essere confuso con la larghezza visibile.

Un aggiornamento parziale non deve spostare, eliminare o alterare pixel statici.

**Non passare alle ottimizzazioni finché queste verifiche non sono affidabili.**

---

# FASE P1 — Consolidamento della pipeline

## 6. Damage tracking

Esamina precisamente:

- `collectDamage()`
- `addDamageRegion()`
- `composeDamageRegions()`
- `presentDamage()`
- La costruzione dei danni delle animazioni.
- Le invalidazioni causate dagli effetti.

### Requisiti

Le regioni devono essere:

- Validate prima dell'utilizzo.
- Clippate al display runtime.
- Unite correttamente.
- Non duplicate inutilmente.
- Indipendenti dalla geometria degli array sorgenti.
- Compatibili con i rettangoli che si sovrappongono.

Il codice usa `kDamageRegionComplexityBudget = 16`.

Questo numero può rimanere come **budget di complessità per frame**, non come limite architetturale al numero di superfici o di eventi.

Se la complessità supera il budget, il fallback deve essere corretto e motivato.

Non trasformare automaticamente qualsiasi incremento della complessità in un full redraw senza considerare la superficie danneggiata.

Verifica che la scelta tra partial e full redraw possa basarsi su un confronto semplice dei costi stimati, senza introdurre un cost model generale.

### Obiettivo

Un cambiamento di 20×20 pixel non deve richiedere un full redraw, salvo dipendenze effettive che lo rendano necessario.

---

## 7. Coordinate e trasformazioni

All'interno di `composeRegion()` identifica esplicitamente:

- Screen coordinates.
- Window coordinates.
- Client coordinates.
- Surface coordinates.
- Parent/child coordinates.
- Animation coordinates.
- Output framebuffer coordinates.

Le conversioni devono essere effettuate una sola volta dove possibile.

Non ripetere trasformazioni differenti in funzioni che disegnano la stessa geometria.

### Controlli specifici

Verifica:

- `windowLeft/windowTop`.
- `sourceFrame/sourceCaption`.
- `clientWidth/clientHeight`.
- `surfaceSourceX/surfaceSourceY`.
- `sourceRow`.
- Stride del buffer.
- Clipping dei child HWND.
- Scaling durante le animazioni.
- Conversioni signed/unsigned.
- Overflow nelle moltiplicazioni e negli offset.

L'origine della finestra non deve cambiare tra full redraw e partial redraw.

Non correggere spostamenti di pixel aggiungendo offset numerici arbitrari.

---

## 8. Contratto dei pixel e alpha blending

Attualmente il codice contiene:

- `blend()`
- `applyPremultipliedOpacity()`
- `PremultipliedSourceOver()`
- Percorsi specializzati per icone, testo, decorazioni e Glass.

Definisci e documenta per ciascun buffer:

- Formato pixel.
- Ordine canali.
- Alpha straight oppure premultiplied.
- Significato del canale alpha.
- Stride.
- Ownership.

### Regola

Ogni percorso di rendering deve ricevere pixel nel formato dichiarato.

Non usare una funzione che interpreta alpha straight su un buffer premoltiplicato.

Non premoltiplicare due volte i canali.

Non applicare l'opacità globale con formule incompatibili fra percorsi diversi.

### Test

Verifica con pixel controllati:

- Alpha 0.
- Alpha 1.
- Alpha 64.
- Alpha 128.
- Alpha 254.
- Alpha 255.
- Testo antialias.
- Icona trasparente.
- Bordo traslucido.
- PNG WhiteSur.
- Sovrapposizione di due superfici semitrasparenti.

Definisci una routine canonica di blending o una separazione esplicita dei due formati, riutilizzando gli helper già presenti.

Elimina duplicazioni soltanto dopo aver verificato che la semantica dei chiamanti coincide.

---

## 9. Snapshot e lifetime delle superfici

Consolida `copyStableMetadata()`.

La sequenza dello snapshot deve garantire una copia coerente dei metadati, mentre il protocollo delle superfici deve garantire che i buffer referenziati restino validi durante l'uso.

### Verifiche obbligatorie

- Overflow di `count * sizeof(entry)`.
- Puntatori nulli.
- Dimensioni degli array.
- Dimensioni delle mappature.
- Frame sequence pari/dispari.
- Snapshot modificato durante la copia.
- Generazioni stale.
- Superficie distrutta durante una presentazione.
- Resize mentre il compositor legge i pixel.
- Fallimento delle allocazioni.
- Cleanup dopo terminazione del processo proprietario.
- Riavvio del compositor.

La sequenza dei metadati non deve essere considerata una garanzia sufficiente di lifetime dei buffer pixel.

Identifica il contratto già implementato per mantenere vive le risorse, e completalo soltanto dove manca una garanzia concreta.

Non introdurre puntatori persistenti verso memoria non validata.

---

## 10. Cursor overlay

Il cursore deve restare indipendente dal framebuffer pulito.

Preserva `presentCursorOverlay()` come percorso di overlay.

### Invarianti

- Spostare il cursore non modifica permanentemente `g_frame`.
- La vecchia posizione viene ripristinata dal contenuto pulito.
- La nuova posizione è composta sul contenuto corrente.
- Il clipping ai bordi dello schermo è corretto.
- Hotspot, pitch e alpha sono rispettati.
- Il cursore non invalida la cache Liquid Glass se non cambia la scena sottostante.

### Test

Muovi il cursore sopra:

- Wallpaper.
- Linee sottili.
- Bordi finestre.
- Child HWND.
- Card Liquid Glass.
- Angoli arrotondati.
- Ombre.

Poi spostalo fuori dall'area di confronto.

I pixel sottostanti devono essere identici a quelli iniziali, in assenza di altre modifiche alla scena.

---

# FASE P2 — Ottimizzazione della scena

## 11. Preparazione del draw list

Attualmente `composeRegion()` costruisce e ordina un array di superfici in funzione dello Z-order.

Questa operazione può essere ripetuta per ogni dirty rectangle.

Sposta la preparazione della draw list fuori dal ciclo di rasterizzazione delle regioni.

### Obiettivo

Per ogni frame coerente:

1. Identifica superfici attive e visibili.
2. Valida i relativi descrittori.
3. Risolvi gerarchia parent/child.
4. Calcola geometrie, clip e trasformazioni.
5. Ordina per Z-order una sola volta.
6. Riutilizza la draw list per tutti i dirty rectangles di quel frame.

Non modificare la semantica degli HWND.

Non ordinare nuovamente le superfici per ogni regione se lo snapshot non è cambiato.

Non introdurre un scene graph generale se una lista preparata è sufficiente.

### Criterio

Le operazioni di ordinamento per frame devono essere indipendenti dal numero di dirty rectangles.

Il risultato visivo deve restare identico.

---

## 12. Occlusion culling

Implementa un percorso conservativo per evitare di disegnare superfici completamente nascoste da altre superfici opache.

Utilizza esclusivamente informazioni affidabili.

### Regola fondamentale

Una regione può essere considerata opaca soltanto quando il suo contenuto finale è garantito completamente opaco.

Non considerare opache:

- Finestre con alpha.
- Superfici con opacità inferiore a 255.
- Angoli arrotondati trasparenti.
- Card Liquid Glass.
- Aree con effetti che dipendono dal backdrop.
- Superfici con pixel alpha non garantito.

### Approccio richiesto

Prepara le regioni coperte procedendo dal fronte verso il retro.

Sottrai soltanto le aree certamente opache.

Per il renderer, conserva la composizione back-to-front delle regioni che restano visibili.

Non introdurre occlusion culling aggressivo se non puoi dimostrarne la correttezza.

### Test

- Due finestre opache sovrapposte.
- Tre finestre sovrapposte.
- Finestra opaca davanti a una trasparente.
- Finestra arrotondata davanti a una opaca.
- Glass davanti a wallpaper.
- Finestre parzialmente fuori display.

Confronta ogni risultato con il renderer senza occlusion culling.

L'ottimizzazione deve poter essere disattivata tramite flag diagnostico per verificare l'equivalenza.

---

## 13. Ottimizzazione dei pixel

Dopo che damage tracking e occlusion culling sono corretti, esamina le routine più costose.

Mantieni il fast path per pixel opachi.

Aggiungi ottimizzazioni soltanto quando il benchmark dimostra che il percorso è significativo.

Candidati consentiti:

- Copia di run opachi mediante operazioni di memoria efficienti.
- Riduzione delle moltiplicazioni ripetute nei cicli sui pixel.
- Calcolo anticipato di pitch e row pointers.
- Evitare ricostruzioni ripetute di geometrie invarianti.
- Limitare il blending ai pixel con alpha effettivo.
- Ridurre copie di framebuffer non necessarie.
- Evitare allocazioni durante la composizione.

Non implementare subito AVX2 o SIMD specializzato.

Prima verifica che il costo principale non sia causato dal numero eccessivo di pixel elaborati.

### Attenzione agli helper di memoria

Il file contiene implementazioni locali di `memcpy` e `memset` che accedono ai byte tramite puntatori `volatile`.

Valuta se tali implementazioni siano realmente necessarie nel percorso freestanding.

`volatile` non è un meccanismo generale di sincronizzazione della memoria e può impedire ottimizzazioni del compilatore.

Non rimuoverlo indiscriminatamente: verifica il contratto del runtime e le reali necessità di barriera o I/O.

Se il percorso ordinario può usare operazioni di memoria più efficienti, sostituiscilo con una soluzione freestanding corretta e verificata.

---

# FASE P3 — Effetti grafici

## 14. Rounded corners e shadow

Le funzioni interessate comprendono:

- `blendWindowShadow()`
- `saveWindowCornerBackdrop()`
- `applyWindowCornerMask()`

### Requisiti

L'estensione dell'effetto deve derivare dalla sua geometria effettiva.

Il damage tracking deve includere i pixel realmente influenzati.

Non deve esistere una differenza visiva tra full e partial repaint.

L'effetto non deve essere applicato due volte sugli stessi pixel durante un aggiornamento parziale.

La ricostruzione degli angoli deve leggere il backdrop corretto, non un buffer temporaneo appartenente a un altro passaggio di rendering.

Non aggiungere nuovi effetti o modificare arbitrariamente l'aspetto WhiteSur.

---

## 15. Backdrop blur

Il codice attuale può richiedere un full redraw quando è attivo il backdrop blur.

Questa scelta preserva la correttezza in alcuni casi, ma può essere molto costosa.

### Intervento

Determina esattamente quali pixel del backdrop contribuiscono al blur di una finestra.

Definisci la regione di dipendenza includendo il raggio del filtro.

Quando cambia una piccola parte dello sfondo:

1. Determina se interseca la regione di dipendenza.
2. Invalida soltanto l'output influenzato.
3. Aggiorna soltanto i dati del backdrop necessari.
4. Ricomponi le regioni interessate.

Mantieni il full redraw come fallback in presenza di casi complessi non ancora rappresentabili correttamente.

Non accettare regressioni grafiche per ridurre il numero di redraw.

---

## 16. Liquid Glass

Il renderer Liquid Glass è già presente e deve essere preservato.

Non creare un secondo shader engine e non modificare lo stile visivo del tema.

Il lavoro riguarda esclusivamente:

- Correttezza del backdrop.
- Invalidazione.
- Cache.
- Lifetime.
- Costo CPU.
- Integrazione con il damage tracking.

### Problema specifico da verificare

`glassBackdropGeneration()` calcola un hash dei pixel leggendo `g_frame`.

Ma, in un repaint parziale, il framebuffer può contenere una combinazione di pixel già ricomposti e pixel appartenenti alla presentazione precedente.

Verifica che l'hash rappresenti sempre la scena sottostante corretta e coerente.

Non usare pixel appartenenti a una card Glass già composta come nuovo backdrop della stessa card.

Non usare pixel dell'HUD o del cursore come parte del backdrop, se non appartengono realmente alla scena prevista.

### Architettura desiderata

La cache deve essere validata mediante:

- Identità e generazione della superficie.
- Dimensioni della card.
- Posizione.
- Parametri dell'effetto.
- Generazione o invalidazione effettiva del backdrop.
- Stato dei layer sottostanti rilevanti.

Preferisci invalidazione spaziale rispetto all'hash ripetuto di grandi regioni.

Se l'hash resta utile come verifica, eseguilo soltanto quando un danno interseca l'area di cattura.

### Caching

Il compositor attuale usa una cache LRU con otto entry.

Valuta questo valore come budget di cache, non come limite massimo di widget o superfici Liquid Glass.

Quando una entry viene espulsa, il suo contenuto deve poter essere ricostruito correttamente.

Evita churn di allocazioni e rilascio ripetuto di grandi buffer quando più card sono visibili.

Imponi un budget di memoria fisica ragionevole e contabilizzato.

Non usare un incremento indiscriminato del numero di entry per correggere problemi di cache.

### Test Liquid Glass

- Card ferma con wallpaper fermo.
- Movimento del solo cursore.
- Aggiornamento HUD.
- Finestra che si muove dietro la card.
- Finestra che si muove davanti alla card.
- Cambio wallpaper.
- Cambio tema.
- Resize della card.
- Due card Glass sovrapposte.
- Cache eviction e successivo ripristino.

La card deve produrre lo stesso risultato visivo con e senza cache.

---

# FASE P4 — Scheduling e prestazioni

## 17. Event-driven frame scheduling

Preserva il wait `0x83`.

Non introdurre busy polling.

La pipeline deve distinguere:

- Nuovo metadata.
- Nuovo danno.
- Nuovo frame di animazione.
- Cambio cursore.
- Aggiornamento HUD.
- Presentazione da ritentare.

### Requisiti

In assenza di attività, il compositor deve restare bloccato.

I timeout necessari alle animazioni non devono generare lavoro quando non esistono animazioni attive.

I timeout dell'HUD non devono essere contabilizzati come errori.

Non inviare frame quando non sono cambiati pixel e non esiste una presentazione obbligatoria.

Preserva la corretta gestione degli eventi concorrenti e delle generazioni.

---

## 18. HUD e contatori

Attualmente il file definisce:

`MICANT_COMPOSITOR_DEBUG_HUD = 1`

e aggiorna l'HUD ogni 250 ms.

Rendi l'HUD disabilitato nelle build normali e abilitabile esplicitamente nelle build diagnostiche.

Non eliminare le statistiche utili.

### Distinzione obbligatoria

Non confondere:

- FPS di composizione.
- FPS di presentazione.
- Tempo di rendering del singolo frame.
- Tempo di composizione accumulato.
- Tempo di presentazione.
- Tempo di parete trascorso nel wait.
- Tempo CPU realmente attribuito al processo.

Nel codice attuale esiste una metrica FPS derivata da `1000 / renderMilliseconds`.

Questa misura rappresenta una stima della capacità di rendering di quel campione, non necessariamente il numero di frame realmente presentati al secondo.

Mantieni denominazioni esplicite e corrette.

Non dichiarare che il compositor usa il 99% CPU soltanto sulla base di tempi di parete o di attesa.

---

## 19. Profiling mirato

Non implementare un profiler generale.

Raccogli contatori aggregati a intervalli, senza logging per frame.

### Metriche obbligatorie

- Frame composti/s.
- Frame presentati/s.
- Full redraw/s.
- Partial redraw/s.
- Pixel composti/s.
- Pixel presentati/s.
- Tempo CPU attribuito al compositor.
- Tempo wall-clock di composizione.
- Tempo wall-clock di presentazione.
- Tempo wall-clock nel wait.
- Wakeup/s.
- Timeout/s.
- Dirty rectangle medi per frame.
- Numero di superfici visibili.
- Numero di superfici effettivamente disegnate.
- Pixel evitati dall'occlusion culling.
- Glass cache hit/miss.
- Glass render time.
- Memoria delle cache.
- Memoria del framebuffer.
- Peak resident memory del processo.
- Presentazioni fallite.

Misura, quando possibile, CPU guest e CPU host QEMU separatamente.

Non correggere le metriche manipolandone il valore mostrato.

---

## 20. Benchmark prima/dopo

Esegui gli stessi scenari e la stessa configurazione prima e dopo ogni ottimizzazione sostanziale.

### Matrice obbligatoria

| Scenario | Cosa misurare |
|---|---|
| Desktop idle | CPU, wakeup, frame/s |
| Movimento cursore | CPU, presentazioni, pixel modificati |
| Una finestra opaca | Costo composizione |
| Cinque finestre sovrapposte | Occlusion culling |
| Dieci finestre sovrapposte | Scalabilità Z-order |
| Drag | Frame time e pixel composti |
| Resize | Allocazioni, clipping, frame time |
| Shadow/rounded corners | Costo effetti |
| Liquid Glass idle | Cache hit e CPU |
| Liquid Glass con backdrop animato | Refresh, frame time e CPU |
| Cambio tema | Full redraw necessario |
| Animazione minimize/restore | Frame pacing |

Conserva le misure numeriche e indica l'ambiente.

Per ciascuna ottimizzazione riporta:

- Baseline.
- Nuovo valore.
- Differenza percentuale.
- Correttezza pixel verificata.
- Eventuali regressioni.

Non inventare benchmark o miglioramenti.

---

# FASE P5 — Robustezza e sicurezza

## 21. Gestione delle risorse

Verifica:

- Allocazione e rilascio di `g_frame`.
- Backdrop blur buffer.
- Cursor overlay.
- Animation arrays.
- Ordered surfaces.
- Metadata snapshot arrays.
- Liquid Glass caches.
- Buffer temporanei dei test interni.

Le allocazioni devono essere:

- Dimensionate in modo sicuro.
- Controllate per overflow.
- Gestite in caso di fallimento.
- Riutilizzate quando appropriato.
- Liberate nel lifecycle corretto.

Evita allocazioni per pixel e per frame.

Non rendere le cache permanenti senza un budget di memoria.

### Budget

Il profilo desktop deve restare entro 500 MiB di RAM fisica complessiva.

Questo budget comprende kernel, compositor, Explorer e applicazioni residenti del profilo verificato.

Non confondere memoria virtuale riservata con pagine fisiche residenti.

---

## 22. Fault containment

Una superficie malformata o un descrittore non valido non deve provocare un accesso arbitrario alla memoria.

Verifica tutti gli accessi basati su:

- Surface address.
- Surface size.
- Width/height.
- Pitch.
- Source origin.
- Pixel format.
- Array count.
- Generation.

Non dereferenziare puntatori soltanto perché il metadata snapshot è coerente.

Distingui la validazione aritmetica dei descrittori dalla garanzia che la memoria rimanga mappata e accessibile.

I fallimenti devono essere espliciti, diagnosticabili e non trasformarsi in successi fittizi.

---

# FASE P6 — Verifica integrata

## 23. QEMU

Verifica il compositor reale in QEMU, non soltanto un rasterizzatore hosted.

Usa il percorso del kernel bare-metal canonico.

Esegui almeno:

- 1024×768.
- 1280×1024.
- 1920×1080, se supportato dal backend e dalla configurazione disponibile.

Non introdurre nuove dipendenze da VirGL.

Il backend VirtIO 2D deve rimanere funzionante.

### Verifiche grafiche

- Desktop.
- Explorer.
- `theme_test.exe`.
- WhiteSur Light/Dark.
- Hover e pressed.
- Minimizzazione e ripristino.
- Resize.
- Drag.
- Finestra parzialmente fuori schermo.
- Più finestre sovrapposte.
- Menu popup.
- Cursore ai bordi.
- Trasparenze.
- Liquid Glass.

### Verifiche di lifecycle

- Creazione/distruzione ripetuta.
- Superfici ridimensionate.
- Terminazione anomala di un client.
- Cambio foreground.
- Restart del compositor, se supportato dal percorso canonico.
- Allocazione fallita.
- Cleanup e recupero della memoria.

Se un caso non è supportato, documenta il limite reale invece di simulare il successo.

---

## 24. Regression suite

Conserva test deterministici per:

- Blending.
- Clipping.
- Dirty rectangle union.
- Full/partial equivalence.
- Occlusion correctness.
- Rounded corners.
- Shadow.
- Cursor overlay.
- Animation geometry.
- Glass cache.
- Metadata snapshot validation.

Il test full/partial deve diventare una regressione stabile.

Una futura modifica al renderer non deve poter reintrodurre linee spostate o bordi alterati senza far fallire il test.

---

# 25. Ordine esecutivo obbligatorio

Segui questa sequenza senza saltare alla fase più interessante.

**C0 — Baseline**

Raccogli log, screenshot e metriche iniziali.

**C1 — Pixel correctness**

Risolvi full vs partial repaint, clipping, coordinate e alpha.

**C2 — Snapshot/lifetime**

Rafforza validazione, ownership e gestione delle risorse.

**C3 — Scene preparation**

Prepara draw list, Z-order e geometrie una sola volta per frame.

**C4 — Occlusion culling**

Elimina il rendering delle regioni sicuramente nascoste.

**C5 — Effects**

Consolida shadow, rounded corners, blur e Liquid Glass.

**C6 — Scheduling**

Elimina lavoro evitabile durante idle, aggiornamenti e presentazione.

**C7 — Performance**

Misura e ottimizza i percorsi realmente costosi.

**C8 — QEMU regressions**

Verifica tutte le funzionalità integrate e il budget di memoria.

Ogni blocco può produrre un commit locale coerente, ma il commit non conclude l'incarico.

Se un blocco richiede una dipendenza canonica specifica, completa la dipendenza minima e torna immediatamente all'obiettivo.

Non usare una grande riscrittura come sostituto degli interventi incrementali.

---

# 26. Criteri di accettazione finali

Il lavoro può essere dichiarato completato soltanto se tutte le seguenti condizioni risultano soddisfatte.

### Correttezza

- [ ] Full e partial composition sono pixel-equivalenti per scene identiche.
- [ ] Il cursore non altera i pixel sottostanti.
- [ ] Non ci sono bordi spezzati o spostamenti dopo hover e repaint.
- [ ] Alpha, clipping e stride sono coerenti.
- [ ] Child HWND e Z-order mantengono la geometria corretta.
- [ ] WhiteSur non presenta regressioni.
- [ ] Liquid Glass non introduce artefatti da cache stale.

### Architettura

- [ ] Un solo compositor canonico.
- [ ] Un solo modello di superficie.
- [ ] Draw list preparata senza lavoro duplicato per dirty rectangle.
- [ ] Occlusion culling conservativo funzionante.
- [ ] Buffer e snapshot validati e gestiti con lifetime corretto.
- [ ] Nessun nuovo contratto Win32 proprietario superfluo.
- [ ] Nessun percorso hosted usato come prova di produzione.

### Prestazioni

- [ ] L'idle non esegue busy polling.
- [ ] L'HUD è disattivato nella configurazione normale.
- [ ] Le metriche CPU e FPS sono semanticamente corrette.
- [ ] Gli aggiornamenti piccoli non provocano inutilmente full redraw.
- [ ] Il lavoro evitato dall'occlusion culling è misurato.
- [ ] Le cache Glass rispettano il budget di memoria.
- [ ] Le ottimizzazioni hanno benchmark prima/dopo.
- [ ] Il desktop verificato resta entro 500 MiB di RAM fisica.

### Integrazione

- [ ] Il kernel bare-metal si compila e si collega.
- [ ] Il compositor viene avviato realmente in Ring 3.
- [ ] VirtIO 2D funziona.
- [ ] Le principali risoluzioni supportate sono verificate.
- [ ] Explorer e le applicazioni Win32 continuano a funzionare.
- [ ] Screenshot QMP e serial log dimostrano le regressioni superate.
- [ ] I commit locali contengono soltanto modifiche attribuibili e verificate.

---

# 27. Formato del rapporto finale

Non consegnare soltanto un elenco di commit.

Il rapporto finale deve contenere:

**A. Problemi trovati**

Funzione, comportamento errato, causa e riproduzione.

**B. Modifiche apportate**

File e funzioni interessate, con spiegazione del contratto preservato o modificato.

**C. Test di correttezza**

Risultati full/partial, clipping, alpha, cursore, Glass, shadow e overlap.

**D. Benchmark**

Tabella numerica prima/dopo, con configurazione QEMU identica.

**E. Memoria**

Allocazioni, cache, footprint del compositor e resident memory del desktop.

**F. Evidenze QEMU**

Percorsi effettivi di screenshot e log generati, senza inventare file o marker.

**G. Limiti residui**

Solo problemi realmente non risolti, con funzione e causa specifica.

## Regola conclusiva

**Non ottimizzare il numero di operazioni a scapito della correttezza visiva.**

Il risultato richiesto è un compositor software robusto e prevedibile, capace di ricomporre soltanto i pixel necessari e di fornire lo stesso risultato indipendentemente dal modo in cui il frame viene suddiviso in regioni.

Non terminare il lavoro al primo test riuscito, al primo commit o dopo un resoconto di avanzamento. Continua attraverso i criteri ancora insoddisfatti, nei limiti effettivi dell'ambiente, senza ampliare lo scope.