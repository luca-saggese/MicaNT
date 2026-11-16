#pragma once

// Bounded Ring-3 BMP and PNG decoders. The parsers consume immutable,
// length-delimited bytes and publish a frame only after validation succeeds.
#include "image_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace micant::um::imaging {
namespace detail {

inline uint16_t ReadLe16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t ReadLe32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t ReadBe32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline bool CheckedMul(uint64_t a, uint64_t b, uint64_t& out) noexcept {
    if (a && b > UINT64_MAX / a) return false;
    out = a * b;
    return true;
}
inline bool FrameSize(uint32_t w, uint32_t h, ImageLimits limits,
                      uint32_t& stride, size_t& bytes) noexcept {
    uint64_t pixels{}, row{}, total{};
    if (!w || !h || !CheckedMul(w, h, pixels) || pixels > limits.maxPixels ||
        !CheckedMul(w, 4, row) || row > UINT32_MAX ||
        !CheckedMul(row, h, total) || total > limits.maxDecodedBytes ||
        total > std::numeric_limits<size_t>::max()) return false;
    stride = static_cast<uint32_t>(row);
    bytes = static_cast<size_t>(total);
    return true;
}
inline uint8_t Premul(uint8_t c, uint8_t a) noexcept {
    return static_cast<uint8_t>((static_cast<uint32_t>(c) * a + 127) / 255);
}

} // namespace detail

inline ImageStatus DecodeBmp(std::span<const uint8_t> in, ImageFrame& output,
                             const ImageAllocator& allocator,
                             ImageLimits limits = {}) noexcept {
    using namespace detail;
    if (in.size() > limits.maxEncodedBytes) return ImageStatus::ImageTooLarge;
    if (in.size() < 14) return ImageStatus::TruncatedInput;
    if (in[0] != 'B' || in[1] != 'M') return ImageStatus::UnknownFormat;
    const uint32_t pixelOffset = ReadLe32(in.data() + 10);
    if (in.size() < 18) return ImageStatus::TruncatedInput;
    const uint32_t dibSize = ReadLe32(in.data() + 14);
    if (dibSize < 40) return ImageStatus::UnsupportedFeature;
    if (dibSize > in.size() - 14) return ImageStatus::TruncatedInput;
    if (static_cast<uint64_t>(pixelOffset) < 14ull + dibSize) return ImageStatus::MalformedData;
    const int32_t signedWidth = static_cast<int32_t>(ReadLe32(in.data() + 18));
    const int32_t signedHeight = static_cast<int32_t>(ReadLe32(in.data() + 22));
    const uint16_t planes = ReadLe16(in.data() + 26);
    const uint16_t bpp = ReadLe16(in.data() + 28);
    const uint32_t compression = ReadLe32(in.data() + 30);
    if (signedWidth <= 0 || signedHeight == 0 || signedHeight == INT32_MIN || planes != 1)
        return ImageStatus::MalformedData;
    if ((bpp != 24 && bpp != 32) || compression != 0)
        return ImageStatus::UnsupportedFeature;
    const uint32_t width = static_cast<uint32_t>(signedWidth);
    const uint32_t height = static_cast<uint32_t>(signedHeight < 0 ? -signedHeight : signedHeight);
    uint32_t stride{}; size_t bytes{};
    if (!FrameSize(width, height, limits, stride, bytes)) return ImageStatus::ImageTooLarge;
    uint64_t rawRowBits{}, paddedRow{}, inputBytes{};
    if (!CheckedMul(width, bpp, rawRowBits) || rawRowBits > UINT64_MAX - 31)
        return ImageStatus::ImageTooLarge;
    paddedRow = ((rawRowBits + 31) / 32) * 4;
    if (!CheckedMul(paddedRow, height, inputBytes) || pixelOffset > in.size() ||
        inputBytes > in.size() - pixelOffset) return ImageStatus::TruncatedInput;
    const uint32_t declaredFileSize = ReadLe32(in.data() + 2);
    const uint64_t requiredFileSize = static_cast<uint64_t>(pixelOffset) + inputBytes;
    if (declaredFileSize && (declaredFileSize < requiredFileSize || declaredFileSize > in.size()))
        return declaredFileSize > in.size() ? ImageStatus::TruncatedInput : ImageStatus::MalformedData;
    ImageFrame frame{};
    frame.width = width; frame.height = height; frame.stride = stride;
    frame.sourceFormat = ImageFormat::Bmp;
    if (!frame.pixels.allocate(bytes, allocator)) return ImageStatus::OutOfMemory;
    const uint32_t sourceBytes = bpp / 8;
    const bool topDown = signedHeight < 0;
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t sourceY = topDown ? y : height - 1 - y;
        const uint8_t* row = in.data() + pixelOffset + static_cast<size_t>(sourceY * paddedRow);
        uint8_t* dst = frame.pixels.data() + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* src = row + static_cast<size_t>(x) * sourceBytes;
            // BI_RGB defines the fourth byte as reserved rather than a
            // portable alpha mask. Alpha-bearing bitfields are unsupported.
            const uint8_t alpha = 255;
            dst[x * 4 + 0] = Premul(src[0], alpha);
            dst[x * 4 + 1] = Premul(src[1], alpha);
            dst[x * 4 + 2] = Premul(src[2], alpha);
            dst[x * 4 + 3] = alpha;
        }
    }
    frame.sourceColor = ImageColorModel::Rgb;
    frame.bitsPerChannel = 8;
    frame.sourceHadAlpha = false;
    output = std::move(frame);
    return ImageStatus::Success;
}

