/*
 * web_ui.h — Premium OTA Web Interface
 * ======================================
 * Purpose      : Single-file embedded HTML/CSS/JS served by the ESP32 HTTP
 *                server.  Designed to be fast to load over Wi-Fi, visually
 *                polished, and fully functional on mobile and desktop.
 *
 * Features:
 *   - Drag-and-drop + click-to-browse upload zone
 *   - Client-side .bin file validation (magic byte check in JS)
 *   - Animated 5-step progress pipeline (Connect→Validate→Flash→Verify→Done)
 *   - Real-time KB/s + estimated time remaining
 *   - Server-sent progress polling via /status every 1 s during flash
 *   - Device info panel populated from /info on page load
 *   - Cyberpunk dark theme matching the TFT display aesthetic
 *   - Fully responsive (320 px → desktop)
 *   - Retry button on failure without page reload
 *
 * Toolchain notes displayed inline:
 *   ESP-IDF   → build/<project>.bin
 *   PlatformIO → .pio/build/<env>/<project>.bin
 *   Arduino   → Sketch → Export Compiled Binary → <sketch>.ino.bin
 */
#pragma once

const char* WEB_UI_HTML =
/* ========================  HTML STARTS  ============================ */
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32-S3 OTA Launcher</title>"
"<style>"
/* ---- Reset & base ---- */
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:#020305;color:#c9d1e0;font-family:'Segoe UI',system-ui,sans-serif;"
"min-height:100vh;display:flex;flex-direction:column;align-items:center;"
"padding:20px 12px;}"
/* ---- Typography ---- */
"h1{font-size:1.3rem;font-weight:700;letter-spacing:.08em;color:#03BFF0;"
"margin-bottom:4px;text-shadow:0 0 12px rgba(3,191,240,0.5);}"
".subtitle{font-size:.75rem;color:#4a5568;margin-bottom:20px;"
"letter-spacing:.12em;text-transform:uppercase;}"
/* ---- Header bar ---- */
".header{width:100%;max-width:520px;display:flex;align-items:center;"
"justify-content:space-between;margin-bottom:16px;"
"border-bottom:1px solid #0d2030;padding-bottom:10px;}"
".logo{display:flex;align-items:center;gap:10px;}"
".logo-dot{width:8px;height:8px;border-radius:50%;background:#03BFF0;"
"box-shadow:0 0 8px #03BFF0;animation:pulse 2s infinite;}"
"@keyframes pulse{0%,100%{opacity:1;transform:scale(1);}50%{opacity:.5;transform:scale(1.4);}}"
/* ---- Card ---- */
".card{background:rgba(13,20,31,0.95);border:1px solid #0d2535;"
"border-radius:14px;padding:24px;width:100%;max-width:520px;"
"box-shadow:0 0 40px rgba(3,191,240,0.06),0 8px 32px rgba(0,0,0,.7);"
"backdrop-filter:blur(8px);margin-bottom:14px;}"
/* ---- Device info grid ---- */
".info-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:0;}"
".info-cell{background:#060d14;border:1px solid #0d2535;border-radius:8px;"
"padding:8px 10px;}"
".info-label{font-size:.65rem;color:#4a5568;text-transform:uppercase;"
"letter-spacing:.1em;}"
".info-value{font-size:.85rem;color:#03BFF0;font-weight:600;margin-top:2px;}"
/* ---- Drop zone ---- */
".drop-zone{border:2px dashed #0d2535;border-radius:10px;padding:28px 20px;"
"text-align:center;cursor:pointer;transition:.25s;margin:16px 0;"
"position:relative;overflow:hidden;}"
".drop-zone.hover,.drop-zone:hover{border-color:#03BFF0;"
"background:rgba(3,191,240,0.04);}"
".drop-zone.has-file{border-color:#00c853;border-style:solid;"
"background:rgba(0,200,83,0.04);}"
".drop-zone.error{border-color:#f44336;border-style:solid;"
"background:rgba(244,67,54,0.04);}"
".dz-icon{font-size:2.2rem;margin-bottom:8px;}"
".dz-main{font-size:.9rem;color:#8892a0;margin-bottom:4px;}"
".dz-sub{font-size:.72rem;color:#3d4a5a;}"
".dz-sub a{color:#03BFF0;text-decoration:none;}"
"#fileInput{display:none;}"
/* ---- Step pipeline ---- */
".steps{display:flex;align-items:center;justify-content:center;"
"gap:0;margin:16px 0;}"
".step{display:flex;flex-direction:column;align-items:center;gap:4px;flex:1;}"
".step-dot{width:22px;height:22px;border-radius:50%;background:#0d2535;"
"border:2px solid #0d2535;display:flex;align-items:center;justify-content:center;"
"font-size:.65rem;transition:.4s;position:relative;z-index:1;}"
".step-line{flex:1;height:2px;background:#0d2535;transition:.4s;"
"margin-top:-18px;position:relative;}"
".step-label{font-size:.6rem;color:#3d4a5a;text-transform:uppercase;"
"letter-spacing:.08em;}"
".step.done .step-dot{background:#00c853;border-color:#00c853;color:#000;}"
".step.done .step-line{background:#00c853;}"
".step.active .step-dot{background:#03BFF0;border-color:#03BFF0;"
"color:#000;animation:stepPulse .8s infinite alternate;}"
".step.done+.step .step-line,"
".step.active+.step .step-line{background:#0d2535;}"
"@keyframes stepPulse{from{box-shadow:0 0 0 0 rgba(3,191,240,.6);}to{box-shadow:0 0 0 8px rgba(3,191,240,0);}}"
/* ---- Progress bar ---- */
".prog-wrap{width:100%;background:#060d14;border-radius:999px;"
"height:6px;margin:12px 0;overflow:hidden;display:none;}"
".prog-bar{height:100%;width:0%;border-radius:999px;transition:width .3s ease;"
"background:linear-gradient(90deg,#0d8fd4,#03BFF0,#00e5ff);"
"position:relative;}"
".prog-bar::after{content:'';position:absolute;top:0;right:0;bottom:0;width:30px;"
"background:linear-gradient(90deg,transparent,rgba(255,255,255,.25));}"
/* ---- Stat row ---- */
".stat-row{display:flex;justify-content:space-between;font-size:.72rem;"
"color:#4a5568;margin-top:4px;min-height:16px;}"
".stat-row span{color:#8892a0;}"
/* ---- Status message ---- */
"#status{font-size:.82rem;margin-top:10px;min-height:20px;text-align:center;"
"color:#8892a0;transition:.3s;}"
"#status.ok{color:#00c853;}"
"#status.err{color:#f44336;}"
"#status.info{color:#03BFF0;}"
/* ---- Buttons ---- */
".btn-row{display:flex;gap:10px;margin-top:16px;}"
".btn{flex:1;padding:11px 16px;border-radius:8px;border:none;cursor:pointer;"
"font-size:.85rem;font-weight:600;letter-spacing:.04em;transition:.2s;"
"text-transform:uppercase;}"
".btn-primary{background:#03BFF0;color:#020305;}"
".btn-primary:hover{background:#00e5ff;box-shadow:0 0 16px rgba(3,191,240,0.4);}"
".btn-primary:disabled{background:#0d2535;color:#3d4a5a;cursor:not-allowed;}"
".btn-ghost{background:transparent;color:#4a5568;border:1px solid #0d2535;}"
".btn-ghost:hover{color:#8892a0;border-color:#4a5568;}"
/* ---- Toolchain hints ---- */
".hints{font-size:.7rem;color:#3d4a5a;line-height:1.6;}"
".hints strong{color:#4a5568;}"
"code{background:#060d14;color:#03BFF0;padding:1px 5px;"
"border-radius:3px;font-size:.68rem;}"
/* ---- Scrollbar ---- */
"::-webkit-scrollbar{width:4px;}"
"::-webkit-scrollbar-track{background:#020305;}"
"::-webkit-scrollbar-thumb{background:#0d2535;border-radius:2px;}"
"</style>"
"</head>"
"<body>"

