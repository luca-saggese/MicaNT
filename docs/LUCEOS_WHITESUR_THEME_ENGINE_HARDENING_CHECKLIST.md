# LuceOS / WhiteSur Theme Engine — Hardening Checklist

Tracker operativo per lo steering mirato del Theme Engine. Lo scope è limitato
ai percorsi T1, T2 e T3: caricamento e fallback del tema, decorazioni e caption
buttons, controlli Win32 nell'applicazione di test dedicata.

Una casella si spunta solo quando il percorso è collegato all'implementazione
reale e la verifica indicata è stata eseguita. Registrare comandi, commit,
log e screenshot nella sezione **Registro delle verifiche**; una compilazione
o un marker seriale, da soli, non provano il comportamento visivo.

## Stato iniziale

- Branch osservato: `main`
- HEAD osservato: `5c3e9533217cfcb3daa169de81cc5e598204a34d`
- Worktree iniziale: modifiche preesistenti presenti; conservarle e non
  attribuirle a questo tracker.
- Stato T1/T2/T3: da determinare seguendo le verifiche qui sotto.
- App di test: individuare quella grafica esistente; se inadeguata o assente,
  creare un eseguibile Ring 3 dedicato (es. `theme_test.exe`).
- Vincoli: niente probe o nuovi controlli in Explorer; niente renderer
  sostitutivi, parser generale, nuovo compositor o funzionalità fuori scope.

## T1 — Caricamento WhiteSur, cambio tema e fallback

### Percorso di caricamento

- [ ] Tracciare il percorso reale `active.profile → native.style →
  validazione → pubblicazione → UxTheme → Win32k → compositor`.
- [ ] Verificare che `active.profile`, gli stili Light/Dark e gli asset siano
  letti dal filesystem; identificare ogni default o dato compilato nel binario.
- [ ] Verificare ownership e passaggio dati in `tools/theme/compile_profile.cpp`,
  parser CSS, Native Theme Model, `MicaThemeSnapshot`, `uxtheme.dll`, API
  snapshot Win32k, metadata shared e compositor Ring 3.
- [ ] Confermare che la pubblicazione avvenga solo dopo validazione completa e
  che i consumer vedano una generazione/snapshot coerente.
- [ ] Verificare che gli handle `HTHEME` rispettino il lifetime previsto
  durante e dopo il cambio di generazione.

### Errori, rollback e fallback

- [ ] Coprire limiti di dimensione, magic/versione, conteggi, attributi,
  percorsi autorizzati e aritmetica dei buffer.
- [ ] Coprire file troncati, stile compilato malformato, CSS non supportato o
  invalido, tema/profilo mancante, asset mancante ed errori di lettura.
- [ ] Coprire fallimento di pubblicazione e notifiche tema non consegnate.
- [ ] Su ogni errore, mantenere integralmente il tema valido precedente; senza
  tema precedente, mantenere il tema Base.
- [ ] Verificare che non restino palette, metadata o generazioni Light/Dark
  miste e che un caricamento valido successivo recuperi correttamente.

### Verifica nell'app dedicata

- [ ] Mostrare WhiteSur Light e verificare pixel di una proprietà cromatica
  applicata a una superficie.
- [ ] Passare a Dark, verificare pixel distinti, poi tornare a Light.
- [ ] Ricaricare il profilo attivo senza ricreare la finestra.
- [ ] Provare profilo inesistente e file compilato malformato; verificare il
  rollback visivo e funzionale al tema valido precedente.
- [ ] Avviare senza WhiteSur e verificare il fallback Base e la finestra ancora
  utilizzabile.

**Criterio T1:** Light e Dark sono letti dal filesystem e applicati davvero;
cambi e ricaricamenti non producono stato incoerente; errori e assenza del tema
conservano il precedente valido o il fallback Base.

## T2 — Decorazioni, hit testing e caption buttons

### Geometria e rendering

- [ ] Tracciare window state/geometry, hit testing nonclient, headerbar style,
  caption height, border metrics, compositor decoration e dirty rectangles.
- [ ] Usare geometria coerente per disegno e hit testing: posizione visibile e
  area cliccabile di ogni pulsante devono coincidere.
- [ ] Verificare bordi, titlebar e testo per finestre attive/inattive in Light e
  Dark, inclusi spessore, altezza, clipping della client area e focus/Z-order.
- [ ] Cambiare tema con finestra aperta e verificare focus, resize,
  maximized/restored e ridisegno senza decorazioni residue.
- [ ] Verificare hover, pressed, active e inactive per i tre caption buttons.
- [ ] Verificare il caricamento degli asset WhiteSur `windows-assets/titlebutton-*`;
  usare glyph nativi come fallback quando l'asset non è disponibile.

### Azioni e test QEMU

- [ ] Eseguire Close attraverso il normale percorso di chiusura finestra.
- [ ] Verificare Minimize e Restore con stato/geometria precedenti preservati.
- [ ] Verificare Maximize e Restore dalla finestra massimizzata.
- [ ] Nella app dedicata, aprire due finestre e verificare attivazione e cambio
  focus.
- [ ] Con QMP e screenshot verificare hover/pressed, azioni, cambio Light/Dark,
  resize e assenza di pixel decorativi nella vecchia posizione.
- [ ] Provare tema/asset assente e verificare glyph di fallback.

