/* Exercise the real config writer, server switch and room cache without an arcade or public-server connection. */
#include "../quest/tc2_online.c"
#include "../quest/tc2_rooms.c"
#include <assert.h>
#include <direct.h>

static bool mock_hosting,mock_connected;
static char preset[64];
int eng_net_room_request;char eng_net_room_request_name[32];
bool eng_net_hosting(void){return mock_hosting;}
bool eng_net_connected(void){return mock_connected;}
void eng_net_preset_server(const char *s){snprintf(preset,sizeof preset,"%s",s);}
Uint32 SDL_GetTicks(void){return 100;}
Uint64 SDL_GetPerformanceCounter(void){return 42;}

/* The rest of the arcade/network API must not run while editing offline settings. */
#define UNUSED_API(result,fn,args) result fn args { fprintf(stderr,"Unexpected call: %s\n",#fn);abort(); }
UNUSED_API(void,tc2_voice_init,(void))
UNUSED_API(void,tc2_voice_set_enabled,(bool on))
UNUSED_API(void,tc2_voice_set_muted,(bool on))
UNUSED_API(void,tc2_voice_set_volume,(int percent))
UNUSED_API(void,tc2_voice_set_push_to_talk,(bool on))
UNUSED_API(void,tc2_voice_set_active,(bool on))
UNUSED_API(void,tc2_voice_tick,(void))
UNUSED_API(void,tc2_voice_get_status,(tc2_voice_status *out))
UNUSED_API(void,tc2_voice_test_output,(const char *path))
UNUSED_API(void,tc2_voice_talk_key,(bool held))
UNUSED_API(int,tc2_voice_wanted_bytes,(void))
UNUSED_API(int,tc2_voice_pack,(uint8_t *out,int max))
UNUSED_API(bool,tc2_voice_unpack,(const uint8_t *block,int available))
UNUSED_API(void,eng_net_init,(const eng_net_game *g))
UNUSED_API(void,eng_net_set_name,(const char *n))
UNUSED_API(bool,eng_net_session_active,(void))
UNUSED_API(bool,eng_net_discovering,(void))
UNUSED_API(int,eng_net_auto_lead,(void))
UNUSED_API(bool,eng_net_may_advance,(int max_lead,int timeout_ms))
UNUSED_API(void,eng_net_poll_paused,(void))
UNUSED_API(void,eng_net_poll,(void))
UNUSED_API(int,eng_net_my_room,(void))
UNUSED_API(void,eng_net_discover,(void))
UNUSED_API(void,eng_net_discover_autojoin,(int on))
UNUSED_API(void,eng_net_status,(char *buf,size_t n))
UNUSED_API(int,eng_net_ping_ms,(void))
UNUSED_API(bool,eng_net_self_ready,(void))
UNUSED_API(void,eng_net_set_ready,(int ready))
UNUSED_API(int,eng_net_roster_count,(void))
UNUSED_API(bool,eng_net_all_ready,(void))
UNUSED_API(int,eng_net_roster_slot,(int i))
UNUSED_API(int,eng_net_slot,(void))
UNUSED_API(void,eng_net_request_start,(void))
UNUSED_API(bool,eng_net_roster,(int i,char *n,size_t len,int *ready,int *self))
UNUSED_API(const char *,eng_net_server,(void))
UNUSED_API(int,eng_net_found_count,(void))
UNUSED_API(bool,eng_net_found,(int i,char *label,size_t n,char *addr,size_t an))
UNUSED_API(bool,eng_net_set_server,(const char *address))
UNUSED_API(bool,eng_net_connect,(void))
UNUSED_API(bool,eng_net_host_start,(void))
UNUSED_API(void,eng_net_host_stop,(void))
UNUSED_API(void,eng_net_disconnect,(void))
UNUSED_API(int,tc2_link_self_state,(void))
UNUSED_API(int,tc2_link_partner_mark,(void))
UNUSED_API(void,tc2_link_get_status,(tc2_link_status *out))
UNUSED_API(bool,tc2_link_ghost,(void))
UNUSED_API(void,tc2_link_set_mark,(int mark))
UNUSED_API(void,tc2_link_set_ghost,(bool on))
UNUSED_API(bool,tc2_link_enabled,(void))
UNUSED_API(void,tc2_link_set_enabled,(bool on))
UNUSED_API(void,tc2_link_session_reset,(void))
UNUSED_API(bool,tc2_link_pack_fits_newest,(int limit))
UNUSED_API(int,tc2_link_pack,(uint8_t *out,int limit))
UNUSED_API(void,tc2_link_trailer,(uint8_t *payload))
UNUSED_API(int,tc2_link_unpack,(const uint8_t *in))
UNUSED_API(bool,qhost_online_menu_open,(void))
UNUSED_API(void,qhost_online_menu,(bool open))
UNUSED_API(void,qhost_test,(const char *action,const char *arg))
UNUSED_API(void,SDL_Delay,(Uint32 ms))
#undef UNUSED_API
uint16_t g_ss22_gun_x,g_ss22_gun_y,tc2_inputs;
int qtc2_cabinet_side;uint32_t rr_frame;uint8_t tc2_ram[0x400000];
const tc2_game_t tc2_game={0};