/* ===== HEADER ===== */
"<div class='header'>"
"<div class='logo'>"
"<div class='logo-dot'></div>"
"<div><div style='font-size:.85rem;font-weight:700;color:#c9d1e0;"
"letter-spacing:.06em;'>ESP32-S3 Launcher</div>"
"<div style='font-size:.62rem;color:#3d4a5a;letter-spacing:.1em;'>OTA UPDATE PORTAL</div></div>"
"</div>"
"<div id='ap-badge' style='font-size:.65rem;color:#4a5568;"
"border:1px solid #0d2535;border-radius:5px;padding:3px 8px;'>"
"AP: ESP32_OTA_Launcher</div>"
"</div>"

/* ===== DEVICE INFO CARD ===== */
"<div class='card'>"
"<div style='font-size:.7rem;color:#4a5568;text-transform:uppercase;"
"letter-spacing:.1em;margin-bottom:10px;'>Device Info</div>"
"<div class='info-grid'>"
"<div class='info-cell'><div class='info-label'>Chip</div>"
"<div class='info-value' id='i-chip'>—</div></div>"
"<div class='info-cell'><div class='info-label'>Flash</div>"
"<div class='info-value' id='i-flash'>—</div></div>"
"<div class='info-cell'><div class='info-label'>Free Heap</div>"
"<div class='info-value' id='i-heap'>—</div></div>"
"<div class='info-cell'><div class='info-label'>IDF Version</div>"
"<div class='info-value' id='i-idf'>—</div></div>"
"<div class='info-cell' style='grid-column:1/-1;'>"
"<div class='info-label'>MAC Address</div>"
"<div class='info-value' id='i-mac'>—</div></div>"
"</div>"
"</div>"

