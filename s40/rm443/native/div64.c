#include <stdint.h>
/* ARM EABI quotient+remainder helper; avoids a newlib/libgcc dependency. */
void s40_div64_words(uint32_t *out,const uint32_t *in,int signed_op) {
    uint64_t n=((uint64_t)in[1]<<32)|in[0],d=((uint64_t)in[3]<<32)|in[2];
    int ns=signed_op&&(in[1]>>31),ds=signed_op&&(in[3]>>31);
    if(ns)n=0-n;if(ds)d=0-d;
    uint64_t q=0,r=n,bit=1;
    if(d) {
        while(d<=(n>>1)) {d<<=1;bit<<=1;}
        do {if(r>=d){r-=d;q|=bit;}d>>=1;bit>>=1;}while(bit);
    }
    if(ns!=ds)q=0-q;if(ns)r=0-r;
    out[0]=q;out[1]=q>>32;out[2]=r;out[3]=r>>32;
}
