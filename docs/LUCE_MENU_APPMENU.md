# STEERING — LuceOS User32 Menu System + Global AppMenu

Leggi integralmente `AGENTS.md` prima di modificare il repository.

## Obiettivo

Riorganizza il sistema di menu Win32 di LuceOS, completa il supporto dei sottomenu e implementa una modalità **AppMenu globale attivabile/disattivabile**, analoga al menu superiore di macOS.

Il risultato deve utilizzare **un unico modello di menu Win32 canonico**, con due modalità di presentazione:

- **AppMenu OFF:** menu nella finestra dell'applicazione.
- **AppMenu ON:** menu nella barra superiore del desktop, aggiornato in base all'applicazione in foreground.

Non creare una seconda implementazione di HMENU, né API proprietarie sostitutive delle API Windows.

## 1. Spostamento del codice fuori da kernel/

Il file attuale è:

`tools/dlls/user32.cpp`

Questo codice è Ring 3 e viene già compilato dentro `user32_boot`. Non deve rimanere sotto `kernel/ps/`.

Spostalo in:

`tools/dlls/user32_menu.cpp`

Oppure, se esiste già una struttura canonica per i componenti freestanding di User32, usa quella.

Aggiorna `CMakeLists.txt` e gli include necessari.

Mantieni invariati gli export pubblici, le convenzioni di chiamata, i contratti ABI e il comportamento già funzionante.

Non copiare il file lasciando due implementazioni attive.

Verifica che il target finale `user32.dll` contenga una sola implementazione di ogni funzione menu.

## 2. Modello HMENU unico

Mantieni in User32 la gestione canonica di:

- `CreateMenu`
- `CreatePopupMenu`
- `DestroyMenu`
- `AppendMenuW/A`
- `InsertMenuW/A`
- `ModifyMenuW/A`
- `RemoveMenu`
- `DeleteMenu`
- `GetMenu`
- `SetMenu`
- `GetSubMenu`
- `GetMenuItemInfoW`
- `SetMenuItemInfoW`
- `EnableMenuItem`
- `CheckMenuItem`
- `CheckMenuRadioItem`
- `DrawMenuBar`
- `TrackPopupMenu`
- `TrackPopupMenuEx`

Implementa soltanto le funzioni mancanti necessarie al comportamento richiesto, rispettando le semantiche Windows documentate.

Non sostituire funzioni mancanti con successi fittizi.

Mantieni ownership, lifetime, distruzione ricorsiva, validazione degli handle e accesso concorrente corretti.

## 3. Supporto reale dei sottomenu

Attualmente `showPopup()` rifiuta le voci che possiedono `submenu`.

Elimina questa limitazione implementando realmente la navigazione gerarchica.

Un menu deve supportare:

File
  Open
  Recent Files
    Documento A
    Documento B
    More
      Documento C
  Exit

Requisiti:

- Apertura del sottomenu a destra della voce.
- Apertura a sinistra se non c'è spazio sul display.
- Posizionamento verticale corretto.
- Submenu depth non vincolata a un solo livello.
- Freccia indicatrice del sottomenu.
- Hover per aprire e cambiare sottomenu.
- Click per selezionare la voce finale.
- Tastiera: frecce, Invio, Esc.
- Chiusura corretta dell'intera catena.
- Focus e mouse capture coerenti.
- Notifiche Windows `WM_INITMENU`, `WM_INITMENUPOPUP`, `WM_MENUSELECT`, `WM_COMMAND` e `WM_MENUCOMMAND` secondo i contratti applicabili.
- Corretta semantica di `TPM_RETURNCMD`, `TPM_NONOTIFY` e degli altri flag effettivamente supportati.

Non usare una sola sessione globale `g_popup` per tutti i menu e tutti i processi.

Ogni catena popup deve avere ownership e lifecycle espliciti, con cleanup anche quando la finestra proprietaria viene distrutta.

Non introdurre thread o busy polling non necessari.

## 4. Test completo in theme_test.exe

Aggiungi esclusivamente a `theme_test.exe` una barra menu Win32 reale.

Deve utilizzare le API standard `CreateMenu`, `CreatePopupMenu`, `AppendMenuW`, `SetMenu` e `DrawMenuBar`.

Menu di test:

File
  New
  Open
  Recent Files
    Documento 1
    Documento 2
    More
      Documento 3
  Separator
  Exit

Edit
  Undo
  Redo
  Separator
  Preferences

View
  Light
  Dark
  Show Details