/* ===== UPLOAD CARD ===== */
"<div class='card'>"

/* Drop zone */
"<div class='drop-zone' id='dz'>"
"<div class='dz-icon'>📦</div>"
"<div class='dz-main' id='dz-main'>Drop firmware .bin here</div>"
"<div class='dz-sub' id='dz-sub'>or <a id='browse-link' href='#'>browse file</a> &nbsp;·&nbsp; "
"ESP-IDF / PlatformIO / Arduino</div>"
"<input type='file' id='fileInput' accept='.bin'>"
"</div>"

/* Step pipeline */
"<div class='steps'>"
"<div class='step' id='s0'>"
"<div class='step-dot'>1</div>"
"<div class='step-label'>Select</div></div>"
"<div class='step-line' id='sl0'></div>"
"<div class='step' id='s1'>"
"<div class='step-dot'>2</div>"
"<div class='step-label'>Validate</div></div>"
"<div class='step-line' id='sl1'></div>"
"<div class='step' id='s2'>"
"<div class='step-dot'>3</div>"
"<div class='step-label'>Upload</div></div>"
"<div class='step-line' id='sl2'></div>"
"<div class='step' id='s3'>"
"<div class='step-dot'>4</div>"
"<div class='step-label'>Flash</div></div>"
"<div class='step-line' id='sl3'></div>"
"<div class='step' id='s4'>"
"<div class='step-dot'>✓</div>"
"<div class='step-label'>Done</div></div>"
"</div>"

/* Progress bar */
"<div class='prog-wrap' id='prog-wrap'>"
"<div class='prog-bar' id='prog-bar'></div>"
"</div>"

/* Stats row */
"<div class='stat-row'>"
"<span id='stat-size'>—</span>"
"<span id='stat-speed'>—</span>"
"<span id='stat-eta'>—</span>"
"</div>"

/* Status message */
"<div id='status'>Ready — select a .bin file to begin</div>"

/* Action buttons */
"<div class='btn-row'>"
"<button class='btn btn-primary' id='flash-btn' disabled>Flash Firmware</button>"
"<button class='btn btn-ghost' id='retry-btn' style='display:none;'"
"onclick='location.reload()'>Retry</button>"
"</div>"

