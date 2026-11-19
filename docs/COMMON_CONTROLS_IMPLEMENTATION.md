# STEERING — MicaNT Native GTK Theme Engine con WhiteSur

## MISSIONE PRINCIPALE

Implementa integralmente in MicaNT un sistema di temi visivi moderno, compatibile con le API Windows, utilizzando **GTK 3 CSS come primo formato esterno supportato** e **WhiteSur GTK Theme come primo tema realmente integrato**.

Repository MicaNT:
https://github.com/luca-saggese/MicaNT

Tema upstream:
https://github.com/vinceliuice/WhiteSur-gtk-theme

L'obiettivo non è portare GTK o GNOME dentro MicaNT, ma creare un **Theme Engine C++23 nativo e indipendente**, capace di interpretare un sottoinsieme rigorosamente definito dei temi GTK e renderizzare gli elementi grafici delle applicazioni Win32 attraverso i sottosistemi canonici di MicaNT.

Il risultato finale deve includere:

- Finestre con decorazioni WhiteSur realmente renderizzate.
- Barre del titolo, bordi e pulsanti finestra tematizzati.
- Pulsanti, checkbox, radio, textbox, menu, tab, scrollbar e progress bar tematizzati.
- Varianti Light, Dark e Auto.
- Colori accento modificabili.
- Opacità e trasparenze.
- Blur behind reale.
- Ombre e angoli arrotondati.
- Cambio tema a runtime senza riavviare Explorer.
- Configurazione persistente per utente.
- Fallback completo al compositor e allo stile base preesistenti.
- Rendering software funzionante in QEMU.
- Integrazione futura con accelerazione hardware senza riscrivere il Theme Engine.
- Test end-to-end con Explorer reale in Ring 3.

**Non fermarti a un mockup, a una demo separata, a una compilazione riuscita o alla sola importazione degli asset.**

Devi collegare il nuovo sistema alla pipeline grafica effettiva di MicaNT.

---

# 1. REGOLE OPERATIVE

Lavora direttamente sul branch `main`.

Non creare branch o worktree aggiuntivi.

Sono autorizzati commit locali incrementali dopo blocchi funzionali coerenti e verificati.

Non effettuare push remoto senza autorizzazione separata.

All'inizio:

1. Leggi `AGENTS.md`.
2. Verifica `git status`.
3. Registra HEAD e modifiche non committate.
4. Identifica i file modificati da altri agenti.
5. Preserva il lavoro preesistente.
6. Identifica il percorso grafico canonico effettivamente compilato per QEMU.
7. Riutilizza le build directory persistenti dove appropriato.

Non utilizzare `git reset --hard`, `git clean` o altre operazioni distruttive.

Non ripristinare il vecchio kernel monolitico e non compilare codice archiviato sotto `old/`.

Non creare nuovi manager di finestre, scheduler, Memory Manager o driver grafici paralleli.

Mantieni la separazione Ring 0/Ring 3 richiesta da MicaNT.

Le DLL user-mode non devono includere header privati `micant/km/`.

## Continuità del lavoro

Il completamento di un singolo blocco non conclude l'incarico.

Dopo ogni commit prosegui con il requisito successivo.

Non fermarti alla prima finestra tematizzata.

Se una dipendenza indispensabile manca, implementa il percorso minimo necessario nel componente proprietario corretto.

Non creare successi simulati per superare i test.

Se l'ambiente impone un'interruzione reale, registra il punto di ripresa esatto e tutti i criteri ancora aperti.

---

# 2. PRIMA ATTIVITÀ OBBLIGATORIA: SPECIFICA DETTAGLIATA

**Prima di modificare il codice applicativo, crea:**

`docs/USERMODE_GTK_VISUAL_STYLES_THEME_ENGINE_SPEC.md`

Il documento deve essere una specifica implementativa effettiva, non una descrizione generale.

Analizza prima il repository e lo stato dei sottosistemi già disponibili.

Leggi:

