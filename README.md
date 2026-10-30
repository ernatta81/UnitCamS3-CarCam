# UnitCamS3 – CarCam

Firmware PlatformIO che trasforma la **M5Stack UnitCamS3** (ESP32-S3 + OV2640) in una camera da auto montata dietro il parabrezza:

- **Guida (dashcam)**: registrazione in loop su microSD, file AVI MJPEG da 1 minuto. Quando la scheda si riempie, i video più vecchi vengono cancellati.
- **Parcheggio (sorveglianza)**: il modulo resta in deep sleep e si sveglia ogni N secondi per confrontare un'immagine con la precedente. Si sveglia anche se scatta un sensore esterno (vibrazione o PIR). Se c'è movimento registra una clip evento, e può scattare una foto time-lapse ogni N minuti.
- **Automatica**: con il filo del quadro (ACC) collegato, quadro acceso → dashcam, quadro spento → parcheggio.
- **Wi-Fi per il telefono**: access point con pagina web per puntare la camera (anteprima), scaricare o cancellare i video e cambiare le impostazioni. Si spegne da solo quando non serve.
- **Protezioni**: arresto per surriscaldamento, recupero automatico dei file tagliati da un'interruzione di corrente, limite di eventi all'ora per non scaricare la batteria.

Riusa board, pinout, HAL e librerie del [demo ufficiale](https://github.com/m5stack/UnitCamS3-UserDemo) e del progetto `UnitCamS3-ONVIF`.

---

## 1. Limiti fisici da conoscere prima

| Aspetto | Dato | Conseguenza |
|---|---|---|
| Temperatura | UnitCamS3: **0–40 °C** dichiarati. Modulo ESP32-S3-WROOM-1-**N16R8**: **max 65 °C** ambiente (limite della PSRAM octal) | Dietro un parabrezza al sole d'estate si superano facilmente 60–80 °C. Il firmware si ferma oltre `tempStopC`, ma il modulo resta **fuori specifica**. In estate usa il parasole e scegli una posizione in ombra |
| Ottica | OV2640, campo visivo diagonale **66,5°** | Vede la corsia davanti, non i lati: le dashcam commerciali hanno 120–150° |
| Notte | Sensore poco sensibile, niente IR | Di notte immagini scure e rumorose. Un illuminatore IR dietro il vetro rifletterebbe comunque sul parabrezza |
| Codec | Solo **MJPEG** (niente H.264 sull'ESP32-S3) | Circa 1–1,5 GB/ora in VGA a 10 fps: una scheda da 32 GB tiene 20–30 ore di loop |
| Scheda SD | Bus SPI, **solo FAT32** (exFAT non supportato dal framework) | Usa una microSD da **32 GB**, meglio *High Endurance* (pensata per scritture continue e caldo) |
| Batterie al litio | Non vanno caricate sopra 45 °C né lasciate oltre 60 °C | **Mai un power bank sul cruscotto**: si gonfia e può incendiarsi |
| PIR | Il vetro blocca l'infrarosso termico | Un PIR dietro il parabrezza **non vede fuori**: serve solo a rilevare intrusi nell'abitacolo |

Video e privacy: per uso personale una dashcam è normalmente lecita, ma non pubblicare video con persone o targhe riconoscibili. Una ripresa continua dello spazio pubblico mentre l'auto è parcheggiata può ricadere nel GDPR (sentenza CGUE C-212/13 *Ryneš*). Tieni il loop corto e cancella ciò che non serve. Monta la camera senza ostruire la visuale.

---

## 2. Alimentazione e consumi

### Cosa consuma (stime, da verificare)

M5Stack non pubblica i consumi della UnitCamS3. Le cifre qui sotto sono **stime** costruite sui consumi tipici di ESP32-S3, OV2640 e SD, misurati sul lato 5 V. Misurale sulla tua unità (vedi *Come misurare*) e rifai i conti con la formula.

| Stato | Corrente a 5 V | Potenza |
|---|---|---|
| Registrazione VGA 10 fps su SD, Wi-Fi spento | ~170–200 mA | **~0,9 W** |
| Registrazione + Wi-Fi acceso senza traffico | ~250–280 mA | ~1,3 W |
| Registrazione + anteprima live sul telefono | ~300–350 mA | ~1,6 W |
| Controllo movimento in parcheggio (~1,3 s da sveglio) | ~120–150 mA per 1,3 s | **~0,8 J per controllo** |
| Clip evento da 20 s | – | ~20 J |
| Deep sleep di tutta la scheda | **1–5 mA** (stima: LDO, sensore in standby, SD a riposo) | ~5–25 mW |

Il PWDN della camera è collegato a GND sulla scheda, quindi il sensore non si può spegnere. Il firmware lo mette in standby via registro e tiene basso il clock.

### Energia al giorno

Formula (lato 5 V):

```
E_giorno [Wh] = 24 · P_sleep + (86400 / T_controllo) · E_controllo / 3600 + N_eventi · E_evento / 3600
```

Con le stime sopra (P_sleep 10 mW, E_controllo 0,8 J, 10 eventi da 20 s al giorno):

| Scenario | Energia/giorno a 5 V | Dalla batteria auto 12 V (*) |
|---|---|---|
| Registrazione continua 24 h (presa sempre accesa) | 21,6 Wh | **~2,1 Ah/giorno** |
| Parcheggio, controllo ogni **10 s** (default) | ~2,2 Wh | ~0,24 Ah/giorno |
| Parcheggio, controllo ogni 30 s | ~0,9 Wh | ~0,12 Ah/giorno |
| Parcheggio, controllo ogni 60 s | ~0,6 Wh | ~0,09 Ah/giorno |
| Parcheggio solo con sensore di vibrazione/PIR (`parkCheckSec` = 0) | ~0,3 Wh + sensore | ~0,05 Ah/giorno |

(*) convertitore 12→5 V con rendimento 85% e 1 mA di assorbimento a vuoto (~0,3 Wh/giorno da solo).

### Quanto dura

**Batteria dell'auto** (es. 60 Ah). Per essere sicuri di ripartire non conviene usarne più del 10% circa, cioè 6 Ah. L'auto da sola consuma già 20–50 mA a riposo (0,5–1,2 Ah/giorno).

| Scenario | Giorni con 6 Ah |
|---|---|
| Registrazione continua | **~3 giorni** → serve per forza lo stacco a bassa tensione |
| Controllo ogni 10 s | ~25 giorni |
| Controllo ogni 30 s | ~50 giorni |
| Solo sensore | trascurabile rispetto al consumo a riposo dell'auto |

**Power bank 10 000 mAh** (37 Wh nominali, ~30 Wh utili a 5 V):

| Scenario | Durata |
|---|---|
| Registrazione continua | ~33 ore (~23 h con Wi-Fi sempre acceso) |
| Parcheggio, controllo ogni 10 s | ~2 settimane |
| Parcheggio, controllo ogni 30 s | ~1 mese |

Attenzione: **molti power bank si spengono da soli** quando l'assorbimento scende sotto 50–100 mA. In deep sleep la camera assorbe pochi mA, quindi il power bank si spegne e la camera non si risveglia più. Servono un power bank con modalità *always-on / low current* (spesso indicata per dispositivi indossabili) oppure una cella 18650 / LiFePO4 con un modulo boost 5 V senza auto-spegnimento. Tieni la batteria **sotto il sedile** o nel vano portaoggetti, non al sole.

### Opzioni di alimentazione

| | Come | Modalità | Pro / contro |
|---|---|---|---|
| **A. Presa accendisigari** | Caricatore USB 12 V → 5 V, cavo fino alla camera | `dashcam` | Semplicissimo. Se la presa è sotto chiave registra solo in marcia. Allo spegnimento si perdono gli ultimi ~3 s, poi il file viene riparato all'avvio successivo. Se la presa è **sempre alimentata**, registra fino a scaricare la batteria |
| **B. Kit di cablaggio (consigliato)** | Kit dashcam 12 V → 5 V con **stacco a bassa tensione** (11,8 / 12,0 / 12,2 V) collegato al +12 permanente con un *add-a-fuse*; filo ACC al fotoaccoppiatore su G20 | `auto` | Registra in marcia e sorveglia da fermo. Il kit protegge l'avviamento. Scegli un kit con basso assorbimento a vuoto |
| **C. Batteria separata** | Power bank always-on o 18650/LiFePO4 + boost | `parking` | Nessun cablaggio, l'auto non si scarica mai. Ma va ricaricata e sente caldo e freddo (vedi sopra) |

### Come misurare

1. Inserisci un tester USB (tipo FNIRSI FNB48/58 o UM25C) tra alimentatore e camera: leggi la corrente in registrazione, con Wi-Fi e con anteprima.
2. Per il deep sleep i tester USB non bastano: usa un multimetro in serie sul +5 V, portata mA.
3. In modalità parcheggio la pagina web mostra *risvegli* e *ms medi da sveglia*. Moltiplicando per la corrente da sveglio ottieni il tuo `E_controllo` reale. A questi va aggiunto circa 0,2 s di boot che il contatore non vede.

---

## 3. Collegamenti (porta Grove HY2.0-4P)

La porta Grove porta **5 V, GND, G19 e G20**. G19/G20 sono le linee USB D-/D+ (le usa il Grove2USB-C per caricare il firmware). Il firmware può usarle come ingressi:

- **G20 → quadro acceso (ACC)**
- **G19 → sensore di risveglio** (vibrazione / PIR)

La posizione dei due segnali sul connettore va controllata sull'etichetta della tua unità. Se li inverti, basta scambiarli nelle impostazioni web.

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

Impostazioni: `Quadro acceso (ACC)` = G20, *ACC attivo alto* **spento**, modalità **Automatica**. Quando il quadro si spegne, dopo 5 s la clip viene chiusa e parte il parcheggio. Riaccendendo il quadro, la camera si sveglia subito.

### Sensore di risveglio

- **Vibrazione / urto** (modulo SW-420 o simili): sveglia la camera se qualcuno urta l'auto. Molti moduli hanno uscita a 5 V: usa un partitore **10 kΩ in serie + 20 kΩ verso GND**. Togli il LED spia del modulo, che da solo consuma qualche mA.
- **PIR nell'abitacolo** (es. HC-SR501, uscita 3,3 V): rileva chi entra in auto, non chi passa fuori.
- Per usarli entrambi su G19: due diodi in OR verso il pin.

Impostazioni: `Sensore di risveglio` = G19, *Sensore attivo alto* acceso (per i moduli che danno livello alto quando scattano). Se il sensore resta attivo quando la camera torna a dormire, il firmware non lo riarma per 30 s, così non si risveglia a raffica.

---

## 4. Montaggio dietro il parabrezza

- **In alto al centro, dietro lo specchietto**, nella fascia serigrafata o oscurata: è fuori dalla tua visuale, pulita dai tergicristalli e un po' più in ombra.
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

## 6. Primo utilizzo

1. Formatta la microSD in **FAT32** e inseriscila.
2. Alimenta la camera e collega il telefono alla rete **`CarCam-xxxxxx`**, password **`unitcams3`**.
3. Apri `http://192.168.4.1/`. La pagina:
   - imposta l'ora prendendola dal telefono (la camera non ha un orologio a batteria),
   - mostra stato, temperatura e spazio libero sulla scheda,
   - ha l'anteprima per puntare la camera,
   - elenca i video *Guida* e *Parcheggio*, da scaricare o eliminare,
   - contiene le impostazioni (**cambia subito la password Wi-Fi**).
4. Il Wi-Fi si spegne dopo `apMinutes` minuti senza telefono collegato: risparmia ~0,3–0,5 W e scalda meno. Per riaverlo, in modalità dashcam togli e ridai corrente, in modalità automatica riaccendi il quadro.

### LED

| LED | Significato |
|---|---|
| Fisso | Sta registrando (guida) |
| Lampeggio veloce | Scheda SD assente o in errore |
| Lampeggio lento (1 s) | Finestra di configurazione all'accensione, poi parte il parcheggio |
| Spento | Parcheggio (deep sleep) oppure LED disattivato nelle impostazioni |

### File sulla scheda

```
/rec/R000123_20261029-153012.avi    guida (loop, si cancellano per primi)
/park/E000124_20261029-221501.avi   evento in parcheggio
/park/P000125_20261029-230000.jpg   foto time-lapse
```

Il numero cresce sempre e serve a trovare il file più vecchio. Data e ora compaiono solo se l'orologio è stato impostato dal telefono. Restano valide durante il deep sleep e si perdono se manca corrente. Gli `.avi` si aprono con VLC, Lettore multimediale di Windows o ffmpeg.

---

## 7. Impostazioni

| Chiave | Default | Note |
|---|---|---|
| `mode` | `dashcam` | `dashcam`, `parking`, `auto` (richiede `accPin`) |
| `frameSize` | `VGA` | da `QVGA` a `UXGA`. Sopra SVGA gli fps calano molto |
| `jpegQuality` | `12` | 6 = qualità massima, 63 = file più piccoli |
| `fps` | `10` | 1–25. Gli fps reali sono scritti nel file e mostrati nella pagina |
| `segmentSec` | `60` | durata di ogni file in guida |
| `minFreeMb` | `1024` | sotto questa soglia di spazio libero cancella i file più vecchi (massimo ¼ della scheda) |
| `vflip` / `hmirror` | `true` | orientamento |
| `led` | `true` | LED acceso mentre registra (false = discreto) |
| `accPin` / `accActiveHigh` | `-1` / `false` | ingresso quadro: 19, 20 o -1 |
| `triggerPin` / `triggerActiveHigh` | `-1` / `true` | sensore di risveglio: 19, 20 o -1 |
| `parkCheckSec` | `10` | controllo movimento con la camera ogni N s (0 = spento). È la voce che pesa di più sui consumi |
| `motionThreshold` | `25` | differenza di luminosità (0–255) perché una cella della griglia 16×12 conti come cambiata |
| `motionAreaPct` | `3` | percentuale di celle cambiate che fa scattare l'evento |
| `eventClipSec` | `20` | durata della clip evento |
| `maxEventsPerHour` | `10` | oltre questo limite gli eventi vengono ignorati fino alla fine dell'ora (strade trafficate) |
| `timelapseMin` | `0` | foto in parcheggio ogni N minuti (0 = spento) |
| `apSsid` / `apPass` | `""` / `unitcams3` | rete Wi-Fi. Password sotto 8 caratteri = rete aperta |
| `apMinutes` | `5` | spegne il Wi-Fi dopo N minuti senza telefono |
| `apAlwaysOn` | `false` | Wi-Fi sempre acceso in guida |
| `tempStopC` / `tempResumeC` | `80` / `70` | temperatura del chip, che in registrazione con Wi-Fi sta 10–20 °C sopra l'aria |

Costanti di compilazione (velocità SD, warm-up della camera, griglia di movimento…) in `src/app_config.h`.

---

## 8. Come funziona il parcheggio

```
deep sleep ──(timer parkCheckSec / sensore / ACC)──► sveglia (~0,2 s boot, niente Wi-Fi)
    ▲                                                    │
    │                                     camera on, 0,7 s per l'esposizione
    │                                                    │
    │                         JPEG decodificato a 1/8 → griglia 16×12 di luminosità
    │                         confronto con la griglia precedente (in RTC memory),
    │                         compensando i cambi di luce generali
    │                                                    │
    │                 movimento o sensore? ── sì ──► clip AVI in /park (eventClipSec)
    │                                                    │
    └──────────── camera in standby, SD smontata, LED spento ◄──┘
```

- Tra un controllo e l'altro c'è una latenza: con un controllo ogni 10 s la clip può partire fino a ~11 s dopo l'inizio del movimento. Il sensore di vibrazione riduce la latenza a ~1,5 s (avvio della camera).
- Con il traffico davanti all'auto ci saranno falsi allarmi. Alza `motionThreshold` o `motionAreaPct`, oppure usa solo il sensore.

## 9. Problemi comuni

- **Errori di scrittura SD / LED che lampeggia veloce**: metti `APP_SD_FREQ_HZ` a `10000000` in `src/app_config.h` e ricompila, oppure cambia scheda (FAT32, 32 GB).
- **Fps più bassi di quelli impostati**: abbassa la risoluzione o aumenta `jpegQuality` (file più piccoli).
- **Download lento**: la SD è in SPI (~0,3–0,6 MB/s). Per molti video togli la scheda e leggila dal PC.
- **Si spegne d'estate**: è la protezione termica. Si riavvia da sola dopo 10 minuti se la temperatura è scesa sotto `tempResumeC`.
- **Password Wi-Fi dimenticata**: **Upload Filesystem Image** ricarica `data/config.json`.

## Struttura

```
UnitCamS3-CarCam/
├── platformio.ini          stessa board del demo (esp32s3box, 16MB flash, PSRAM octal)
├── partitions.csv
├── data/config.json        impostazioni di default (Upload Filesystem Image)
├── lib/                    AsyncTCP + ESPAsyncWebServer (dal demo)
└── src/
    ├── main.cpp            avvio: guida / finestra di configurazione / ciclo di parcheggio
    ├── app_config.h        costanti di compilazione
    ├── hal/                camera, SD, LED, ingressi Grove, temperatura, orologio, config
    ├── net/                access point Wi-Fi con spegnimento automatico
    ├── storage/            nomi file, cancellazione dei più vecchi, elenco
    ├── recorder/           scrittore AVI MJPEG (con riparazione dopo blackout), registrazione in loop
    ├── parking/            deep sleep, risvegli, rilevamento movimento
    └── servers/            pagina web e API
```
