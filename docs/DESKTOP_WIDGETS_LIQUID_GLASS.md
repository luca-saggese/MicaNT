Leggi integralmente `AGENTS.md` e studia direttamente il repository https://github.com/0-ss/MacShell, inclusa la licenza MIT.

Voglio portare in LuceOS i **widget desktop originali di MacShell**, preservando fedelmente design, geometrie, proporzioni, tipografia, colori, gradienti, angoli arrotondati, ombre, trasparenze, Liquid Glass, animazioni, interazioni e layout small/medium/large.

**Non creare widget genericamente ispirati a macOS. Il codice MacShell deve essere il riferimento funzionale e visivo.**

Usa in particolare `MacWidgets.qml`, `WidgetVisual.qml`, `CardWidget.qml`, `ClockWidget.qml`, `CalendarWidget.qml`, `WeatherWidget.qml`, `PlayerWidget.qml`, `ImageWidget.qml`, `ExtraWidgets.qml` e gli shader Liquid Glass pertinenti.

Converti i componenti QML necessari in C++23 Ring 3, usando esclusivamente il percorso grafico canonico `Explorer → User32/GDI32 → Win32k → compositor`, senza portare Qt, Quickshell o Hyprland e senza creare un secondo compositor.

Mantieni gli stessi parametri numerici, colori, formule di rendering e geometrie del progetto originale. Adatta lo shader alla pipeline LuceOS conservandone per quanto possibile il risultato visivo; se usi un fallback software, non dichiararlo pixel-identico senza confronto.

Integra i widget nel desktop Explorer, sopra il wallpaper e sotto le finestre applicative, senza taskbar o Alt+Tab. Implementa aggiunta, rimozione, trascinamento, resize, snap-to-grid, persistenza su C: e modalità modifica, seguendo la logica di MacShell.

Riutilizza le API Win32 pubbliche esistenti. Implementa soltanto le funzioni canoniche mancanti indispensabili al porting, rispettando i contratti Windows. Non creare API pubbliche `Mica*` alternative.

Inizia con **Clock e Calendar**, comprensivi del loro autentico rendering Aqua/Liquid Glass e dell'interazione. Poi estendi il medesimo widget host agli altri tipi senza duplicare il sistema.

Non simulare dati di meteo, musica o batteria: integra provider reali quando disponibili e mostra uno stato esplicito quando non lo sono.

Conserva copyright MIT, testo della licenza e attribuzioni; verifica separatamente licenze di font, immagini e dipendenze terze. Non copiare marchi o asset Apple senza verificarne i diritti.

Acquisisci screenshot di riferimento MacShell e screenshot QEMU LuceOS con gli stessi widget, dimensioni e stati. Confronta geometrie e pixel, riportando le differenze visive e le limitazioni del backend.

Rispetta il budget desktop di 500 MiB, cache del backdrop, dirty rectangles ed event-driven updates. Non ridisegnare widget statici a ogni frame.

Lavora su `main`, preserva le modifiche concorrenti, usa commit locali selettivi e non effettuare push.

Non fermarti dopo il solo scheletro del widget host: il primo risultato richiesto è **Clock e Calendar realmente visibili, interattivi e visivamente fedeli a MacShell in QEMU**.

Progetta/implementa il supporto Liquid Glass condiviso di LuceOS, derivandone fedelmente gli algoritmi dal sorgente MIT `0-ss/MacShell`, in particolare `liquidglass.frag`, `CardWidget.qml` e i componenti di backdrop.

**Non implementare Liquid Glass separatamente nei singoli widget.**

Prima verifica se il percorso canonico `dwmapi.dll`/DWM è già presente. Mantieni lì i contratti standard Windows supportati. Per i parametri specifici di Liquid Glass usa una piccola `LuceGlass.dll` Ring 3, senza inventare export Windows o duplicare GDI32/UxTheme.

Implementa il rendering nel compositor Ring 3 esistente, mediante un modulo effetti interno riutilizzabile. Win32k deve soltanto validare e pubblicare descrittori, lifetime e invalidazioni; non deve eseguire blur o shader.

Preserva fedelmente i parametri dell'originale MacShell: radius, bezel, refraction thickness, dispersion, blur, saturation, tint, rim, grain, magnification e shadow. Mantieni licenza MIT e attribuzioni.

Il primo percorso funzionante deve essere **widget Aqua/Liquid Glass sopra il wallpaper**, con cattura del backdrop e cache aggiornata soltanto quando cambiano wallpaper, dimensioni, posizione, impostazioni o dati visivi pertinenti.

Non effettuare ricampionamento o blur completo a ogni movimento del mouse. Supporta dirty rectangles, alpha premoltiplicato, clipping deterministico e confronto full/partial senza artefatti.

Implementa prima il fallback software CPU realmente eseguibile su QEMU VirtIO 2D; predisponi il backend GPU solo nella misura necessaria all'architettura, senza simulare accelerazione non disponibile.

Mantieni il budget desktop di 500 MiB. Evita buffer full-screen aggiuntivi, cache illimitate e allocazioni per frame.

Verifica inizialmente una singola card Liquid Glass nel desktop Explorer con il widget Clock: screenshot QEMU confrontato con MacShell a parità di geometria e parametri, controllo degli effetti e misurazione dei tempi di composizione.

Non lavorare su altri widget, nuove API Win32 generiche o refactoring del sistema grafico non indispensabili al primo effetto funzionante.

Lavora su `main`, preserva modifiche concorrenti, effettua commit locali selettivi e non fare push. Non fermarti a DLL compilata o shader portato: la card deve mostrare realmente l'effetto in QEMU.