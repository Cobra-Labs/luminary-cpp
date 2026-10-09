#include "net/node_discovery.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
using namespace luminary::core;
using namespace luminary::net;

#define CHECK(c) do{ if(!(c)){ std::cerr<<"FAIL line "<<__LINE__<<": "#c<<"\n"; return 1;} }while(0)

// Unabhaengig nach Art-Net-4-Spec zusammengebaut (feste Offsets, keine Parser-Helfer)
static std::vector<uint8_t> make_reply(size_t size = 239) {
    std::vector<uint8_t> r(239, 0);
    memcpy(r.data(), "Art-Net", 8);
    r[8]=0x00; r[9]=0x21;                 // OpPollReply, little-endian
    r[10]=192; r[11]=168; r[12]=1; r[13]=77;
    r[14]=0x36; r[15]=0x19;               // Port 6454 little-endian
    r[16]=0x01; r[17]=0x02;               // VersInfo 0x0102
    r[18]=0x01; r[19]=0x02;               // Net 1, SubNet 2
    r[20]=0x12; r[21]=0x34;               // Oem
    r[23]=0xD2;                           // Status1
    r[24]=0x78; r[25]=0x56;               // ESTA lo/hi
    strcpy((char*)&r[26], "LumNode");
    strcpy((char*)&r[44], "Luminary Node - Node Control Panel");
    strcpy((char*)&r[108], "#0001 [0000] ok");
    r[172]=0; r[173]=2;                   // NumPorts 2
    r[174]=0x80; r[175]=0x80;             // beide Output
    r[182]=0x80; r[183]=0x00;             // GoodOutputA: Port0 aktiv
    r[190]=0x03; r[191]=0x04;             // SwOut 3 und 4
    r[200]=0x00;                          // StNode
    uint8_t mac[6]={0xDE,0xAD,0xBE,0xEF,0x00,0x01}; memcpy(&r[201],mac,6);
    r[211]=1; r[212]=0x08;
    r.resize(size);
    return r;
}

