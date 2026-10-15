#include "quest_cover.h"
#include <math.h>
void qcover_calibrate(QCover *cover,float head_y){
    if(!isfinite(head_y))return;
    cover->upright_y=head_y;cover->calibrated=true;cover->ducked=false;
}
bool qcover_pedal(QCover *cover,float head_y,bool tracked){
    if(!tracked||!cover->calibrated||!isfinite(head_y))return false;
    float drop=cover->upright_y-head_y;
    /* 8 cm hysteresis avoids switching on small tracking/body movements. */
    if(drop>=.20f)cover->ducked=true;
    else if(drop<=.12f)cover->ducked=false;
    return !cover->ducked;
}
float qcover_reach(V3 hide){
    /* Slides run 0.23-1.78 m. Ask for a comfortable lean or duck; the
     * arcade's own slide carries the view the rest of the way. */
    float length=sqrtf(dot(hide,hide));
    return fminf(.35f,fmaxf(.15f,length));
}
bool qcover_spot_pedal(QCover *cover,V3 hide,V3 displacement,bool tracked,float *approach){
    if(approach)*approach=0;
    float length=sqrtf(dot(hide,hide));
    if(!tracked||!cover->calibrated||!(length>0)||!isfinite(dot(displacement,displacement)))return false;
    float reach=qcover_reach(hide),along=dot(displacement,hide)/length;
    if(approach)*approach=fminf(1,fmaxf(0,along/reach));
    /* Enter at the mark, leave once back past halfway: no flicker at the edge. */
    if(along>=reach)cover->ducked=true;
    else if(along<=reach*.5f)cover->ducked=false;
    return !cover->ducked;
}
