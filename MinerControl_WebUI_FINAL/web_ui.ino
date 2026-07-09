/*
 * web_ui.ino — Web server handlers + HTML/CSS/JS generation
 * Updated with per-phase and apparent/reactive power display
 */

// ==================== WEB SERVER HANDLERS ====================

void handleRestart() {
    if (!server.hasArg("miner")) return server.send(400, "text/plain", "Missing parameter");
    int idx = server.arg("miner").toInt();
    String mode = server.hasArg("mode") ? server.arg("mode") : "";

    String result;
    if (mode == "standby") {
        HTTPClient http;
        http.begin("http://" + String(miners[idx].ip) + "/api/system/restart");
        http.addHeader("Content-Type", "application/json");
        int code = http.POST("{\"mode\":\"standby\"}");
        result = (code == 200) ? "Standby sent" : "Failed HTTP " + String(code);
        http.end();
    } else {
        result = restartBitAxe(idx);
    }
    lastAction = String(miners[idx].name) + " " + (mode.length() ? mode : "restart");
    showWebUI();
    server.send(200, "text/plain", result);
}

void handleStatus() {
    if (!server.hasArg("miner")) return server.send(400, "application/json", "{\"error\":\"Missing\"}");
    int idx = server.arg("miner").toInt();
    String result;
    if (strcmp(miners[idx].type, "bitaxe") == 0) result = getBitAxeStatus(idx);
    else if (strcmp(miners[idx].type, "avalon") == 0) result = getAvalonStatus(idx);
    else result = "{\"error\":\"Unknown type\"}";
    lastAction = String(miners[idx].name) + " status";
    showWebUI();
    server.send(200, "application/json", result);
}

void handleAvalon() {
    if (!server.hasArg("miner") || !server.hasArg("cmd"))
        return server.send(400, "text/plain", "Missing parameters");
    int idx = server.arg("miner").toInt();
    String cmd = server.arg("cmd");
    String result = controlAvalon(idx, cmd);
    lastAction = "Avalon " + cmd;
    showWebUI();
    server.send(200, "text/plain", result);
}

void handleRelay() {
    if (!server.hasArg("relay")) return server.send(400, "text/plain", "Missing parameter");
    int relayNum = server.arg("relay").toInt();
    String action = server.hasArg("action") ? server.arg("action") : "TOGGLE";
    String result = controlTasmotaRelay(relayNum, action);
    lastAction = "Relay " + String(relayNum) + " " + action;
    showWebUI();
    server.send(200, "text/plain", result);
}

void handleRelayStatus() {
    server.send(200, "application/json", getTasmotaRelayStatus());
}