static void read_config(char *out,size_t size){
    FILE *f=fopen(CFG_PATH,"r");assert(f);size_t n=fread(out,1,size-1,f);out[n]=0;fclose(f);
}
int main(void){
    inited=true;cfg_volume=170;cfg_voice=false;snprintf(name,sizeof name,"TESTER");
    join_ip[3]=42;join_port=30001;
    last_room_id=7;last_side=2;last_seen=true;snprintf(last_code,sizeof last_code,"KQ7M");
    assert(save_cfg());
    char before[1024],after[1024];read_config(before,sizeof before);
    const char *invalid[]={"", "https://example.org", "host/path", "host:0", "host:65536", "host:-1", "host:12x", "host:",
        "host:9999999999999999999999999", "::1", "a..b", "-host", "host-", "host-.com", "host name", "host\nname",
        "999.1.2.3", "192.168.1", "192.168.1.2.", "4294967297.1.2.3", "a_b.example"};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;i++){
        assert(tc2_online_set_server(invalid[i]));read_config(after,sizeof after);assert(!strcmp(before,after));
        assert(last_room_id==7&&!strcmp(server,"zonesync.net"));
    }
    char too_long[65];memset(too_long,'a',64);too_long[64]=0;assert(tc2_online_set_server(too_long));
    assert(!tc2_online_set_server("zonesync.net"));assert(last_room_id==7);
    mock_hosting=true;assert(tc2_online_set_server("other.example"));mock_hosting=false;
    mock_connected=true;assert(tc2_online_set_server("other.example"));mock_connected=false;
    mode=MODE_ROOM;assert(tc2_online_set_server("other.example"));mode=MODE_NONE;
    episode=true;assert(tc2_online_set_server("other.example"));episode=false;
    snprintf(pending_code,sizeof pending_code,"KQ7M");assert(tc2_online_set_server("other.example"));pending_code[0]=0;
    /* Opening the config can fail; report it without switching servers or forgetting the room. */
    assert(rename(CFG_PATH,"previous.cfg")==0);assert(_mkdir(CFG_PATH)==0);
    assert(tc2_online_set_server("other.example"));assert(last_room_id==7&&!strcmp(server,"zonesync.net"));
    assert(_rmdir(CFG_PATH)==0);assert(rename("previous.cfg",CFG_PATH)==0);
    room_count=2;server_players=4;answered=true;browsing=true;game_n=2;
    snprintf(server_name,sizeof server_name,"Old lobby");snprintf(resolved_for,sizeof resolved_for,"zonesync.net");
    assert(!tc2_online_set_server("LAN-1.Example:30000"));
    assert(!strcmp(server,"lan-1.example:30000")&&!strcmp(preset,server));
    assert(!last_room_id&&!last_seen&&!last_code[0]&&!room_count&&!server_players&&!answered&&!server_name[0]);
    assert(!resolved_for[0]&&!browsing&&!game_n&&sock==ENG_SOCK_BAD);
    read_config(after,sizeof after);
    assert(strstr(after,"server=lan-1.example:30000\n")&&!strstr(after,"rejoin="));
    assert(strstr(after,"name=TESTER\n")&&strstr(after,"voice=0\n")&&strstr(after,"voice_volume=170\n"));
    assert(strstr(after,"join=192.168.1.42:30001\n"));
    server[0]=0;load_cfg();assert(!strcmp(server,"lan-1.example:30000"));
    assert(!tc2_online_set_server("192.168.1.20:65535"));
    assert(!tc2_online_set_server("192.168.001.020:00000000027750"));
    assert(!strcmp(server,"192.168.1.20:27750"));
    assert(!tc2_online_set_server("localhost:1"));
    assert(!tc2_online_set_server("example.org."));
    char longest[64];memset(longest,'a',57);memcpy(longest+57,":27750",7);
    assert(!tc2_online_set_server(longest));assert(strlen(server)==63);
    assert(!tc2_online_set_server(tc2_online_default_server()));
    server[0]=0;load_cfg();assert(!strcmp(server,"zonesync.net"));
    puts("PASS: server validation, persistence, active-session guard, save failure and old-server state reset");
    return 0;
}