/* Toolchain hints */
"<details style='margin-top:18px;'>"
"<summary style='font-size:.72rem;color:#3d4a5a;cursor:pointer;"
"user-select:none;list-style:none;'>Where is my .bin file? ▸</summary>"
"<div class='hints' style='margin-top:8px;padding-left:4px;'>"
"<strong>ESP-IDF</strong> &nbsp;→ <code>build/&lt;project&gt;.bin</code><br>"
"<strong>PlatformIO</strong> → <code>.pio/build/&lt;env&gt;/firmware.bin</code><br>"
"<strong>Arduino IDE 2</strong> → Sketch › Export Compiled Binary → <code>&lt;sketch&gt;.ino.bin</code>"
"</div></details>"
"</div>"

/* ===== JAVASCRIPT ===== */
"<script>"

/* ---- Utilities ---- */
"const $=id=>document.getElementById(id);"
"const setClass=(el,cls)=>{el.className=cls;};"
"const fmt=(n,u)=>n.toFixed(1)+u;"

/* ---- Step helpers ---- */
"const STEPS=['s0','s1','s2','s3','s4'];"
"function setStep(n){"
"STEPS.forEach((id,i)=>{"
"const el=$(id);"
"if(i<n){el.classList.remove('active');el.classList.add('done');}"
"else if(i===n){el.classList.remove('done');el.classList.add('active');}"
"else{el.classList.remove('active','done');}"
"});}"

/* ---- Status helper ---- */
"function setStatus(msg,cls=''){"
"const s=$('status');s.textContent=msg;s.className=cls;}"

/* ---- Format file size ---- */
"function fmtSize(b){"
"if(b>=1048576)return(b/1048576).toFixed(2)+' MB';"
"return(b/1024).toFixed(1)+' KB';}"

/* ---- Load device info ---- */
"async function loadInfo(){"
"try{"
"const r=await fetch('/info');const d=await r.json();"
"$('i-chip').textContent=d.chip+' ('+d.cores+' cores)';"
"$('i-flash').textContent=d.flash_mb+' MB';"
"$('i-heap').textContent=d.free_heap_kb+' KB';"
"$('i-idf').textContent=d.idf;"
"$('i-mac').textContent=d.mac;"
"$('ap-badge').textContent='AP: '+d.ap_ssid;"
"}catch(e){console.warn('Info load failed',e);}}"
"loadInfo();"

/* ---- Drop zone wiring ---- */
"const dz=$('dz'),fi=$('fileInput');"
"$('browse-link').onclick=e=>{e.preventDefault();fi.click();};"
"dz.onclick=e=>{if(e.target!==$('browse-link'))fi.click();};"
"dz.ondragover=e=>{e.preventDefault();dz.classList.add('hover');};"
"dz.ondragleave=()=>dz.classList.remove('hover');"
"dz.ondrop=e=>{e.preventDefault();dz.classList.remove('hover');"
"if(e.dataTransfer.files[0])handleFile(e.dataTransfer.files[0]);};"
"fi.onchange=()=>{if(fi.files[0])handleFile(fi.files[0]);};"

/* ---- File selection & client-side validation ---- */
"let selFile=null,uploadStart=0;"
"async function handleFile(f){"
"selFile=null;$('flash-btn').disabled=true;"
"$('retry-btn').style.display='none';"
"dz.classList.remove('error','has-file');"
"if(!f.name.toLowerCase().endsWith('.bin')){"
"setStatus('⚠ Select a .bin file — not a .elf, .zip, or sketch folder','err');"
"dz.classList.add('error');return;}"

/* Read first 4 bytes and check magic 0xE9 */
"const slice=f.slice(0,4);"
"const ab=await slice.arrayBuffer();"
"const u8=new Uint8Array(ab);"
"if(u8[0]!==0xE9){"
"setStatus('⚠ Invalid binary — magic byte mismatch (got 0x'+u8[0].toString(16).toUpperCase()+'). This is not an ESP32 firmware.','err');"
"dz.classList.add('error');return;}"