Help
  About LuceOS

I comandi selezionati devono generare messaggi reali verso la finestra e aggiornare uno stato visibile nel client area.

Implementa almeno una voce checked, una disabled e una voce radio per verificare gli stati.

Non usare pulsanti custom per simulare menu.

Non aggiungere sonde in Explorer.

## 5. Modalità Global AppMenu

Introduci un'impostazione persistente:

`AppMenu enabled = true/false`

Il nome e il formato interno devono seguire il sistema di configurazione già esistente in LuceOS.

La modalità deve essere modificabile durante la sessione e mantenuta dopo il riavvio.

La presentazione dei menu deve dipendere da questa impostazione, non dalla singola applicazione.

### AppMenu disabilitato

Comportamento Windows tradizionale:

- La barra menu rimane nella finestra proprietaria.
- I popup si aprono sotto la relativa voce.
- Non viene visualizzata la barra AppMenu globale.
- Il client area rispetta l'altezza della barra menu.
- Le applicazioni continuano a utilizzare User32 normalmente.

### AppMenu abilitato

Comportamento globale:

- La barra menu non viene disegnata dentro la finestra dell'applicazione.
- La sua altezza viene restituita al client area attraverso il normale layout Win32.
- Explorer mostra una barra orizzontale nella parte superiore dello schermo.
- La barra contiene i menu dell'applicazione attualmente in foreground.
- Il contenuto cambia immediatamente quando cambia la finestra attiva.
- Quando l'applicazione modifica i suoi HMENU, la barra globale viene aggiornata.
- Quando l'applicazione termina, i menu precedenti non restano visibili.
- In assenza di una finestra applicativa con HMENU, viene mostrato soltanto il comportamento della shell previsto per quel caso, senza menu inventati.
- La modalità non deve alterare la semantica degli HMENU posseduti dalle applicazioni.

**AppMenu è una modalità di presentazione del sistema User32 esistente, non un secondo menu system.**

## 6. Ownership e comunicazione fra processi

Questo requisito è fondamentale.

Gli HMENU sono oggetti posseduti dal processo/applicazione. Explorer non deve dereferenziare puntatori HMENU provenienti da un altro address space e non deve leggere direttamente le tabelle private di User32 di altri processi.

Riutilizza l'owner Win32k e i meccanismi canonici IPC/process/window già disponibili.

Implementa soltanto il trasporto privato minimo necessario per:

- Identificare la finestra foreground e il processo proprietario.
- Pubblicare uno snapshot validato della struttura menu per la presentazione.
- Ricevere aggiornamenti del menu.
- Aprire i popup nel contesto corretto, conservando la semantica Windows.
- Inoltrare la selezione al processo che possiede il menu.
- Invalidare snapshot stale mediante handle/generation.
- Gestire distruzione finestra, processo terminato e cambio foreground.

La copia visualizzata da Explorer non deve diventare una seconda fonte di verità.

Le query e le operazioni sul menu devono rispettare ownership, access checks, sincronizzazione e cleanup.

Evita nuovi syscall pubblici, nuove DLL o duplicazioni di Win32k se è sufficiente estendere una ABI privata esistente.

## 7. Rendering e Theme Engine

Tutti i menu, sia locali sia globali, devono utilizzare il tema attivo di LuceOS.

Il rendering deve passare dalle API esistenti:

`User32 → GDI32 / UxTheme → Win32k → Compositor`

Usa `OpenThemeData(..., L"MENU")` e le primitive UxTheme canoniche nei casi supportati.

Tematizza:

- Barra menu.
- Background dei popup.
- Voci normali.
- Hover.
- Pressed.
- Disabled.
- Checked e radio.
- Separatori.
- Frecce dei submenu.
- Bordi.
- Testo.
- Padding.
- Altezza delle righe.

WhiteSur Light, Dark e Base devono funzionare.

Non introdurre colori hardcoded quando il Theme Snapshot fornisce una proprietà valida.

Quando una proprietà del tema manca, usa un fallback coerente e dichiarato.

Il cambio Light/Dark deve aggiornare immediatamente menu aperti e barra globale, senza ricreare l'applicazione.

Il compositor non deve conoscere HMENU, MenuItem o WM_COMMAND: deve soltanto comporre le superfici.

## 8. Barra AppMenu nel desktop Explorer

Implementa la barra globale nel componente shell/desktop di Explorer già canonico.

Non inserirla direttamente in `tools/compositor_app.cpp`.