- `docs/KERNEL_ARCHITECTURE.md`
- `docs/KERNEL_MILESTONES.md`
- `docs/KERNEL_INTEGRATION_STATUS.md`
- `docs/KERNEL_GPU_DRIVER_IMPLEMENTATION_SPEC.md`
- Le specifiche del compositor, di Explorer, del font engine, dei decoder immagini, delle icone e dei cursori, se presenti.
- `include/micant/um/subsystems/winuser/uxtheme.hpp`
- `include/micant/um/subsystems/winuser/dwmapi.hpp`
- `include/micant/um/subsystems/winuser/user32.hpp`
- `include/micant/um/subsystems/winuser/gdi32.hpp`
- `include/micant/um/subsystems/winuser/comctl32.hpp`
- `tools/compositor_app.cpp`
- Le implementazioni DLL freestanding effettivamente utilizzate.
- Il window manager Win32k canonico.
- Il rendering di Explorer e dei suoi controlli.

Per ogni componente indica se è dichiarato, implementato, compilato, collegato, esercitato in QEMU o verificato su hardware.

## Contenuto obbligatorio della specifica

La specifica deve documentare:

1. Obiettivi e non-obiettivi.
2. Stato corrente del repository.
3. Gap analysis.
4. Architettura Ring 0/Ring 3.
5. Ownership canonica.
6. Modello interno dei temi.
7. Formato GTK 3 CSS supportato.
8. Importazione WhiteSur.
9. Asset e licenze.
10. Compilazione degli stili.
11. Parser e selettori.
12. Risoluzione delle proprietà.
13. Stati dei controlli.
14. Palette e colori.
15. Varianti Light/Dark/Auto.
16. Accenti e contrasto.
17. Font e metriche.
18. Pulsanti e controlli comuni.
19. Decorazioni nonclient.
20. Titlebar e pulsanti finestra.
21. Trasparenza.
22. Blur behind.
23. Ombre.
24. Rounded corners.
25. Animazioni.
26. DPI e scaling.
27. API UxTheme.
28. API DWM.
29. API User32.
30. Integrazione Win32k.
31. Integrazione GDI/Direct2D.
32. Compositor.
33. Cache.
34. Persistenza.
35. Multiutente.
36. Sicurezza dei temi.
37. Fallback.
38. Prestazioni.
39. Test unitari.
40. Test QEMU.
41. Fault injection.
42. Criteri di accettazione.
43. Dipendenze fra fasi.
44. Roadmap dettagliata.

Per ogni sottopasso inserisci file interessati, API, invarianti, dipendenze, error handling e test.

**Dopo aver creato il documento devi iniziare immediatamente l'implementazione.**

---

# 3. DOWNLOAD IMMEDIATO DI WHITESUR

Scarica subito il repository ufficiale:

```bash
git clone https://github.com/vinceliuice/WhiteSur-gtk-theme.git
```

Usa un percorso temporaneo esterno alla build di produzione.

Non aggiungere automaticamente il clone completo a MicaNT.

Registra lo SHA esatto del commit upstream.

Prima di copiare asset, controlla:

- `README.md`
- `COPYING`
- Script di installazione.
- Directory `src/`.
- Directory `release/`.
- Directory `other/`.
- Struttura GTK 3.
- Asset SVG e PNG.
- Template Sass/SCSS.
- File CSS generati.
- Informazioni di copyright.
- Licenze di eventuali asset di terze parti.

Il repository dichiara MIT, ma verifica i singoli componenti e preserva le relative attribuzioni.

## WhiteSur come tema primario

Importa inizialmente un insieme ristretto ma completo di varianti:

- WhiteSur Light.
- WhiteSur Dark.
- WhiteSur con accento predefinito.
- WhiteSur con almeno un accento alternativo.
- WhiteSur Solid.
- WhiteSur con opacità normale.

Per il primo risultato usa la configurazione upstream più vicina al default.

Non cercare di importare tutte le combinazioni di WhiteSur prima di rendere funzionante la prima.

WhiteSur contiene sorgenti e asset che possono richiedere una fase di generazione.

Se necessario, utilizza gli strumenti upstream **esclusivamente sul sistema di sviluppo** per produrre CSS e risorse finite.

Non aggiungere Sass, GTK, GLib, Qt o un runtime Linux come dipendenze del kernel o dell'ambiente Ring 3 di produzione.

