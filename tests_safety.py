import socket, subprocess, time, os, json, signal, threading
os.environ["XDG_CONFIG_HOME"]="/tmp/eng/cfg5"
subprocess.run("pkill -x luminary; rm -rf /tmp/eng/cfg5 /tmp/luminary.sock; mkdir -p /tmp/eng/cfg5/luminary; cp -r /home/claude/proj/luminary-cpp-main/fixtures /tmp/eng/cfg5/luminary/ 2>/dev/null; true", shell=True)

# Art-Net-Mitschnitt auf 127.0.0.2:6454: pro Universe zuletzt gesehenes Datenpaket + Zaehler
rx=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); rx.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); rx.bind(("127.0.0.2",6454)); rx.settimeout(0.1)
seen={}; count={}; stop=False; lock=threading.Lock()
def listen():
    while not stop:
        try: d,_=rx.recvfrom(2048)
        except socket.timeout: continue
        if d[:8]==b"Art-Net\0" and d[8:10]==b"\x00\x50":
            u=d[14]|(d[15]<<8)
            with lock: seen[u]=d[18:530]; count[u]=count.get(u,0)+1
threading.Thread(target=listen,daemon=True).start()
def reset():
    with lock: seen.clear(); count.clear()
def pkts(): 
    with lock: return dict(count)

def start_engine(*args):
    return subprocess.Popen(["/tmp/eng/luminary","-l",*args],cwd="/tmp/eng",stdout=open("/tmp/eng/safety.log","a"),stderr=subprocess.STDOUT)
def cmd(c):
    s=socket.socket(socket.AF_UNIX); s.connect("/tmp/luminary.sock"); s.sendall((c+"\n").encode()); d=b""
    while not d.endswith(b"\n"): d+=s.recv(1<<16)
    s.close(); return d.decode().strip()
class Persistent:   # wie Electron: eine dauerhafte Verbindung
    def __init__(s): s.s=socket.socket(socket.AF_UNIX); s.s.connect("/tmp/luminary.sock")
    def cmd(s,c):
        s.s.sendall((c+"\n").encode()); d=b""
        while not d.endswith(b"\n"): d+=s.s.recv(1<<16)
        return d.decode().strip()
    def close(s): s.s.close()
def ok(c,m):
    print(("OK   " if c else "FAIL ")+m)
    if not c: os._exit(1)

# ---------- 1) --wait-for-target: vor dem Ziel kommt NICHTS raus
e=start_engine("--wait-for-target"); time.sleep(1.2)
# Ziel der Engine ist noch das Standard-Broadcast; wir lauschen auf 127.0.0.2, also hier nur indirekt:
# Ohne Ziel darf auch an keine andere Adresse gesendet werden -> Test ueber den Zaehler nach dem Setzen.
c=Persistent()
ok(c.cmd("PING")=="PONG","Engine laeuft mit --wait-for-target")
time.sleep(0.5); ok(sum(pkts().values())==0,"vor SET_ARTNET_TARGET: keine Pakete")
c.cmd("PATCH 0 1 stairvillePAR Channel5"); 
ok(c.cmd("SET_ARTNET_TARGET 127.0.0.2")=="OK","Ziel gesetzt"); time.sleep(0.6)
ok(pkts().get(0,0)>5,f"nach dem Ziel kommen Pakete (U0: {pkts().get(0,0)})")

# ---------- 2) nur aktive Universes
ok(set(pkts().keys())=={0},f"nur Universe 0 wird gesendet (statt 0-3): {sorted(pkts())}")
print("      aktiv laut Engine:", c.cmd("ACTIVE_UNIVERSES"))
c.cmd("SET 2 5 99"); time.sleep(0.4)
ok(2 in pkts() and 1 not in pkts() and 3 not in pkts(),f"Schreiben nach Universe 2 aktiviert genau diese: {sorted(pkts())}")
reset(); time.sleep(0.5)
rate=pkts().get(0,0)/0.5; ok(30<rate<50,f"Universe 0 laeuft mit ~40 Hz ({rate:.0f} Hz)")
c.cmd("PATCH 1 1 stairvillePAR Channel5"); time.sleep(0.3); reset(); time.sleep(0.4)
ok(1 in pkts(),"PATCH auf Universe 1 startet dessen Ausgabe")

# ---------- 3) Fail-Safe: Nebelkanal 40 auf U0, Desktop 'stuerzt ab'
ok(c.cmd("SET_FAILSAFE 0:40 0:41")=="OK","SET_FAILSAFE gesetzt")
ok(c.cmd("SET_FAILSAFE 0:999").startswith("ERR") and c.cmd("SET_FAILSAFE abc").startswith("ERR") and c.cmd("SET_FAILSAFE 9:1").startswith("ERR"),"ungueltige Fail-Safe-Listen abgelehnt")
c.cmd("SET_FAILSAFE 0:40 0:41"); c.cmd("SET 0 40 200"); c.cmd("SET 0 41 250"); c.cmd("SET 0 1 77"); time.sleep(0.3)
ok(seen[0][39]==200 and seen[0][40]==250,"Nebel laeuft (Kanal 40=200, 41=250)")
t0=time.time(); c.close()                      # Verbindung weg = Absturz der App
print("      Desktop-Verbindung getrennt, warte auf die Schonfrist (8 s)...")
time.sleep(5); ok(seen[0][39]==200,"nach 5 s noch unveraendert (Schonfrist)")
while seen[0][39]!=0 and time.time()-t0<14: time.sleep(0.2)
dt=time.time()-t0
ok(seen[0][39]==0 and seen[0][40]==0,f"nach {dt:.1f} s: Nebelkanaele 40/41 = 0 (Fail-Safe)")
ok(seen[0][0]==77,"andere Kanaele bleiben unberuehrt (Kanal 1 = 77)")

# ---------- 4) Reconnect innerhalb der Schonfrist loest nichts aus
c=Persistent(); c.cmd("SET 0 40 120"); time.sleep(0.2)
c.close(); time.sleep(3); c=Persistent(); time.sleep(7)
ok(seen[0][39]==120,"Reconnect innerhalb der Schonfrist: Fail-Safe loest nicht aus")

# ---------- 5) APPLY_FAILSAFE + sauberes Beenden
ok(c.cmd("APPLY_FAILSAFE")=="OK","APPLY_FAILSAFE"); time.sleep(0.2)
ok(seen[0][39]==0,"APPLY_FAILSAFE setzt den Nebelkanal sofort auf 0")
c.cmd("SET 0 40 222"); time.sleep(0.2); ok(seen[0][39]==222,"Nebel wieder an (222)")
e.send_signal(signal.SIGTERM); e.wait(timeout=5); time.sleep(0.2)
ok(seen[0][39]==0,"SIGTERM: letztes Frame vor dem Beenden hat Nebelkanal = 0")
stop=True