**Criterio T2:** bordi e pulsanti usano il tema attivo, lo stato visivo segue
l'input e Close/Minimize/Maximize/Restore eseguono le azioni Win32 reali.

## T3 — Controlli Win32 nell'applicazione di test dedicata

### App e collegamento canonico

- [ ] Individuare e completare un'app grafica Ring 3 indipendente da Explorer;
  se necessario creare `theme_test.exe` con message loop Win32 reale.
- [ ] Usare le DLL di produzione User32, GDI32, UxTheme e Comctl32.
- [ ] Ospitare soltanto le famiglie richieste: BUTTON, checkbox, radio, EDIT,
  menu, scrollbar, SysTabControl32 e progress bar.
- [ ] Verificare il percorso `theme snapshot → UxTheme → User32/Comctl32 → GDI
  surface → compositor` per ciascun controllo.
- [ ] Usare proprietà del tema e fallback Base; non duplicare palette WhiteSur
  nei controlli e non dichiarare successo se il rendering/operazione fallisce.

### Stati e comportamento

- [ ] Verificare stati Normal, Hover, Pressed, Disabled, Focused,
  Checked/Unchecked e Selected/Unselected dove applicabili.
- [ ] BUTTON: click produce `BN_CLICKED`.
- [ ] Checkbox: click cambia lo stato verificabile.
- [ ] Radio: selezione esclusiva nel gruppo.
- [ ] EDIT: input, focus e stato disabled.
- [ ] Menu: apertura, focus/selezione e comando selezionato.
- [ ] Scrollbar: input aggiorna posizione e contenuto/stato osservabile.
- [ ] Tab: selezione aggiorna la pagina attiva.
- [ ] Progress: avanzamento aggiorna il valore e la superficie.

### Tema, lifetime e regressioni

- [ ] Visualizzare insieme i controlli supportati; acquisire screenshot QEMU
  Light, Dark e Base.
- [ ] Confrontare aree rappresentative di ciascun controllo e verificare anche
  stato/notifiche Win32, non solo differenze globali di pixel.
- [ ] Cambiare tema con controlli e finestre vivi senza ricrearli.
- [ ] Verificare creazione/distruzione, handle invalidi, `HTHEME` open/close,
  cleanup degli oggetti GDI, focus, ownership parent/child e terminazione.
- [ ] Verificare reload del tema mentre i controlli sono vivi e assenza di
  leak/corruzione delle superfici con gli strumenti disponibili.
- [ ] Spostare i probe pertinenti da Explorer solo dopo che le verifiche
  equivalenti sono operative nella app dedicata.
- [ ] Aggiornare `scripts/boot_micant.py` per eseguire i controlli del tema
  attraverso l'app dedicata; non dipendere da stringhe diagnostiche di
  Explorer per il risultato WhiteSur/Comctl32.
- [ ] Confermare che Explorer si avvii e usi i temi come normale consumer,
  senza nuovi controlli o probe di test.

**Criterio T3:** tutti i controlli richiesti sono disegnati dalle DLL reali,
seguono il tema e mantengono comportamento Win32 corretto nella app Ring 3.

## Gate integrato e report

- [ ] Build dei componenti canonici coinvolti e regressioni mirate.
- [ ] Avvio in QEMU del kernel modulare canonico; avvio di `theme_test.exe` come
  processo Ring 3.
- [ ] Eseguire le azioni reali via QMP e raccogliere stato Win32, screenshot e
  log seriale per T1, T2 e T3.
- [ ] Avviare anche senza WhiteSur e confermare tema Base e compositor canonico.
- [ ] Usare Explorer solo come controllo di non regressione.
- [ ] Aggiornare il registro con comando esatto, source SHA, esito, percorsi a
  screenshot/log e regressioni osservate.
- [ ] Riportare separatamente per T1/T2/T3: passato, fallito o non verificato.
- [ ] Annotare file modificati, commit locali, comandi QEMU e limiti realmente
  aperti; un commit non chiude l'incarico.

### Registro delle verifiche

| Data | Punto | Comando/build | Source SHA | Esito | Log e screenshot | Note |
|---|---|---|---|---|---|---|
| 2026-11-16 | T1 — Percorso di caricamento | `python3 qemu.py --headless --theme-test --memory-mib 2048 --timeout 110 --stable-seconds 3` | `061f262f` + worktree | Parziale / non verificato | `/private/tmp/micant-t1-unmap-diag-2048j/serial.log`, `/private/tmp/micant-t1-nativestyle-stages-2048/serial.log`, `/private/tmp/micant-t1-publish-capture-2048/serial.log` | La cattura precedente aveva isolato un rifiuto ExHeap con `g_addressSpaceReady=false`; l’audit non ha trovato scritture legittime del flag a false. In un run successivo `active.profile` e `native.style` sono stati aperti, dimensionati, letti e chiusi; `readSnapshot` è ritornato senza il vecchio errore ExHeap. Timeout prima della verifica della pubblicazione Win32k; i run seguenti sono falliti prima del desktop durante la mappatura SEC_IMAGE di NTDLL. Diagnostica temporanea rimossa; percorso e T1 non chiusi, nessun commit. |
| — | T1/T2/T3 | — | — | Non verificato | — | — |

## Fuori scope

Non aggiungere parser GTK generale, effetti blur, animazioni, famiglie di
controlli, temi oltre WhiteSur/Base, funzionalità Explorer, file manager, Task
Manager, compositor paralleli, accelerazione GPU o refactoring globale.