Non eseguire gli script upstream come passaggio non verificato della build: ispeziona prima le operazioni effettuate, soprattutto installazioni, modifiche a directory utente e dipendenze esterne.

Prepara un packaging MicaNT contenente soltanto gli asset necessari e le licenze.

Struttura indicativa:

```text
System/
└── Themes/
    ├── Base/
    └── WhiteSur/
        ├── Light/
        ├── Dark/
        ├── assets/
        ├── theme.manifest
        └── LICENSES/
```

I nomi sono indicativi; adattali all'architettura dei percorsi di sistema esistente.

La selezione del tema non deve essere hardcoded in Explorer.

---

# 4. PARALLELIZZAZIONE OBBLIGATORIA

Dividi l'implementazione in blocchi indipendenti.

Avvia worker paralleli quando l'ambiente lo consente.

### Blocco A — Parser GTK CSS e Theme Compiler

Responsabilità:

- Lexer/parser.
- CSS subset.
- Variabili colore.
- Selettori.
- Specificità.
- Risoluzione proprietà.
- Compilazione dello stile interno.
- Validazione.
- Cache.

Non modificare compositor o Win32k.

### Blocco B — WhiteSur e asset

Responsabilità:

- Download.
- Provenienza.
- Licenze.
- Generazione asset.
- Packaging.
- Varianti.
- Test dei temi reali.

Non modificare le API Windows.

### Blocco C — UxTheme e controlli Win32

Responsabilità:

- Theme handles.
- API UxTheme.
- Rendering dei controlli.
- Integrazione User32/GDI/Comctl32.
- Notifiche.

Non modificare il parser contemporaneamente al Blocco A.

### Blocco D — Decorazioni e compositor

Responsabilità:

- Titlebar.
- Bordi.
- Window buttons.
- Rounded corners.
- Alpha.
- Blur.
- Shadows.
- Dirty rectangles.
- Presentazione.

Non modificare gli stessi file del Blocco C senza coordinamento.

### Blocco E — Preferences e persistenza

Responsabilità:

- Light/Dark/Auto.
- Accento.
- Impostazioni utente.
- Tema selezionato.
- Cambio runtime.
- Fallback.
- Registry/Userenv.

### Blocco F — Integrazione e test

Responsabilità:

- Build.
- Test Windows API.
- Test immagini.
- QEMU.
- Explorer.
- Verifica del fallback.
- Regressioni.

## Regole di coordinamento

Tutti i blocchi operano sullo stesso branch `main`.

Non creare branch temporanei.

Assegna ownership chiara dei file.

I worker possono lavorare in parallelo su file distinti.

Le modifiche a file condivisi devono essere serializzate.

**Un solo coordinatore può effettuare commit alla volta.**

Prima di ogni commit verifica HEAD, `git status`, diff staged e ownership dei file.

Non committare modifiche concorrenti di altri agenti senza averle integrate.

Non effettuare commit che lascino intenzionalmente il percorso canonico non compilabile.

---

# 5. NATIVE THEME MODEL

Crea un formato interno di tema indipendente da GTK.

Il modello deve esprimere:

- Color tokens.
- Font families.
- Font sizes.
- Text colors.
- Background colors.
- Border colors.
- Border thickness.
- Corner radius.
- Padding.
- Margins.
- Minimum sizes.
- Gradients.
- Shadow parameters.
- Opacity.
- Surface materials.
- Control states.
- Animation durations.
- Nonclient metrics.
- DPI scaling.

Il runtime deve interrogare questo modello, non riparsare GTK CSS a ogni repaint.

Il formato compilato deve avere ownership e lifetime espliciti.

Non introdurre un secondo renderer grafico.

Il modello deve essere utilizzabile dalle API Windows esistenti.

---

# 6. GTK 3 CSS PARSER

Implementa un parser sicuro e deterministico.

Il primo sottoinsieme deve includere le funzionalità necessarie a rendere WhiteSur realmente utilizzabile.

Supporta progressivamente:

- Rule sets.
- CSS node selectors.
- Class selectors.
- State pseudo-classes.
- Descendant e child combinators richiesti.
- Specificity.
- Cascade.
- `@define-color`.
- Variabili colore GTK.
- `@import` con restrizioni.
- `rgb()`.
- `rgba()`.
- `alpha()`.
- `mix()`.
- `shade()`.
- `lighter()`.
- `darker()`.
- `background-color`.
- `color`.
- `border`.
- `border-radius`.
- `padding`.
- `margin`.
- `min-width`.
- `min-height`.
- `box-shadow`.
- `background-image`.
- Gradienti usati dagli asset selezionati.
- URL relativi verso asset autorizzati.
- Proprietà di font necessarie.

Non chiamare il parser compatibile con tutto GTK 3 finché i relativi test non lo dimostrano.

Per ogni costrutto utilizzato dal WhiteSur importato, implementa il comportamento necessario oppure registra un fallback specifico e verificabile.

## Sicurezza del parser

Imponi limiti a:

- Dimensione dei CSS.
- Profondità degli import.
- Numero di selettori.
- Numero di regole.
- Complessità delle espressioni.
- Numero di asset.
- Dimensioni delle immagini.
- Memoria allocata.
- Tempo di compilazione.

Impedisci traversal dei percorsi.

Non autorizzare URL remoti o accessi arbitrari al filesystem.

Rifiuta gli input invalidi senza corrompere il tema attivo.

---

# 7. THEME COMPILER E CACHE

Implementa una fase di compilazione:

`GTK CSS + Asset → Validated Native Theme → Runtime Style Tables`

La compilazione deve produrre dati utilizzabili direttamente dai renderer.

Prevedi:

- Tabelle di stile per controllo.
- Tabelle di colore.
- Varianti degli stati.
- Asset prerasterizzati.
- Cache per DPI.
- Cache per versione del tema.
- Invalidazione controllata.
- Reference counting o ownership equivalente.
- Sostituzione atomica del tema attivo.

Il cambio tema non deve invalidare immediatamente memoria ancora utilizzata da finestre o frame in composizione.

Non bloccare il compositor durante operazioni costose di parsing.

Il caricamento del nuovo tema deve avvenire prima della sostituzione di quello attivo.

---

# 8. UXTHEME.DLL REALE

Completa le API Windows del Theme Engine.

Implementa con semantica reale:

- `OpenThemeData`.
- `OpenThemeDataEx`, quando necessario.
- `CloseThemeData`.
- `GetThemeColor`.
- `GetThemeFont`.
- `GetThemePartSize`.
- `GetThemeMargins`.
- `GetThemeBackgroundContentRect`.
- `DrawThemeBackground`.
- `DrawThemeText`.
- `DrawThemeTextEx`.
- `IsThemeActive`.
- `IsAppThemed`.
- `SetWindowTheme`.
- `GetCurrentThemeName`.
- `GetThemeTransitionDuration`.
- `GetThemeSysColor`, per il contratto supportato.

Gli handle `HTHEME` devono possedere una lifetime reale.

Rimuovi gli handle statici fittizi e i ritorni di successo senza operazione.

Le funzioni devono rispettare validazione, codici HRESULT, parametri e stati.

Non creare una nuova DLL parallela se quella canonica esiste già.

---

# 9. CONTROLLI WINDOWS TEMATIZZATI

Collega il Theme Engine a User32, GDI32, UxTheme e Comctl32.

Implementa lo styling effettivo di:

- Push button.
- Default button.
- Toggle button, se supportato dal controllo.
- Checkbox.
- Radio button.
- Textbox.
- Password box.
- Combo box.
- List box.
- Menu.
- Context menu.
- Tab.
- Progress bar.
- Scrollbar.
- Slider/trackbar.
- Toolbar.
- Status bar.
- Tooltip.

Ogni controllo deve gestire:

- Normal.
- Hover.
- Pressed.
- Focused.
- Disabled.
- Checked.
- Selected.
- Inactive, dove applicabile.

Focus e disabled devono rimanere distinguibili anche quando il tema utilizza colori simili.

Non alterare la semantica o l'accessibilità dei controlli per seguire il tema.

Il rendering deve utilizzare superfici, font e primitive grafiche canoniche.

---

# 10. DECORAZIONI DELLE FINESTRE

Implementa il tema per la parte nonclient delle finestre.

Prevedi:

