/* Read-only binary comparison helper, using the vendored MinHook HDE decoder.
 * Never loads, modifies or executes either game image. */
#include "hde64.h"
#include <stdint.h>
#include <string.h>

struct swvr_reference {
    uint32_t offset, length;
    int64_t delta;
    uint32_t kind, width, operand;
};

__declspec(dllexport) unsigned swvr_instruction64(const unsigned char* code) {
    hde64s decoded;
    unsigned length=hde64_disasm(code,&decoded);
    if(!length || (decoded.flags&F_ERROR))return 0;
    return length|((unsigned)decoded.opcode<<8)|((unsigned)decoded.opcode2<<16)|((unsigned)decoded.modrm_reg<<24);
}

__declspec(dllexport) unsigned swvr_normalize64(const unsigned char* code,
    unsigned size, unsigned char* output, struct swvr_reference* refs,
    unsigned capacity, unsigned* count) {
    unsigned at=0;
    *count=0;
    memcpy(output,code,size);
    while(at<size) {
        hde64s decoded;
        unsigned length=hde64_disasm(code+at,&decoded), immediate=0, operand=0, width=0, kind=0;
        int64_t delta=0;
        if(!length || length>size-at || (decoded.flags&F_ERROR))return at+1;
        if(decoded.flags&F_IMM8)immediate+=1;
        if(decoded.flags&F_IMM16)immediate+=2;
        if(decoded.flags&F_IMM32)immediate+=4;
        if(decoded.flags&F_IMM64)immediate+=8;
        if(decoded.flags&F_RELATIVE) {
            kind=1;width=immediate;operand=at+length-immediate;
            if(width==1){int8_t value;memcpy(&value,code+operand,1);delta=value;}
            else if(width==4){int32_t value;memcpy(&value,code+operand,4);delta=value;}
            else return at+1;
        } else if((decoded.flags&F_DISP32) && decoded.modrm_mod==0 && decoded.modrm_rm==5) {
            int32_t value;
            if(decoded.p_67)return at+1; /* Do not guess address-size semantics. */
            kind=2;width=4;operand=at+length-immediate-4;
            memcpy(&value,code+operand,4);delta=value;
        } else if(decoded.flags&(F_DISP8|F_DISP16|F_DISP32)) {
            kind=3;width=(decoded.flags&F_DISP8)?1:(decoded.flags&F_DISP16)?2:4;
            operand=at+length-immediate-width;
            delta=width==1?(int8_t)decoded.disp.disp8:width==2?(int16_t)decoded.disp.disp16:(int32_t)decoded.disp.disp32;
        }
        if(kind) {
            struct swvr_reference* item;
            if(*count>=capacity)return at+1;
            item=&refs[(*count)++];
            item->offset=at;item->length=length;item->delta=delta;item->kind=kind;item->width=width;item->operand=operand;
            if(kind!=3)memset(output+operand,0,width);
        }
        at+=length;
    }
    return 0;
}
