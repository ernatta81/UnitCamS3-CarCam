/**
 * @file http_server.cpp
 * @brief
 * @version 0.2
 * @date 2026-10-30
 *
 */
#include "http_server.h"
#include <Arduino.h>
#include <SD.h>
#include <esp_camera.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "../app_config.h"
#include "../hal/hal.h"
#include "../net/net_manager.h"
#include "../parking/parking.h"
#include "../storage/storage.h"


static AsyncWebServer* _server = nullptr;


// From the user demo camera apis: sends a malloc'ed buffer and frees it
class AsyncBufferResponse: public AsyncAbstractResponse {
    private:
        uint8_t * _buf;
        size_t _len;
        size_t _index;
    public:
        AsyncBufferResponse(uint8_t * buf, size_t len, const char * contentType){
            _buf = buf;
            _len = len;
            _callback = nullptr;
            _code = 200;
            _contentLength = _len;
            _contentType = contentType;
            _index = 0;
        }
        ~AsyncBufferResponse(){
            if(_buf != nullptr){
                free(_buf);
            }
        }
        bool _sourceValid() const { return _buf != nullptr; }
        virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override{
            size_t ret = _content(buf, maxLen, _index);
            if(ret != RESPONSE_TRY_AGAIN){
                _index += ret;
            }
            return ret;
        }
        size_t _content(uint8_t *buffer, size_t maxLen, size_t index){
            memcpy(buffer, _buf+index, maxLen);
            if((index+maxLen) == _len){
                free(_buf);
                _buf = nullptr;
            }
            return maxLen;
        }
};