- Barra del titolo.
- Bordi.
- Caption buttons.
- Minimize.
- Maximize.
- Restore.
- Close.
- Active/inactive state.
- Focus.
- Shadow.
- Corner radius.
- Titolo finestra.
- Icona finestra.
- DPI scaling.

Le decorazioni devono essere coerenti con l'input e l'hit testing Win32k.

Il pulsante Close deve chiudere realmente la finestra secondo le normali regole Win32.

Il ridimensionamento deve continuare a funzionare con bordi trasparenti o arrotondati.

La decorazione non deve sovrapporsi indebitamente alla superficie client.

Mantieni separati:

- Layout e hit testing controllati dal sistema finestre.
- Descrizione estetica prodotta dal Theme Engine.
- Rendering e presentazione del compositor.

---

# 11. DWM, BLUR E TRASPARENZE

Integra gli effetti nel compositor Ring 3 esistente.

Non creare un secondo compositor.

Implementa:

- Alpha blending corretto.
- Superfici traslucide.
- Clip.
- Rounded-corner masks.
- Window shadows.
- Blur behind.
- Backdrop tint.
- Optional noise.
- Cache delle regioni sfocate.
- Dirty rectangles.
- Invalidazione delle superfici.
- Recovery dopo errori di rendering.

La pipeline deve rispettare l'ordine corretto:

1. Composizione delle superfici sottostanti.
2. Campionamento della regione da sfocare.
3. Applicazione del blur.
4. Tint e opacità.
5. Composizione della finestra.
6. Decorazioni e ombre secondo l'ordine di stacking.
7. Presentazione finale.

Evita il blur dell'intero schermo quando cambia soltanto una piccola regione.

Non ricampionare inutilmente lo sfondo su ogni frame.

Prevedi impostazioni di qualità e riduzione del costo sotto carico.

## API DWM

Collega le operazioni effettivamente implementate di:

- `DwmSetWindowAttribute`.
- `DwmGetWindowAttribute`.
- `DwmExtendFrameIntoClientArea`.
- `DwmEnableBlurBehindWindow`, se supportato.
- Attributi per dark mode.
- Colori dei bordi e delle caption.
- Preferenze per angoli arrotondati.
- Backdrop type.

Non dichiarare che MicaNT supporta integralmente Acrylic o Mica Windows soltanto perché produce un effetto simile.

Mantieni distinta la compatibilità API dall'effetto visivo nativo.

---

# 12. FALLBACK OBBLIGATORIO AL COMPOSITOR BASE

**Questo requisito è inderogabile.**

Il Theme Engine è un'estensione opzionale della pipeline grafica canonica.

Il compositor di base già implementato deve continuare a funzionare anche quando:

- WhiteSur non è presente.
- Un CSS è malformato.
- Un asset non si trova.
- Il parser fallisce.
- La compilazione del tema fallisce.
- Il tema richiede proprietà non supportate.
- Il Theme Manager si arresta.
- La memoria non è sufficiente.
- Il dispositivo grafico viene resettato.
- Il blur fallisce.
- Il caricamento del nuovo tema viene annullato.

Il fallback deve preservare:

- Desktop visibile.
- Finestre.
- Input.
- Focus.
- Hit testing.
- Titoli.
- Pulsanti fondamentali.
- Presentazione.
- Cleanup.

Realizza tre livelli distinti:

**Fallback A — Theme Base**

Tema nativo essenziale di MicaNT, sempre disponibile.

**Fallback B — Basic Software Composition**

Compositor software canonico senza effetti opzionali.

**Fallback C — Bootstrap Display**

Percorso minimo di visualizzazione già supportato quando il compositor completo non è disponibile, senza pretendere che tutte le funzioni Win32 siano operative.

Non confondere il fallback visivo con il ripristino automatico di un processo o servizio grafico che non dispone ancora dei meccanismi di recovery necessari.

Il Theme Manager non deve diventare una dipendenza obbligatoria per il boot del kernel.

### Test obbligatorio

Rimuovi deliberatamente WhiteSur dall'immagine QEMU.

Explorer deve continuare ad avviarsi e le finestre devono continuare a essere visualizzate mediante il tema base.

Ripeti con CSS invalido e asset mancanti.

