// Gera a pagina embarcada a partir da mesma documentacao usada no repositorio.
// Sem dependencias externas; execute novamente quando editar docs/API.md.
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const markdown = fs.readFileSync(path.join(root, 'docs/API.md'), 'utf8').replace(/\r\n/g, '\n');
const escape = value => value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
function inline(value) {
  return value.split(/(`[^`]+`|\*\*[^*]+\*\*|\[[^\]]+\]\([^)]+\))/g).map(part => {
    if (part.startsWith('`')) return '<code>' + escape(part.slice(1, -1)) + '</code>';
    if (part.startsWith('**')) return '<strong>' + escape(part.slice(2, -2)) + '</strong>';
    const link = part.match(/^\[([^\]]+)\]\(([^)]+)\)$/);
    if (link && /^(https?:\/\/|\/|#)/.test(link[2])) return '<a href="' + escape(link[2]) + '">' + escape(link[1]) + '</a>';
    return escape(part);
  }).join('');
}
const lines = markdown.trim().split('\n');
const rendered = [];
for (let i = 0; i < lines.length;) {
  const line = lines[i];
  if (!line.trim()) { ++i; continue; }
  if (line.startsWith('```')) {
    const code = [];
    for (++i; i < lines.length && !lines[i].startsWith('```'); ++i) code.push(lines[i]);
    if (i === lines.length) throw Error('Bloco de codigo nao fechado');
    rendered.push('<pre><code>' + escape(code.join('\n')) + '</code></pre>');
    ++i; continue;
  }
  const heading = line.match(/^(#{1,3}) (.+)$/);
  if (heading) {
    const level = heading[1].length;
    rendered.push('<h' + level + '>' + inline(heading[2]) + '</h' + level + '>');
    ++i; continue;
  }
  if (line.startsWith('|')) {
    const rows = [];
    while (i < lines.length && lines[i].startsWith('|')) rows.push(lines[i++]);
    const cells = row => row.replace(/^\||\|$/g, '').split('|').map(cell => cell.trim());
    if (!rows[1] || !cells(rows[1]).every(cell => /^:?-+:?$/.test(cell))) throw Error('Tabela Markdown invalida');
    const row = (value, tag) => '<tr>' + cells(value).map(cell => '<' + tag + '>' + inline(cell) + '</' + tag + '>').join('') + '</tr>';
    rendered.push('<div class="table-scroll"><table><thead>' + row(rows[0], 'th') + '</thead><tbody>' + rows.slice(2).map(value => row(value, 'td')).join('') + '</tbody></table></div>');
    continue;
  }
  const paragraph = [];
  while (i < lines.length && lines[i].trim() && !/^(#|\||```)/.test(lines[i])) paragraph.push(lines[i++]);
  if (!paragraph.length) throw Error('Markdown nao suportado: ' + line);
  rendered.push('<p>' + inline(paragraph.join(' ')) + '</p>');
}
const html = `<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Documentação da API — Relay Switch 5CH</title><style>
:root{color-scheme:dark;font:16px system-ui,sans-serif;color:#e7edf5;background:#101824}*{box-sizing:border-box}body{margin:0}main{max-width:1050px;margin:auto;padding:24px}nav{position:sticky;top:0;background:#101824;padding:16px 0;border-bottom:1px solid #34475c}a{color:#8ec4ff}h1{font-size:1.75rem}h2{font-size:1.25rem;margin-top:32px}p{line-height:1.6;color:#ced9e5}code{font-family:ui-monospace,monospace;font-size:.87em;overflow-wrap:anywhere;background:#1b2736;padding:2px 4px;border-radius:4px}pre{background:#1b2736;padding:16px;border:1px solid #34475c;border-radius:8px;overflow:auto;line-height:1.6}pre code{padding:0;white-space:pre;overflow-wrap:normal}.table-scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:.92rem}td,th{padding:12px;text-align:left;border:1px solid #34475c;vertical-align:top}th{background:#1b2736}@media(max-width:600px){main{padding:14px}td,th{padding:8px}h1{font-size:1.4rem}}
</style></head><body><main><nav><a href="/">← Voltar ao painel</a></nav>
${rendered.join('\n')}
</main><script>document.querySelectorAll('code').forEach(element=>{element.textContent=element.textContent.replaceAll('http://IP_DO_ESP',location.origin)});</script></body></html>`;
const header = '#pragma once\n#include <Arduino.h>\n\n// Gerado por node scripts/build-api-docs.cjs; fonte: docs/API.md.\nstatic const char API_DOCS_PAGE[] PROGMEM = R"APIDOC(' + html + ')APIDOC";\n';
const target = path.join(root, 'ApiDocsPage.h');
if (process.argv.includes('--check')) {
  if (!fs.existsSync(target) || fs.readFileSync(target, 'utf8').replace(/\r\n/g, '\n') !== header) {
    console.error('ApiDocsPage.h desatualizado; execute node scripts/build-api-docs.cjs');
    process.exitCode = 1;
  } else console.log('Documentacao embarcada sincronizada com docs/API.md');
} else {
  fs.writeFileSync(target, header);
  console.log('ApiDocsPage.h gerado: ' + Buffer.byteLength(html) + ' bytes de HTML');
}
