#ifndef WS_ONLY_H
#define WS_ONLY_H

#define WS_ONLY_BODY \
  "<!DOCTYPE html>" \
  "<html lang=\"pt-BR\">" \
  "<head>" \
  "  <meta charset=\"utf-8\">" \
  "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">" \
  "  <title>Acesso Restrito</title>" \
  "  <style>" \
  "    body { margin:0; font-family:sans-serif; background:#121212; color:#eee; display:flex; justify-content:center; align-items:center; height:100vh; text-align:center; padding:1rem; }" \
  "    .container { background:#1e1e1e; padding:2rem; border-radius:12px; box-shadow: 0 4px 20px rgba(0,0,0,0.5); max-width:600px; max-height:1200px; }" \
  "    h1 { color:#f1c40f; font-size:1.5rem; margin-bottom:1rem; }" \
  "    p { line-height:1.6; color:#ccc; margin-bottom:2rem; }" \
  "    .btn { background:#3498db; color:white; text-decoration:none; padding:0.8rem 1.5rem; border-radius:6px; font-weight:bold; transition: background 0.2s; }" \
  "    .btn:hover { background:#2980b9; }" \
  "    .code { font-family:monospace; background:#000; padding:0.2rem 0.4rem; border-radius:4px; color:#e74c3c; }" \
  "  </style>" \
  "</head>" \
  "<body>" \
  "  <div class=\"container\">" \
  "    <h1>🔌 Endpoint de Dados</h1>" \
  "    <p>Os caminhos <span class=\"code\">/ws</span> são exclusivos para comunicação via protocolo <strong>WebSocket</strong> e não suportam visualização direta no navegador.</p>" \
  "    <a href=\"/\" class=\"btn\">Voltar para a Home</a>" \
  "  </div>" \
  "</body>" \
  "</html>"

#endif /* WS_ONLY_H */