static const char _index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="it"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ParkCam</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#f4f4f5;color:#18181b}
main{max-width:720px;margin:0 auto;padding:16px}
h1{font-size:20px}h2{font-size:16px;margin:0 0 8px}h3{font-size:15px;margin:22px 0 4px}
.card{background:#fff;border-radius:8px;padding:16px;margin-bottom:16px;box-shadow:0 1px 2px #0002}
.warn{background:#fef3c7}
img{width:100%;border-radius:4px;background:#000;min-height:120px;display:block}
.row{margin:4px 0;font-size:14px}
label{display:block;margin:10px 0 4px;font-size:14px}
input,select{width:100%;padding:8px;box-sizing:border-box;border:1px solid #d4d4d8;border-radius:4px;font-size:15px}
input[type=checkbox]{width:auto;margin-right:6px}
button{margin:12px 8px 0 0;padding:10px 16px;border:0;border-radius:4px;background:#2563eb;color:#fff;font-size:15px}
button.sec{background:#e4e4e7;color:#18181b}
.files div{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:6px 0;border-bottom:1px solid #eee;font-size:14px;word-break:break-all}
.files a{color:#2563eb}
.files button{margin:0;padding:4px 10px;background:#dc2626;font-size:13px}
small{display:block;margin-top:8px;color:#71717a}
</style></head><body><main>
<h1>UnitCamS3 Parking Monitor</h1>
<div class="card warn" id="warn" hidden></div>
<div class="card"><h2>Stato</h2><div id="status">...</div></div>
<div class="card">
<img id="live" alt="">
<button type="button" id="liveBtn" onclick="toggleLive()">Avvia anteprima</button>
<small>Serve per puntare la camera. Tenerla aperta consuma e scalda il modulo.</small>
</div>
<div class="card">
<h2>Eventi e foto</h2>
<div class="files" id="files"></div>
<button type="button" class="sec" id="moreBtn" onclick="showMore()" hidden>Altri</button>
<small>I file .avi si aprono con VLC. Il download è lento (scheda SD in SPI): meglio togliere la scheda per scaricare molti video.</small>
</div>
<form class="card" method="post" action="/api/v1/config">
<h2>Impostazioni</h2>
<h3>Video</h3>
<label>Risoluzione</label>
<select name="frameSize" id="frameSize">
<option value="QVGA">QVGA 320x240</option><option value="CIF">CIF 400x296</option>
<option value="HVGA">HVGA 480x320</option><option value="VGA">VGA 640x480</option>
<option value="SVGA">SVGA 800x600</option><option value="XGA">XGA 1024x768</option>
<option value="HD">HD 1280x720</option><option value="SXGA">SXGA 1280x1024</option>
<option value="UXGA">UXGA 1600x1200</option>
</select>
<label>Qualità JPEG (6 = migliore, 63 = file più piccoli)</label><input name="jpegQuality" id="jpegQuality" type="number" min="6" max="63">
<label>Fotogrammi al secondo</label><input name="fps" id="fps" type="number" min="1" max="25">
<label>Spazio libero minimo, sotto cancella i video più vecchi (MB)</label><input name="minFreeMb" id="minFreeMb" type="number" min="100" max="65536">
<label><input type="checkbox" name="vflip" id="vflip">Capovolgi in verticale</label>
<label><input type="checkbox" name="hmirror" id="hmirror">Specchia in orizzontale</label>
<h3>Ingressi sulla porta Grove</h3>
<label>Quadro acceso (ACC): sorveglianza in pausa mentre guidi</label>
<select name="accPin" id="accPin"><option value="-1">Non collegato</option><option value="19">G19</option><option value="20">G20</option></select>
<label><input type="checkbox" name="accActiveHigh" id="accActiveHigh">ACC attivo alto (spento con il fotoaccoppiatore verso GND)</label>
<label>Sensore di risveglio (vibrazione / PIR)</label>
<select name="triggerPin" id="triggerPin"><option value="-1">Non collegato</option><option value="19">G19</option><option value="20">G20</option></select>
<label><input type="checkbox" name="triggerActiveHigh" id="triggerActiveHigh">Sensore attivo alto</label>
<small>Usare G19/G20 come ingressi disattiva la USB della porta Grove: per caricare il firmware scollega i sensori.</small>
<h3>Sorveglianza</h3>
<label>Controllo movimento con la camera ogni (s, 0 = spento)</label><input name="parkCheckSec" id="parkCheckSec" type="number" min="0" max="3600">
<label>Differenza di luminosità per cella (5-100)</label><input name="motionThreshold" id="motionThreshold" type="number" min="5" max="100">
<label>Area minima cambiata (%)</label><input name="motionAreaPct" id="motionAreaPct" type="number" min="1" max="100">
<label>Durata clip evento (s)</label><input name="eventClipSec" id="eventClipSec" type="number" min="5" max="300">
<label>Massimo eventi all'ora</label><input name="maxEventsPerHour" id="maxEventsPerHour" type="number" min="1" max="60">
<label>Foto time-lapse ogni (min, 0 = spento)</label><input name="timelapseMin" id="timelapseMin" type="number" min="0" max="1440">
<h3>Wi-Fi</h3>
<label>Nome rete (vuoto = ParkCam-xxxxxx)</label><input name="apSsid" id="apSsid" maxlength="32">
<label>Password (8-63 caratteri, vuoto = mantieni quella attuale)</label><input name="apPass" type="password" maxlength="63" autocomplete="new-password">
<label><input type="checkbox" name="apOpen" id="apOpen">Rete aperta senza password (sconsigliato)</label>
<label>Spegni il Wi-Fi dopo (min senza telefono collegato)</label><input name="apMinutes" id="apMinutes" type="number" min="1" max="120">
<h3>Temperatura (sensore interno del chip)</h3>
<label>Salta i controlli sopra (°C)</label><input name="tempStopC" id="tempStopC" type="number" min="50" max="100">
<button>Salva e riavvia</button>
</form>
</main>
<script>
const $=id=>document.getElementById(id);
let live=false,files=[],shown=0,filled=false;
function esc(t){return String(t).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function fill(c){
  for(const k in c){const e=$(k);if(!e)continue;if(e.type==='checkbox')e.checked=c[k];else e.value=c[k];}
}
function refresh(){
  fetch('/api/v1/status').then(r=>r.json()).then(d=>{
    const s=d.stats,w=[];
    if(!d.sdOk)w.push('Scheda SD assente o in errore: gli eventi non verranno salvati.');
    if(d.apOpen)w.push('Wi-Fi aperto: chiunque vicino all\'auto può vedere e cancellare i video.');
    else if(d.apDefaultPass)w.push('Password Wi-Fi predefinita: cambiala nelle impostazioni.');
    if(d.temp>d.config.tempStopC-10)w.push('Temperatura alta: '+d.temp.toFixed(0)+' °C.');
    $('warn').innerHTML=w.map(esc).join('<br>');$('warn').hidden=!w.length;
    const rows=[
      ['Scheda SD',d.sdOk?d.sdFreeMb+' MB liberi su '+d.sdTotalMb+' MB':'non disponibile'],
      ['Temperatura chip',d.temp.toFixed(1)+' °C'],
      ['Ora',d.timeValid?d.time:'non impostata'],
      ['Quadro (ACC)',d.acc===null?'non collegato':(d.acc?'acceso (sorveglianza in pausa)':'spento')],
      ['Da accensione',s.wakes+' risvegli, '+(s.wakes?Math.round(s.awakeMs/s.wakes):0)+' ms medi da sveglia, '+s.events+' eventi, '+s.photos+' foto, '+s.skipped+' eventi oltre il limite'],
      ['Wi-Fi',d.apSsid+', '+d.clients+' collegati, si spegne dopo '+d.config.apMinutes+' min senza telefono, poi parte la sorveglianza'],
      ['Acceso da',Math.floor(d.uptime/60)+' min']];
    $('status').innerHTML=rows.map(r=>'<div class="row">'+r[0]+': <b>'+esc(r[1])+'</b></div>').join('');
    if(!filled){filled=true;fill(d.config);}
  }).catch(()=>{});
}
function toggleLive(){
  live=!live;$('liveBtn').textContent=live?'Ferma anteprima':'Avvia anteprima';
  if(live)nextFrame();else $('live').src='data:,';
}
function nextFrame(){if(live)$('live').src='/api/v1/capture?t='+Date.now();}
$('live').onload=()=>setTimeout(nextFrame,200);
$('live').onerror=()=>setTimeout(nextFrame,1000);
function loadFiles(){
  $('files').textContent='Caricamento...';$('moreBtn').hidden=true;
  fetch('/api/v1/files').then(r=>r.json()).then(l=>{
    files=l.sort((a,b)=>parseInt(b.slice(1))-parseInt(a.slice(1)));shown=0;
    $('files').textContent=files.length?'':'Nessun file.';showMore();
  }).catch(()=>{$('files').textContent='Errore.';});
}
function showMore(){
  const html=files.slice(shown,shown+50).map(n=>{const p='/park/'+n;
    return '<div><a href="/api/v1/download?path='+encodeURIComponent(p)+'">'+esc(n)+'</a><button type="button" data-p="'+esc(p)+'" onclick="del(this)">Elimina</button></div>';}).join('');
  $('files').insertAdjacentHTML('beforeend',html);shown+=50;$('moreBtn').hidden=shown>=files.length;
}
function del(b){
  if(!confirm('Eliminare '+b.dataset.p+'?'))return;
  fetch('/api/v1/delete',{method:'POST',body:new URLSearchParams({path:b.dataset.p})})
    .then(r=>{if(r.ok)b.parentNode.remove();else r.text().then(alert);});
}
// No battery backed clock on the camera: it takes the phone's time
fetch('/api/v1/time',{method:'POST',body:new URLSearchParams({epoch:Math.floor(Date.now()/1000),tz:new Date().getTimezoneOffset()})})
  .finally(()=>{refresh();loadFiles();});
setInterval(refresh,5000);
</script>
</body></html>
)rawliteral";


static void _handle_index(AsyncWebServerRequest* request)
{
    request->send_P(200, "text/html", _index_html);
}


static void _send_error(AsyncWebServerRequest* request, int code, const char* message)
{
    request->send(code, "text/plain; charset=utf-8", message);
}


static void _handle_status(AsyncWebServerRequest* request)
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();
    auto stats = Parking::get_stats();

    DynamicJsonDocument doc(3072);
    doc["sdOk"] = hal->isSdCardValid();
    doc["sdTotalMb"] = (uint32_t)(hal->sdTotalBytes() >> 20);
    doc["sdFreeMb"] = (uint32_t)(hal->sdFreeBytes() >> 20);
    doc["temp"] = hal->getTemperature();
    doc["timeValid"] = hal->isTimeValid();
    doc["time"] = hal->getTimeString("%d/%m/%Y %H:%M:%S");
    doc["uptime"] = millis() / 1000;
    if (config.acc_pin >= 0)
        doc["acc"] = hal->isAccOn();
    else
        doc["acc"] = nullptr;
    doc["apSsid"] = NetManager::get_ap_ssid();
    doc["apOpen"] = config.ap_password.length() < 8;
    doc["apDefaultPass"] = config.ap_password == APP_DEFAULT_AP_PASSWORD;
    doc["clients"] = NetManager::get_client_count();
    doc["freeHeap"] = ESP.getFreeHeap();

    JsonObject s = doc.createNestedObject("stats");
    s["wakes"] = stats.wakes;
    s["awakeMs"] = stats.awake_ms;
    s["events"] = stats.events;
    s["photos"] = stats.photos;
    s["skipped"] = stats.skipped_events;

    JsonObject c = doc.createNestedObject("config");
    c["frameSize"] = config.frame_size;
    c["jpegQuality"] = config.jpeg_quality;
    c["fps"] = config.fps;
    c["minFreeMb"] = config.min_free_mb;
    c["vflip"] = config.vflip;
    c["hmirror"] = config.hmirror;
    c["accPin"] = config.acc_pin;
    c["accActiveHigh"] = config.acc_active_high;
    c["triggerPin"] = config.trigger_pin;
    c["triggerActiveHigh"] = config.trigger_active_high;
    c["parkCheckSec"] = config.park_check_sec;
    c["motionThreshold"] = config.motion_threshold;
    c["motionAreaPct"] = config.motion_area_pct;
    c["eventClipSec"] = config.event_clip_sec;
    c["maxEventsPerHour"] = config.max_events_per_hour;
    c["timelapseMin"] = config.timelapse_min;
    c["apSsid"] = config.ap_ssid;
    c["apOpen"] = config.ap_password.length() < 8;
    c["apMinutes"] = config.ap_minutes;
    c["tempStopC"] = config.temp_stop_c;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}


static void _handle_capture(AsyncWebServerRequest* request)
{
    size_t len = 0;
    uint8_t* buf = nullptr;

    if (HAL::hal::GetHal()->isCamRunning())
    {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb != nullptr)
        {
            buf = (uint8_t*)ps_malloc(fb->len);
            if (buf != nullptr)
            {
                memcpy(buf, fb->buf, fb->len);
                len = fb->len;
            }
            esp_camera_fb_return(fb);
        }
    }

    if (buf == nullptr)
    {
        _send_error(request, 503, "no frame");
        return;
    }

    AsyncBufferResponse* response = new AsyncBufferResponse(buf, len, "image/jpeg");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}


static void _handle_files(AsyncWebServerRequest* request)
{
    if (!HAL::hal::GetHal()->isSdCardValid())
    {
        request->send(200, "application/json", "[]");
        return;
    }
    request->send(200, "application/json", Storage::list_json(APP_DIR_PARK));
}


static void _handle_download(AsyncWebServerRequest* request)
{
    String path = request->hasParam("path") ? request->getParam("path")->value() : "";
    if (!Storage::is_media_path(path) || !HAL::hal::GetHal()->isSdCardValid() || !SD.exists(path))
    {
        _send_error(request, 404, "File non trovato");
        return;
    }
    request->send(SD, path, path.endsWith(".jpg") ? "image/jpeg" : "video/x-msvideo", true);
}


static void _handle_delete(AsyncWebServerRequest* request)
{
    String path = request->hasParam("path", true) ? request->getParam("path", true)->value() : "";
    if (!Storage::is_media_path(path))
    {
        _send_error(request, 400, "Percorso non valido");
        return;
    }
    if (!HAL::hal::GetHal()->isSdCardValid() || !SD.remove(path))
    {
        _send_error(request, 404, "Eliminazione non riuscita");
        return;
    }
    request->send(200, "text/plain", "ok");
}


static void _handle_set_time(AsyncWebServerRequest* request)
{
    if (!request->hasParam("epoch", true))
    {
        _send_error(request, 400, "missing epoch");
        return;
    }

    time_t epoch = request->getParam("epoch", true)->value().toInt();
    int tz = request->hasParam("tz", true) ? request->getParam("tz", true)->value().toInt() : 0;
    if (epoch < APP_TIME_VALID_AFTER)
    {
        _send_error(request, 400, "bad epoch");
        return;
    }

    HAL::hal::GetHal()->setTime(epoch, constrain(tz, -14 * 60, 14 * 60));
    request->send(200, "text/plain", "ok");
}


static int _get_int(AsyncWebServerRequest* request, const char* name, int value, int low, int high)
{
    if (request->hasParam(name, true))
        value = constrain(request->getParam(name, true)->value().toInt(), low, high);
    return value;
}


static String _get_string(AsyncWebServerRequest* request, const char* name, const String& value)
{
    return request->hasParam(name, true) ? request->getParam(name, true)->value() : value;
}


static bool _is_valid_ssid(const String& ssid)
{
    if (ssid.length() > 32)
        return false;
    for (size_t i = 0; i < ssid.length(); i++)
    {
        if (ssid[i] < 32 || ssid[i] > 126)
            return false;
    }
    return true;
}


static void _handle_set_config(AsyncWebServerRequest* request)
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();

    // Check first, nothing changes on an error
    int acc_pin = _get_int(request, "accPin", config.acc_pin, -1, 48);
    int trigger_pin = _get_int(request, "triggerPin", config.trigger_pin, -1, 48);
    String frame_size = _get_string(request, "frameSize", config.frame_size);
    String ap_ssid = _get_string(request, "apSsid", config.ap_ssid);
    String ap_pass = _get_string(request, "apPass", "");

    if (!HAL::hal::isValidFrameSize(frame_size))
        return _send_error(request, 400, "Valore non valido");
    if (!HAL::hal::isValidInputPin(acc_pin) || !HAL::hal::isValidInputPin(trigger_pin) || (acc_pin >= 0 && acc_pin == trigger_pin))
        return _send_error(request, 400, "ACC e sensore devono usare pin diversi (G19 / G20)");
    if (ap_pass != "" && (ap_pass.length() < 8 || ap_pass.length() > 63))
        return _send_error(request, 400, "La password Wi-Fi deve avere da 8 a 63 caratteri");
    if (!_is_valid_ssid(ap_ssid))
        return _send_error(request, 400, "Nome rete non valido");

    config.frame_size = frame_size;
    config.jpeg_quality = _get_int(request, "jpegQuality", config.jpeg_quality, 6, 63);
    config.fps = _get_int(request, "fps", config.fps, 1, 25);
    config.min_free_mb = _get_int(request, "minFreeMb", config.min_free_mb, 100, 65536);
    // Unchecked boxes are not posted
    config.vflip = request->hasParam("vflip", true);
    config.hmirror = request->hasParam("hmirror", true);

    config.acc_pin = acc_pin;
    config.acc_active_high = request->hasParam("accActiveHigh", true);
    config.trigger_pin = trigger_pin;
    config.trigger_active_high = request->hasParam("triggerActiveHigh", true);

    config.park_check_sec = _get_int(request, "parkCheckSec", config.park_check_sec, 0, 3600);
    config.motion_threshold = _get_int(request, "motionThreshold", config.motion_threshold, 5, 100);
    config.motion_area_pct = _get_int(request, "motionAreaPct", config.motion_area_pct, 1, 100);
    config.event_clip_sec = _get_int(request, "eventClipSec", config.event_clip_sec, 5, 300);
    config.max_events_per_hour = _get_int(request, "maxEventsPerHour", config.max_events_per_hour, 1, 60);
    config.timelapse_min = _get_int(request, "timelapseMin", config.timelapse_min, 0, 1440);

    config.ap_ssid = ap_ssid;
    // A typed password wins over the "open" box, an empty one keeps the current password
    if (ap_pass != "")
        config.ap_password = ap_pass;
    else if (request->hasParam("apOpen", true))
        config.ap_password = "";
    config.ap_minutes = _get_int(request, "apMinutes", config.ap_minutes, 1, 120);

    config.temp_stop_c = _get_int(request, "tempStopC", config.temp_stop_c, 50, 100);

    if (!hal->saveConfig())
        return _send_error(request, 500, "Salvataggio non riuscito");
    hal->printConfig();

    request->send(200, "text/html",
                  "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<meta http-equiv=\"refresh\" content=\"15;url=/\"></head><body style=\"font-family:system-ui,sans-serif;padding:16px\">"
                  "<p>Salvato, riavvio...</p><p>Se hai cambiato nome o password del Wi-Fi ricollega il telefono alla nuova rete. "
                  "Il Wi-Fi si riaccende solo all'accensione o quando giri la chiave (se l'ACC è collegato).</p></body></html>");

    hal->requestReboot();
}


void HttpServer::start()
{
    _server = new AsyncWebServer(APP_HTTP_PORT);

    _server->on("/", HTTP_GET, _handle_index);
    _server->on("/api/v1/status", HTTP_GET, _handle_status);
    _server->on("/api/v1/capture", HTTP_GET, _handle_capture);
    _server->on("/api/v1/files", HTTP_GET, _handle_files);
    _server->on("/api/v1/download", HTTP_GET, _handle_download);
    _server->on("/api/v1/delete", HTTP_POST, _handle_delete);
    _server->on("/api/v1/time", HTTP_POST, _handle_set_time);
    _server->on("/api/v1/config", HTTP_POST, _handle_set_config);

    _server->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "not found");
    });

    _server->begin();
    log_i("http server started");
}
