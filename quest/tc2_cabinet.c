#include "tc2_cabinet.h"
#include "quest_options.h"
#include <stdio.h>
#include "tc2_link.h"
int qtc2_cabinet_side=1;
int qtc2_cabinet_active_side(void){return tc2_link_enabled()?tc2_link_side()+1:qtc2_cabinet_side;}
void qtc2_cabinet_prepare_player(void){
    extern uint8_t tc2_ram[0x1000000];
    /* Linked cabinets get their routes from the real cabinet ID. */
    if(tc2_link_enabled())return;
    if(qtc2_route_select(tc2_ram,sizeof tc2_ram,qtc2_cabinet_side))
        fprintf(stderr,"[CABINET] Robert route selected before original player initialization\n");
}
void qtc2_cabinet_configure(const char *path){
    QOptions options;qoptions_load(path,&options);qtc2_cabinet_side=options.player_side;
    fprintf(stderr,"[CABINET] boot side P%d (%s)\n",qtc2_cabinet_side,qtc2_cabinet_side==2?"right/blue":"left/red");
}