La barra deve:

- Occupare la parte superiore del desktop.
- Seguire la larghezza effettiva dello schermo.
- Rispettare il display mode runtime.
- Avere dimensioni e padding determinati dal tema.
- Essere visualizzata sopra wallpaper e finestre normali secondo il comportamento AppMenu richiesto.
- Restare interattiva anche quando l'applicazione foreground è in un altro processo.
- Non rubare permanentemente il focus all'applicazione.
- Non comparire nell'Alt+Tab o come normale finestra della taskbar.
- Aggiornare solo le aree modificate.

Non utilizzare una finestra fullscreen trasparente che catturi tutti i click del desktop.

Gestisci correttamente la work area e la massimizzazione delle applicazioni quando AppMenu è attivo.

Evita che un click sulla barra cambi il foreground in Explorer prima di identificare quale menu dell'applicazione deve essere aperto.

## 9. Attivazione e disattivazione

Aggiungi un'opzione di sistema accessibile dall'interfaccia grafica esistente, senza inventare una seconda applicazione di configurazione.

Per i test, `theme_test.exe` deve poter verificare entrambe le modalità.

Il cambio deve essere runtime:

OFF → ON:
- Rimuove la barra menu locale.
- Mostra AppMenu nella shell.
- Aggiorna il client area senza distruggere la finestra.

ON → OFF:
- Nasconde AppMenu.
- Ripristina la barra locale.
- Ripristina la geometria e il layout della finestra.

Le transizioni non devono duplicare, perdere o distruggere gli HMENU.

Se la shell AppMenu non è disponibile o termina, il sistema deve ripristinare un percorso usabile per accedere ai menu delle applicazioni, senza perdere i comandi.

## 10. Test QEMU obbligatori

Esegui in QEMU almeno questi scenari:

A. AppMenu OFF: menu locale File, Edit, View, Help.

B. Apertura di File → Recent Files → More → Documento 3, con invio reale di `WM_COMMAND`.

C. Navigazione con mouse e tastiera, inclusi Esc e click esterno.

D. Voce checked, disabled e radio.

E. Attivazione AppMenu ON con `theme_test.exe` già aperto.

F. Menu trasferito visivamente nella barra superiore senza duplicazioni.

G. Cambio foreground tra `theme_test.exe` e un'altra applicazione con menu.

H. Cambio foreground verso una finestra senza menu.

I. Cambio WhiteSur Light → Dark con menu aperto e con AppMenu attivo.

J. AppMenu ON → OFF senza ricreazione della finestra.

K. Chiusura o terminazione del processo proprietario durante un popup.

L. Verifica che massimizzazione, ridimensionamento e client area rimangano corretti in entrambe le modalità.

M. Verifica screenshot QMP, messaggi reali e assenza di regressioni su Explorer.

Non considerare sufficiente vedere una barra disegnata. I comandi devono arrivare realmente all'applicazione proprietaria.

## 11. Limiti dell'incarico

Non lavorare su:

- MacShell.
- Liquid Glass.
- Dock.
- Widget desktop.
- Compositor redesign.
- Refactoring generale di GDI32.
- Nuove API pubbliche Windows.
- Altri controlli Comctl32.
- Nuovo menu Start.

Modifica soltanto le dipendenze necessarie alla funzionalità descritta.

Mantieni il budget desktop di 500 MiB e non introdurre un polling continuo per sincronizzare il menu globale.

## 12. Completamento

Il task è completo soltanto quando:

1. Il codice menu è stato spostato da `kernel/ps/` nel percorso User32 Ring 3 appropriato.
2. Le API standard HMENU continuano a funzionare.
3. I sottomenu annidati sono realmente operativi.
4. `theme_test.exe` mostra ed esercita i menu di test.
5. AppMenu ON/OFF è configurabile e persistente.
6. AppMenu ON sposta la presentazione nella barra globale del desktop.
7. La barra segue correttamente l'applicazione foreground.
8. I comandi vengono consegnati al vero proprietario del menu.
9. WhiteSur Light/Dark/Base tematizzano menu locali e globali.
10. Non esistono duplicazioni del modello HMENU.
11. Focus, Z-order, popup e layout rimangono corretti.
12. Le verifiche QEMU confermano il comportamento.

Non terminare dopo il solo spostamento del file o dopo un semplice menu di prova.

Continua fino al risultato integrato. Esegui commit locali selettivi soltanto delle tue modifiche e non fare push.