void handleEnergy() {
    String json = "{";
    // Active power totals
    json += "\"solar_w\":" + String(liveSolar, 1) + ",";
    json += "\"grid_w\":" + String(liveGrid, 1) + ",";
    json += "\"home_w\":" + String(liveHome, 1) + ",";
    // Apparent power totals
    json += "\"solar_va\":" + String(liveSolarVA, 1) + ",";
    json += "\"grid_va\":" + String(liveGridVA, 1) + ",";
    json += "\"home_va\":" + String(liveHomeVA, 1) + ",";
    // Reactive power totals
    json += "\"solar_var\":" + String(liveSolarVAR, 1) + ",";
    json += "\"grid_var\":" + String(liveGridVAR, 1) + ",";
    json += "\"home_var\":" + String(liveHomeVAR, 1) + ",";
    // Power saved (surplus)
    json += "\"saved_w\":" + String(livePowerSaved, 1) + ",";
    json += "\"saved_va\":" + String(livePowerSavedVA, 1) + ",";
    // Per-phase data
    json += "\"p1_solar_w\":" + String(phase1.solar_w, 1) + ",";
    json += "\"p1_grid_w\":" + String(phase1.grid_w, 1) + ",";
    json += "\"p1_home_w\":" + String(phase1.home_w, 1) + ",";
    json += "\"p1_solar_va\":" + String(phase1.solar_va, 1) + ",";
    json += "\"p1_grid_va\":" + String(phase1.grid_va, 1) + ",";
    json += "\"p1_home_va\":" + String(phase1.home_va, 1) + ",";
    json += "\"p2_solar_w\":" + String(phase2.solar_w, 1) + ",";
    json += "\"p2_grid_w\":" + String(phase2.grid_w, 1) + ",";
    json += "\"p2_home_w\":" + String(phase2.home_w, 1) + ",";
    json += "\"p2_solar_va\":" + String(phase2.solar_va, 1) + ",";
    json += "\"p2_grid_va\":" + String(phase2.grid_va, 1) + ",";
    json += "\"p2_home_va\":" + String(phase2.home_va, 1) + ",";
    // Device info
    json += "\"refoss_found\":" + String(refossFound ? "true" : "false") + ",";
    json += "\"refoss_ip\":\"" + refossIP + "\",";
    json += "\"samples\":" + String(aggCount) + ",";
    json += "\"mining_state\":" + String(miningState) + ",";
    json += "\"mining_name\":\"" + String(profiles[miningState].name) + "\",";
    json += "\"mining_w\":" + String(profiles[miningState].totalW) + ",";
    json += "\"auto_enabled\":" + String(autoMiningEnabled ? "true" : "false") + ",";
    json += "\"verify\":\"" + lastVerifyResult + "\",";
    json += "\"avalon_actual\":\"" + actualAvalonMode + "\",";
    json += "\"avalon_mhs\":" + String(actualAvalonMHS, 0) + ",";
    json += "\"avalon_mpo\":" + String(actualAvalonMPO) + ",";
    // Per-channel data (6 channels)
    json += "\"channels\":[";
    for (int i = 0; i < 6; i++) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + String(chNames[i]) + "\",";
        json += "\"w\":" + String(liveCh[i].power_w, 1) + ",";
        json += "\"pf\":" + String(liveCh[i].pf, 2) + ",";
        json += "\"va\":" + String(liveCh[i].apparent_va, 1) + ",";
        json += "\"var\":" + String(liveCh[i].reactive_var, 1) + "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleSupabaseSync() {
    pushToSupabase();
    server.send(200, "text/plain", "Synced to Supabase (" + String(aggCount) + " samples)");
}

void handleNotFound() { server.send(404, "text/plain", "Not found"); }

// ==================== HTML PAGE ====================

