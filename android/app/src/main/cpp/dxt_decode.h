#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <algorithm>

// One DXT1/3/5 block into a 4x4 byte-ordered RGBA8 tile.
inline void decodeDXTBlock(unsigned mode,const uint8_t* block,uint8_t tile[16][4]){
    const uint8_t* colors=block+(mode==1?0:8);
    uint16_t endpoints[2];std::memcpy(endpoints,colors,4);
    uint8_t palette[4][4]{};
    for(unsigned i=0;i<2;++i){uint32_t r=(endpoints[i]>>11)&31,g=(endpoints[i]>>5)&63,b=endpoints[i]&31;
        palette[i][0]=(r<<3)|(r>>2);palette[i][1]=(g<<2)|(g>>4);palette[i][2]=(b<<3)|(b>>2);palette[i][3]=255;}
    if(mode!=1||endpoints[0]>endpoints[1]){for(unsigned c=0;c<3;++c){palette[2][c]=(2*palette[0][c]+palette[1][c])/3;palette[3][c]=(palette[0][c]+2*palette[1][c])/3;}palette[2][3]=palette[3][3]=255;}
    else {for(unsigned c=0;c<3;++c)palette[2][c]=(palette[0][c]+palette[1][c])/2;palette[2][3]=255;}
    uint32_t indices;std::memcpy(&indices,colors+4,4);
    for(unsigned p=0;p<16;++p)std::memcpy(tile[p],palette[(indices>>(p*2))&3],4);
    if(mode==3)for(unsigned p=0;p<16;++p)tile[p][3]=((block[p/2]>>((p&1)*4))&15)*17;
    if(mode==5){
        uint8_t alpha[8]{};alpha[0]=block[0];alpha[1]=block[1];
        if(alpha[0]>alpha[1])for(unsigned i=1;i<=6;++i)alpha[i+1]=((7-i)*alpha[0]+i*alpha[1])/7;
        else {for(unsigned i=1;i<=4;++i)alpha[i+1]=((5-i)*alpha[0]+i*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
        uint64_t alphaIndices=0;for(unsigned i=0;i<6;++i)alphaIndices|=uint64_t(block[2+i])<<(8*i);
        for(unsigned p=0;p<16;++p)tile[p][3]=alpha[(alphaIndices>>(p*3))&7];
    }
}

// Decode the pixels [left,right) x [top,bottom) of a DXT1/3/5 image of
// width x height straight into RGBA8 rows at destination (pitch bytes apart,
// first row = top). Only the blocks under the rectangle are decoded: texture
// streaming locks small regions of large mips, and the CPU fallback used to
// decode the whole level for each of them.
inline void decodeDXTRect(unsigned mode,uint32_t width,uint32_t height,const uint8_t* source,size_t sourceBytes,
                          uint32_t left,uint32_t top,uint32_t right,uint32_t bottom,uint8_t* destination,size_t pitch){
    if(mode!=1&&mode!=3&&mode!=5)throw std::invalid_argument("Unsupported DXT mode");
    uint64_t blocksWide=(uint64_t(width)+3)/4,blocksHigh=(uint64_t(height)+3)/4;
    uint32_t blockBytes=mode==1?8:16;
    if(!width||!height||blocksWide*blocksHigh*blockBytes>sourceBytes||uint64_t(width)*height>0x1fffffff)
        throw std::invalid_argument("Invalid DXT dimensions or truncated blocks");
    if(left>=right||top>=bottom||right>width||bottom>height)throw std::invalid_argument("Invalid DXT rectangle");
    uint8_t tile[16][4];
    for(uint32_t by=top/4;by<=(bottom-1)/4;++by)for(uint32_t bx=left/4;bx<=(right-1)/4;++bx){
        decodeDXTBlock(mode,source+(size_t(by)*blocksWide+bx)*blockBytes,tile);
        uint32_t x0=std::max(bx*4,left),x1=std::min(bx*4+4,right),y0=std::max(by*4,top),y1=std::min(by*4+4,bottom);
        for(uint32_t y=y0;y<y1;++y)
            std::memcpy(destination+size_t(y-top)*pitch+size_t(x0-left)*4,tile[(y&3)*4+(x0&3)],size_t(x1-x0)*4);
    }
}

// The same for a DXT1 image into little-endian A1R5G5B5 rows. DXT1 colours are built
// from 5:6:5 endpoints and its alpha is one bit, so this keeps nearly all of it in
// half the memory and texture bandwidth of RGBA8, which low-end Mali needs.
inline void decodeDXT1Rect16(uint32_t width,uint32_t height,const uint8_t* source,size_t sourceBytes,
                             uint32_t left,uint32_t top,uint32_t right,uint32_t bottom,uint8_t* destination,size_t pitch){
    uint64_t blocksWide=(uint64_t(width)+3)/4,blocksHigh=(uint64_t(height)+3)/4;
    if(!width||!height||blocksWide*blocksHigh*8>sourceBytes||uint64_t(width)*height>0x1fffffff)
        throw std::invalid_argument("Invalid DXT dimensions or truncated blocks");
    if(left>=right||top>=bottom||right>width||bottom>height)throw std::invalid_argument("Invalid DXT rectangle");
    uint8_t tile[16][4];
    for(uint32_t by=top/4;by<=(bottom-1)/4;++by)for(uint32_t bx=left/4;bx<=(right-1)/4;++bx){
        decodeDXTBlock(1,source+(size_t(by)*blocksWide+bx)*8,tile);
        uint32_t x0=std::max(bx*4,left),x1=std::min(bx*4+4,right),y0=std::max(by*4,top),y1=std::min(by*4+4,bottom);
        for(uint32_t y=y0;y<y1;++y)for(uint32_t x=x0;x<x1;++x){
            const uint8_t* texel=tile[(y&3)*4+(x&3)];
            uint16_t packed=uint16_t((texel[3]>>7)<<15|(texel[0]>>3)<<10|(texel[1]>>3)<<5|(texel[2]>>3));
            std::memcpy(destination+size_t(y-top)*pitch+size_t(x-left)*2,&packed,2);
        }
    }
}

// Decode DXT1, DXT3 or DXT5 into byte-ordered RGBA8, including partial edge blocks.
inline std::vector<uint8_t> decodeDXT(unsigned mode,uint32_t width,uint32_t height,
                                     const uint8_t* source,size_t sourceBytes){
    if(!width||!height)throw std::invalid_argument("Invalid DXT dimensions or truncated blocks");
    std::vector<uint8_t> rgba(size_t(width)*height*4);
    decodeDXTRect(mode,width,height,source,sourceBytes,0,0,width,height,rgba.data(),size_t(width)*4);
    return rgba;
}
