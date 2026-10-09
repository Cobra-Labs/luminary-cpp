const { startPwaServer } = require('/home/claude/proj/pwa-server.cjs');
const http = require('http'); const net = require('net'); const fs = require('fs');
const get = (port, path, method = 'GET') => new Promise((res, rej) => {
  const r = http.request({ host: '127.0.0.1', port, path, method }, resp => { let b = ''; resp.on('data', c => b += c); resp.on('end', () => res({ status: resp.statusCode, headers: resp.headers, body: b })); });
  r.on('error', rej); r.end();
});
const raw = (port, text) => new Promise(res => { const s = net.connect(port, '127.0.0.1', () => s.write(text)); let b = ''; s.on('data', d => b += d); s.on('close', () => res(b)); s.on('error', () => res(b)); setTimeout(() => { s.destroy(); }, 600); });
let failed = 0; const ok = (c, m) => { console.log((c ? 'OK   ' : 'FAIL ') + m); if (!c) failed++; };
(async () => {
  const dir = '/home/claude/proj/pwa/dist';
  const srv = await startPwaServer({ dir, port: 4180 });
  ok(srv.port === 4180, 'startet auf dem Wunschport ' + srv.port);
  let r = await get(4180, '/');                      ok(r.status === 200 && r.body.includes('<div id="root">') && /text\/html/.test(r.headers['content-type']), 'GET / liefert index.html');
  ok(r.headers['cache-control'] === 'no-cache', 'index.html wird nicht gecacht');
  const asset = fs.readdirSync(dir + '/assets').find(f => f.endsWith('.js'));
  r = await get(4180, '/assets/' + asset);           ok(r.status === 200 && /javascript/.test(r.headers['content-type']) && /immutable/.test(r.headers['cache-control']), 'gehashtes Asset: JS-Typ + immutable-Cache');
  r = await get(4180, '/sw.js');                     ok(r.status === 200 && r.headers['cache-control'] === 'no-cache', 'sw.js: kein Cache');
  r = await get(4180, '/manifest.json');             ok(r.status === 200 && /json/.test(r.headers['content-type']), 'manifest.json: JSON-Typ');
  r = await get(4180, '/icons/icon-192.png');        ok(r.status === 200 && r.headers['content-type'] === 'image/png', 'Icon: image/png');
  r = await get(4180, '/irgendeine/route');          ok(r.status === 200 && r.body.includes('<div id="root">'), 'SPA-Route ohne Endung -> index.html');
  r = await get(4180, '/assets/gibt-es-nicht.js');   ok(r.status === 404, 'fehlende Datei mit Endung -> 404 (kein index.html)');
  r = await get(4180, '/', 'HEAD');                  ok(r.status === 200 && r.body === '' && Number(r.headers['content-length']) > 100, 'HEAD: Header ohne Body');
  r = await get(4180, '/', 'POST');                  ok(r.status === 405 && r.headers.allow === 'GET, HEAD', 'POST -> 405');
  r = await get(4180, '/?x=1#y');                    ok(r.status === 200, 'Query/Fragment werden ignoriert');
  // Pfad-Ausbruch (roh gesendet, damit der Client nichts normalisiert)
  for (const p of ['/../../../../etc/passwd', '/..%2f..%2f..%2fetc/passwd', '/%2e%2e/%2e%2e/etc/passwd', '/assets/..%5c..%5c..%5cetc/passwd', '/%00', '/%']) {
    const resp = await raw(4180, `GET ${p} HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n`);
    ok(!/root:x:0:0/.test(resp) && /HTTP\/1\.1 (200|400|403|404)/.test(resp) && !(resp.includes('HTTP/1.1 200') && !resp.includes('<div id="root">')), `Pfad-Ausbruch abgewehrt: ${p}`);
  }
  // Port belegt -> naechster Port
  const second = await startPwaServer({ dir, port: 4180 });
  ok(second.port === 4181, 'Port 4180 belegt -> weicht auf ' + second.port + ' aus');
  await second.close();
  // Alle Ports belegt -> Fehler statt Absturz
  const blockers = []; for (let p = 4190; p < 4193; p++) { const s = net.createServer().listen(p, '0.0.0.0'); blockers.push(s); }
  await new Promise(r => setTimeout(r, 100));
  let err = null; try { await startPwaServer({ dir, port: 4190, maxPortTries: 3 }); } catch (e) { err = e; }
  ok(err && err.code === 'EADDRINUSE', 'alle Ports belegt -> sauberer Fehler (' + (err && err.code) + ')');
  blockers.forEach(s => s.close());
  // Ordner ohne Bundle
  err = null; try { await startPwaServer({ dir: '/tmp', port: 4200 }); } catch (e) { err = e; }
  ok(err && /not found/.test(err.message), 'Ordner ohne index.html -> verstaendlicher Fehler');
  // Kaputte Anfrage beendet den Server nicht
  await raw(4180, 'GARBAGE\r\n\r\n');
  r = await get(4180, '/'); ok(r.status === 200, 'Server laeuft nach kaputter Anfrage weiter');
  await srv.close();
  let up = true; try { await get(4180, '/'); } catch { up = false; }
  ok(!up, 'close() beendet den Server');
  process.exit(failed ? 1 : 0);
})();
