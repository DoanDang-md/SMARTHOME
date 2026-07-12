/**
 * @file GatewayHtml.h
 * @brief HTML PROGMEM — giao diện Web Gateway (tiếng Việt có dấu, UTF-8).
 *
 * LƯU Ý: File này phải lưu UTF-8 (không BOM). Content-Type: text/html; charset=utf-8.
 * Font: Be Vietnam Pro (hỗ trợ đầy đủ dấu tiếng Việt).
 */
#pragma once
#include <Arduino.h>

const char wifi_setup_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Cài đặt WiFi - SmartHome Gateway</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Be+Vietnam+Pro:wght@400;500;600;700&display=swap');
    body { font-family:'Be Vietnam Pro','Segoe UI',system-ui,sans-serif; background-color:#f0f2f5; padding:20px; }
    .card { max-width:400px; margin:auto; background:white; padding:30px; border-radius:10px; box-shadow:0 4px 12px rgba(0,0,0,0.1); }
    h2 { color:#1a73e8; text-align:center; }
    p { text-align:center; color:#555; font-size:14px; }
    input[type="text"], input[type="password"] { width:100%; padding:12px; margin:10px 0 20px 0; border:1px solid #ddd; border-radius:5px; box-sizing:border-box; font-family:inherit; }
    input[type="submit"] { background-color:#1a73e8; color:white; border:none; padding:14px; width:100%; border-radius:5px; font-size:16px; cursor:pointer; font-family:inherit; font-weight:600; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Cài đặt mạng LAN</h2>
    <p>Nhập thông tin WiFi nhà bạn để Gateway kết nối.</p>
    <form action="/save_wifi" method="POST" accept-charset="UTF-8">
      <label>Tên WiFi (SSID):</label>
      <input type="text" name="ssid" required placeholder="Ví dụ: WiFi_Nha_Toi">
      <label>Mật khẩu:</label>
      <input type="password" name="password" placeholder="Để trống nếu không có mật khẩu">
      <input type="submit" value="Lưu và khởi động lại">
    </form>
  </div>
</body>
</html>
)rawliteral";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>SmartHome Gateway — Bảng điều khiển</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Be+Vietnam+Pro:wght@300;400;500;600;700&display=swap');
    :root {
      --bg: #0f172a; --surface: #1e293b; --surface2: #293548;
      --accent: #6366f1; --accent2: #22d3ee; --green: #22c55e;
      --red: #ef4444; --yellow: #eab308; --text: #e2e8f0;
      --text2: #94a3b8; --border: #334155;
    }
    * { margin:0; padding:0; box-sizing:border-box; }
    body {
      font-family:'Be Vietnam Pro','Segoe UI',system-ui,sans-serif;
      background:var(--bg); color:var(--text); min-height:100vh; padding:24px 16px;
      -webkit-font-smoothing:antialiased;
    }
    .header { display:flex; align-items:center; justify-content:space-between; max-width:1100px; margin:0 auto 28px; flex-wrap:wrap; gap:12px; }
    .header h1 { font-size:1.5rem; font-weight:700; background:linear-gradient(135deg,var(--accent),var(--accent2)); -webkit-background-clip:text; -webkit-text-fill-color:transparent; background-clip:text; }
    .header .subtitle { font-size:.8rem; color:var(--text2); margin-top:2px; }
    .badge { background:var(--green); color:#fff; font-size:.7rem; font-weight:600; padding:3px 10px; border-radius:99px; }
    .badge.offline { background:var(--red); }
    .grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(300px,1fr)); gap:18px; max-width:1100px; margin:0 auto; }
    .node-card { background:var(--surface); border:1px solid var(--border); border-radius:16px; padding:20px; transition:transform .2s,box-shadow .2s; position:relative; overflow:hidden; }
    .node-card::before { content:''; position:absolute; top:0; left:0; right:0; height:3px; background:linear-gradient(90deg,var(--accent),var(--accent2)); }
    .node-card:hover { transform:translateY(-3px); box-shadow:0 12px 32px rgba(0,0,0,.4); }
    .node-card.inactive { opacity:.85; border:1px dashed var(--border); }
    .card-top { display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:16px; }
    .node-title { font-size:1rem; font-weight:600; }
    .node-mac { font-size:.72rem; color:var(--text2); margin-top:3px; font-family:ui-monospace,monospace; }
    .type-badge { font-size:.68rem; font-weight:600; padding:4px 10px; border-radius:99px; white-space:nowrap; }
    .type-relay { background:rgba(99,102,241,.18); color:#a5b4fc; border:1px solid rgba(99,102,241,.3); }
    .type-sensor { background:rgba(34,211,238,.12); color:#67e8f9; border:1px solid rgba(34,211,238,.25); }
    .type-hybrid { background:rgba(234,179,8,.12); color:#fde047; border:1px solid rgba(234,179,8,.25); }
    .type-ir { background:rgba(239,68,68,.12); color:#fca5a5; border:1px solid rgba(239,68,68,.25); }
    .sensors { display:grid; grid-template-columns:1fr 1fr; gap:12px; margin-bottom:16px; }
    .sensor-box { background:var(--surface2); border-radius:10px; padding:12px; text-align:center; }
    .sensor-label { font-size:.68rem; color:var(--text2); margin-bottom:4px; text-transform:uppercase; letter-spacing:.04em; }
    .sensor-val { font-size:1.35rem; font-weight:700; }
    .sensor-val.temp { color:#fb923c; }
    .sensor-val.hum { color:var(--accent2); }
    .sensor-val.na { color:var(--border); font-size:1rem; }
    .card-bottom { display:flex; justify-content:space-between; align-items:center; gap:8px; flex-wrap:wrap; }
    .last-seen { font-size:.7rem; color:var(--text2); }
    .controls { display:flex; gap:8px; }
    .btn { border:none; padding:7px 16px; border-radius:8px; font-size:.8rem; font-weight:600; cursor:pointer; transition:all .15s; font-family:inherit; }
    .btn-on { background:var(--green); color:#fff; }
    .btn-on:hover { background:#16a34a; transform:scale(1.04); }
    .btn-off { background:var(--red); color:#fff; }
    .btn-off:hover { background:#dc2626; transform:scale(1.04); }
    .btn-del { background:transparent; color:var(--text2); border:1px solid var(--border); }
    .btn-del:hover { border-color:var(--red); color:var(--red); }
    .status-dot { display:inline-block; width:8px; height:8px; border-radius:50%; margin-right:5px; }
    .dot-on { background:var(--green); box-shadow:0 0 6px var(--green); }
    .dot-off { background:var(--border); }
    .status-text { font-size:.8rem; font-weight:600; }
    .add-section { max-width:1100px; margin:28px auto 0; background:var(--surface); border:1px solid var(--border); border-radius:16px; padding:24px; }
    .add-section h2 { font-size:1rem; font-weight:600; margin-bottom:16px; color:var(--accent2); }
    .form-row { display:flex; gap:12px; flex-wrap:wrap; align-items:flex-end; }
    .form-row label { font-size:.8rem; color:var(--text2); }
    .form-row input, .form-row select {
      background:var(--surface2); border:1px solid var(--border); border-radius:8px;
      padding:8px 14px; color:var(--text); font-size:.85rem; font-family:inherit; outline:none;
    }
    .form-row input:focus, .form-row select:focus { border-color:var(--accent); }
    .btn-add { background:linear-gradient(135deg,var(--accent),#4f46e5); color:#fff; padding:9px 22px; border-radius:8px; font-size:.85rem; font-weight:600; border:none; cursor:pointer; font-family:inherit; }
    .btn-add:hover { opacity:.88; }
    .empty-state { text-align:center; padding:40px; color:var(--text2); grid-column:1/-1; }
    .empty-state .icon { font-size:2.5rem; margin-bottom:10px; }
    .toast { position:fixed; bottom:24px; right:24px; background:var(--green); color:#fff; padding:12px 22px; border-radius:10px; font-size:.85rem; font-weight:600; opacity:0; transform:translateY(10px); transition:all .3s; pointer-events:none; z-index:999; max-width:90vw; }
    .toast.show { opacity:1; transform:translateY(0); }
    .toast.error { background:var(--red); }
    .refresh-btn { background:transparent; border:1px solid var(--border); color:var(--text2); padding:6px 14px; border-radius:8px; font-size:.78rem; cursor:pointer; font-family:inherit; }
    .refresh-btn:hover { border-color:var(--accent2); color:var(--accent2); }
    .disc-bar { max-width:1100px; margin:0 auto 18px; background:var(--surface); border:1px solid rgba(234,179,8,.35); border-radius:12px; padding:14px 20px; display:none; align-items:center; gap:14px; flex-wrap:wrap; }
    .disc-bar.visible { display:flex; }
    .disc-bar .disc-icon { font-size:1.4rem; }
    .disc-bar .disc-info { flex:1; min-width:160px; }
    .disc-bar .disc-title { font-weight:600; color:#fde047; font-size:.9rem; }
    .disc-bar .disc-sub { font-size:.75rem; color:var(--text2); margin-top:2px; }
    .disc-name-wrap { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
    .disc-name-input { background:var(--surface2); border:1px solid rgba(234,179,8,.5); border-radius:8px; padding:7px 12px; color:var(--text); font-size:.82rem; font-family:inherit; outline:none; width:200px; }
    .disc-name-input:focus { border-color:#fde047; }
    .btn-approve { background:var(--green); color:#fff; padding:7px 16px; border-radius:8px; font-size:.8rem; font-weight:600; border:none; cursor:pointer; white-space:nowrap; font-family:inherit; }
    .hint { margin-top:8px; font-size:.75rem; color:var(--text2); }
  </style>
</head>
<body>
  <div class="header">
    <div>
      <h1>&#127968; SmartHome Gateway</h1>
      <div class="subtitle">Quản lý thiết bị IoT qua ESP-NOW</div>
    </div>
    <div style="display:flex;gap:10px;align-items:center">
      <span id="wifi-badge" class="badge">Trực tuyến</span>
      <button class="refresh-btn" type="button" onclick="loadNodes()">&#8635; Làm mới</button>
    </div>
  </div>

  <div id="disc-bar" class="disc-bar">
    <div class="disc-icon">&#128276;</div>
    <div class="disc-info">
      <div class="disc-title">Phát hiện thiết bị mới!</div>
      <div class="disc-sub" id="disc-mac-text">MAC: --:--:--:--:--:--</div>
    </div>
    <div class="disc-name-wrap">
      <input class="disc-name-input" id="disc-name" type="text" placeholder="Tên thiết bị (VD: Đèn phòng ngủ)" maxlength="32">
      <button class="btn-approve" id="disc-btn" type="button" onclick="approveDevice()">&#10003; Thêm vào mạng</button>
    </div>
  </div>

  <div class="grid" id="node-grid">
    <div class="empty-state"><div class="icon">&#128268;</div><div>Đang tải dữ liệu...</div></div>
  </div>

  <div class="add-section">
    <h2>&#43; Thêm thiết bị thủ công</h2>
    <form id="add-form" onsubmit="addNode(event)" accept-charset="UTF-8">
      <div class="form-row">
        <div>
          <label>Tên thiết bị</label><br>
          <input type="text" id="f-name" name="name" placeholder="VD: Quạt phòng khách" maxlength="32" required>
        </div>
        <div>
          <label>Địa chỉ MAC</label><br>
          <input type="text" id="f-mac" name="mac" placeholder="AA:BB:CC:DD:EE:FF" pattern="[0-9A-Fa-f:]{17}" required>
        </div>
        <div>
          <label>Loại thiết bị</label><br>
          <select id="f-type" name="type">
            <option value="1">Relay (Bật/Tắt)</option>
            <option value="2">Hồng ngoại (IR)</option>
            <option value="3">Cảm biến</option>
            <option value="4">Hybrid (Relay + Cảm biến)</option>
          </select>
        </div>
        <div><button type="submit" class="btn-add" id="btn-add-node">Thêm thiết bị</button></div>
      </div>
      <div class="hint">&#8505; ID tự động gán. Không bấm nhiều lần — MAC trùng sẽ bị từ chối.</div>
    </form>
  </div>

  <div id="toast" class="toast" role="status"></div>

  <script>
    var TYPE_LABELS = {
      1: 'Relay',
      2: 'Hồng ngoại (IR)',
      3: 'Cảm biến',
      4: 'Hybrid (Relay + Cảm biến)'
    };
    var TYPE_CSS = {1:'type-relay', 2:'type-ir', 3:'type-sensor', 4:'type-hybrid'};
    var discoveredMac = '';
    var discoveredType = 1;
    var addInFlight = false;
    var approveInFlight = false;

    function showToast(msg, isError) {
      var t = document.getElementById('toast');
      t.textContent = msg;
      t.className = 'toast show' + (isError ? ' error' : '');
      setTimeout(function(){ t.className = 'toast'; }, 2800);
    }

    /** s = số giây trước (từ Gateway millis), không dùng Date.now() */
    function formatAgo(s) {
      s = Number(s) || 0;
      if (s < 5)  return 'vừa xong';
      if (s < 60) return s + ' giây trước';
      if (s < 3600) return Math.floor(s / 60) + ' phút trước';
      return Math.floor(s / 3600) + ' giờ trước';
    }

    function hasRelay(type)  { return type === 1 || type === 4; }
    function hasSensor(type) { return type === 3 || type === 4; }

    function buildCard(node) {
      var active      = node.active;
      var typeLabel   = TYPE_LABELS[node.device_type] || 'Không rõ';
      var typeCSS     = TYPE_CSS[node.device_type]    || 'type-relay';
      var isOn        = node.status === 1;
      var displayName = node.name || ('Node #' + node.id);

      var sensorHTML = '';
      if (hasSensor(node.device_type)) {
        var hasData = active && (node.temperature !== 0 || node.humidity !== 0);
        var tempVal = hasData ? node.temperature.toFixed(1) + '°C' : '--';
        var humVal  = hasData ? node.humidity.toFixed(1) + '%' : '--';
        var tempCls = hasData ? 'temp' : 'na';
        var humCls  = hasData ? 'hum'  : 'na';
        sensorHTML =
          '<div class="sensors">' +
            '<div class="sensor-box">' +
              '<div class="sensor-label">&#127777; Nhiệt độ</div>' +
              '<div class="sensor-val ' + tempCls + '">' + tempVal + '</div>' +
            '</div>' +
            '<div class="sensor-box">' +
              '<div class="sensor-label">&#128167; Độ ẩm</div>' +
              '<div class="sensor-val ' + humCls + '">' + humVal + '</div>' +
            '</div>' +
          '</div>';
      } else if (node.device_type === 2) {
        var hexCode = node.last_ir_data ? '0x' + Number(node.last_ir_data).toString(16).toUpperCase() : 'Chưa có';
        var lastCodeVal = node.last_ir_data || 0;
        var isRawToken = (lastCodeVal & 0x80000000) !== 0;
        var displayCode = isRawToken
          ? ('&#127911; Xung raw (' + (lastCodeVal & 0x7FFFFFFF) + ' xung)')
          : hexCode;

        if (node.is_learning) {
          sensorHTML =
            '<div class="sensors">' +
              '<div class="sensor-box" style="grid-column:1/-1; border:1px dashed var(--accent); text-align:center; padding:16px;">' +
                '<div style="color:var(--accent); font-weight:600; font-size:0.95rem; margin-bottom:6px;">&#128308; Đang chờ tín hiệu remote...</div>' +
                '<div style="color:var(--text2); font-size:0.8rem;">Đèn LED trên Node đang sáng. Hãy chĩa remote vào mắt thu và bấm phím cần học.</div>' +
              '</div>' +
            '</div>';
        } else if (lastCodeVal !== 0) {
          sensorHTML =
            '<div class="sensors">' +
              '<div class="sensor-box" style="grid-column:1/-1; border:1px solid var(--green); padding:12px;">' +
                '<div style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:8px; margin-bottom:8px;">' +
                  '<div style="color:var(--green); font-weight:600; font-size:0.9rem;">&#10004; Đã thu được mã IR mới:</div>' +
                  '<div class="sensor-val" style="color:var(--accent2);font-size:1.15rem;font-family:ui-monospace,monospace;">' + displayCode + '</div>' +
                '</div>' +
                '<div style="display:flex; gap:6px; flex-wrap:wrap;">' +
                  '<input type="text" id="ir-name-' + node.id + '" placeholder="VD: Bật TV Samsung..." style="flex:1; min-width:140px; padding:6px 10px; border-radius:6px; border:1px solid var(--border); background:var(--surface); color:var(--text); font-size:0.85rem; font-family:inherit;">' +
                  '<button class="btn btn-add" type="button" style="padding:6px 14px; font-size:0.85rem;" onclick="saveIrCmd(' + node.id + ',' + lastCodeVal + ')">&#128190; Lưu lệnh</button>' +
                '</div>' +
              '</div>' +
            '</div>';
        } else {
          sensorHTML =
            '<div class="sensors">' +
              '<div class="sensor-box" style="grid-column:1/-1; text-align:center; padding:12px;">' +
                '<div style="color:var(--text2); font-size:0.85rem;">Bấm «Bắt đầu học lệnh» bên dưới để thêm phím điều khiển.</div>' +
              '</div>' +
            '</div>';
        }
      } else {
        sensorHTML =
          '<div class="sensors">' +
            '<div class="sensor-box" style="grid-column:1/-1">' +
              '<div class="sensor-label">Loại node</div>' +
              '<div class="sensor-val" style="color:var(--text2);font-size:1rem">' + typeLabel + '</div>' +
            '</div>' +
          '</div>';
      }

      var controlHTML = '';
      if (hasRelay(node.device_type)) {
        controlHTML =
          '<div class="controls">' +
            '<button class="btn btn-on" type="button" onclick="sendCmd(' + node.id + ',1)">&#9889; Bật</button>' +
            '<button class="btn btn-off" type="button" onclick="sendCmd(' + node.id + ',0)">&#9866; Tắt</button>' +
          '</div>';
      } else if (node.device_type === 2) {
        var cmds = node.ir_commands || [];
        var btnList = '';
        if (cmds.length === 0) {
          btnList = '<div style="color:var(--text2); font-size:0.8rem; text-align:center; padding:6px 0;">Chưa lưu lệnh điều khiển nào</div>';
        } else {
          for (var idx = 0; idx < cmds.length; idx++) {
            var c = cmds[idx];
            btnList += '<div style="display:flex; gap:6px; margin-bottom:6px;">' +
              '<button class="btn btn-on" type="button" style="flex:1; padding:8px; font-size:0.85rem;" onclick="sendIrCmd(' + node.id + ',' + c.c + ')">&#128225; ' + c.n + '</button>' +
              '<button class="btn btn-off" type="button" style="padding:8px 12px;" onclick="delIrCmd(' + node.id + ',' + idx + ')">&#10005;</button>' +
              '</div>';
          }
        }
        var learnBtn = node.is_learning
          ? '<button class="btn" type="button" style="width:100%; background:var(--surface2); color:var(--text2); border:1px solid var(--border); margin-bottom:10px; cursor:not-allowed;" disabled>&#128308; Đang chờ remote...</button>'
          : '<button class="btn btn-add" type="button" style="width:100%; margin-bottom:10px; padding:8px; font-weight:600;" onclick="startIrLearn(' + node.id + ')">&#128226; Bắt đầu học lệnh</button>';

        controlHTML =
          '<div class="controls" style="flex-direction:column; gap:0; width:100%;">' +
            learnBtn +
            '<div style="border-top:1px solid var(--border); padding-top:8px; width:100%;">' + btnList + '</div>' +
          '</div>';
      }

      var statusHTML = '<div></div>';
      if (hasRelay(node.device_type)) {
        var dotCls  = isOn ? 'dot-on' : 'dot-off';
        var stColor = isOn ? 'var(--green)' : 'var(--text2)';
        var stText  = isOn ? 'BẬT' : 'TẮT';
        statusHTML =
          '<div>' +
            '<span class="status-dot ' + dotCls + '"></span>' +
            '<span class="status-text" style="color:' + stColor + '">' + stText + '</span>' +
          '</div>';
      }

      // Thời gian: relay → lúc bật/tắt; khác → lần cập nhật gần nhất
      var timeHTML = '';
      if (hasRelay(node.device_type)) {
        if (node.has_status_time) {
          timeHTML = (isOn ? '&#128994; Bật · ' : '&#128308; Tắt · ') +
                     formatAgo(node.status_changed_ago_s);
        } else {
          timeHTML = isOn ? '&#128994; Đang BẬT' : '&#128308; Đang TẮT';
        }
        if (active && node.last_seen_ago_s !== undefined) {
          timeHTML += '<br><span style="opacity:.75">Cập nhật · ' + formatAgo(node.last_seen_ago_s) + '</span>';
        }
      } else {
        timeHTML = active
          ? ('&#128344; Cập nhật · ' + formatAgo(node.last_seen_ago_s))
          : 'Chưa nhận dữ liệu';
      }

      return (
        '<div class="node-card ' + (active ? '' : 'inactive') + '">' +
          '<div class="card-top">' +
            '<div>' +
              '<div class="node-title">' + displayName + '</div>' +
              '<div class="node-mac">ID: ' + node.id + ' &nbsp;|&nbsp; ' + node.mac + '</div>' +
            '</div>' +
            '<span class="type-badge ' + typeCSS + '">' + typeLabel + '</span>' +
          '</div>' +
          sensorHTML +
          '<div class="card-bottom">' +
            statusHTML +
            '<div class="last-seen">' + timeHTML + '</div>' +
            controlHTML +
            '<button class="btn btn-del" type="button" title="Xóa thiết bị" onclick="deleteNode(' + node.id + ')">&#128465;</button>' +
          '</div>' +
        '</div>'
      );
    }

    function loadNodes() {
      fetch('/api/nodes')
        .then(function(r) { return r.json(); })
        .then(function(data) {
          var grid = document.getElementById('node-grid');
          if (!data || !data.length) {
            grid.innerHTML = '<div class="empty-state"><div class="icon">&#128268;</div><div>Chưa có thiết bị nào. Hãy thêm thiết bị bên dưới.</div></div>';
            return;
          }
          var focusedId  = null;
          var focusedVal = '';
          if (document.activeElement && document.activeElement.tagName === 'INPUT') {
            focusedId  = document.activeElement.id;
            focusedVal = document.activeElement.value;
          }
          grid.innerHTML = data.map(buildCard).join('');
          if (focusedId) {
            var el = document.getElementById(focusedId);
            if (el) { el.value = focusedVal; el.focus(); }
          }
        })
        .catch(function() { showToast('Lỗi tải dữ liệu!', true); });
    }

    function sendCmd(nodeId, onOff) {
      fetch('/api/control', {
        method: 'POST',
        headers: {'Content-Type': 'application/json; charset=utf-8'},
        body: JSON.stringify({node_id: nodeId, command: onOff ? 1 : 2})
      })
      .then(function(r) { return r.json(); })
      .then(function() {
        showToast(onOff ? 'Đã gửi lệnh BẬT node #' + nodeId : 'Đã gửi lệnh TẮT node #' + nodeId);
        setTimeout(loadNodes, 600);
      })
      .catch(function() { showToast('Lỗi gửi lệnh!', true); });
    }

    function sendIrCmd(nodeId, irData) {
      if (!irData) {
        showToast('Chưa có mã IR để phát!', true);
        return;
      }
      fetch('/api/ir/send', {
        method: 'POST',
        headers: {'Content-Type': 'application/json; charset=utf-8'},
        body: JSON.stringify({node_id: nodeId, ir_data: irData})
      })
      .then(function(r) { return r.json(); })
      .then(function() { showToast('Đã gửi lệnh phát IR → node #' + nodeId); })
      .catch(function() { showToast('Lỗi gửi lệnh IR!', true); });
    }

    function startIrLearn(nodeId) {
      fetch('/api/ir/learn', {
        method: 'POST',
        headers: {'Content-Type': 'application/json; charset=utf-8'},
        body: JSON.stringify({node_id: nodeId})
      })
      .then(function(r) { return r.json(); })
      .then(function() {
        showToast('Đã bật LED, đang chờ remote (node #' + nodeId + ')...');
        loadNodes();
      })
      .catch(function() { showToast('Lỗi gửi lệnh học IR!', true); });
    }

    function saveIrCmd(nodeId, code) {
      if (!code) { showToast('Chưa có mã IR để lưu!', true); return; }
      var nameInput = document.getElementById('ir-name-' + nodeId);
      var name = nameInput ? nameInput.value.trim() : '';
      if (!name) {
        showToast('Vui lòng nhập tên lệnh!', true);
        if (nameInput) nameInput.focus();
        return;
      }
      var btn = nameInput && nameInput.parentElement
        ? nameInput.parentElement.querySelector('button') : null;
      if (btn) { btn.disabled = true; }
      fetch('/api/ir/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/json; charset=utf-8'},
        body: JSON.stringify({node_id: nodeId, name: name, code: code})
      })
      .then(function(r) { return r.json(); })
      .then(function() {
        showToast('Đã lưu lệnh «' + name + '»!');
        if (nameInput) nameInput.value = '';
        // Tải lại ngay — form «mã mới» sẽ biến mất (last_ir_data=0 trên server)
        loadNodes();
        setTimeout(loadNodes, 400);
      })
      .catch(function() { showToast('Lỗi lưu lệnh IR!', true); })
      .finally(function() { if (btn) btn.disabled = false; });
    }

    function delIrCmd(nodeId, idx) {
      if (!confirm('Xóa lệnh điều khiển này?')) return;
      fetch('/api/ir/delete', {
        method: 'POST',
        headers: {'Content-Type': 'application/json; charset=utf-8'},
        body: JSON.stringify({node_id: nodeId, index: idx})
      })
      .then(function(r) { return r.json(); })
      .then(function() { showToast('Đã xóa lệnh!'); loadNodes(); })
      .catch(function() { showToast('Lỗi xóa lệnh IR!', true); });
    }

    function deleteNode(id) {
      if (!confirm('Xóa thiết bị này khỏi hệ thống?')) return;
      fetch('/api/delete?id=' + id)
        .then(function() { showToast('Đã xóa thiết bị!'); loadNodes(); });
    }

    function addNode(e) {
      e.preventDefault();
      if (addInFlight) {
        showToast('Đang thêm thiết bị, vui lòng chờ...', true);
        return;
      }
      var name = document.getElementById('f-name').value.trim() || 'Thiết bị';
      var mac  = document.getElementById('f-mac').value.trim();
      var type = document.getElementById('f-type').value;
      if (!mac) {
        showToast('Nhập địa chỉ MAC!', true);
        return;
      }
      addInFlight = true;
      var btn = document.getElementById('btn-add-node');
      if (btn) { btn.disabled = true; btn.textContent = 'Đang thêm...'; }
      fetch('/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded; charset=utf-8'},
        body: 'name=' + encodeURIComponent(name) + '&mac=' + encodeURIComponent(mac) + '&type=' + type
      })
      .then(function(r) { return r.text(); })
      .then(function(html) {
        if (html && html.indexOf('đã có sẵn') !== -1) {
          showToast('MAC đã tồn tại — không tạo trùng.', true);
        } else {
          showToast('Đã thêm «' + name + '» thành công!');
          e.target.reset();
        }
        loadNodes();
      })
      .catch(function() { showToast('Lỗi thêm thiết bị!', true); })
      .finally(function() {
        addInFlight = false;
        if (btn) { btn.disabled = false; btn.textContent = 'Thêm thiết bị'; }
      });
    }

    function approveDevice() {
      if (approveInFlight) {
        showToast('Đang thêm thiết bị, vui lòng chờ...', true);
        return;
      }
      var nameInput = document.getElementById('disc-name');
      var name = nameInput.value.trim();
      if (!name) {
        nameInput.focus();
        nameInput.style.borderColor = 'var(--red)';
        return;
      }
      if (!discoveredMac) {
        showToast('Không có MAC phát hiện!', true);
        return;
      }
      nameInput.style.borderColor = '';
      approveInFlight = true;
      var btn = document.getElementById('disc-btn');
      if (btn) { btn.disabled = true; }
      fetch('/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded; charset=utf-8'},
        body: 'name=' + encodeURIComponent(name) + '&mac=' + encodeURIComponent(discoveredMac) + '&type=' + discoveredType
      })
      .then(function(r) { return r.text(); })
      .then(function(html) {
        if (html && html.indexOf('đã có sẵn') !== -1) {
          showToast('MAC đã tồn tại — không tạo trùng.', true);
        } else {
          showToast('Đã thêm «' + name + '» thành công!');
        }
        document.getElementById('disc-bar').classList.remove('visible');
        nameInput.value = '';
        discoveredMac = '';
        loadNodes();
      })
      .catch(function() { showToast('Lỗi!', true); })
      .finally(function() {
        approveInFlight = false;
        if (btn) { btn.disabled = false; }
      });
    }

    function checkDiscovery() {
      fetch('/api/discovered_mac')
        .then(function(r) { return r.json(); })
        .then(function(data) {
          if (data && data.mac && data.mac !== '00:00:00:00:00:00' && data.mac !== discoveredMac) {
            discoveredMac = data.mac;
            discoveredType = data.type;
            var loai = TYPE_LABELS[data.type] || ('Loại ' + data.type);
            document.getElementById('disc-mac-text').textContent = 'MAC: ' + data.mac + ' | Loại: ' + loai;
            document.getElementById('disc-bar').classList.add('visible');
          }
        })
        .catch(function() {});
    }

    setInterval(loadNodes, 2500);
    setInterval(checkDiscovery, 6000);
    loadNodes();
    checkDiscovery();
  </script>
</body>
</html>
)rawliteral";