Nessun errore del tema deve causare kernel panic.

---

# 13. LIGHT, DARK E AUTO

Implementa tre modalità:

**Light**

Attiva la palette chiara WhiteSur.

**Dark**

Attiva la palette scura WhiteSur.

**Auto**

Seleziona la modalità in base a una configurazione oraria.

La prima implementazione Auto deve consentire una fascia oraria configurabile, per esempio giorno/notte.

Il cambio deve reagire a:

- Ora di sistema.
- Modifica dell'orario.
- Modifica del fuso orario.
- Ripresa da sospensione, quando supportata.
- Cambio impostazioni.

Non usare un loop di polling continuo.

Utilizza timer e notifiche già disponibili nel sistema.

Supporta il cambio Light ↔ Dark senza chiudere Explorer.

Le finestre esistenti devono essere invalidate e ridisegnate coerentemente.

Preserva gli handle e le superfici attive durante il cambio.

---

# 14. COLORI ACCENTO

Implementa un sistema di accenti indipendente dal tema.

Supporta almeno:

- Predefinito WhiteSur.
- Blu.
- Viola.
- Verde.
- Rosso.
- Arancione.

Quando WhiteSur fornisce una variante reale per un accento, usa quella.

Quando non esiste una variante, utilizza soltanto trasformazioni compatibili con il modello interno di stile.

Non modificare arbitrariamente asset che non possono essere ricolorati correttamente.

Preserva il contrasto minimo per testi e stati interattivi.

Gli accenti devono applicarsi coerentemente a:

- Pulsanti principali.
- Selezioni.
- Focus.
- Slider.
- Progress bar.
- Elementi attivi.
- Controlli compatibili.

---

# 15. CONFIGURAZIONE PER UTENTE

Integra le impostazioni del tema con Cm e Userenv.

Prevedi:

- Theme ID.
- Light/Dark/Auto.
- Accent color.
- Transparency enabled.
- Blur enabled.
- Blur quality.
- Animation enabled.
- Reduced motion.
- Contrast mode.
- DPI override, quando applicabile.

La configurazione deve essere persistente.

Non utilizzare un unico file globale scrivibile da tutti gli utenti.

Le impostazioni di un account non devono sovrascrivere quelle di un altro.

Se Userenv o il profilo utente non sono ancora completamente disponibili, utilizza un profilo base di sistema esplicitamente identificato e completa successivamente il percorso per-utente.

Non rendere obbligatorio il login completo per avviare Explorer in modalità diagnostica.

---

# 16. INTERFACCIA IMPOSTAZIONI

Aggiungi una pagina o un'applicazione nativa minima per configurare l'aspetto.

Deve consentire:

- Scelta tema.
- Anteprima.
- Light/Dark/Auto.
- Accento.
- Trasparenze.
- Blur.
- Animazioni.
- Ripristino default.

Il cambio deve applicarsi a runtime.

Il pulsante di ripristino deve riportare al tema base senza richiedere il riavvio.

Evita di introdurre un framework UI nuovo soltanto per questa schermata.

Riutilizza i controlli Win32 esistenti.

---

# 17. FONT, ICONE E CURSORI

Il Theme Engine deve integrarsi con gli altri componenti del desktop.

### Font

Utilizza il Font Engine TrueType/OpenType esistente o in sviluppo.

Il tema può selezionare font, dimensione e peso, ma non deve includere un rasterizer duplicato.

### Icone

Riutilizza il sistema icone Windows di MicaNT.

WhiteSur GTK Theme non deve sostituire automaticamente il tema icone di sistema.

### Cursori

Riutilizza il Cursor Manager e il tema cursori già installato.

La selezione di WhiteSur non deve cambiare cursori senza esplicita configurazione.

### Wallpaper

Il wallpaper resta una preferenza separata.

Il Theme Manager può utilizzare i colori dello sfondo per alcuni effetti, ma non deve duplicare il decoder immagini.

---

# 18. PERFORMANCE

L'obiettivo è un desktop moderno senza overhead eccessivo.

Principi:

- Nessun parsing CSS durante il repaint.
- Nessun I/O tema nel percorso critico del compositor.
- Asset cached.
- Dirty rectangles.
- Evitare copie inutili.
- Nessuna riallocazione completa a ogni frame.
- Blur limitato alle regioni necessarie.
- Degradazione qualitativa degli effetti opzionali sotto carico.
- Nessun blocco del thread UI durante compilazione temi.
- Nessun blocco di Win32k durante caricamento asset.

Strumenta:

- Tempo caricamento tema.
- Tempo compilazione CSS.
- Memoria della cache.
- Durata del repaint.
- Durata della composizione.
- Costo del blur.
- Frame persi.
- Tempo cambio tema.
- Fallimenti e fallback.

Non stabilire come completate prestazioni che non sono state misurate.

---

# 19. TEST AUTOMATICI

Crea test mirati per:

- CSS valido.
- CSS malformato.
- Import ricorsivi.
- Selettori.
- Specificità.
- Stati hover/pressed/disabled.
- Variabili colore.
- Espressioni colore.
- Alpha.
- Gradients.
- Border radius.
- Shadows.
- Asset mancanti.
- Asset troncati.
- Theme manifest invalido.
- Cache.
- Invalidazione.
- Cambio tema.
- Theme handle lifecycle.
- Multiutente.
- DPI.
- Memory pressure.
- Fallback.
- Cleanup.

Aggiungi test reali per le API UxTheme e DWM attraverso le DLL effettivamente compilate.

Non considerare sufficienti i soli test delle classi interne.

---

# 20. QEMU — VERIFICA END-TO-END CON EXPLORER

Esegui la compilazione reale del kernel AMD64, delle DLL user-mode, del Theme Engine, del compositor e di Explorer.

Utilizza il percorso QEMU canonico.

Il test deve raggiungere la vera applicazione Explorer in Ring 3.

Prepara un corpus di verifiche:

**TH-Q0 — Basic compositor**

Avvia Explorer senza WhiteSur e verifica il percorso esistente.

**TH-Q1 — WhiteSur Light**

Carica WhiteSur Light e verifica barre del titolo, bordi e pulsanti.

**TH-Q2 — WhiteSur Dark**

Cambia a WhiteSur Dark senza riavviare Explorer.

**TH-Q3 — Controls**

Visualizza pulsanti, checkbox, radio, textbox, menu, scrollbar e tab con stati differenti.

**TH-Q4 — Window states**

Verifica active/inactive, maximize, minimize, restore e close.

**TH-Q5 — Transparency**

Verifica trasparenze reali sopra wallpaper e altre finestre.

**TH-Q6 — Blur**

Verifica che i pixel sottostanti vengano realmente sfocati e non semplicemente scuriti.

**TH-Q7 — Accent**

Cambia il colore accento e verifica i controlli interessati.

**TH-Q8 — Automatic**

Simula il passaggio giorno/notte tramite il meccanismo temporale supportato.

**TH-Q9 — Fault recovery**

Corrompi un file CSS e verifica fallback senza crash.

**TH-Q10 — Missing asset**

Rimuovi un asset e verifica degradazione corretta.

**TH-Q11 — Theme reload**

Sostituisci il tema attivo a runtime senza use-after-free o superfici invalide.

**TH-Q12 — Explorer interaction**

Muovi, ridimensiona e sovrapponi due finestre.

**TH-Q13 — No regression**

Verifica input, cursori, icone, font, immagini, Win32k e desktop.

**TH-Q14 — Process termination**

Termina un'applicazione con controlli tematizzati e verifica cleanup.

**TH-Q15 — Persistence**

Riavvia QEMU e verifica il recupero delle preferenze dalla storage persistente.

**TH-Q16 — Hardware acceleration**

Se disponibile, verifica l'effettivo utilizzo di una pipeline GPU accelerata.

Non confondere la presentazione VirtIO-GPU 2D con composizione GPU accelerata.

## Evidenza richiesta

Per ogni test registra:

- Comando eseguito.
- SHA del codice.
- Risultato.
- Log QEMU.
- Screenshot o QMP pixel evidence.
- Backend utilizzato.
- Eventuali limitazioni.

Non dichiarare superati test non eseguiti.

