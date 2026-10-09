// Integral of exp(-height/falloff) along the visible ray. The horizontal
// limit avoids cancellation; clamping optical depth prevents overflow.
float HeightFogOpacity(float startHeight,float endHeight,float distance,float density,float baseHeight,float falloff,float maximumOpacity)
{
    if(!(distance>0 && density>0 && falloff>0)) return 0;
    float a=clamp(-(startHeight-baseHeight)/falloff,-40,40);
    float b=clamp(-(endHeight-baseHeight)/falloff,-40,40);
    float delta=b-a;
    float average;
    if(abs(delta)<.001) average=exp(a)*(1+delta*.5+delta*delta/6);
    else average=(exp(b)-exp(a))/delta;
    float tau=min(80,max(0,density*distance*average));
    return min(saturate(maximumOpacity),1-exp(-tau));
}
