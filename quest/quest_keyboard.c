#include "quest_keyboard.h"
#include "quest_lobby.h"
#include "quest_openvr_keyboard.h"
static bool active,modal;
bool qkeyboard_modal(void){return modal;}
static unsigned request;
void qkeyboard_shutdown(void){qovr_keyboard_shutdown();if(active)SDL_StopTextInput();active=modal=false;}
void qkeyboard_update(void){
    bool editing=qlobby_editing();
    if(!editing){if(active){qovr_keyboard_cancel();SDL_StopTextInput();active=modal=false;}return;}
    if(!active||request!=qlobby_keyboard_request()){
        active=true;request=qlobby_keyboard_request();
#if defined(TCVR_STEAM_FRAME) || defined(TCVR_PC)
        bool opened=qovr_keyboard_show(qlobby_text_value());modal=opened;
        if(!opened)qlobby_keyboard_error(qovr_keyboard_error());
        else qlobby_keyboard_result(true);
#endif
#ifdef TCVR_STEAM_FRAME
        SDL_EventState(SDL_TEXTINPUT,SDL_ENABLE);
#else
        SDL_Rect area={24,150,592,54};SDL_SetTextInputRect(&area);SDL_StartTextInput();
#ifndef TCVR_PC
        qlobby_keyboard_result(true);
#endif
#endif
    }
    char text[16];int result=qovr_keyboard_poll(text);
    if(result)modal=false;
    if(result==1){qlobby_replace_text(text);qlobby_keyboard_error("PRESS A OR ENTER TO CONNECT");}
    else if(result==-1)qlobby_input(QL_BACK);
    else if(result==-2)qlobby_keyboard_error(qovr_keyboard_error());
}
bool qkeyboard_event(const SDL_Event *event){
    if(!qlobby_editing())return false;
    if(modal){
        if(event->type==SDL_KEYDOWN&&event->key.keysym.sym==SDLK_ESCAPE){qovr_keyboard_cancel();modal=false;qlobby_input(QL_BACK);}
        return event->type==SDL_KEYDOWN||event->type==SDL_TEXTINPUT;
    }
    if(event->type==SDL_TEXTINPUT){qlobby_text(event->text.text);return true;}
    if(event->type!=SDL_KEYDOWN)return false;
#ifdef TCVR_PC
    if(event->key.keysym.sym==SDLK_v&&(event->key.keysym.mod&(KMOD_CTRL|KMOD_GUI))){
        char *text=SDL_GetClipboardText();
        if(text){qlobby_text(text);SDL_free(text);}
        return true;
    }
#endif
    switch(event->key.keysym.sym){
        case SDLK_BACKSPACE:qlobby_backspace();break;
        case SDLK_RETURN:case SDLK_KP_ENTER:qlobby_input(QL_ACCEPT);break;
        case SDLK_ESCAPE:qlobby_input(QL_BACK);break;
        default:break;
    }
    return true;
}
