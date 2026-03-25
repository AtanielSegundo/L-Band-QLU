#ifndef INDEX_H
#define INDEX_H

#define INDEX_BODY \
  "<!DOCTYPE html><html><head><meta charset=\"utf-8\">" \
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">" \
  "<title>QLU</title><style>" \
  "*{box-sizing:border-box;margin:0;padding:0}" \
  "body{font-family:system-ui,sans-serif;background:#0a0a0f;color:#e0e0e0;min-height:100vh;padding:10px;display:flex;flex-direction:column;align-items:center;gap:10px}" \
  ".top{display:flex;gap:10px;width:92vw;max-width:900px;align-items:stretch}" \
  ".ctl{flex:1;background:#13131a;border-radius:10px;border:1px solid #222;padding:14px;display:flex;flex-direction:column;gap:10px;justify-content:center}" \
  ".ctl h4{font-size:.55rem;text-transform:uppercase;letter-spacing:2px;color:#555}" \
  ".cg{display:flex;flex-direction:column;gap:3px}" \
  ".cg label{font-size:.65rem;color:#666;text-transform:uppercase;letter-spacing:1px}" \
  "select,input[type=number]{background:#0a0a0f;color:#fff;border:1px solid #333;padding:7px;border-radius:5px;font-size:.85rem;width:100%%}" \
  "select:focus,input:focus{outline:0;border-color:#4cc9f0;box-shadow:0 0 0 2px rgba(76,201,240,.15)}" \
  ".fh{flex:1;background:#13131a;border-radius:10px;border:1px solid #222;padding:18px;display:flex;flex-direction:column;align-items:center;justify-content:center;cursor:pointer;user-select:none;transition:border-color .3s,transform .15s;position:relative;overflow:hidden}" \
  ".fh:hover{border-color:#333;transform:scale(1.01)}" \
  ".sq{font-size:2.8rem;font-weight:800;line-height:1;letter-spacing:-2px;transition:color .5s}" \
  ".su{font-size:.8rem;font-weight:400;opacity:.4;margin-left:2px}" \
  ".gt{font-size:.8rem;font-weight:600;margin-top:5px;text-transform:uppercase;letter-spacing:3px;transition:color .5s}" \
  ".eh{font-size:.5rem;color:#444;margin-top:6px;letter-spacing:1px}" \
  ".dt{width:92vw;max-width:900px;max-height:0;overflow:hidden;transition:max-height .4s cubic-bezier(.4,0,.2,1),opacity .3s;opacity:0}" \
  ".dt.open{max-height:120px;opacity:1}" \
  ".mr{display:grid;grid-template-columns:repeat(5,1fr);gap:8px}" \
  ".mc{background:#13131a;border-radius:8px;border:1px solid #222;padding:12px 8px;text-align:center}" \
  ".mc:hover{border-color:#333}" \
  ".ml{font-size:.55rem;text-transform:uppercase;letter-spacing:1.5px;color:#555;margin-bottom:5px}" \
  ".mv{font-size:1.2rem;font-weight:700;color:#fff}" \
  ".ms{font-size:.6rem;font-weight:400;color:#555}" \
  ".ge{color:#00e676}.gg{color:#4cc9f0}.gf{color:#ffd600}.gp{color:#ff9100}.gc{color:#ff1744}" \
  ".be{border-color:rgba(0,230,118,.25)}.bg{border-color:rgba(76,201,240,.25)}.bf{border-color:rgba(255,214,0,.25)}.bp{border-color:rgba(255,145,0,.25)}.bc{border-color:rgba(255,23,68,.25)}" \
  ".cw{position:relative;width:92vw;max-width:900px;flex:1;min-height:50vh;background:#06060a;border-radius:10px;border:1px solid #222;overflow:hidden}" \
  "canvas{display:block;width:100%%;height:100%%}" \
  ".al{position:absolute;color:#fff;font-size:.55rem;pointer-events:none;opacity:.25;font-family:monospace}" \
  ".aq{top:6px;left:50%%;transform:translateX(-50%%)}.ai{top:50%%;right:6px;transform:translateY(-50%%)}" \
  ".cd{width:6px;height:6px;border-radius:50%%;display:inline-block;margin-right:3px}" \
  ".cn{background:#00e676;box-shadow:0 0 5px #00e676}.cf{background:#ff1744;box-shadow:0 0 5px #ff1744}" \
  ".sb{font-size:.5rem;color:#444;letter-spacing:1px;text-transform:uppercase}" \
  "@media(max-width:640px){.top{flex-direction:column}.mr{grid-template-columns:repeat(3,1fr)}.sq{font-size:2rem}}" \
  "</style></head><body>" \
  "<div class=\"top\">" \
  "<div class=\"ctl\"><h4>Config</h4>" \
  "<div class=\"cg\"><label>Modulation</label><select id=\"ms\"><option value=\"0\">Loading...</option></select></div>" \
  "<div class=\"cg\"><label>Roll-off</label><input type=\"number\" id=\"ro\" min=\"0\" max=\"1\" step=\"0.01\" value=\"0.25\"></div>" \
  "<div class=\"sb\" id=\"st\"><span class=\"cd cf\" id=\"cd\"></span>Connecting...</div>" \
  "</div>" \
  "<div class=\"fh\" id=\"fh\">" \
  "<div class=\"sq\" id=\"sqi\">--<span class=\"su\">%%</span></div>" \
  "<div class=\"gt\" id=\"gr\">--</div>" \
  "<div class=\"eh\" id=\"eh\">CLICK FOR DETAILS</div>" \
  "</div></div>" \
  "<div class=\"dt\" id=\"dt\"><div class=\"mr\">" \
  "<div class=\"mc\"><div class=\"ml\">MER</div><div class=\"mv\" id=\"mer\">--<span class=\"ms\">dB</span></div></div>" \
  "<div class=\"mc\"><div class=\"ml\">EVM</div><div class=\"mv\" id=\"evm\">--<span class=\"ms\">%%</span></div></div>" \
  "<div class=\"mc\"><div class=\"ml\">C/N0</div><div class=\"mv\" id=\"cn0\">--<span class=\"ms\">dBHz</span></div></div>" \
  "<div class=\"mc\"><div class=\"ml\">Stability</div><div class=\"mv\" id=\"stb\">--<span class=\"ms\">%%</span></div></div>" \
  "<div class=\"mc\"><div class=\"ml\">Skew</div><div class=\"mv\" id=\"skw\">--<span class=\"ms\">pt</span></div></div>" \
  "</div></div>" \
  "<div class=\"cw\">" \
  "<div class=\"al aq\">Q</div><div class=\"al ai\">I</div>" \
  "<canvas id=\"cv\"></canvas></div>" \
  "<script>" \
  "const $=id=>document.getElementById(id);" \
  "const ip=new URLSearchParams(location.search).get('ip')||location.hostname;" \
  "const fh=$('fh'),dt=$('dt'),eh=$('eh');" \
  "let ex=0,hT,lT;" \
  "function sE(v){ex=v;dt.classList.toggle('open',v);eh.textContent=v?'CLICK TO COLLAPSE':'CLICK FOR DETAILS'}" \
  "fh.onclick=()=>{clearTimeout(hT);clearTimeout(lT);sE(!ex)};" \
  "fh.onmouseenter=()=>{clearTimeout(lT);if(!ex)hT=setTimeout(()=>sE(1),3e3)};" \
  "fh.onmouseleave=()=>{clearTimeout(hT);if(ex)lT=setTimeout(()=>sE(0),3e3)};" \
  "dt.onmouseenter=()=>clearTimeout(lT);" \
  "dt.onmouseleave=()=>{if(ex)lT=setTimeout(()=>sE(0),3e3)};" \
  "const cv=$('cv'),cx=cv.getContext('2d');" \
  "function rz(){const r=cv.parentElement.getBoundingClientRect(),d=devicePixelRatio;cv.width=r.width*d;cv.height=r.height*d;cx.scale(d,d);cv.style.width=r.width+'px';cv.style.height=r.height+'px'}" \
  "addEventListener('resize',rz);rz();" \
  "function dI(pts){" \
  "const w=cv.parentElement.clientWidth,h=cv.parentElement.clientHeight,mx=w/2,my=h/2,sc=Math.min(w,h)/2/1.65;" \
  "cx.fillStyle='#06060a';cx.fillRect(0,0,w,h);" \
  "cx.strokeStyle='rgba(255,255,255,.07)';cx.lineWidth=1;" \
  "for(let v=-1;v<=1;v+=.5){cx.beginPath();cx.moveTo(mx+v*sc,0);cx.lineTo(mx+v*sc,h);cx.stroke();cx.beginPath();cx.moveTo(0,my+v*sc);cx.lineTo(w,my+v*sc);cx.stroke()}" \
  "cx.strokeStyle='rgba(255,255,255,.18)';cx.beginPath();cx.moveTo(0,my);cx.lineTo(w,my);cx.moveTo(mx,0);cx.lineTo(mx,h);cx.stroke();" \
  "cx.strokeStyle='rgba(76,201,240,.1)';cx.setLineDash([3,3]);cx.beginPath();cx.arc(mx,my,sc,0,6.28);cx.stroke();cx.setLineDash([]);" \
  "cx.shadowBlur=3;cx.shadowColor='#00ffcc';cx.fillStyle='rgba(0,255,204,.7)';" \
  "for(let p of pts){cx.beginPath();cx.arc(mx+p.i*sc,my-p.q*sc,2.5,0,6.28);cx.fill()}" \
  "cx.shadowBlur=0}" \
  "const G={Excellent:['ge','be'],Good:['gg','bg'],Fair:['gf','bf'],Poor:['gp','bp'],Critical:['gc','bc']};" \
  "function gOf(v){return v>=90?'Excellent':v>=75?'Good':v>=55?'Fair':v>=30?'Poor':'Critical'}" \
  "let sS=null;" \
  "let wS;" \
  "function cS(){" \
  "wS=new WebSocket('ws://'+ip+'/ws/stream');" \
  "wS.onopen=()=>{$('cd').className='cd cn';$('st').innerHTML='<span class=\"cd cn\"></span>Connected'};" \
  "wS.onclose=()=>{$('cd').className='cd cf';$('st').innerHTML='<span class=\"cd cf\"></span>Reconnecting...';setTimeout(cS,2e3)};" \
  "wS.onmessage=e=>{try{const d=JSON.parse(e.data);" \
  "$('mer').innerHTML=d.mer.toFixed(1)+'<span class=\"ms\">dB</span>';" \
  "$('evm').innerHTML=d.evm.toFixed(1)+'<span class=\"ms\">%%</span>';" \
  "$('cn0').innerHTML=d.cn0.toFixed(1)+'<span class=\"ms\">dBHz</span>';" \
  "if(d.stability!=null)$('stb').innerHTML=d.stability.toFixed(1)+'<span class=\"ms\">%%</span>';" \
  "if(d.skew!=null)$('skw').innerHTML=d.skew.toFixed(1)+'<span class=\"ms\">pt</span>';" \
  "if(d.sqi!=null){sS=sS==null?d.sqi:.15*d.sqi+.85*sS;" \
  "const g=gOf(sS),c=G[g]||['',''];" \
  "$('sqi').innerHTML=sS.toFixed(1)+'<span class=\"su\">%%</span>';$('sqi').className='sq '+c[0];" \
  "$('gr').textContent=g;$('gr').className='gt '+c[0];" \
  "fh.className='fh '+c[1]}" \
  "if(d.points)dI(d.points)" \
  "}catch(x){}}}" \
  "cS();" \
  "const mS=$('ms'),rI=$('ro');" \
  "let wC;" \
  "function cC(){" \
  "wC=new WebSocket('ws://'+ip+'/ws/config');" \
  "wC.onopen=()=>{wC.send('INFO');setTimeout(()=>wC.send('CURRENT'),500)};" \
  "wC.onclose=()=>setTimeout(cC,2e3);" \
  "wC.onmessage=e=>{try{const d=JSON.parse(e.data);" \
  "if(d.options){mS.innerHTML='';d.options.forEach(o=>{const e=document.createElement('option');e.value=o.val;e.textContent=o.name;mS.appendChild(e)})}" \
  "if(d.modulation!=null)mS.value=d.modulation;" \
  "if(d.roll_off!=null)rI.value=d.roll_off" \
  "}catch(x){}}}" \
  "cC();" \
  "function sU(){if(wC&&wC.readyState==1)wC.send(JSON.stringify({type:'UPDATE_YOURS',modulation:+mS.value,roll_off:+rI.value}))}" \
  "mS.onchange=sU;rI.onchange=sU;" \
  "setInterval(()=>{if(wC&&wC.readyState==1)wC.send('CURRENT')},5e3)" \
  "</script></body></html>"

#endif /* INDEX_H */
