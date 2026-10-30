# UnitCamS3 – Parking Monitor

Firmware PlatformIO che trasforma la **M5Stack UnitCamS3** (ESP32-S3 + OV2640) in una **sorveglianza da parcheggio** montata dietro il parabrezza. Non registra mentre guidi: serve solo quando l'auto è ferma.

- La camera resta quasi sempre in **deep sleep** e si sveglia ogni N secondi (default 10) per ~1,3 s. Confronta l'immagine con quella del risveglio precedente e, se vede movimento, registra una **clip AVI** sulla microSD.
- Si sveglia subito anche se scatta un **sensore esterno** (vibrazione o PIR) collegato alla porta Grove.
- Facoltativo: una **foto time-lapse** ogni N minuti.
- Facoltativo: con il filo del quadro (**ACC**) collegato, la sorveglianza va **in pausa mentre guidi** e riparte 60 s dopo che hai spento.
- **Wi-Fi per il telefono** per alcuni minuti all'accensione (e quando giri la chiave, se c'è l'ACC): anteprima per puntare la camera, elenco eventi da scaricare o cancellare, impostazioni. Poi il Wi-Fi si spegne.
- **Protezioni**: limite di eventi all'ora (strade trafficate), cancellazione dei file più vecchi a scheda piena, recupero delle clip tagliate da un'interruzione di corrente, controlli saltati se fa troppo caldo.