/* Valid file */
"selFile=f;"
"dz.classList.add('has-file');"
"$('dz-main').textContent='✓  '+f.name;"
"$('dz-sub').textContent=fmtSize(f.size)+' · ESP32 binary validated (magic 0xE9)';"
"$('stat-size').textContent=fmtSize(f.size);"
"$('flash-btn').disabled=false;"
"setStep(0);"
"setStatus('File validated — ready to flash','ok');}"

/* ---- Upload + progress polling ---- */
"let pollTimer=null;"

"function stopPoll(){if(pollTimer){clearInterval(pollTimer);pollTimer=null;}}"

"async function pollStatus(){"
"try{"
"const r=await fetch('/status');const d=await r.json();"
"if(!d.ok)return;"
"const p=d.progress||0;"
"$('prog-bar').style.width=p+'%';"
"if(d.kbps>0)$('stat-speed').textContent=fmt(d.kbps,' KB/s');"
"if(d.state==='success'){"
"stopPoll();setStep(5);$('prog-bar').style.width='100%';"
"setStatus('✓  Firmware flashed! Device is rebooting...','ok');"
"setStep(4);}"
"if(d.state==='failed'){"
"stopPoll();setStatus('✗  '+d.msg,'err');"
"$('retry-btn').style.display='';}"
"}catch(e){}}"

"$('flash-btn').onclick=async()=>{"
"if(!selFile)return;"
"$('flash-btn').disabled=true;"
"$('retry-btn').style.display='none';"
"$('prog-wrap').style.display='block';"
"$('prog-bar').style.width='0%';"
"uploadStart=Date.now();"
"setStep(1);setStatus('Uploading firmware...','info');"

/* Start polling device-side flash progress every 1 s */
"stopPoll();"
"pollTimer=setInterval(pollStatus,1000);"

/* XHR for upload progress tracking */
"const xhr=new XMLHttpRequest();"
"let lastLoaded=0,lastTime=uploadStart;"

"xhr.upload.onprogress=e=>{"
"const p=(e.loaded/e.total)*100;"
"$('prog-bar').style.width=p+'%';"
"const now=Date.now();"
"const dt=(now-lastTime)/1000;"
"if(dt>0.2){"
"const kbps=((e.loaded-lastLoaded)/1024)/dt;"
"$('stat-speed').textContent=fmt(kbps,' KB/s');"
"const remain=(e.total-e.loaded)/1024/kbps;"
"$('stat-eta').textContent='~'+Math.ceil(remain)+'s left';"
"lastLoaded=e.loaded;lastTime=now;}"
"setStep(2);};"

"xhr.onload=()=>{"
"stopPoll();"
"if(xhr.status===200){"
"try{const d=JSON.parse(xhr.responseText);"
"setStatus(d.msg||'Flashing complete!','ok');"
"}catch{setStatus('Upload complete — device flashing...','ok');}"
"$('prog-bar').style.width='100%';"
"$('stat-speed').textContent='';$('stat-eta').textContent='';"
"setStep(4);"
/* Poll a few more times to catch final SUCCESS/FAILED state */
"let checks=0;const finalPoll=setInterval(async()=>{"
"await pollStatus();if(++checks>5)clearInterval(finalPoll);"
"},800);"
"}else{"
"stopPoll();"
"try{const d=JSON.parse(xhr.responseText);"
"setStatus('✗  '+d.error,'err');"
"}catch{setStatus('✗  Upload failed ('+xhr.status+')','err');}"
"$('retry-btn').style.display='';"
"$('flash-btn').disabled=false;}"
"};"

"xhr.onerror=()=>{"
"stopPoll();"
"setStatus('✗  Network error — check Wi-Fi connection','err');"
"$('retry-btn').style.display='';"
"$('flash-btn').disabled=false;"
"};"

"xhr.open('POST','/upload',true);"
"xhr.setRequestHeader('Content-Type','application/octet-stream');"
"setStep(3);"
"xhr.send(selFile);"
"};"

"</script>"
"</body>"
"</html>";
/* ========================  HTML ENDS  ============================ */