void handleRoot() {
    String h = "<!DOCTYPE html><html><head>";
    h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    h += "<title>Solar Mining Control</title><style>";
    h += "*{margin:0;padding:0;box-sizing:border-box;}";
    h += "body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(135deg,#0f2027,#203a43,#2c5364);min-height:100vh;padding:15px;color:#fff;}";
    h += ".ctr{max-width:1200px;margin:0 auto;} h1{text-align:center;font-size:2.2em;margin-bottom:5px;}";
    h += ".sub{text-align:center;opacity:.8;margin-bottom:20px;}";
    h += ".card{background:rgba(255,255,255,.08);backdrop-filter:blur(10px);border-radius:12px;padding:18px;border:1px solid rgba(255,255,255,.15);margin-bottom:15px;}";
    h += ".ct{font-size:1.5em;margin-bottom:6px;font-weight:600;} .ci{opacity:.7;margin-bottom:4px;font-size:.9em;}";
    h += ".stats{background:rgba(0,0,0,.25);padding:12px;border-radius:8px;margin:10px 0;display:none;} .stats div{margin:3px 0;}";
    h += "button{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;border:none;padding:12px 18px;font-size:.95em;border-radius:8px;cursor:pointer;width:100%;margin:5px 0;font-weight:600;}";
    h += ".brow{display:flex;gap:6px;}.brow button{flex:1;}";
    h += ".bon{background:linear-gradient(135deg,#00b09b,#96c93d);}";
    h += ".boff{background:linear-gradient(135deg,#ff416c,#ff4b2b);}";
    h += ".bstat{background:linear-gradient(135deg,#f093fb,#f5576c);}";
    h += ".blo{background:linear-gradient(135deg,#30cfd0,#330867);}";
    h += ".bmd{background:linear-gradient(135deg,#fa709a,#fee140);}";
    h += ".bhi{background:linear-gradient(135deg,#a8edea,#fed6e3);color:#333;}";
    h += "#result{position:fixed;top:15px;right:15px;max-width:340px;padding:15px;background:rgba(0,0,0,.92);border-radius:10px;display:none;z-index:1000;}";
    h += ".ep{border:2px solid #00ff88;} .rg{border:2px solid #FFD700;}";
    h += ".egrid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin:10px 0;}";
    h += ".ev{text-align:center;padding:10px;background:rgba(0,0,0,.3);border-radius:8px;}";
    h += ".ev .n{font-size:1.8em;font-weight:700;} .ev .l{font-size:.75em;opacity:.7;}";
    h += ".ev .va{font-size:.85em;color:#FFA500;margin-top:2px;}";
    h += ".sol{color:#FFD700;} .grc{color:#ff6b6b;} .hmc{color:#4ecdc4;} .svc{color:#00ff88;}";
    h += ".chg{display:grid;grid-template-columns:repeat(3,1fr);gap:5px;margin-top:8px;}";
    h += ".chi{font-size:.8em;padding:6px;background:rgba(0,0,0,.2);border-radius:5px;text-align:center;}";
    h += ".phg{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:8px;}";
    h += ".phi{padding:8px;background:rgba(0,0,0,.2);border-radius:6px;font-size:.8em;}";
    h += ".phi b{color:#FFD700;}";
    h += ".mn{margin-top:10px;padding:10px;background:rgba(0,0,0,.2);border-radius:8px;}";
    h += ".mnt{font-size:1.1em;font-weight:600;}";
    h += "@media(max-width:768px){h1{font-size:1.6em;}.egrid{grid-template-columns:repeat(2,1fr);}}";
    h += "</style></head><body><div class='ctr'>";
    h += "<h1>&#9889; Solar Mining Control</h1>";
    h += "<div class='sub'>ESP32 @ " + espIP + "</div>";
    server.sendContent(h);
    sendEnergyPanel();
    sendRelayGroup1();
    sendRelayGroup2();
    sendAvalonPanel();
    sendSupabasePanel();
    sendJavaScript();
    server.sendContent("</div></body></html>");
    server.sendContent("");
}

// ==================== ENERGY PANEL ====================

void sendEnergyPanel() {
    String h = "<div class='card ep'>";
    h += "<div class='ct'>&#128268; Energy Monitor (EMO6P)</div>";
    h += "<div class='ci' id='refI'>Searching...</div>";
    // Main totals: Solar, Grid, Home, Saved — with VA underneath
    h += "<div class='egrid'>";
    h += "<div class='ev sol'><div class='n' id='eS'>--</div><div class='l'>&#9728; Solar W</div><div class='va' id='eSva'>--VA</div></div>";
    h += "<div class='ev grc'><div class='n' id='eG'>--</div><div class='l'>&#9889; Grid W</div><div class='va' id='eGva'>--VA</div></div>";
    h += "<div class='ev hmc'><div class='n' id='eH'>--</div><div class='l'>&#127968; Home W</div><div class='va' id='eHva'>--VA</div></div>";
    h += "<div class='ev svc'><div class='n' id='eSv'>--</div><div class='l'>&#128161; Saved W</div><div class='va' id='eSvva'>--VA</div></div>";
    h += "</div>";
    // Per-phase breakdown
    h += "<div class='phg' id='phD'>";
    h += "<div class='phi'><b>Phase 1 (A)</b><br>S:<span id='p1s'>--</span>W G:<span id='p1g'>--</span>W H:<span id='p1h'>--</span>W(calc)</div>";
    h += "<div class='phi'><b>Phase 2 (B/C)</b><br>S:<span id='p2s'>--</span>W G:<span id='p2g'>--</span>W H:<span id='p2h'>--</span>W(meas)</div>";
    h += "</div>";
    // Per-channel details
    h += "<div id='chD' class='chg'></div>";
    h += "<div style='text-align:center;margin-top:8px;font-size:.8em;opacity:.6;'>";
    h += "<span id='eSmp'>0</span> samples | Next push: <span id='eNxt'>--</span></div>";
    h += "</div>";
    server.sendContent(h);
}