Riusa board, pinout, HAL e librerie del [demo ufficiale](https://github.com/m5stack/UnitCamS3-UserDemo) e del progetto `UnitCamS3-ONVIF`.

---

## 1. Limiti fisici da conoscere prima

| Aspetto | Dato | Conseguenza |
|---|---|---|
| Temperatura | UnitCamS3: **0–40 °C** dichiarati. Modulo ESP32-S3-WROOM-1-**N16R8**: **max 65 °C** ambiente (limite della PSRAM octal) | Dietro un parabrezza al sole d'estate si superano 60–80 °C. In deep sleep il modulo quasi non si scalda, ma resta **fuori specifica**: usa il parasole. Sopra `tempStopC` i risvegli saltano la camera |
| Ottica | OV2640, campo visivo diagonale **66,5°** | Vede ciò che sta davanti all'auto, non i lati |
| Notte | Sensore poco sensibile, niente IR | Di notte immagini scure e rumorose. Un illuminatore IR dietro il vetro rifletterebbe sul parabrezza |
| Rilevamento | Controllo ogni N secondi, non continuo | Con controllo ogni 10 s la clip può iniziare fino a ~11 s dopo il movimento. Un sensore di vibrazione la fa partire in ~1,5 s |
| Scheda SD | Bus SPI, **solo FAT32** (exFAT non supportato dal framework) | microSD da **32 GB**, meglio *High Endurance* (regge caldo e tante scritture) |
| Batterie al litio | Non vanno caricate sopra 45 °C né lasciate oltre 60 °C | **Mai un power bank sul cruscotto**: si gonfia e può incendiarsi |
| PIR | Il vetro blocca l'infrarosso termico | Un PIR dietro il parabrezza **non vede fuori**: rileva solo chi entra nell'abitacolo |

Privacy: una ripresa dello spazio pubblico mentre l'auto è parcheggiata può ricadere nel GDPR (sentenza CGUE C-212/13 *Ryneš*). Salva solo gli eventi, tienili poco e non pubblicare video con persone o targhe riconoscibili.

---

## 2. Consumi e batteria

### Cosa consuma (stime, da verificare)

M5Stack non pubblica i consumi della UnitCamS3. Le cifre qui sotto sono **stime** costruite sui consumi tipici di ESP32-S3, OV2640 e SD, sul lato 5 V. Misurale sulla tua unità (vedi *Come misurare*) e rifai i conti con la formula.

| Stato | Corrente a 5 V | Energia / potenza |
|---|---|---|
| Deep sleep di tutta la scheda | **1–5 mA** (LDO, sensore in standby, SD a riposo) | ~5–25 mW |
| Controllo movimento (~1,3 s sveglio, niente Wi-Fi) | ~120–150 mA | **~0,8 J a controllo** |
| Clip evento da 20 s | ~180 mA | ~20 J |
| Finestra Wi-Fi con anteprima (all'accensione) | ~300 mA | ~1,5 W per `apMinutes` minuti |

Il PWDN della camera è collegato a GND sulla scheda, quindi il sensore non si può spegnere. Il firmware lo mette in standby via registro e tiene basso il clock.

### Energia al giorno

Formula (lato 5 V):

```
E_giorno [Wh] = 24 · P_sleep + (86400 / parkCheckSec) · E_controllo / 3600 + N_eventi · E_evento / 3600
```

Con le stime sopra (P_sleep 10 mW, E_controllo 0,8 J, 10 eventi da 20 s al giorno):

| Impostazione | Energia/giorno a 5 V | Dalla batteria auto 12 V (*) | Giorni con 6 Ah (10% di 60 Ah) | Power bank 10 000 mAh (~30 Wh utili) |
|---|---|---|---|---|
| Controllo ogni **10 s** (default) | ~2,2 Wh | ~0,24 Ah | ~25 | ~2 settimane |
| Controllo ogni 30 s | ~0,9 Wh | ~0,12 Ah | ~50 | ~1 mese |
| Controllo ogni 60 s | ~0,6 Wh | ~0,09 Ah | ~70 | ~1,5 mesi |
| Solo sensore (`parkCheckSec` = 0) | ~0,3 Wh + sensore | ~0,05 Ah | limitato dall'auto stessa | mesi (scarica spontanea, vedi sotto) |

(*) convertitore 12→5 V con rendimento 85% e 1 mA a vuoto (~0,3 Wh/giorno da solo).

Per la batteria auto conviene non superare il 10% circa per essere sicuri di ripartire. L'auto da sola consuma già 20–50 mA a riposo (0,5–1,2 Ah/giorno), che è più della camera.

Il parametro che pesa di più è `parkCheckSec`: ogni controllo costa ~0,8 J, mentre il sonno costa quasi nulla.

### Opzioni di alimentazione

| | Come | Pro / contro |
|---|---|---|
| **A. Kit di cablaggio (consigliato)** | Kit dashcam 12 V → 5 V con **stacco a bassa tensione** (11,8 / 12,0 / 12,2 V) sul +12 permanente tramite *add-a-fuse*. Filo ACC al fotoaccoppiatore su G20 | Autonomia di settimane, il kit protegge l'avviamento. Con l'ACC la camera non lavora mentre guidi. Scegli un kit con basso assorbimento a vuoto |
| **B. Batteria separata** | Power bank *always-on* oppure 18650 / LiFePO4 + modulo boost 5 V senza auto-spegnimento | Nessun cablaggio e l'auto non si scarica mai. Va ricaricata e soffre caldo e freddo |
| ~~Presa accendisigari~~ | | Quasi sempre si spegne con il quadro: in parcheggio la camera resterebbe senza corrente |

Attenzione: **molti power bank si spengono da soli** quando l'assorbimento scende sotto 50–100 mA. In deep sleep la camera assorbe pochi mA, quindi il power bank si spegne e la camera non si risveglia più. Cerca la modalità *always-on / low current* (spesso indicata per dispositivi indossabili). Tieni la batteria **sotto il sedile** o nel vano portaoggetti, non al sole.

Senza ACC e con alimentazione permanente la sorveglianza resta attiva anche in marcia: vedrà movimento di continuo e salverà fino a `maxEventsPerHour` clip inutili all'ora. Collega l'ACC oppure usa la batteria separata e spegnila quando guidi.

### Come misurare

1. Tester USB (tipo FNIRSI FNB48/58 o UM25C) tra alimentazione e camera: leggi la corrente durante la finestra Wi-Fi e durante una clip.
2. Per il deep sleep i tester USB non bastano: usa un multimetro in serie sul +5 V, portata mA.
3. La pagina web mostra *risvegli* e *ms medi da sveglia* dall'ultima accensione: moltiplica per la corrente da sveglio per avere il tuo `E_controllo` reale. Aggiungi ~0,2 s di boot che il contatore non vede.

---

## 3. Collegamenti (porta Grove HY2.0-4P)

La porta Grove porta **5 V, GND, G19 e G20**. G19/G20 sono le linee USB D-/D+ (le usa il Grove2USB-C per caricare il firmware). Il firmware può usarle come ingressi:

- **G20 → quadro acceso (ACC)**: sorveglianza in pausa mentre guidi
- **G19 → sensore di risveglio** (vibrazione / PIR)

Controlla sull'etichetta della tua unità quale contatto è G19 e quale G20. Se li inverti, basta scambiarli nelle impostazioni web.

> Con G19/G20 configurati come ingressi la USB della porta è disattivata. Per caricare un nuovo firmware scollega i sensori, collega il Grove2USB-C e cortocircuita G0 e GND all'accensione (procedura di download M5Stack).
> I pin accettano **massimo 3,3 V**: mai collegare 12 V o 5 V direttamente.

### Ingresso ACC con fotoaccoppiatore (isolato dai disturbi dell'auto)

```
 +12 V ACC (fusibile sotto chiave, add-a-fuse 1-2 A)
      │
   [ 3,3 kΩ ]            ~4 mA a 14,4 V
      │
      ├──── PC817 pin 1 (anodo LED)
      │
      │     1N4148 in antiparallelo al LED (protezione inversione)
      │
 GND auto ── PC817 pin 2 (catodo LED)

 PC817 pin 4 (collettore) ──── G20      pull-up interno: attivo basso
 PC817 pin 3 (emettitore) ──── GND della porta Grove
```

Impostazioni: `Quadro acceso (ACC)` = G20, *ACC attivo alto* **spento**.

Cosa succede:
- **Giri la chiave**: la camera si sveglia, accende il Wi-Fi per `apMinutes` minuti (per vedere gli eventi dal telefono), poi dorme senza consumare finché il quadro resta acceso.
- **Spegni il quadro**: dopo 60 s, il tempo di scendere e allontanarti, riparte la sorveglianza con una nuova immagine di riferimento.

### Sensore di risveglio

- **Vibrazione / urto** (modulo SW-420 o simili): sveglia la camera se qualcuno urta l'auto. Molti moduli hanno uscita a 5 V: usa un partitore **10 kΩ in serie + 20 kΩ verso GND**. Togli il LED spia del modulo, che da solo consuma qualche mA.
- **PIR nell'abitacolo** (es. HC-SR501, uscita 3,3 V): rileva chi entra in auto.
- Per usarli entrambi su G19: due diodi in OR verso il pin.

Impostazioni: `Sensore di risveglio` = G19, *Sensore attivo alto* acceso (per i moduli che danno livello alto quando scattano). Se il sensore è ancora attivo quando la camera torna a dormire, il firmware non lo riarma per 30 s, così non si risveglia a raffica.

---

## 4. Montaggio dietro il parabrezza

- **In alto al centro, dietro lo specchietto**, nella fascia serigrafata o oscurata: è più in ombra e fuori dalla visuale.
- **Obiettivo a contatto col vetro** (meno di 1 cm) con una cornice nera opaca o un pezzo di gomma piuma nera attorno, per eliminare i riflessi del cruscotto.
- Fissala con biadesivo 3M VHB o un supporto stampato 3D. Le ventose si staccano col caldo.
- Non coprire il sensore pioggia/luce né la telecamera ADAS dell'auto.
- Fai passare il cavo nel cielo e lungo il montante, **dietro** l'airbag a tendina e mai davanti al suo coperchio.
- Usa un involucro chiaro, non nero, e il parasole quando parcheggi al sole.
- Orientamento: apri l'anteprima dal telefono e regola *Capovolgi* / *Specchia* finché l'immagine è dritta.

---

## 5. Compilare e caricare (VS Code + PlatformIO)

1. Installa l'estensione **PlatformIO IDE** in VS Code (viene proposta da `.vscode/extensions.json`).
2. `File → Open Folder…` → `UnitCamS3-CarCam` (la cartella con `platformio.ini`).
3. **Build**, poi **Upload** con il Grove2USB-C. Se non entra in download mode, cortocircuita G0–GND all'accensione.
4. Facoltativo: modifica `data/config.json` e usa **Upload Filesystem Image** per precaricare le impostazioni. Sovrascrive quelle salvate.

Da terminale:

```bash
pio run -t upload
```

```bash
pio run -t uploadfs
```

## 6. Uso

1. Formatta la microSD in **FAT32** e inseriscila.
2. Dai corrente: il LED lampeggia lentamente. Collega il telefono alla rete **`ParkCam-xxxxxx`**, password **`unitcams3`**.
3. Apri `http://192.168.4.1/`. La pagina:
   - imposta l'ora prendendola dal telefono (la camera non ha un orologio a batteria),
   - mostra temperatura, spazio libero sulla scheda e statistiche dei risvegli,
   - ha l'anteprima per puntare la camera,
   - elenca eventi e foto, da scaricare o eliminare,
   - contiene le impostazioni (**cambia subito la password Wi-Fi**).
4. Dopo `apMinutes` minuti senza telefono collegato il Wi-Fi si spegne, il LED si spegne e parte la sorveglianza. Per riavere il Wi-Fi gira la chiave (con l'ACC collegato) oppure togli e ridai corrente.

### File sulla scheda

```
/park/E000124_20261029-221501.avi   evento (movimento o sensore)
/park/P000125_20261029-230000.jpg   foto time-lapse
```

Il numero cresce sempre: a scheda piena (sotto `minFreeMb` liberi) si cancellano i file con il numero più basso. Data e ora compaiono solo se l'orologio è stato impostato dal telefono. Restano valide durante il deep sleep e si perdono se manca corrente. Gli `.avi` si aprono con VLC, Lettore multimediale di Windows o ffmpeg. Il download dal Wi-Fi è lento (~0,3–0,6 MB/s): per molti file togli la scheda.

---

## 7. Impostazioni

| Chiave | Default | Note |
|---|---|---|
| `frameSize` | `VGA` | da `QVGA` a `UXGA`. Sopra SVGA gli fps delle clip calano molto |
| `jpegQuality` | `12` | 6 = qualità massima, 63 = file più piccoli |
| `fps` | `10` | fps delle clip, 1–25 |
| `minFreeMb` | `1024` | sotto questa soglia di spazio libero cancella i file più vecchi (massimo ¼ della scheda) |
| `vflip` / `hmirror` | `true` | orientamento |
| `accPin` / `accActiveHigh` | `-1` / `false` | ingresso quadro: 19, 20 o -1 |
| `triggerPin` / `triggerActiveHigh` | `-1` / `true` | sensore di risveglio: 19, 20 o -1 |
| `parkCheckSec` | `10` | controllo movimento con la camera ogni N s (0 = solo sensore). È la voce che pesa di più sui consumi |
| `motionThreshold` | `25` | differenza di luminosità (0–255) perché una cella della griglia 16×12 conti come cambiata |
| `motionAreaPct` | `3` | percentuale di celle cambiate che fa scattare l'evento |
| `eventClipSec` | `20` | durata della clip evento |
| `maxEventsPerHour` | `10` | oltre questo limite gli eventi vengono ignorati e la camera non viene accesa fino alla fine dell'ora |
| `timelapseMin` | `0` | foto ogni N minuti (0 = spento) |
| `apSsid` / `apPass` | `""` / `unitcams3` | rete Wi-Fi. Password sotto 8 caratteri = rete aperta |
| `apMinutes` | `5` | il Wi-Fi si spegne dopo N minuti senza telefono, poi parte la sorveglianza |
| `tempStopC` | `80` | sopra questa temperatura del chip i risvegli saltano la camera |

Costanti di compilazione (velocità SD, warm-up della camera, ritardo dopo lo spegnimento del quadro, griglia di movimento…) in `src/app_config.h`.

---

## 8. Come funziona

```
deep sleep ──(timer parkCheckSec / sensore)──► sveglia (~0,2 s boot, niente Wi-Fi)
    ▲                                              │
    │                                camera on, 0,7 s per l'esposizione
    │                                              │
    │                     JPEG decodificato a 1/8 → griglia 16×12 di luminosità
    │                     confronto con la griglia precedente (in RTC memory),
    │                     compensando i cambi di luce generali
    │                                              │
    │             movimento o sensore? ── sì ──► clip AVI in /park (eventClipSec)
    │                                              │
    └──────── camera in standby, SD smontata, LED spento ◄──┘

quadro acceso ──► Wi-Fi per apMinutes ──► sonno in pausa ──(quadro spento + 60 s)──► sorveglianza
```

Con il traffico davanti all'auto ci saranno falsi allarmi: alza `motionThreshold` o `motionAreaPct`, oppure usa solo il sensore.

## 9. Problemi comuni

- **Errori di scrittura SD**: metti `APP_SD_FREQ_HZ` a `10000000` in `src/app_config.h` e ricompila, oppure cambia scheda (FAT32, 32 GB).
- **Troppi eventi**: alza `motionThreshold` / `motionAreaPct` o `parkCheckSec`, abbassa `maxEventsPerHour`.
- **Nessun evento**: controlla l'inquadratura dall'anteprima e abbassa le soglie. Il primo controllo dopo l'avvio serve solo a fissare il riferimento.
- **Password Wi-Fi dimenticata**: **Upload Filesystem Image** ricarica `data/config.json`.

## Struttura

```
UnitCamS3-CarCam/
├── platformio.ini          stessa board del demo (esp32s3box, 16MB flash, PSRAM octal)
├── partitions.csv
├── data/config.json        impostazioni di default (Upload Filesystem Image)
├── lib/                    AsyncTCP + ESPAsyncWebServer (dal demo)
└── src/
    ├── main.cpp            avvio: finestra Wi-Fi oppure ciclo di sorveglianza
    ├── app_config.h        costanti di compilazione
    ├── hal/                camera, SD, LED, ingressi Grove, temperatura, orologio, config
    ├── net/                access point Wi-Fi con spegnimento automatico
    ├── storage/            nomi file, cancellazione dei più vecchi, elenco
    ├── recorder/           scrittore AVI MJPEG (con riparazione dopo blackout), clip evento
    ├── parking/            deep sleep, risvegli, rilevamento movimento
    └── servers/            pagina web e API
```
