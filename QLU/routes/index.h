#ifndef INDEX_H
#define INDEX_H

#define INDEX_BODY \
  "<!DOCTYPE html>" \
  "<html lang=\"pt-BR\">" \
  "<head>" \
  "  <meta charset=\"utf-8\">" \
  "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">" \
  "  <title>QLU Monitor</title>" \
  "  <style>" \
  "    body { margin:0; font-family:'Courier New', monospace; background:#121212; color:#eee; display:flex; flex-direction:column; align-items:center; padding:1rem; }" \
  "    .metrics-container { display:grid; grid-template-columns: repeat(auto-fit, minmax(100px, 1fr)); gap:1rem; width:80vw; margin-bottom:1rem; }" \
  "    .metric-card { background:#1e1e1e; padding:1rem; border-radius:8px; text-align:center; border: 1px solid #333; }" \
  "    .metric-card h3 { margin:0 0 0.5rem 0; font-size:0.8rem; color:#888; text-transform:uppercase; letter-spacing:1px; }" \
  "    .metric-card span { font-size:1.4rem; font-weight:bold; color:#4cc9f0; }" \
  "    .canvas-wrapper { position: relative; width: 80vw; height: 80vh; background: #000; border-radius: 8px; border: 1px solid #333; overflow: hidden; }" \
  "    canvas { display: block; width: 100%; height: 100%; }" \
  "    .axis-label { position: absolute; color: #fff; font-size: 0.7rem; pointer-events: none; opacity: 0.6; }" \
  "    .lbl-top { top: 5px; left: 50%; transform: translateX(-50%); }" \
  "    .lbl-right { top: 50%; right: 5px; transform: translateY(-50%); }" \
  "  </style>" \
  "</head>" \
  "<body>" \
  "  <div class=\"metrics-container\">" \
  "    <div class=\"metric-card\"><h3>SNR (dB)</h3><span id=\"snr\">--</span></div>" \
  "    <div class=\"metric-card\"><h3>MER (dB)</h3><span id=\"mer\">--</span></div>" \
  "    <div class=\"metric-card\"><h3>EVM (%%)</h3><span id=\"evm\">--</span></div>" \
  "    <div class=\"metric-card\"><h3>C/N0</h3><span id=\"cn0\">--</span></div>" \
  "  </div>" \
  "  <div class=\"canvas-wrapper\">" \
  "    <div class=\"axis-label lbl-top\">Q +1.5</div>" \
  "    <div class=\"axis-label lbl-right\">I +1.5</div>" \
  "    <canvas id=\"iqCanvas\"></canvas>" \
  "  </div>" \
  "  <script>" \
  "    const cvs = document.getElementById('iqCanvas');" \
  "    const ctx = cvs.getContext('2d');" \
  "    const MAX_VAL = 1.5;" \
  "    function resize() {" \
  "      const rect = cvs.parentElement.getBoundingClientRect();" \
  "      cvs.width = rect.width * window.devicePixelRatio;" \
  "      cvs.height = rect.height * window.devicePixelRatio;" \
  "      ctx.scale(window.devicePixelRatio, window.devicePixelRatio);" \
  "      cvs.style.width = rect.width + 'px';" \
  "      cvs.style.height = rect.height + 'px';" \
  "    }" \
  "    window.addEventListener('resize', resize);" \
  "    resize();" \
  "    function drawConstellation(points) {" \
  "      const w = cvs.parentElement.clientWidth;" \
  "      const h = cvs.parentElement.clientHeight;" \
  "      const cx = w / 2;" \
  "      const cy = h / 2;" \
  "      const scale = (Math.min(w, h) / 2) / (MAX_VAL * 1.1);" \
  "      ctx.fillStyle = '#000000';" \
  "      ctx.fillRect(0, 0, w, h);" \
  "      /* EIXOS BRANCOS (com 30% de opacidade para não ofuscar os pontos) */" \
  "      ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';" \
  "      ctx.lineWidth = 1;" \
  "      ctx.beginPath();" \
  "      ctx.moveTo(0, cy); ctx.lineTo(w, cy);" \
  "      ctx.moveTo(cx, 0); ctx.lineTo(cx, h);" \
  "      ctx.stroke();" \
  "      /* CÍRCULO UNITÁRIO BRANCO (opacidade 15%) */" \
  "      ctx.strokeStyle = 'rgba(255, 255, 255, 0.15)';" \
  "      ctx.beginPath(); ctx.arc(cx, cy, 1.0 * scale, 0, 2*Math.PI); ctx.stroke();" \
  "      /* PONTOS (Ciano Neon) */" \
  "      ctx.fillStyle = '#00ffcc';" \
  "      ctx.shadowBlur = 5;" \
  "      ctx.shadowColor = '#00ffcc';" \
  "      for (let p of points) {" \
  "        const px = cx + (p.i * scale);" \
  "        const py = cy - (p.q * scale);" \
  "        ctx.beginPath();" \
  "        ctx.arc(px, py, 3, 0, 2 * Math.PI);" \
  "        ctx.fill();" \
  "      }" \
  "      /* Reset shadow para não afetar outros desenhos */" \
  "      ctx.shadowBlur = 0;" \
  "    }" \
  "    const params = new URLSearchParams(window.location.search);" \
  "    const ip = params.get('ip') || location.hostname;" \
  "    const sock = new WebSocket('ws://' + ip + '/ws/stream');" \
  "    sock.onmessage = evt => {" \
  "      try {" \
  "        const data = JSON.parse(evt.data);" \
  "        document.getElementById('snr').textContent = data.snr.toFixed(1);" \
  "        document.getElementById('mer').textContent = data.mer.toFixed(1);" \
  "        document.getElementById('evm').textContent = data.evm.toFixed(1);" \
  "        document.getElementById('cn0').textContent = data.cn0.toFixed(1);" \
  "        if (data.points) drawConstellation(data.points);" \
  "      } catch (e) { console.error(e); }" \
  "    };" \
  "  </script>" \
  "</body>" \
  "</html>"

#endif /* INDEX_H */