// ==================== RELAY PANELS ====================

void sendRelayGroup1() {
    String h = "<div class='card rg'>";
    h += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;'>";
    h += "<div class='ct'>&#128268; R1 &mdash; BitAxe + Nerdaxe (81W)</div>";
    h += "<div id='r1s' style='font-size:1.1em;font-weight:600;'>--</div></div>";
    h += "<div class='brow'>";
    h += "<button class='bon' onclick='relayCmd(1,\"ON\")'>&#9989; ON</button>";
    h += "<button class='boff' onclick='relayCmd(1,\"OFF\")'>&#10060; OFF</button></div>";
    h += "<div class='mn'><div class='mnt'>&#128421; BitAxe Rafa 21W <span style='opacity:.6;font-size:.8em;'>192.168.1.21</span></div>";
    h += "<div id='stats0' class='stats'></div>";
    h += "<div class='brow'><button class='bstat' onclick='getStatus(0)'>&#128202; Status</button>";
    h += "<button onclick='restart(0)'>&#128260; Restart</button></div></div>";
    h += "<div class='mn'><div class='mnt'>&#128421; Nerdaxe 60W <span style='opacity:.6;font-size:.8em;'>192.168.1.28</span></div>";
    h += "<div id='stats1' class='stats'></div>";
    h += "<div class='brow'><button class='bstat' onclick='getStatus(1)'>&#128202; Status</button>";
    h += "<button onclick='restart(1)'>&#128260; Restart</button></div></div></div>";
    server.sendContent(h);
}

void sendRelayGroup2() {
    String h = "<div class='card rg'>";
    h += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;'>";
    h += "<div class='ct'>&#128268; R2 &mdash; Octaxe (180W)</div>";
    h += "<div id='r2s' style='font-size:1.1em;font-weight:600;'>--</div></div>";
    h += "<div class='brow'>";
    h += "<button class='bon' onclick='relayCmd(2,\"ON\")'>&#9989; ON</button>";
    h += "<button class='boff' onclick='relayCmd(2,\"OFF\")'>&#10060; OFF</button></div>";
    h += "<div class='mn'><div class='mnt'>&#128421; Octaxe 180W <span style='opacity:.6;font-size:.8em;'>192.168.1.37</span></div>";
    h += "<div id='stats2' class='stats'></div>";
    h += "<div class='brow'><button class='bstat' onclick='getStatus(2)'>&#128202; Status</button>";
    h += "<button onclick='restart(2)'>&#128260; Restart</button></div></div></div>";
    server.sendContent(h);
}

void sendAvalonPanel() {
    String h = "<div class='card' style='border:2px solid #4facfe;'>";
    h += "<div class='ct'>&#128268; Avalon Q (API Only)</div>";
    h += "<div class='ci'>192.168.1.51:4028 &bull; CGMiner &bull; No relay</div>";
    h += "<div id='stats3' class='stats'></div>";
    h += "<button class='bstat' onclick='getAvalonStatus(3)'>&#128202; Get Status</button>";
    h += "<div class='brow'><button class='blo' onclick='avalonCmd(3,\"standby\")'>&#128164; Sleep</button>";
    h += "<button style='background:linear-gradient(135deg,#FF6B6B,#FFE66D);' onclick='avalonCmd(3,\"wakeup\")'>&#9728; Wake</button></div>";
    h += "<div class='brow'><button class='blo' onclick='avalonCmd(3,\"low\")'>&#128012; Low</button>";
    h += "<button class='bmd' onclick='avalonCmd(3,\"mid\")'>&#9889; Mid</button>";
    h += "<button class='bhi' onclick='avalonCmd(3,\"high\")'>&#128640; High</button></div>";
    h += "<button onclick='avalonCmd(3,\"reboot\")'>&#128260; Reboot</button></div>";
    server.sendContent(h);
}

