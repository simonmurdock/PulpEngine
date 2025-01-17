#include <EngineCommon.h>
#include "MD5.h"

X_NAMESPACE_BEGIN(core)

namespace Hash
{
    namespace
    {
// Constants for MD5Transform routine.
#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

        inline uint32_t F(uint32_t x, uint32_t y, uint32_t z)
        {
            return x & y | ~x & z;
        }

        inline uint32_t G(uint32_t x, uint32_t y, uint32_t z)
        {
            return x & z | y & ~z;
        }

        inline uint32_t H(uint32_t x, uint32_t y, uint32_t z)
        {
            return x ^ y ^ z;
        }

        inline uint32_t I(uint32_t x, uint32_t y, uint32_t z)
        {
            return y ^ (x | ~z);
        }

        inline uint32_t rotate_left(uint32_t x, int n)
        {
            return (x << n) | (x >> (32 - n));
        }

        inline void FF(uint32_t& a, uint32_t b, uint32_t c, uint32_t d,
            uint32_t x, uint32_t s, uint32_t ac)
        {
            a = rotate_left(a + F(b, c, d) + x + ac, s) + b;
        }

        inline void GG(uint32_t& a, uint32_t b, uint32_t c, uint32_t d,
            uint32_t x, uint32_t s, uint32_t ac)
        {
            a = rotate_left(a + G(b, c, d) + x + ac, s) + b;
        }

        inline void HH(uint32_t& a, uint32_t b, uint32_t c, uint32_t d,
            uint32_t x, uint32_t s, uint32_t ac)
        {
            a = rotate_left(a + H(b, c, d) + x + ac, s) + b;
        }

        inline void II(uint32_t& a, uint32_t b, uint32_t c, uint32_t d,
            uint32_t x, uint32_t s, uint32_t ac)
        {
            a = rotate_left(a + I(b, c, d) + x + ac, s) + b;
        }

    } // namespace

    // ------------------------------------

    MD5::MD5()
    {
        reset();
    }

    MD5::~MD5()
    {
    }

    void MD5::reset(void)
    {
        finalized_ = false;

        state_[0] = 0x67452301;
        state_[1] = 0xefcdab89;
        state_[2] = 0x98badcfe;
        state_[3] = 0x10325476;

        count_[0] = 0;
        count_[1] = 0;

        zero_object(buffer_);
    }

    void MD5::update(const char* pStr)
    {
        size_t length = core::strUtil::strlen(pStr);
        update(reinterpret_cast<const void*>(pStr), length);
    }

    void MD5::update(const void* pBuf, size_t bytelength)
    {
        const uint8_t* pInput = reinterpret_cast<const uint8_t*>(pBuf);

        // compute number of bytes mod 64
        size_t index = count_[0] / 8 % BLOCK_BYTES;

        // Update number of bits
        if ((count_[0] += static_cast<uint32_t>(bytelength << 3)) < (bytelength << 3)) {
            count_[1]++;
        }

        count_[1] += static_cast<uint32_t>(bytelength >> 29);

        // number of bytes we need to fill in buffer_
        size_t firstpart = 64 - index;
        size_t i;

        // transform as many times as possible.
        if (bytelength >= firstpart) {
            // fill buffer_ first, transform
            memcpy(&buffer_[index], pInput, firstpart);
            transform(buffer_);

            // transform chunks of blocksize (64 bytes)
            for (i = firstpart; i + BLOCK_BYTES <= bytelength; i += BLOCK_BYTES) {
                transform(&pInput[i]);
            }

            index = 0;
        }
        else {
            i = 0;
        }

        // buffer_ remaining input
        memcpy(&buffer_[index], &pInput[i], bytelength - i);
    }