namespace detail {
struct BitReader {
    std::span<const uint8_t> data; size_t pos{}; uint64_t bits{}; unsigned count{};
    [[nodiscard]] bool fullyConsumed() const noexcept { return pos == data.size(); }
    bool Read(unsigned n, uint32_t& out) noexcept {
        while (count < n) {
            if (pos >= data.size()) return false;
            bits |= static_cast<uint64_t>(data[pos++]) << count; count += 8;
        }
        out = static_cast<uint32_t>(bits & ((uint64_t{1} << n) - 1));
        bits >>= n; count -= n; return true;
    }
    bool AlignByte() noexcept { const unsigned drop = count & 7; uint32_t ignored{}; return !drop || Read(drop, ignored); }
};
struct Huffman {
    std::array<uint16_t, 16> count{};
    std::array<uint16_t, 288> symbol{};
    bool Build(const uint8_t* lengths, size_t n) noexcept {
        count.fill(0);
        for (size_t i=0;i<n;++i) { if (lengths[i] > 15) return false; ++count[lengths[i]]; }
        if (count[0] == n) return false;
        int left=1; for (unsigned len=1;len<=15;++len) { left=(left<<1)-count[len]; if (left<0) return false; }
        std::array<uint16_t,16> offsets{}; offsets[1]=0;
        for (unsigned len=1;len<15;++len) offsets[len+1]=static_cast<uint16_t>(offsets[len]+count[len]);
        for (size_t sym=0;sym<n;++sym) if (lengths[sym]) symbol[offsets[lengths[sym]]++]=static_cast<uint16_t>(sym);
        return true;
    }
    bool Decode(BitReader& br, uint16_t& out) const noexcept {
        uint32_t code=0, first=0, index=0, bit{};
        for (unsigned len=1;len<=15;++len) {
            if (!br.Read(1,bit)) return false;
            code |= bit;
            const uint32_t amount=count[len];
            if (code >= first && code-first < amount) { out=symbol[index+(code-first)]; return true; }
            index += amount; first=(first+amount)<<1; code <<= 1;
        }
        return false;
    }
};

inline bool InflateZlib(std::span<const uint8_t> src, std::span<uint8_t> out) noexcept {
    const size_t expected = out.size();
    if (src.size()<6) return false;
    const uint8_t cmf=src[0], flg=src[1];
    if ((cmf&15)!=8 || (cmf>>4)>7 || ((static_cast<uint16_t>(cmf)<<8)|flg)%31 || (flg&32)) return false;
    BitReader br{src.subspan(2,src.size()-6)};
    size_t outSize = 0;
    static constexpr uint16_t lengthBase[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static constexpr uint8_t lengthExtra[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static constexpr uint16_t distBase[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static constexpr uint8_t distExtra[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    bool final=false;
    while(!final) {
        uint32_t f{},type{}; if(!br.Read(1,f)||!br.Read(2,type)) return false; final=f!=0;
        if(type==0) {
            if(!br.AlignByte()) return false;
            uint32_t len{},nlen{}; if(!br.Read(16,len)||!br.Read(16,nlen)||((len^0xffffu)&0xffffu)!=nlen) return false;
            if(len>expected-outSize) return false;
            for(uint32_t i=0;i<len;++i){uint32_t v{};if(!br.Read(8,v))return false;out[outSize++]=static_cast<uint8_t>(v);}
            continue;
        }
        if(type==3) return false;
        Huffman lit{},dist{};
        if(type==1) {
            uint8_t ll[288]{}; for(unsigned i=0;i<=143;++i)ll[i]=8;for(unsigned i=144;i<=255;++i)ll[i]=9;for(unsigned i=256;i<=279;++i)ll[i]=7;for(unsigned i=280;i<288;++i)ll[i]=8;
            uint8_t dd[32];std::fill(std::begin(dd),std::end(dd),5);
            if(!lit.Build(ll,288)||!dist.Build(dd,32))return false;
        } else {
            uint32_t hlit{},hdist{},hclen{};if(!br.Read(5,hlit)||!br.Read(5,hdist)||!br.Read(4,hclen))return false;
            hlit+=257;hdist+=1;hclen+=4;if(hlit>286||hdist>32)return false;
            static constexpr uint8_t order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t cl[19]{};for(uint32_t i=0;i<hclen;++i){uint32_t v{};if(!br.Read(3,v))return false;cl[order[i]]=static_cast<uint8_t>(v);}
            Huffman ch{};if(!ch.Build(cl,19))return false;
            std::array<uint8_t,318> lens{};size_t used=0,total=hlit+hdist;
            while(used<total){uint16_t s{};if(!ch.Decode(br,s))return false;
                if(s<16)lens[used++]=static_cast<uint8_t>(s);
                else {uint32_t extra{},repeat{};uint8_t value{};
                    if(s==16){if(!used||!br.Read(2,extra))return false;repeat=extra+3;value=lens[used-1];}
                    else if(s==17){if(!br.Read(3,extra))return false;repeat=extra+3;}
                    else if(s==18){if(!br.Read(7,extra))return false;repeat=extra+11;}
                    else return false;
                    if(repeat>total-used)return false;while(repeat--)lens[used++]=value;
                }
            }
            if(!lit.Build(lens.data(),hlit)||!dist.Build(lens.data()+hlit,hdist))return false;
        }
        for(;;){uint16_t sym{};if(!lit.Decode(br,sym))return false;
            if(sym<256){if(outSize>=expected)return false;out[outSize++]=static_cast<uint8_t>(sym);continue;}
            if(sym==256)break;if(sym<257||sym>285)return false;
            const unsigned li=sym-257;uint32_t ext{};if(!br.Read(lengthExtra[li],ext))return false;uint32_t length=lengthBase[li]+ext;
            uint16_t ds{};if(!dist.Decode(br,ds)||ds>=30)return false;uint32_t de{};if(!br.Read(distExtra[ds],de))return false;uint32_t distance=distBase[ds]+de;
            if(distance>outSize||length>expected-outSize)return false;
            while(length--){out[outSize]=out[outSize-distance];++outSize;}
        }
    }
    if(outSize!=expected || !br.fullyConsumed())return false;
    // Validate the zlib Adler-32 trailer.
    uint32_t a=1,b=0;for(uint8_t v:out){a=(a+v)%65521;b=(b+a)%65521;}
    return ((b<<16)|a)==ReadBe32(src.data()+src.size()-4);
}
inline uint32_t Crc32(std::span<const uint8_t> bytes) noexcept {
    uint32_t crc=0xffffffffu;for(uint8_t b:bytes){crc^=b;for(int i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&static_cast<uint32_t>(-static_cast<int32_t>(crc&1)));}return ~crc;
}
} // namespace detail

inline ImageStatus DecodePng(std::span<const uint8_t> in, ImageFrame& output,
                             const ImageAllocator& allocator,
                             ImageLimits limits = {}) noexcept {
    using namespace detail;
    static constexpr uint8_t sig[8]={137,80,78,71,13,10,26,10};
    if(in.size()>limits.maxEncodedBytes)return ImageStatus::ImageTooLarge;
    if(in.size()<8)return ImageStatus::TruncatedInput;
    if(std::memcmp(in.data(),sig,8)!=0)return ImageStatus::UnknownFormat;
    uint32_t width=0,height=0;uint8_t depth=0,color=0;
    bool haveHeader=false,haveEnd=false,havePalette=false,haveTransparency=false;
    std::array<uint8_t, 768> palette{};
    std::array<uint8_t, 256> paletteAlpha{};
    uint32_t paletteEntries=0;
    paletteAlpha.fill(255);
    uint64_t compressedBytes=0;
    size_t off=8;bool seenIdat=false,idatClosed=false;
    while(off<in.size()){
        if(in.size()-off<12)return ImageStatus::TruncatedInput;
        const uint32_t n=ReadBe32(in.data()+off);off+=4;
        if(n>in.size()-off-8)return ImageStatus::TruncatedInput;
        const uint8_t* type=in.data()+off;const auto data=in.subspan(off+4,n);
        const uint32_t crc=ReadBe32(in.data()+off+4+n);
        if(Crc32(in.subspan(off,4+n))!=crc)return ImageStatus::MalformedData;
        for (size_t i = 0; i < 4; ++i) {
            const uint8_t c = type[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
                return ImageStatus::MalformedData;
        }
        if (type[2] >= 'a' && type[2] <= 'z') return ImageStatus::MalformedData;
        if(std::memcmp(type,"IHDR",4)==0){
            if(haveHeader||n!=13)return ImageStatus::MalformedData;
            width=ReadBe32(data.data());height=ReadBe32(data.data()+4);depth=data[8];color=data[9];
            if(data[10]!=0||data[11]!=0||data[12]!=0)return ImageStatus::UnsupportedFeature;
            haveHeader=true;
            if(depth!=8||(color!=2&&color!=3&&color!=6))return ImageStatus::UnsupportedFeature;
            uint32_t stride{};size_t bytes{};if(!FrameSize(width,height,limits,stride,bytes))return ImageStatus::ImageTooLarge;
        }else if(std::memcmp(type,"IDAT",4)==0){
            if(!haveHeader||haveEnd||idatClosed)return ImageStatus::MalformedData;
            seenIdat=true;
            if(n>limits.maxIntermediateBytes||compressedBytes>limits.maxIntermediateBytes-n)return ImageStatus::ImageTooLarge;
            compressedBytes+=n;
        }else if(std::memcmp(type,"IEND",4)==0){if(n!=0)return ImageStatus::MalformedData;haveEnd=true;off+=static_cast<size_t>(n)+8;break;
        }else if(std::memcmp(type,"PLTE",4)==0){
            if(!haveHeader||seenIdat||havePalette||n==0||n>768||n%3!=0)return ImageStatus::MalformedData;
            if (color == 3 && n / 3 > 256) return ImageStatus::MalformedData;
            std::memcpy(palette.data(), data.data(), n);
            paletteEntries = n / 3;
            havePalette=true;
        }else if(std::memcmp(type,"tRNS",4)==0){
            if (!haveHeader || seenIdat || haveTransparency || color != 3 ||
                !havePalette || n == 0 || n > paletteEntries)
                return ImageStatus::MalformedData;
            std::memcpy(paletteAlpha.data(), data.data(), n);
            haveTransparency = true;
        }else {
            if(seenIdat)idatClosed=true;
            if(!haveHeader||(type[0]&0x20)==0)return ImageStatus::UnsupportedFeature;
        }
        off+=static_cast<size_t>(n)+8;
    }
    if(!haveHeader)return ImageStatus::MalformedData;
    if(color==3&&!havePalette)return ImageStatus::MalformedData;
    if(compressedBytes==0||!haveEnd)return ImageStatus::TruncatedInput;
    if(off!=in.size())return ImageStatus::MalformedData;
    const uint32_t channels=color==6?4:(color==3?1:3);uint64_t rowBytes64{},rawSize64{};
    if(!CheckedMul(width,channels,rowBytes64)||rowBytes64>UINT32_MAX||!CheckedMul(rowBytes64+1,height,rawSize64)||rawSize64>limits.maxIntermediateBytes||rawSize64>SIZE_MAX||compressedBytes>limits.maxIntermediateBytes-rawSize64)return ImageStatus::ImageTooLarge;
    uint64_t outputSize64{};
    if(!CheckedMul(width,height,outputSize64)||!CheckedMul(outputSize64,4,outputSize64)||
       outputSize64>limits.maxDecodedBytes||outputSize64>limits.maxIntermediateBytes-rawSize64-compressedBytes)
        return ImageStatus::ImageTooLarge;
    ImageBuffer compressed{};
    if(compressedBytes>SIZE_MAX||!compressed.allocate(static_cast<size_t>(compressedBytes),allocator))return ImageStatus::OutOfMemory;
    size_t compressedOffset=0;off=8;
    while(off<in.size()){
        const uint32_t n=ReadBe32(in.data()+off);const uint8_t* type=in.data()+off+4;
        if(std::memcmp(type,"IDAT",4)==0){std::memcpy(compressed.data()+compressedOffset,in.data()+off+8,n);compressedOffset+=n;}
        off+=static_cast<size_t>(n)+12;
        if(std::memcmp(type,"IEND",4)==0)break;
    }
    ImageBuffer raw{};
    if(rawSize64>SIZE_MAX||!raw.allocate(static_cast<size_t>(rawSize64),allocator))return ImageStatus::OutOfMemory;
    if(!InflateZlib(compressed.span(),raw.span()))return ImageStatus::MalformedData;
    const size_t rowBytes=static_cast<size_t>(rowBytes64);
    for(uint32_t y=0;y<height;++y){const size_t rowStart=static_cast<size_t>(y)*(rowBytes+1);const uint8_t filter=raw[rowStart];if(filter>4)return ImageStatus::MalformedData;
        uint8_t* dst=raw.data()+rowStart+1;const uint8_t* src=dst;const uint8_t* prev=y?raw.data()+rowStart-(rowBytes+1)+1:nullptr;
        for(size_t x=0;x<rowBytes;++x){const uint8_t left=x>=channels?dst[x-channels]:0,up=prev?prev[x]:0,ul=prev&&x>=channels?prev[x-channels]:0;uint8_t predictor=0;
            if(filter==1)predictor=left;else if(filter==2)predictor=up;else if(filter==3)predictor=static_cast<uint8_t>((static_cast<unsigned>(left)+up)/2);else if(filter==4){const int p=static_cast<int>(left)+up-ul;const int dl=p-static_cast<int>(left),du=p-static_cast<int>(up),dul=p-static_cast<int>(ul);const int pa=dl<0?-dl:dl,pb=du<0?-du:du,pc=dul<0?-dul:dul;predictor=pa<=pb&&pa<=pc?left:(pb<=pc?up:ul);}
            dst[x]=static_cast<uint8_t>(src[x]+predictor);
        }
    }
    uint32_t stride{};size_t bytes{};if(!FrameSize(width,height,limits,stride,bytes))return ImageStatus::ImageTooLarge;
    ImageFrame frame{};frame.width=width;frame.height=height;frame.stride=stride;frame.sourceFormat=ImageFormat::Png;
    if(!frame.pixels.allocate(bytes,allocator))return ImageStatus::OutOfMemory;
    for(uint32_t y=0;y<height;++y){const uint8_t* src=raw.data()+static_cast<size_t>(y)*(rowBytes+1)+1;uint8_t* dst=frame.pixels.data()+static_cast<size_t>(y)*stride;
        for(uint32_t x=0;x<width;++x){
            uint8_t r{},g{},b{},a=255;
            if(color==3){
                const uint8_t index=src[x];
                if(index>=paletteEntries)return ImageStatus::MalformedData;
                r=palette[index*3];g=palette[index*3+1];b=palette[index*3+2];
                a=paletteAlpha[index];
            }else{
                r=src[x*channels];g=src[x*channels+1];b=src[x*channels+2];
                if(color==6)a=src[x*channels+3];
            }
            dst[x*4]=Premul(b,a);dst[x*4+1]=Premul(g,a);
            dst[x*4+2]=Premul(r,a);dst[x*4+3]=a;
        }}
    frame.sourceColor=ImageColorModel::Rgb;frame.bitsPerChannel=8;
    frame.sourceHadAlpha=color==6||(color==3&&haveTransparency);
    output=std::move(frame);return ImageStatus::Success;
}

} // namespace micant::um::imaging