void sendSupabasePanel() {
    String h = "<div class='card' style='border:2px solid #00ff88;'>";
    h += "<div class='ct'>&#128202; Supabase Cloud DB</div>";
    h += "<div class='ci'>solar-cluster</div>";
    h += "<button onclick='syncSupa()' style='background:linear-gradient(135deg,#00ff88,#00b4d8);color:#000;'>&#128260; Sync Now</button>";
    h += "<div id='supS' style='margin-top:8px;font-size:.85em;opacity:.7;'>Waiting...</div></div>";
    h += "<div id='result'></div>";
    server.sendContent(h);
}

// ==================== JAVASCRIPT ====================

void sendJavaScript() {
    String j = "<script>";
    j += "function showR(t,c){var r=document.getElementById('result');r.innerHTML=t;r.style.display='block';r.style.borderLeft='4px solid '+c;setTimeout(()=>r.style.display='none',5000);}";

    // Miner status
    j += "function getStatus(i){showR('Getting status...','#00ffff');";
    j += "fetch('/status?miner='+i).then(r=>r.json()).then(d=>{";
    j += "var s=document.getElementById('stats'+i);s.style.display='block';";
    j += "s.innerHTML='<div><b>Hash:</b> '+d.hashrate+'</div>';";
    j += "s.innerHTML+='<div><b>Temp:</b> '+(d.temp||'N/A')+'&#176;C</div>';";
    j += "s.innerHTML+='<div><b>Power:</b> '+(d.power||'N/A')+'W</div>';";
    j += "s.innerHTML+='<div><b>Best:</b> '+(d.bestDiff||'N/A')+'</div>';";
    j += "showR('Updated!','#00ff00');";
    j += "}).catch(e=>showR('Error: '+e,'#ff0000'));}";
    server.sendContent(j);

    j = "function restart(i){showR('Restarting...','#ffd700');";
    j += "fetch('/restart?miner='+i).then(r=>r.text()).then(t=>showR(t,'#00ff00')).catch(e=>showR('Error','#ff0000'));}";

    j += "function standbyBitaxe(i){showR('Standby...','#ffd700');";
    j += "fetch('/restart?miner='+i+'&mode=standby').then(r=>r.text()).then(t=>showR(t,'#00ff00')).catch(e=>showR('Error','#ff0000'));}";

    // Avalon
    j += "function getAvalonStatus(i){showR('Getting status...','#00ffff');";
    j += "fetch('/status?miner='+i).then(r=>r.json()).then(d=>{";
    j += "var s=document.getElementById('stats'+i);s.style.display='block';";
    j += "s.innerHTML='<div><b>Hash:</b> '+d.hashrate+'</div>';";
    j += "s.innerHTML+='<div><b>Temp:</b> '+d.temp+'&#176;C</div>';";
    j += "s.innerHTML+='<div><b>Power:</b> '+d.power+'W</div>';";
    j += "s.innerHTML+='<div><b>Mode:</b> '+d.mode+'</div>';";
    j += "s.innerHTML+='<div><b>Uptime:</b> '+d.uptime+'</div>';";
    j += "showR('Updated!','#00ff00');";
    j += "}).catch(e=>showR('Error: '+e,'#ff0000'));}";

    j += "function avalonCmd(i,cmd){";
    j += "var m={'summary':'Getting...','standby':'Sleeping...','wakeup':'Waking...','low':'Low mode...','mid':'Mid mode...','high':'High mode...','reboot':'Rebooting...'};";
    j += "showR(m[cmd]||'Sending...','#ffd700');";
    j += "fetch('/avalon?miner='+i+'&cmd='+cmd).then(r=>r.text()).then(t=>showR(t,'#00ffff')).catch(e=>showR('Error: '+e,'#ff0000'));}";
    server.sendContent(j);

    // Relay commands
    j = "function relayCmd(n,a){showR('Relay '+n+' '+a+'...','#ffd700');";
    j += "fetch('/relay?relay='+n+'&action='+a).then(r=>r.text()).then(t=>{showR(t,'#00ff00');getRS();}).catch(e=>showR('Error','#ff0000'));}";

    // Relay status
    j += "function getRS(){fetch('/relaystatus').then(r=>r.json()).then(d=>{";
    j += "if(d.error){document.getElementById('r1s').innerHTML='offline';document.getElementById('r2s').innerHTML='offline';return;}";
    j += "var p1=d.power1||'--';var p2=d.power2||'--';";
    j += "var c1=p1=='ON'?'#00ff00':'#ff4444';";
    j += "var c2=p2=='ON'?'#00ff00':'#ff4444';";
    j += "document.getElementById('r1s').innerHTML='<span style=\"color:'+c1+'\">'+p1+'</span>';";
    j += "document.getElementById('r2s').innerHTML='<span style=\"color:'+c2+'\">'+p2+'</span>';";
    j += "}).catch(e=>{document.getElementById('r1s').innerHTML='offline';document.getElementById('r2s').innerHTML='offline';});}";
    server.sendContent(j);

    // Energy + mining state polling — updated with VA/VAR + per-phase
    j = "function getE(){fetch('/energy').then(r=>r.json()).then(d=>{";
    // Main totals
    j += "document.getElementById('eS').textContent=Math.round(d.solar_w);";
    j += "document.getElementById('eG').textContent=Math.round(d.grid_w);";
    j += "document.getElementById('eH').textContent=Math.round(d.home_w);";
    j += "document.getElementById('eSv').textContent=Math.round(d.saved_w);";
    // Apparent power
    j += "document.getElementById('eSva').textContent=Math.round(d.solar_va)+'VA';";
    j += "document.getElementById('eGva').textContent=Math.round(d.grid_va)+'VA';";
    j += "document.getElementById('eHva').textContent=Math.round(d.home_va)+'VA';";
    j += "document.getElementById('eSvva').textContent=Math.round(d.saved_va)+'VA';";
    // Per-phase
    j += "document.getElementById('p1s').textContent=Math.round(d.p1_solar_w);";
    j += "document.getElementById('p1g').textContent=Math.round(d.p1_grid_w);";
    j += "document.getElementById('p1h').textContent=Math.round(d.p1_home_w);";
    j += "document.getElementById('p2s').textContent=Math.round(d.p2_solar_w);";
    j += "document.getElementById('p2g').textContent=Math.round(d.p2_grid_w);";
    j += "document.getElementById('p2h').textContent=Math.round(d.p2_home_w);";
    // Samples
    j += "document.getElementById('eSmp').textContent=d.samples;";
    // Info line
    j += "var info=d.refoss_found?'EMO6P @ '+d.refoss_ip:'Searching...';";
    j += "info+=' | Mining: '+d.mining_name+' ('+d.mining_w+'W)';";
    j += "if(d.avalon_actual&&d.avalon_actual!='unknown') info+=' | AV:'+d.avalon_actual.toUpperCase();";
    j += "if(d.avalon_mpo>0) info+=' '+d.avalon_mpo+'W';";
    j += "document.getElementById('refI').textContent=info;";
    // Per-channel cards with VA/VAR
    j += "var cd=document.getElementById('chD');cd.innerHTML='';";
    j += "var colors=['#FFD700','#ff6b6b','#87CEEB','#4ecdc4','#FFD700','#4ecdc4'];";
    j += "d.channels.forEach(function(c,i){";
    j += "cd.innerHTML+='<div class=\"chi\" style=\"border-left:3px solid '+colors[i]+'\"><b>'+c.name+'</b><br>'+c.w.toFixed(1)+'W<br>pf:'+c.pf.toFixed(2)+'<br><span style=\"color:#FFA500\">'+c.va.toFixed(0)+'VA '+c.var.toFixed(0)+'VAR</span></div>';";
    j += "});}).catch(e=>{});}";
    server.sendContent(j);

    // Supabase sync
    j = "function syncSupa(){showR('Syncing...','#00ff88');";
    j += "fetch('/supabase').then(r=>r.text()).then(t=>{showR(t,'#00ff88');document.getElementById('supS').textContent='Last sync: just now';}).catch(e=>showR('Error','#ff0000'));}";

    // Auto-refresh
    j += "setTimeout(getRS,1000);";
    j += "setTimeout(getE,1500);";
    j += "setInterval(getRS,15000);";
    j += "setInterval(getE,10000);";
    j += "</script>";
    server.sendContent(j);
}
