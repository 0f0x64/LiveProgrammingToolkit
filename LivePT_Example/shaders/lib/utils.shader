float2 rot(float2 pos,float angle)
{
    float2x2 r = {
        cos(angle),-sin(angle),
        sin(angle), cos(angle)
    };
    pos= mul(pos,r);
    return pos;
     
}