int main() {
    // --- build_art_poll
    auto poll = build_art_poll();
    CHECK(poll.size()==14);
    CHECK(memcmp(poll.data(),"Art-Net\0",8)==0);
    CHECK(poll[8]==0x00 && poll[9]==0x20);     // OpPoll 0x2000 LE
    CHECK(poll[10]==0 && poll[11]==14);        // ProtVer BE

    // --- parse
    auto full = make_reply();
    auto n = parse_art_poll_reply(full.data(), full.size(), "192.168.1.77");
    CHECK(n);
    CHECK(n->ip=="192.168.1.77" && n->port==6454);
    CHECK(n->short_name=="LumNode" && n->long_name=="Luminary Node - Node Control Panel");
    CHECK(n->node_report=="#0001 [0000] ok");
    CHECK(n->mac=="de:ad:be:ef:00:01");
    CHECK(n->firmware==0x0102 && n->oem==0x1234 && n->esta==0x5678);
    CHECK(n->net==1 && n->sub_net==2 && n->bind_index==1);
    CHECK(n->ports.size()==2);
    CHECK(n->ports[0].output && n->ports[0].universe==((1<<8)|(2<<4)|3) && n->ports[0].data_active);
    CHECK(n->ports[1].universe==((1<<8)|(2<<4)|4) && !n->ports[1].data_active);

    // aeltere Nodes (207 Byte) werden akzeptiert, fehlende Felder bleiben 0
    auto old = make_reply(207);
    auto o = parse_art_poll_reply(old.data(), old.size(), "10.0.0.5");
    CHECK(o && o->bind_index==0 && o->mac=="de:ad:be:ef:00:01");

    // Ablehnen: zu kurz, falsche ID, falscher OpCode, ArtDMX
    CHECK(!parse_art_poll_reply(full.data(), 100, "x"));
    CHECK(!parse_art_poll_reply(full.data(), 206, "x"));
    auto bad = full; bad[0]='X'; CHECK(!parse_art_poll_reply(bad.data(), bad.size(), "x"));
    bad = full; bad[9]=0x50; CHECK(!parse_art_poll_reply(bad.data(), bad.size(), "x"));
    CHECK(!parse_art_poll_reply(nullptr, 300, "x"));

    // NumPorts > 4 darf nicht ueber die Arrays hinauslesen; Steuerzeichen werden ersetzt
    auto wild = full; wild[172]=0xFF; wild[173]=0xFF; wild[26]='A'; wild[27]=0x07; wild[28]='\n';
    auto w = parse_art_poll_reply(wild.data(), wild.size(), "x");
    CHECK(w && w->ports.size()<=8 && w->short_name=="A??Node");

    // Input-Ports
    auto in = make_reply(); in[174]=0x40; in[175]=0x00; in[172]=0; in[173]=1; in[178]=0x80; in[186]=0x05;
    auto ni = parse_art_poll_reply(in.data(), in.size(), "x");
    CHECK(ni && ni->ports.size()==1 && ni->ports[0].input && ni->ports[0].universe==((1<<8)|(2<<4)|5) && ni->ports[0].data_active);

    // --- Loopback-Integration: Fake-Node auf 127.0.0.2:16454, Discovery pollt dorthin
    const uint16_t P = 16454;
    int node_fd = socket(AF_INET, SOCK_DGRAM, 0);
    int on=1; setsockopt(node_fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof on);
    sockaddr_in na{}; na.sin_family=AF_INET; na.sin_port=htons(P); inet_pton(AF_INET,"127.0.0.2",&na.sin_addr);
    CHECK(bind(node_fd,(sockaddr*)&na,sizeof na)==0);

    std::atomic<int> polls_seen{0};
    std::atomic<bool> stop{false};
    std::thread fake([&]{
        auto reply = make_reply();
        while(!stop){
            pollfd p{node_fd,POLLIN,0};
            if(::poll(&p,1,50)>0){
                uint8_t buf[64]; sockaddr_in from{}; socklen_t fl=sizeof from;
                ssize_t k=recvfrom(node_fd,buf,sizeof buf,0,(sockaddr*)&from,&fl);
                if(k==14 && buf[8]==0x00 && buf[9]==0x20){
                    polls_seen++;
                    // Reply an Quelle:6454-Port (hier P), wie ein echter Node
                    sockaddr_in to=from; to.sin_port=htons(P);
                    sendto(node_fd,reply.data(),reply.size(),0,(sockaddr*)&to,sizeof to);
                    // zweiter Reply mit anderem BindIndex (Mehrfachgeraet)
                    auto r2=reply; r2[211]=2; sendto(node_fd,r2.data(),r2.size(),0,(sockaddr*)&to,sizeof to);
                    // Muell dazu
                    uint8_t junk[20]={1,2,3}; sendto(node_fd,junk,sizeof junk,0,(sockaddr*)&to,sizeof to);
                }
            }
        }
    });

    NodeDiscovery d([]{ return std::string("127.0.0.2"); }, P, "127.0.0.2");
    d.start();
    CHECK(d.last_error().empty());
    for(int i=0;i<30 && d.nodes().size()<2;i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto nodes = d.nodes();
    std::cout << "gefundene Nodes: " << nodes.size() << " polls_seen=" << polls_seen << "\n";
    CHECK(nodes.size()==2);                       // BindIndex 1 und 2, Muell ignoriert
    CHECK(nodes[0].ip=="127.0.0.2" && nodes[0].short_name=="LumNode");
    CHECK(polls_seen>=1);

    // poll_now loest sofort einen weiteren Poll aus (nicht erst nach 3 s)
    int before = polls_seen;
    d.poll_now();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(polls_seen>before);
    CHECK(d.nodes().size()==2);                   // Update statt Duplikat

    // Node verstummt -> verschwindet nach Timeout
    stop=true; fake.join();
    std::cout << "warte auf Timeout (" << NodeDiscovery::NODE_TIMEOUT_MS/1000 << " s)...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(NodeDiscovery::NODE_TIMEOUT_MS+1500));
    CHECK(d.nodes().empty());
    d.stop();
    close(node_fd);
    std::cout << "ALLE TESTS OK\n";
}