    MD5Digest& MD5::finalize(void)
    {
        static unsigned char padding[64] = {
            0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

        if (!finalized_) {
            // Save number of bits
            uint8_t bits[8];
            encode(bits, count_, 8);

            // pad out to 56 mod 64.
            size_t index = count_[0] / 8 % 64;
            size_t padLen = (index < 56) ? (56 - index) : (120 - index);
            update(padding, padLen);

            // Append length (before padding)
            update(bits, 8);

            // Store state_ in digest
            encode(digest_.bytes, state_, 16);

            // zero sensitive information.
            zero_object(buffer_);
            zero_object(count_);

            finalized_ = true;
        }

        return digest_;
    }

    void MD5::transform(const uint8_t block[BLOCK_BYTES])
    {
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], x[16];
        decode(x, block, BLOCK_BYTES);

        /* Round 1 */
        FF(a, b, c, d, x[0], S11, 0xd76aa478);  /* 1 */
        FF(d, a, b, c, x[1], S12, 0xe8c7b756);  /* 2 */
        FF(c, d, a, b, x[2], S13, 0x242070db);  /* 3 */
        FF(b, c, d, a, x[3], S14, 0xc1bdceee);  /* 4 */
        FF(a, b, c, d, x[4], S11, 0xf57c0faf);  /* 5 */
        FF(d, a, b, c, x[5], S12, 0x4787c62a);  /* 6 */
        FF(c, d, a, b, x[6], S13, 0xa8304613);  /* 7 */
        FF(b, c, d, a, x[7], S14, 0xfd469501);  /* 8 */
        FF(a, b, c, d, x[8], S11, 0x698098d8);  /* 9 */
        FF(d, a, b, c, x[9], S12, 0x8b44f7af);  /* 10 */
        FF(c, d, a, b, x[10], S13, 0xffff5bb1); /* 11 */
        FF(b, c, d, a, x[11], S14, 0x895cd7be); /* 12 */
        FF(a, b, c, d, x[12], S11, 0x6b901122); /* 13 */
        FF(d, a, b, c, x[13], S12, 0xfd987193); /* 14 */
        FF(c, d, a, b, x[14], S13, 0xa679438e); /* 15 */
        FF(b, c, d, a, x[15], S14, 0x49b40821); /* 16 */

        /* Round 2 */
        GG(a, b, c, d, x[1], S21, 0xf61e2562);  /* 17 */
        GG(d, a, b, c, x[6], S22, 0xc040b340);  /* 18 */
        GG(c, d, a, b, x[11], S23, 0x265e5a51); /* 19 */
        GG(b, c, d, a, x[0], S24, 0xe9b6c7aa);  /* 20 */
        GG(a, b, c, d, x[5], S21, 0xd62f105d);  /* 21 */
        GG(d, a, b, c, x[10], S22, 0x2441453);  /* 22 */
        GG(c, d, a, b, x[15], S23, 0xd8a1e681); /* 23 */
        GG(b, c, d, a, x[4], S24, 0xe7d3fbc8);  /* 24 */
        GG(a, b, c, d, x[9], S21, 0x21e1cde6);  /* 25 */
        GG(d, a, b, c, x[14], S22, 0xc33707d6); /* 26 */
        GG(c, d, a, b, x[3], S23, 0xf4d50d87);  /* 27 */
        GG(b, c, d, a, x[8], S24, 0x455a14ed);  /* 28 */
        GG(a, b, c, d, x[13], S21, 0xa9e3e905); /* 29 */
        GG(d, a, b, c, x[2], S22, 0xfcefa3f8);  /* 30 */
        GG(c, d, a, b, x[7], S23, 0x676f02d9);  /* 31 */
        GG(b, c, d, a, x[12], S24, 0x8d2a4c8a); /* 32 */

        /* Round 3 */
        HH(a, b, c, d, x[5], S31, 0xfffa3942);  /* 33 */
        HH(d, a, b, c, x[8], S32, 0x8771f681);  /* 34 */
        HH(c, d, a, b, x[11], S33, 0x6d9d6122); /* 35 */
        HH(b, c, d, a, x[14], S34, 0xfde5380c); /* 36 */
        HH(a, b, c, d, x[1], S31, 0xa4beea44);  /* 37 */
        HH(d, a, b, c, x[4], S32, 0x4bdecfa9);  /* 38 */
        HH(c, d, a, b, x[7], S33, 0xf6bb4b60);  /* 39 */
        HH(b, c, d, a, x[10], S34, 0xbebfbc70); /* 40 */
        HH(a, b, c, d, x[13], S31, 0x289b7ec6); /* 41 */
        HH(d, a, b, c, x[0], S32, 0xeaa127fa);  /* 42 */
        HH(c, d, a, b, x[3], S33, 0xd4ef3085);  /* 43 */
        HH(b, c, d, a, x[6], S34, 0x4881d05);   /* 44 */
        HH(a, b, c, d, x[9], S31, 0xd9d4d039);  /* 45 */
        HH(d, a, b, c, x[12], S32, 0xe6db99e5); /* 46 */
        HH(c, d, a, b, x[15], S33, 0x1fa27cf8); /* 47 */
        HH(b, c, d, a, x[2], S34, 0xc4ac5665);  /* 48 */

        /* Round 4 */
        II(a, b, c, d, x[0], S41, 0xf4292244);  /* 49 */
        II(d, a, b, c, x[7], S42, 0x432aff97);  /* 50 */
        II(c, d, a, b, x[14], S43, 0xab9423a7); /* 51 */
        II(b, c, d, a, x[5], S44, 0xfc93a039);  /* 52 */
        II(a, b, c, d, x[12], S41, 0x655b59c3); /* 53 */
        II(d, a, b, c, x[3], S42, 0x8f0ccc92);  /* 54 */
        II(c, d, a, b, x[10], S43, 0xffeff47d); /* 55 */
        II(b, c, d, a, x[1], S44, 0x85845dd1);  /* 56 */
        II(a, b, c, d, x[8], S41, 0x6fa87e4f);  /* 57 */
        II(d, a, b, c, x[15], S42, 0xfe2ce6e0); /* 58 */
        II(c, d, a, b, x[6], S43, 0xa3014314);  /* 59 */
        II(b, c, d, a, x[13], S44, 0x4e0811a1); /* 60 */
        II(a, b, c, d, x[4], S41, 0xf7537e82);  /* 61 */
        II(d, a, b, c, x[11], S42, 0xbd3af235); /* 62 */
        II(c, d, a, b, x[2], S43, 0x2ad7d2bb);  /* 63 */
        II(b, c, d, a, x[9], S44, 0xeb86d391);  /* 64 */

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;

        // zero sensitive information.
        zero_object(x);
    }

    void MD5::decode(uint32_t* pOutput, const uint8_t* pInput, size_t len)
    {
        for (size_t i = 0, j = 0; j < len; i++, j += 4)
            pOutput[i] = ((uint32_t)pInput[j]) | (((uint32_t)pInput[j + 1]) << 8) | (((uint32_t)pInput[j + 2]) << 16) | (((uint32_t)pInput[j + 3]) << 24);
    }

    // encodes input (uint32) into output (unsigned char). Assumes len is
    // a multiple of 4.
    void MD5::encode(uint8_t* pOutput, const uint32_t* pInput, size_t len)
    {
        for (size_t i = 0, j = 0; j < len; i++, j += 4) {
            pOutput[j] = pInput[i] & 0xff;
            pOutput[j + 1] = (pInput[i] >> 8) & 0xff;
            pOutput[j + 2] = (pInput[i] >> 16) & 0xff;
            pOutput[j + 3] = (pInput[i] >> 24) & 0xff;
        }
    }

    MD5::Digest MD5::calc(const void* src, size_t bytelength)
    {
        MD5 hasher;
        hasher.update(src, bytelength);
        return hasher.finalize();
    }


} // namespace Hash

X_NAMESPACE_END