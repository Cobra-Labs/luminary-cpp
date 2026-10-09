import socket, subprocess, time, os, json, urllib.request, urllib.error, urllib.parse, signal
os.environ["XDG_CONFIG_HOME"] = "/tmp/eng/cfg3"
subprocess.run("rm -rf /tmp/eng/cfg3 /tmp/luminary.sock; mkdir -p /tmp/eng/cfg3/luminary; printf '[engine]\\nartnet_target = \"127.0.0.2\"\\n' > /tmp/eng/cfg3/luminary/config.toml", shell=True)
eng = subprocess.Popen(["/tmp/eng/luminary","-l"], cwd="/tmp/eng", stdout=open("/tmp/eng/bridge_engine.log","w"), stderr=subprocess.STDOUT)
time.sleep(1.5)
def cmd(c):
    s=socket.socket(socket.AF_UNIX); s.connect("/tmp/luminary.sock"); s.sendall((c+"\n").encode()); d=b""
    while not d.endswith(b"\n"): d+=s.recv(1<<20)
    s.close(); return d.decode().strip()
BASE="http://127.0.0.1:7346"
def http(method, path, body=None, token=None):
    req=urllib.request.Request(BASE+path, data=(body.encode() if body is not None else None), method=method)
    if token: req.add_header("Authorization","Bearer "+token)
    try:
        with urllib.request.urlopen(req, timeout=5) as r: return r.status, r.read().decode()
    except urllib.error.HTTPError as e: return e.code, e.read().decode()
ok=lambda c,m: print(("OK   " if c else "FAIL ")+m) or (c or exit(1))
try:
    pin=cmd("GEN_PIN").split()[1]
    st,tok=http("POST","/api/pair","pin="+pin); ok(st==200,"Pairing -> Token")
    ok(http("GET","/api/state")[0]==401,"/api/state ohne Token -> 401")

    show={"patches":[{"id":"p1","name":"PAR 1"}],"cues":[{"id":"c-1","name":"Blau"}]}
    ok(cmd("SET_SHOW "+json.dumps(show))=="OK","SET_SHOW (JSON mit Leerzeichen)")
    ok(cmd("SET_LIVE "+json.dumps({"blackout":False,"fogOn":False}))=="OK","SET_LIVE")
    ok(cmd("SET_SHOW not json").startswith("ERR"),"SET_SHOW lehnt Nicht-JSON ab")
    big=json.dumps({"x":"a"*300000}); ok(cmd("SET_SHOW "+big)=="OK","SET_SHOW 300 KB")
    ok(cmd("SET_SHOW "+json.dumps(show))=="OK","SET_SHOW zurueck")

    st,body=http("GET","/api/state?rev=0&liverev=0&universes=0,1",token=tok); j=json.loads(body)
    ok(st==200 and j["show"]==show and j["live"]["fogOn"] is False,"GET /api/state liefert Show + Live")
    ok(len(j["values"]["0"])==512 and "1" in j["values"],"values fuer Universe 0 und 1 (je 512)")
    rev=j["showRev"]; lrev=j["liveRev"]
    j2=json.loads(http("GET",f"/api/state?rev={rev}&liverev={lrev}",token=tok)[1])
    ok(j2["show"] is None and j2["live"] is None,"bekannte Revision -> show/live = null (kein Re-Download)")
    cmd("SET_LIVE "+json.dumps({"blackout":False,"fogOn":True}))
    j3=json.loads(http("GET",f"/api/state?rev={rev}&liverev={lrev}",token=tok)[1])
    ok(j3["show"] is None and j3["live"]["fogOn"] is True and j3["liveRev"]>lrev,"Live-Aenderung -> nur live kommt neu")

    # Fader aus der PWA -> Engine-Wert + Ereignis (mit Zusammenfassen)
    for v in range(1,51): http("POST","/api/set",f"universe=0&channel=3&value={v}",tok)
    http("POST","/api/set","universe=0&channel=7&value=99",tok)
    vals=json.loads(http("GET","/api/state",token=tok)[1])["values"]["0"]
    ok(vals[2]==50 and vals[6]==99,"/api/set schreibt in die Engine (Kanal 3=50, 7=99)")
    ev=json.loads(cmd("EVENTS")[3:])
    sets=[e for e in ev if e["t"]=="set"]
    ok(len(sets)==2 and {(e["c"],e["v"]) for e in sets}=={(3,50),(7,99)},f"50 Fader-Events zu 1 zusammengefasst ({len(sets)} Events)")
    ok(json.loads(cmd("EVENTS")[3:])==[],"EVENTS leert die Warteschlange")

    # 16 Bit
    ok(http("POST","/api/set16","universe=0&msb=10&lsb=11&value=43981",tok)[0]==200,"/api/set16")
    vals=json.loads(http("GET","/api/state",token=tok)[1])["values"]["0"]
    ok(vals[9]==0xAB and vals[10]==0xCD,"16-Bit-Wert 0xABCD -> 171/205")
    ok(http("POST","/api/set16","universe=0&msb=5&lsb=5&value=1",tok)[0]==400,"set16 mit msb==lsb abgelehnt")
    cmd("EVENTS")

    # Aktionen
    ok(http("POST","/api/action","type=cue&id=c-1",tok)[0]==200,"Aktion cue")
    ok(http("POST","/api/action","type=preset&id=3f2a-bb",tok)[0]==200,"Aktion preset")
    ok(http("POST","/api/action","type=fog&on=1&flow=70&heat=90",tok)[0]==200,"Aktion fog")
    ok(http("POST","/api/action","type=cue&id="+urllib.parse.quote('a","x":"b'),tok)[0]==400,"JSON-Injection ueber id abgelehnt")
    ok(http("POST","/api/action","type=cue&id="+"a"*65,tok)[0]==400,"zu lange id abgelehnt")
    ok(http("POST","/api/action","type=wipe&id=x",tok)[0]==400,"unbekannte Aktion abgelehnt")
    ev=json.loads(cmd("EVENTS")[3:])
    ok(ev==[{"t":"cue","id":"c-1"},{"t":"preset","id":"3f2a-bb"},{"t":"fog","on":True,"flow":70,"heat":90}],"Ereignisse in richtiger Reihenfolge: "+json.dumps(ev))

    # Blackout wirkt sofort in der Engine
    http("POST","/api/set","universe=0&channel=1&value=200",tok); cmd("EVENTS")
    http("POST","/api/action","type=blackout&on=1",tok)
    vals=json.loads(http("GET","/api/state",token=tok)[1])["values"]["0"]
    ok(vals[0]==0 and sum(vals)==0,"Blackout nullt alle Kanaele sofort")
    ok(json.loads(cmd("EVENTS")[3:])==[{"t":"blackout","on":True}],"Blackout-Ereignis fuer den Desktop")

    # Thread-Leak: viele Verbindungen -> Thread-Zahl bleibt klein
    pid=eng.pid
    for i in range(300): http("GET","/api/ping",token=tok)
    time.sleep(0.3); http("GET","/api/ping",token=tok)   # loest letztes Aufraeumen aus
    n=len(os.listdir(f"/proc/{pid}/task"))
    ok(n<25,f"nach 300 Requests nur {n} Threads in der Engine")
finally:
    eng.send_signal(signal.SIGTERM)