Se Explorer non è ancora avviabile per una dipendenza esterna al Theme Engine, completa tutti i test indipendenti possibili, identifica il blocco preciso e continua sulla dipendenza minima di integrazione quando rientra nella ownership disponibile.

---

# 21. COMMIT INCREMENTALI

Effettua commit dopo blocchi coerenti.

Sequenza indicativa:

1. `docs(theme): specify native GTK visual styles engine`
2. `feat(theme): import WhiteSur assets and license metadata`
3. `feat(theme): implement GTK CSS parser`
4. `feat(theme): add native compiled style model`
5. `feat(uxtheme): implement real themed control contracts`
6. `feat(win32k): connect nonclient themed rendering`
7. `feat(compositor): implement alpha masks shadows and blur`
8. `feat(theme): add light dark auto and accent settings`
9. `feat(theme): add user preferences and live reload`
10. `test(theme): verify WhiteSur Explorer QEMU integration`

Questi commit sono esempi, non un limite al numero necessario.

Prima di ogni commit:

- Verifica `git status`.
- Seleziona solo file pertinenti.
- Controlla il diff staged.
- Esegui i test del blocco integrato.
- Registra correttamente le attribuzioni di terze parti.
- Evita di includere file di build.
- Non committare il lavoro non integrato di altri agenti.

Dopo ogni commit continua con il requisito successivo.

Aggiorna `docs/KERNEL_INTEGRATION_STATUS.md` soltanto ai checkpoint significativi verificati.

---

# 22. CRITERI FINALI DI COMPLETAMENTO

Considera completato il lavoro quando:

1. Il documento di specifica è presente e aggiornato.
2. WhiteSur è scaricato e importato con licenze e provenienza.
3. Le varianti Light e Dark sono utilizzabili.
4. Il parser GTK CSS compila i costrutti effettivamente necessari ai temi importati.
5. Il Native Theme Model è indipendente da GTK.
6. UxTheme dispone di implementazioni reali.
7. I controlli Win32 sono tematizzati.
8. Le decorazioni delle finestre utilizzano WhiteSur.
9. Light/Dark/Auto funziona.
10. Gli accenti sono modificabili.
11. Trasparenze, blur, ombre e angoli arrotondati funzionano sul compositor.
12. Il cambio tema a runtime non richiede riavvio.
13. Le preferenze sono persistenti quando il profilo utente è disponibile.
14. Explorer utilizza realmente il Theme Engine.
15. Il compositor base funziona senza WhiteSur.
16. Errori di CSS, asset e caricamento non compromettono il desktop.
17. I test QEMU integrati sono eseguiti.
18. Non sono presenti regressioni note introdotte sui percorsi grafici principali.
19. I commit sono coerenti e verificabili.
20. La documentazione distingue implementato, collegato, verificato QEMU e accelerato hardware.

Non dichiarare il progetto completato sulla base della sola compilazione.

Non dichiarare compatibilità generale con ogni tema GTK 3 se è stato validato soltanto WhiteSur.

Non dichiarare accelerazione GPU se il blending è eseguito dalla CPU.

Non introdurre un secondo compositor.

Non sostituire il desktop attuale con una demo statica.

---

# ISTRUZIONE FINALE

**Comincia subito scaricando WhiteSur e analizzando il repository MicaNT.**

Prima scrivi la specifica completa in:

`docs/USERMODE_GTK_VISUAL_STYLES_THEME_ENGINE_SPEC.md`

Poi avvia lo sviluppo parallelo per blocchi indipendenti.

Lavora su `main`.

Integra ogni blocco nel percorso canonico.

Effettua commit incrementali coerenti.

Esegui verifiche in QEMU con Explorer.

**Il primo obiettivo visibile è una finestra Explorer realmente tematizzata WhiteSur Light/Dark, con controlli Win32 funzionanti.**

Il secondo è il cambio tema a runtime.

Il terzo è la composizione con trasparenze, ombre e blur.

**Il requisito di continuità essenziale è che, in qualsiasi momento, MicaNT possa tornare al tema e al compositor base già funzionanti, senza perdere l'interfaccia grafica.**

Non fermarti dopo un singolo blocco completato. Prosegui sui requisiti ancora aperti finché l'ambiente di esecuzione lo consente, riportando con precisione eventuali limiti reali.