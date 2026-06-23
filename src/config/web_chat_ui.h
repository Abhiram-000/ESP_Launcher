/*
 * web_chat_ui.h — Unified SPA Web UI for AI Chat, IDE, and Remote
 * ================================================================
 * Fully styled with glassmorphism, responsive mobile layouts,
 * haptic vibration feedbacks, live SSE streaming chat, and Captured IDE Console logs!
 */
#pragma once

static const char* WEB_CHAT_HTML = R"html(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>ESP32 Launcher Portal</title>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&family=Fira+Code:wght@400;500&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg: #08090d;
            --accent: #00f2fe;
            --accent-glow: rgba(0, 242, 254, 0.15);
            --magenta: #f35588;
            --magenta-glow: rgba(243, 85, 136, 0.15);
            --card-bg: rgba(255, 255, 255, 0.03);
            --card-border: rgba(255, 255, 255, 0.08);
            --text: #f0f3f8;
            --text-dim: #8a99ad;
            --success: #39ff14;
            --fail: #ff3366;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            -webkit-tap-highlight-color: transparent;
        }

        body {
            font-family: 'Outfit', sans-serif;
            background: var(--bg);
            color: var(--text);
            min-height: 100vh;
            overflow-x: hidden;
            display: flex;
            flex-direction: column;
        }

        /* Ambient background glow */
        body::before {
            content: '';
            position: fixed;
            top: -10%;
            left: -10%;
            width: 120%;
            height: 120%;
            background: radial-gradient(circle at 10% 10%, rgba(0, 242, 254, 0.05), transparent 40%),
                        radial-gradient(circle at 90% 90%, rgba(243, 85, 136, 0.05), transparent 40%);
            z-index: -1;
            pointer-events: none;
        }

        header {
            padding: 16px 20px;
            background: rgba(8, 9, 13, 0.8);
            backdrop-filter: blur(10px);
            border-bottom: 1px solid var(--card-border);
            display: flex;
            align-items: center;
            justify-content: space-between;
            position: sticky;
            top: 0;
            z-index: 100;
        }

        header h1 {
            font-size: 1.25rem;
            font-weight: 700;
            background: linear-gradient(135deg, var(--accent), var(--magenta));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            letter-spacing: 0.5px;
        }

        .status-dot {
            width: 8px;
            height: 8px;
            background-color: var(--success);
            border-radius: 50%;
            box-shadow: 0 0 8px var(--success);
            animation: pulse 1.5s infinite;
        }

        @keyframes pulse {
            0% { opacity: 0.5; }
            50% { opacity: 1; }
            100% { opacity: 0.5; }
        }

        /* Tabs Navigation */
        .tabs {
            display: flex;
            background: rgba(255, 255, 255, 0.02);
            border-bottom: 1px solid var(--card-border);
            padding: 4px;
        }

        .tab-btn {
            flex: 1;
            padding: 12px;
            background: transparent;
            border: none;
            color: var(--text-dim);
            font-size: 0.9rem;
            font-weight: 600;
            font-family: inherit;
            cursor: pointer;
            transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
            border-radius: 6px;
            text-align: center;
        }

        .tab-btn.active {
            color: var(--accent);
            background: var(--accent-glow);
            box-shadow: inset 0 0 0 1px rgba(0, 242, 254, 0.2);
        }

        /* Main Scrollable Viewport */
        .tab-content {
            flex: 1;
            display: none;
            flex-direction: column;
            padding: 20px;
            max-width: 800px;
            width: 100%;
            margin: 0 auto;
        }

        .tab-content.active {
            display: flex;
        }

        /* Glassmorphic card styling */
        .glass-card {
            background: var(--card-bg);
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
            border: 1px solid var(--card-border);
            border-radius: 12px;
            padding: 16px;
            margin-bottom: 16px;
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.2);
        }

        /* CHAT PANE */
        .chat-container {
            flex: 1;
            display: flex;
            flex-direction: column;
            height: calc(100vh - 220px);
        }

        .messages-list {
            flex: 1;
            overflow-y: auto;
            padding: 10px;
            display: flex;
            flex-direction: column;
            gap: 12px;
            margin-bottom: 12px;
            border: 1px solid var(--card-border);
            border-radius: 8px;
            background: rgba(0, 0, 0, 0.1);
        }

        .message {
            max-width: 85%;
            padding: 10px 14px;
            border-radius: 12px;
            font-size: 0.95rem;
            line-height: 1.4;
            word-wrap: break-word;
            animation: slideIn 0.25s ease-out;
        }

        @keyframes slideIn {
            from { opacity: 0; transform: translateY(5px); }
            to { opacity: 1; transform: translateY(0); }
        }

        .message.user {
            align-self: flex-end;
            background: var(--accent-glow);
            border: 1px solid rgba(0, 242, 254, 0.2);
            border-bottom-right-radius: 2px;
        }

        .message.assistant {
            align-self: flex-start;
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-bottom-left-radius: 2px;
        }

        .chat-input-bar {
            display: flex;
            gap: 8px;
        }

        .chat-input-bar textarea {
            flex: 1;
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 8px;
            color: var(--text);
            padding: 10px 12px;
            font-family: inherit;
            font-size: 0.95rem;
            resize: none;
            height: 44px;
            transition: border-color 0.3s;
        }

        .chat-input-bar textarea:focus {
            outline: none;
            border-color: var(--accent);
        }

        .btn {
            background: var(--accent-glow);
            border: 1px solid var(--accent);
            color: var(--accent);
            padding: 0 18px;
            border-radius: 8px;
            font-weight: 600;
            font-size: 0.9rem;
            cursor: pointer;
            transition: all 0.2s;
            display: flex;
            align-items: center;
            justify-content: center;
            height: 44px;
        }

        .btn:active {
            transform: scale(0.97);
            background: rgba(0, 242, 254, 0.3);
        }

        .btn.magenta {
            background: var(--magenta-glow);
            border-color: var(--magenta);
            color: var(--magenta);
        }

        .btn.magenta:active {
            background: rgba(243, 85, 136, 0.3);
        }

        /* Quick Prompts */
        .quick-prompts {
            display: flex;
            gap: 8px;
            overflow-x: auto;
            padding: 4px 0 12px 0;
        }

        .prompt-chip {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 100px;
            padding: 6px 12px;
            font-size: 0.8rem;
            white-space: nowrap;
            cursor: pointer;
            transition: all 0.2s;
            color: var(--text-dim);
        }

        .prompt-chip:active {
            border-color: var(--accent);
            color: var(--accent);
        }

        /* IDE PANE */
        .ide-container {
            display: flex;
            flex-direction: column;
            height: calc(100vh - 160px);
            gap: 12px;
        }

        .ide-selector {
            display: flex;
            gap: 8px;
            align-items: center;
        }

        .ide-selector select, .ide-selector input {
            flex: 1;
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 8px;
            color: var(--text);
            padding: 8px 12px;
            font-family: inherit;
            height: 38px;
        }

        .ide-selector select:focus, .ide-selector input:focus {
            outline: none;
            border-color: var(--accent);
        }

        .code-editor {
            flex: 2;
            width: 100%;
            background: #040508;
            border: 1px solid var(--card-border);
            border-radius: 8px;
            color: #d1d4db;
            font-family: 'Fira Code', monospace;
            font-size: 0.85rem;
            padding: 12px;
            resize: none;
            line-height: 1.5;
            tab-size: 4;
            transition: border-color 0.3s;
        }

        .code-editor:focus {
            outline: none;
            border-color: var(--magenta);
        }

        .ide-buttons {
            display: flex;
            gap: 8px;
        }

        .ide-buttons button {
            flex: 1;
        }

        /* Terminal log output */
        .console-panel {
            flex: 1;
            background: #020305;
            border: 1px solid rgba(255, 255, 255, 0.05);
            border-radius: 8px;
            padding: 10px;
            font-family: 'Fira Code', monospace;
            font-size: 0.8rem;
            color: var(--success);
            overflow-y: auto;
            white-space: pre-wrap;
            min-height: 80px;
        }

        /* CONTROLLER PANE */
        .controller-pane {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 30px;
            height: calc(100vh - 180px);
        }

        .dpad {
            position: relative;
            width: 180px;
            height: 180px;
        }

        .dpad-btn {
            position: absolute;
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid rgba(255, 255, 255, 0.1);
            color: var(--text);
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 12px;
            cursor: pointer;
            transition: all 0.15s;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
        }

        .dpad-btn:active {
            background: var(--accent-glow);
            border-color: var(--accent);
            color: var(--accent);
            transform: scale(0.94);
        }

        .dpad-btn.up { top: 0; left: 60px; width: 60px; height: 60px; }
        .dpad-btn.down { bottom: 0; left: 60px; width: 60px; height: 60px; }
        .dpad-btn.left { top: 60px; left: 0; width: 60px; height: 60px; }
        .dpad-btn.right { top: 60px; right: 0; width: 60px; height: 60px; }
        .dpad-btn.center { top: 60px; left: 60px; width: 60px; height: 60px; border-radius: 50%; }

        .select-bar {
            width: 140px;
            height: 48px;
        }

        /* Code blocks rendering inside Chat */
        .code-container {
            margin: 12px 0;
            background: #030407;
            border: 1px solid var(--card-border);
            border-radius: 8px;
            overflow: hidden;
            text-align: left;
        }
        .code-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: rgba(255, 255, 255, 0.02);
            padding: 6px 12px;
            border-bottom: 1px solid var(--card-border);
            font-size: 0.75rem;
            color: var(--text-dim);
            font-family: inherit;
        }
        .code-actions {
            display: flex;
            gap: 6px;
        }
        .load-ide-btn, .flash-btn {
            border-radius: 4px;
            padding: 3.5px 9px;
            font-size: 0.7rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
        }
        .load-ide-btn {
            background: var(--accent-glow);
            border: 1px solid var(--accent);
            color: var(--accent);
            box-shadow: 0 0 10px rgba(0, 242, 254, 0.1);
        }
        .flash-btn {
            background: var(--magenta-glow);
            border: 1px solid var(--magenta);
            color: var(--magenta);
            box-shadow: 0 0 10px rgba(243, 85, 136, 0.1);
        }
        .load-ide-btn:active, .flash-btn:active {
            transform: scale(0.95);
        }
        .load-ide-btn:active {
            background: var(--accent);
            color: #000;
        }
        .flash-btn:active {
            background: var(--magenta);
            color: #000;
        }
        .code-container pre {
            padding: 12px;
            margin: 0;
            overflow-x: auto;
        }
        .code-container code {
            font-family: 'Fira Code', monospace;
            font-size: 0.8rem;
            color: #00f2fe;
            white-space: pre;
        }
    </style>
</head>
<body>
    <header>
        <h1>ESP32 AI Portal</h1>
        <div class="status-dot"></div>
    </header>

    <div class="tabs">
        <button class="tab-btn active" onclick="switchTab('chat-tab')">AI Chat</button>
        <button class="tab-btn" onclick="switchTab('ide-tab')">Lua IDE</button>
        <button class="tab-btn" onclick="switchTab('controller-tab')">Remote</button>
    </div>

    <!-- CHAT TAB -->
    <div id="chat-tab" class="tab-content active">
        <div class="chat-container">
            <div id="msg-list" class="messages-list">
                <div class="message assistant">Hello! I am your ESP32's AI partner. Type a prompt below or run direct scripts using the IDE!</div>
            </div>
            
            <div class="quick-prompts">
                <div class="prompt-chip" onclick="quickPrompt('Blink onboard LED 10 times')">Blink LED</div>
                <div class="prompt-chip" onclick="quickPrompt('Read all ADC pins and print values')">Read ADC</div>
                <div class="prompt-chip" onclick="quickPrompt('Show free system memory')">Free Heap</div>
                <div class="prompt-chip" onclick="quickPrompt('List script directory')">List Scripts</div>
                <div class="prompt-chip" onclick="quickPrompt('Blink LED when UP button pressed')">Button LED</div>
                <div class="prompt-chip" onclick="quickPrompt('Read I2C device at address 0x68')">I2C Scan</div>
            </div>

            <div class="chat-input-bar">
                <textarea id="chat-input" placeholder="Ask AI to write Lua code..."></textarea>
                <button class="btn" id="send-btn" onclick="sendChatMessage()">Send</button>
            </div>
        </div>
    </div>

    <!-- IDE TAB -->
    <div id="ide-tab" class="tab-content">
        <div class="ide-container">
            <div class="ide-selector">
                <select id="file-select" onchange="loadFile()">
                    <option value="">-- New Script --</option>
                </select>
                <input type="text" id="filename-input" placeholder="script.lua" style="flex:0.6;" value="remote.lua">
            </div>

            <textarea id="code-editor" class="code-editor" spellcheck="false" placeholder="-- Enter Lua code here&#10;sys.print('Hello from Phone!')"></textarea>

            <div class="ide-buttons">
                <button class="btn" onclick="saveScript()">Save Script</button>
                <button class="btn magenta" onclick="runScript()">Run Code</button>
            </div>

            <div class="console-panel" id="console-log">-- System Developer Console initialized --</div>
        </div>
    </div>

    <!-- CONTROLLER TAB -->
    <div id="controller-tab" class="tab-content">
        <div class="controller-pane">
            <div class="dpad">
                <div class="dpad-btn up" onclick="triggerKey('up')">▲</div>
                <div class="dpad-btn left" onclick="triggerKey('left')">◀</div>
                <div class="dpad-btn center" onclick="triggerKey('select')">OK</div>
                <div class="dpad-btn right" onclick="triggerKey('right')">▶</div>
                <div class="dpad-btn down" onclick="triggerKey('down')">▼</div>
            </div>
            <button class="btn select-bar" onclick="triggerKey('select')">SELECT</button>
        </div>
    </div>

    <script>
        let eventSource = null;

        function switchTab(tabId) {
            document.querySelectorAll('.tab-btn').forEach((btn, idx) => {
                btn.classList.toggle('active', btn.getAttribute('onclick').includes(tabId));
            });
            document.querySelectorAll('.tab-content').forEach(pane => {
                pane.classList.toggle('active', pane.id === tabId);
            });
            vibrate(30);
            if (tabId === 'ide-tab') {
                refreshFileList();
            }
        }

        function vibrate(ms) {
            if (navigator.vibrate) navigator.vibrate(ms);
        }

        /* ─── AI CHAT FLOW ─────────────────────────────────────────────────── */
        function appendMsg(role, content) {
            const list = document.getElementById('msg-list');
            const div = document.createElement('div');
            div.className = `message ${role}`;
            div.innerText = content;
            div.dataset.raw = content;
            list.appendChild(div);
            list.scrollTop = list.scrollHeight;
            return div;
        }

        function quickPrompt(text) {
            document.getElementById('chat-input').value = text;
            sendChatMessage();
        }

        function formatMarkdown(text) {
            const codeBlockRegex = /```(?:lua)?\n([\s\S]*?)```/g;
            let formatted = text;
            formatted = formatted.replace(codeBlockRegex, (match, code) => {
                const escapedCode = btoa(unescape(encodeURIComponent(code.trim())));
                const escapedFilename = btoa(unescape(encodeURIComponent('ai_script.lua')));
                return `<div class="code-container">
                    <div class="code-header">
                        <span>Lua Script</span>
                        <div class="code-actions">
                            <button class="load-ide-btn" onclick="loadToIde('${escapedCode}')">Edit</button>
                            <button class="flash-btn" onclick="flashCode('${escapedCode}','${escapedFilename}')">Flash</button>
                        </div>
                    </div>
                    <pre><code>${escapeHtml(code.trim())}</code></pre>
                </div>`;
            });
            return formatted.replace(/\n/g, '<br>');
        }

        function escapeHtml(text) {
            return text
                .replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .replace(/"/g, "&quot;")
                .replace(/'/g, "&#039;");
        }

        function loadToIde(base64Code) {
            const decoded = decodeURIComponent(escape(atob(base64Code)));
            document.getElementById('ide-editor').value = decoded;
            
            const filenameInput = document.querySelector('.ide-selector input');
            if (filenameInput && !filenameInput.value) {
                filenameInput.value = 'ai_script.lua';
            }
            
            switchTab('ide-tab');
            vibrate(80);
            
            const consoleLog = document.getElementById('console-log');
            if (consoleLog) {
                consoleLog.innerHTML = `<span style="color:#00f2fe;">[SYSTEM] Loaded script from AI chat successfully! Click "Run Code" to execute.</span>\n` + consoleLog.innerHTML;
            }
        }

        function flashCode(base64Code, base64Filename) {
            const code = decodeURIComponent(escape(atob(base64Code)));
            let filename = 'ai_script.lua';
            try { filename = decodeURIComponent(escape(atob(base64Filename))); } catch(e) {}
            
            vibrate(60);
            const consoleLog = document.getElementById('console-log');
            
            // Step 1: Save the script
            printConsoleLog('System: Flashing "' + filename + '"...');
            
            fetch('/chat/ide/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'file=' + encodeURIComponent(filename) + '&code=' + encodeURIComponent(code)
            })
            .then(r => r.json())
            .then(res => {
                if (!res.ok) {
                    printConsoleLog('Error saving: ' + (res.error || 'unknown'));
                    return;
                }
                // Step 2: Run it immediately
                printConsoleLog('System: Saved. Executing...');
                return fetch('/chat/ide/run?file=' + encodeURIComponent(filename));
            })
            .then(r => r ? r.json() : null)
            .then(res => {
                if (!res) return;
                if (res.ok) {
                    let out = 'Flash complete! Executed in ' + res.elapsed_ms + 'ms\n';
                    out += '---------------------------------\n';
                    out += res.logs || '(No console output)';
                    printConsoleLog(out);
                } else {
                    let err = 'Runtime Error: ' + (res.error || 'Unknown') + '\n';
                    err += '---------------------------------\n';
                    err += res.logs || '';
                    printConsoleLog(err);
                }
            })
            .catch(err => printConsoleLog('Flash failed: ' + err));
            
            // Switch to IDE tab to show console output
            switchTab('ide-tab');
        }

        function sendChatMessage() {
            const input = document.getElementById('chat-input');
            const msg = input.value.trim();
            if (!msg) return;

            input.value = '';
            appendMsg('user', msg);
            vibrate(40);

            if (eventSource) eventSource.close();

            const aiBubble = appendMsg('assistant', '...');
            
            eventSource = new EventSource('/chat/send?msg=' + encodeURIComponent(msg));
            
            eventSource.onmessage = function(e) {
                if (aiBubble.dataset.raw === '...') {
                    aiBubble.innerText = '';
                    aiBubble.dataset.raw = '';
                }
                
                if (e.data === '[DONE]') {
                    eventSource.close();
                    eventSource = null;
                    aiBubble.innerHTML = formatMarkdown(aiBubble.dataset.raw);
                    return;
                }
                
                try {
                    const parsed = JSON.parse(e.data);
                    if (parsed.token) {
                        aiBubble.dataset.raw += parsed.token;
                        aiBubble.innerText = aiBubble.dataset.raw;
                        document.getElementById('msg-list').scrollTop = document.getElementById('msg-list').scrollHeight;
                    }
                } catch(err) {
                    console.error("Error parsing chunk", err);
                }
            };

            eventSource.onerror = function(e) {
                if (aiBubble.dataset.raw === '...') {
                    aiBubble.innerText = 'Error connecting to Cerebras LLM. Check API key/Network.';
                }
                if (eventSource) {
                    eventSource.close();
                    eventSource = null;
                }
            };
        }

        /* ─── LUA IDE FLOW ─────────────────────────────────────────────────── */
        function refreshFileList() {
            fetch('/chat/ide/files')
                .then(r => r.json())
                .then(data => {
                    const select = document.getElementById('file-select');
                    // Retain "-- New Script --"
                    select.innerHTML = '<option value="">-- New Script --</option>';
                    if (data && data.files) {
                        data.files.forEach(f => {
                            const opt = document.createElement('option');
                            opt.value = f;
                            opt.innerText = f;
                            select.appendChild(opt);
                        });
                    }
                })
                .catch(err => console.error("Error fetching scripts list", err));
        }

        function loadFile() {
            const select = document.getElementById('file-select');
            const file = select.value;
            if (!file) {
                document.getElementById('filename-input').value = 'remote.lua';
                document.getElementById('code-editor').value = '';
                return;
            }
            document.getElementById('filename-input').value = file;
            vibrate(40);

            fetch('/chat/ide/load?file=' + encodeURIComponent(file))
                .then(r => r.text())
                .then(text => {
                    document.getElementById('code-editor').value = text;
                })
                .catch(err => alert("Load failed: " + err));
        }

        function saveScript() {
            const file = document.getElementById('filename-input').value.trim();
            const code = document.getElementById('code-editor').value;
            if (!file) return alert("Please specify a filename");

            vibrate(50);
            fetch('/chat/ide/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'file=' + encodeURIComponent(file) + '&code=' + encodeURIComponent(code)
            })
            .then(r => r.json())
            .then(res => {
                if (res.ok) {
                    printConsoleLog("System: Script saved successfully as " + file);
                    refreshFileList();
                } else {
                    printConsoleLog("Error saving script: " + res.error);
                }
            })
            .catch(err => printConsoleLog("Save failed: " + err));
        }

        function runScript() {
            const file = document.getElementById('filename-input').value.trim();
            if (!file) return alert("Please save or specify a filename to run");

            vibrate(60);
            printConsoleLog("System: Initializing " + file + " execution...");
            
            fetch('/chat/ide/run?file=' + encodeURIComponent(file))
                .then(r => r.json())
                .then(res => {
                    if (res.ok) {
                        let out = "System: Execution complete!\n";
                        out += "Elapsed time: " + res.elapsed_ms + "ms\n";
                        out += "---------------------------------\n";
                        out += res.logs || "(No console outputs)";
                        printConsoleLog(out);
                    } else {
                        let err = "Runtime Error: " + (res.error || "Unknown error") + "\n";
                        err += "---------------------------------\n";
                        err += res.logs || "";
                        printConsoleLog(err);
                    }
                })
                .catch(err => printConsoleLog("Execution task failed: " + err));
        }

        function printConsoleLog(text) {
            const pane = document.getElementById('console-log');
            pane.innerText = text;
            pane.scrollTop = pane.scrollHeight;
        }

        /* ─── CONTROLLER FLOW ──────────────────────────────────────────────── */
        function triggerKey(keyId) {
            vibrate(40);
            fetch('/remote/input?btn=' + keyId)
                .catch(err => console.error("Key trigger failed", err));
        }
    </script>
</body>
</html>)html";
