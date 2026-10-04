#include "Engine3D.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include "TVOlogo.h"

// 640x480 VGA default configuration
#ifndef FBW
#define FBW 640
#endif
#ifndef FBH
#define FBH 480
#endif
#ifndef FBW2
#define FBW2 (FBW / 2)
#endif

#define m00 0
#define m01 1
#define m02 2
#define m03 3
#define m10 4
#define m11 5
#define m12 6
#define m13 7
#define m20 8
#define m21 9
#define m22 10
#define m23 11
#define m30 12
#define m31 13
#define m32 14
#define m33 15

uint16_t LTW = FBW;
uint8_t CNFGDialogColor; 
uint8_t CNFGBGColor;
uint8_t CNFGLastColor;
uint8_t * frontframe;

uint8_t sintable[128] = { 0, 6, 12, 18, 25, 31, 37, 43, 49, 55, 62, 68, 74, 80, 86, 91, 97, 103, 109, 114, 120, 125, 131, 136, 141, 147, 152, 157, 162, 166, 171, 176, 180, 185, 189, 193, 197, 201, 205, 208, 212, 215, 219, 222, 225, 228, 230, 233, 236, 238, 240, 242, 244, 246, 247, 249, 250, 251, 252, 253, 254, 254, 255, 255, 255, 255, 255, 254, 254, 253, 252, 251, 250, 249, 247, 246, 244, 242, 240, 238, 236, 233, 230, 228, 225, 222, 219, 215, 212, 208, 205, 201, 197, 193, 189, 185, 180, 176, 171, 166, 162, 157, 152, 147, 141, 136, 131, 125, 120, 114, 109, 103, 97, 91, 86, 80, 74, 68, 62, 55, 49, 43, 37, 31, 25, 18, 12, 6, };

void (*CNFGTackPixel)( int x, int y );

// -----------------------------------------------------------------------------
// SYSTEM & DOUBLE BUFFER MANAGEMENT
// -----------------------------------------------------------------------------

Engine3D::~Engine3D() {
    if (_ownsBackBuffer && _backBuffer) {
        free(_backBuffer);
        _backBuffer = nullptr;
    }
}

void Engine3D::begin(uint8_t spino, uint8_t* extram) {
    frameCount = 0;
    _vga->begin( extram); 

    init(
        _vga->VRAM(),     // Pass base pointer of allocated VGA frame buffer
        _vga->width(),    // 640 pixels wide
        _vga->height()    // 480 lines high
    );
}

// Memory mapping and resolution initializer
void Engine3D::init(uint8_t* vram, uint16_t width, uint16_t height) {
    _screen = vram;  
    _width  = width;                 // 640 pixels
    _height = height;                // 480 pixels
    _hres   = _width / 8;            // 80 bytes per scanline stride
    _vres   = _height;               // 480 total active scanlines

#if BITBAND == 1
    uint32_t vram_addr = (uint32_t)_screen;
    _adr = (volatile uint32_t*)(0x22000000 + ((vram_addr - 0x20000000) * 32));
#endif
}

bool Engine3D::setDoubleBuffering(bool enable, uint8_t* externalBuffer) {
    if (_ownsBackBuffer && _backBuffer) {
        free(_backBuffer);
        _backBuffer = nullptr;
        _ownsBackBuffer = false;
    }

    _doubleBuffered = enable;
    if (!enable) {
        _backBuffer = nullptr;
        return true;
    }

    size_t bufferSize = (size_t)_hres * _height; // 80 * 480 = 38,400 bytes

    if (externalBuffer != nullptr) {
        _backBuffer = externalBuffer;
        _ownsBackBuffer = false;
    } else {
        _backBuffer = (uint8_t*)malloc(bufferSize);
        _ownsBackBuffer = (_backBuffer != nullptr);
        if (!_backBuffer) {
            _doubleBuffered = false;
            return false; // Dynamic allocation failed
        }
    }

    memset(_backBuffer, 0, bufferSize);
    return true;
}

void Engine3D::display() {
    if (_doubleBuffered && _screen && _backBuffer) {
        size_t bufferSize = (size_t)_hres * _height;
        _vga->waitVBlank(); // copy during blanking to avoid tearing
        std::memcpy(_screen, _backBuffer, bufferSize);
    }
}

void Engine3D::adjust(int16_t cnt, int16_t hcnt, int16_t vcnt) {
    if (_vga) {
       // _tntsc->adjust(cnt, hcnt, vcnt);
    }
}

// -----------------------------------------------------------------------------
// CONTEXT ACCESSORS
// -----------------------------------------------------------------------------
void Engine3D::setPen(int x, int y) { _penX = x; _penY = y; }
void Engine3D::setColor(uint8_t col) { _lastColor = col ? 1 : 0; }
void Engine3D::setBGColor(uint8_t col) { _bgColor = col ? 1 : 0; }
void Engine3D::setDialogColor(uint8_t col) { _dialogColor = col ? 1 : 0; }

void Engine3D::clear() {
    fill(0);
}

void Engine3D::fill(uint8_t color) {
    uint8_t* targetBuffer = _doubleBuffered ? _backBuffer : _screen;
    if (!targetBuffer) return;

    size_t bufferSize = (size_t)_hres * _height;
    if (color == WHITE) {
        memset(targetBuffer, 0xFF, bufferSize);
    } else if (color == BLACK) {
        memset(targetBuffer, 0x00, bufferSize);
    } else if (color == INVERT) {
        for (size_t i = 0; i < bufferSize; ++i) {
            targetBuffer[i] = ~targetBuffer[i];
        }
    }
}

void Engine3D::drawPixel(uint16_t x, uint16_t y, uint8_t c) {
    if (x >= _width || y >= _height)
        return;

    sp(x, y, c);
}

void Engine3D::Schematic(uint16_t x, uint16_t y, const unsigned char * bmp, uint16_t i, uint16_t width, uint16_t lines){
    uint8_t temp, lshift, rshift, save, xtra;
    uint16_t si = 0;
    
    rshift = x & 7;
    lshift = 8 - rshift;

    if (width == 0) {
        width = *(bmp + i);
        i++;
    }
    if (lines == 0) {
        lines = *(bmp + i);
        i++;
    }
    
    xtra = width & 7;
    if (xtra == 0) {
        xtra = 8;
        width = width / 8;
    } else {
        width = (width / 8) + 1;
    }
    
    for (uint16_t l = 0; l < lines; l++) {
        si = (y + l) * _hres + (x / 8);
        
        if (width == 1) {
            temp = 0xff >> (rshift + xtra);
        } else {
            temp = 0;
        }

        save = _screen[si];
        _screen[si] &= ((0xff << lshift) | temp);
        temp = *(bmp + i++);
        _screen[si++] |= (temp >> rshift);

        uint16_t b = i + width - 1;
        for (; i < b; i++) {
            save = _screen[si];
            _screen[si] = (temp << lshift);
            temp = *(bmp + i);
            _screen[si++] |= (temp >> rshift);
        }

        if ((rshift + xtra) < 8) {
            _screen[si - 1] |= (save & (0xff >> (rshift + xtra)));
        }

        if ((int16_t)(rshift + xtra - 8) > 0) {
            _screen[si] &= (0xff >> (rshift + xtra - 8));
            _screen[si] |= (temp << lshift);
        } else if (rshift > 0) {
            _screen[si] |= (temp << lshift);
        }
    }
}

void Engine3D::intro_1() {
    const uint16_t w = TVOlogo[0];
    const uint16_t h = TVOlogo[1];

    const int16_t targetX = (hres() - w) >> 1;
    const int16_t targetY = (vres() - h) >> 1;

    const int16_t startX = hres() - w;
    const int16_t startY = 0;

    const uint16_t totalSteps = 30;

    for (uint16_t step = 0; step <= totalSteps; step++) {
        clear_screen();

        int16_t currentX = startX + ((targetX - startX) * step) / totalSteps;
        int16_t currentY = startY + ((targetY - startY) * step) / totalSteps;

        Schematic((uint16_t)currentX, (uint16_t)currentY, TVOlogo, 2, w, h);
        delay(30);
    }

    delay(2000);
    clear_screen();
}

void Engine3D::intro() {
    const uint16_t w = TVOlogo[0];
    const uint16_t h = TVOlogo[1];

    const int16_t startX  = 0;
    const int16_t endX    = hres() - w;
    const int16_t targetX = (hres() - w) >> 1;
    const int16_t centerY = (vres() - h) >> 1;

    const uint16_t stepsPerPhase = 30;

    for (uint16_t step = 0; step <= stepsPerPhase; step++) {
        clear_screen();
        int16_t currentX = startX + ((endX - startX) * step) / stepsPerPhase;
        Schematic((uint16_t)currentX, (uint16_t)centerY, TVOlogo, 2, w, h);
        delay(30);
    }

    for (uint16_t step = 0; step <= stepsPerPhase; step++) {
        clear_screen();
        int16_t currentX = endX + ((targetX - endX) * step) / stepsPerPhase;
        Schematic((uint16_t)currentX, (uint16_t)centerY, TVOlogo, 2, w, h);
        delay(30);
    }

    delay(2000);
    clear_screen();
}

void Engine3D::bitmap(uint16_t x, uint16_t y, const unsigned char * bmp, uint16_t i, uint16_t width, uint16_t lines) {
    if (!bmp || width == 0 || lines == 0)
        return;

    const uint16_t srcBytes = (width + 7) >> 3;
    const uint16_t dstByte = x >> 3;
    const uint8_t shift = x & 7;

    for (uint16_t row = 0; row < lines; row++) {
        const uint8_t *src = bmp + i + row * srcBytes;
        uint8_t *dst = _screen + (y + row) * _hres + dstByte;

        if (shift == 0) {
            for (uint16_t b = 0; b < srcBytes; b++) {
                dst[b] = src[b];
            }
        } else {
            for (uint16_t b = 0; b < srcBytes; b++) {
                uint8_t cur = src[b];
                uint8_t left = cur >> shift;
                uint8_t right = cur << (8 - shift);

                if (b == 0) {
                    uint8_t mask = 0xFF << (8 - shift);
                    dst[0] = (dst[0] & mask) | left;
                } else {
                    dst[b] = left;
                }

                uint8_t nextMask = 0xFF >> (8 - shift);
                dst[b + 1] = (dst[b + 1] & nextMask) | right;
            }
        }
    }
}

void Engine3D::LoadBitmap(const uint8_t* pImg, uint16_t imgSize) {
    uint8_t* target = getBackBuffer();

    if (!target || !pImg)
        return;

    size_t bufferSize = imgSize ? imgSize : ((size_t)_hres * _height);
    memcpy(target, pImg, bufferSize);
}

void Engine3D::render_ntsc_line(uint16_t line_number, uint8_t *line_buffer) {
    uint16_t start_line = 25;
    uint8_t* frame_buffer = getBackBuffer();

    if (!frame_buffer || !line_buffer)
        return;

    if (line_number >= start_line && line_number < (start_line + _height)) {
        uint16_t image_y = line_number - start_line;
        uint16_t offset = image_y * _hres;

        // Copy ONE line (80 bytes for 640px) to DMA line buffer
        memcpy(line_buffer, &frame_buffer[offset], _hres);
    } else {
        memset(line_buffer, 0x00, _hres); 
    }
}

// -----------------------------------------------------------------------------
// MATH & VECTOR HELPERS
// -----------------------------------------------------------------------------
int Engine3D::labs(int x) { return (x < 0) ? -x : x; }

template <typename T> int _v_sgn(T val) { return (T(0) < val) - (val < T(0)); }

#ifndef abs
#define abs(a) (((a) > 0) ? (a) : -(a))
#endif

int16_t Engine3D::sinFixed(uint8_t iv) {
    if( iv > 127 ) {
        return -sintable[iv-128];
    } else {
        return sintable[iv];
    }
}

int16_t Engine3D::cosFixed(uint8_t iv) {
    return sinFixed(iv + 64);
}

void Engine3D::makeXRotationMatrix(uint8_t angle, int16_t* f) {
    f[0] = 256;  f[1] = 0;  f[2] = 0;  f[3] = 0;
    f[4] = 0;  f[5] = cosFixed(angle);  f[6] = -sinFixed(angle);  f[7] = 0;
    f[8] = 0;  f[9] = sinFixed(angle);  f[10] = cosFixed(angle);  f[11] = 0;
    f[12] = 0;  f[13] = 0;  f[14] = 0;  f[15] = 256;
}

void Engine3D::makeYRotationMatrix(uint8_t angle, int16_t* f) {
    f[0] = cosFixed(angle);  f[1] = 0;  f[2] = sinFixed(angle);  f[3] = 0;
    f[4] = 0;  f[5] = 256;  f[6] = 0;  f[7] = 0;
    f[8] = -sinFixed(angle);  f[9] = 0;  f[10] = cosFixed(angle);  f[11] = 0;
    f[12] = 0;  f[13] = 0;  f[14] = 0;  f[15] = 256;
}

void Engine3D::identity(int16_t* matrix) {
    matrix[0] = 256; matrix[1] = 0; matrix[2] = 0; matrix[3] = 0;
    matrix[4] = 0; matrix[5] = 256; matrix[6] = 0; matrix[7] = 0;
    matrix[8] = 0; matrix[9] = 0; matrix[10] = 256; matrix[11] = 0;
    matrix[12] = 0; matrix[13] = 0; matrix[14] = 0; matrix[15] = 256;
}

void Engine3D::perspective(int fovx, int aspect, int zNear, int zFar, int16_t* out) {
    int16_t f = fovx;
    out[0] = f * 256 / aspect; out[1] = 0; out[2] = 0; out[3] = 0;
    out[4] = 0; out[5] = f; out[6] = 0; out[7] = 0;
    out[8] = 0; out[9] = 0;
    out[10] = 256 * (zFar + zNear) / (zNear - zFar);
    out[11] = 2 * zFar * zNear / (zNear - zFar);
    out[12] = 0; out[13] = 0; out[14] = -256; out[15] = 0;
}

void Engine3D::makeTranslateMatrix(int x, int y, int z, int16_t* out) {
    identity(out);
    out[m03] += x;
    out[m13] += y;
    out[m23] += z;
}

void Engine3D::translate(int16_t* matrix, int16_t x, int16_t y, int16_t z) {
    int16_t ftmp[16];
    identity(ftmp);
    ftmp[m03] += x;
    ftmp[m13] += y;
    ftmp[m23] += z;
    multiply(ftmp, matrix, matrix);
}

void Engine3D::scale(int16_t* f, int16_t x, int16_t y, int16_t z) {
    f[m00] = (f[m00] * x)>>8;
    f[m01] = (f[m01] * x)>>8;
    f[m02] = (f[m02] * x)>>8;
    f[m03] = (f[m03] * x)>>8;

    f[m10] = (f[m10] * y)>>8;
    f[m11] = (f[m11] * y)>>8;
    f[m12] = (f[m12] * y)>>8;
    f[m13] = (f[m13] * y)>>8;

    f[m20] = (f[m20] * z)>>8;
    f[m21] = (f[m21] * z)>>8;
    f[m22] = (f[m22] * z)>>8;
    f[m23] = (f[m23] * z)>>8;
}

void Engine3D::rotateEuler(int16_t* matrix, int16_t x, int16_t y, int16_t z) {
    int16_t ftmp[16];

    int16_t cx = cosFixed(x);
    int16_t sx = sinFixed(x);
    int16_t cy = cosFixed(y);
    int16_t sy = sinFixed(y);
    int16_t cz = cosFixed(z);
    int16_t sz = sinFixed(z);

    ftmp[m00] = (cy*cz)>>8;
    ftmp[m10] = ((((sx*sy)>>8)*cz)-(cx*sz))>>8;
    ftmp[m20] = ((((cx*sy)>>8)*cz)+(sx*sz))>>8;
    ftmp[m30] = 0;

    ftmp[m01] = (cy*sz)>>8;
    ftmp[m11] = ((((sx*sy)>>8)*sz)+(cx*cz))>>8;
    ftmp[m21] = ((((cx*sy)>>8)*sz)-(sx*cz))>>8;
    ftmp[m31] = 0;

    ftmp[m02] = -sy;
    ftmp[m12] = (sx*cy)>>8;
    ftmp[m22] = (cx*cy)>>8;
    ftmp[m32] = 0;

    ftmp[m03] = 0;
    ftmp[m13] = 0;
    ftmp[m23] = 0;
    ftmp[m33] = 1;
    multiply(matrix, ftmp, matrix);
}

void Engine3D::multiply(int16_t* fin1, int16_t* fin2, int16_t* fout) {
    int16_t fotmp[16];

    fotmp[m00] = ((int32_t)fin1[m00] * (int32_t)fin2[m00] + (int32_t)fin1[m01] * (int32_t)fin2[m10] + (int32_t)fin1[m02] * (int32_t)fin2[m20] + (int32_t)fin1[m03] * (int32_t)fin2[m30])>>8;
    fotmp[m01] = ((int32_t)fin1[m00] * (int32_t)fin2[m01] + (int32_t)fin1[m01] * (int32_t)fin2[m11] + (int32_t)fin1[m02] * (int32_t)fin2[m21] + (int32_t)fin1[m03] * (int32_t)fin2[m31])>>8;
    fotmp[m02] = ((int32_t)fin1[m00] * (int32_t)fin2[m02] + (int32_t)fin1[m01] * (int32_t)fin2[m12] + (int32_t)fin1[m02] * (int32_t)fin2[m22] + (int32_t)fin1[m03] * (int32_t)fin2[m32])>>8;
    fotmp[m03] = ((int32_t)fin1[m00] * (int32_t)fin2[m03] + (int32_t)fin1[m01] * (int32_t)fin2[m13] + (int32_t)fin1[m02] * (int32_t)fin2[m23] + (int32_t)fin1[m03] * (int32_t)fin2[m33])>>8;

    fotmp[m10] = ((int32_t)fin1[m10] * (int32_t)fin2[m00] + (int32_t)fin1[m11] * (int32_t)fin2[m10] + (int32_t)fin1[m12] * (int32_t)fin2[m20] + (int32_t)fin1[m13] * (int32_t)fin2[m30])>>8;
    fotmp[m11] = ((int32_t)fin1[m10] * (int32_t)fin2[m01] + (int32_t)fin1[m11] * (int32_t)fin2[m11] + (int32_t)fin1[m12] * (int32_t)fin2[m21] + (int32_t)fin1[m13] * (int32_t)fin2[m31])>>8;
    fotmp[m12] = ((int32_t)fin1[m10] * (int32_t)fin2[m02] + (int32_t)fin1[m11] * (int32_t)fin2[m12] + (int32_t)fin1[m12] * (int32_t)fin2[m22] + (int32_t)fin1[m13] * (int32_t)fin2[m32])>>8;
    fotmp[m13] = ((int32_t)fin1[m10] * (int32_t)fin2[m03] + (int32_t)fin1[m11] * (int32_t)fin2[m13] + (int32_t)fin1[m12] * (int32_t)fin2[m23] + (int32_t)fin1[m13] * (int32_t)fin2[m33])>>8;

    fotmp[m20] = ((int32_t)fin1[m20] * (int32_t)fin2[m00] + (int32_t)fin1[m21] * (int32_t)fin2[m10] + (int32_t)fin1[m22] * (int32_t)fin2[m20] + (int32_t)fin1[m23] * (int32_t)fin2[m30])>>8;
    fotmp[m21] = ((int32_t)fin1[m20] * (int32_t)fin2[m01] + (int32_t)fin1[m21] * (int32_t)fin2[m11] + (int32_t)fin1[m22] * (int32_t)fin2[m21] + (int32_t)fin1[m23] * (int32_t)fin2[m31])>>8;
    fotmp[m22] = ((int32_t)fin1[m20] * (int32_t)fin2[m02] + (int32_t)fin1[m21] * (int32_t)fin2[m12] + (int32_t)fin1[m22] * (int32_t)fin2[m22] + (int32_t)fin1[m23] * (int32_t)fin2[m32])>>8;
    fotmp[m23] = ((int32_t)fin1[m20] * (int32_t)fin2[m03] + (int32_t)fin1[m21] * (int32_t)fin2[m13] + (int32_t)fin1[m22] * (int32_t)fin2[m23] + (int32_t)fin1[m23] * (int32_t)fin2[m33])>>8;

    fotmp[m30] = ((int32_t)fin1[m30] * (int32_t)fin2[m00] + (int32_t)fin1[m31] * (int32_t)fin2[m10] + (int32_t)fin1[m32] * (int32_t)fin2[m20] + (int32_t)fin1[m33] * (int32_t)fin2[m30])>>8;
    fotmp[m31] = ((int32_t)fin1[m30] * (int32_t)fin2[m01] + (int32_t)fin1[m31] * (int32_t)fin2[m11] + (int32_t)fin1[m32] * (int32_t)fin2[m21] + (int32_t)fin1[m33] * (int32_t)fin2[m31])>>8;
    fotmp[m32] = ((int32_t)fin1[m30] * (int32_t)fin2[m02] + (int32_t)fin1[m31] * (int32_t)fin2[m12] + (int32_t)fin1[m32] * (int32_t)fin2[m22] + (int32_t)fin1[m33] * (int32_t)fin2[m32])>>8;
    fotmp[m33] = ((int32_t)fin1[m30] * (int32_t)fin2[m03] + (int32_t)fin1[m31] * (int32_t)fin2[m13] + (int32_t)fin1[m32] * (int32_t)fin2[m23] + (int32_t)fin1[m33] * (int32_t)fin2[m33])>>8;

    std::memcpy(fout, fotmp, sizeof(fotmp));
}

void Engine3D::pointTransform(int16_t* pin, int16_t* f, int16_t* pout) {
    int16_t ptmp[2];
    ptmp[0] = ((pin[0] * f[m00] + pin[1] * f[m01] + pin[2] * f[m02])>>8) + f[m03];
    ptmp[1] = ((pin[0] * f[m10] + pin[1] * f[m11] + pin[2] * f[m12])>>8) + f[m13];
    pout[2] = ((pin[0] * f[m20] + pin[1] * f[m21] + pin[2] * f[m22])>>8) + f[m23];
    pout[0] = ptmp[0];
    pout[1] = ptmp[1];
}

void transform4D(int16_t* pin, int16_t* f, int16_t* pout) {
    int16_t ptmp[3];
    ptmp[0] = (pin[0] * f[m00] + pin[1] * f[m01] + pin[2] * f[m02] + pin[3] * f[m03])>>8;
    ptmp[1] = (pin[0] * f[m10] + pin[1] * f[m11] + pin[2] * f[m12] + pin[3] * f[m13])>>8;
    ptmp[2] = (pin[0] * f[m20] + pin[1] * f[m21] + pin[2] * f[m22] + pin[3] * f[m23])>>8;
    pout[3] = (pin[0] * f[m30] + pin[1] * f[m31] + pin[2] * f[m32] + pin[3] * f[m33])>>8;
    pout[0] = ptmp[0];
    pout[1] = ptmp[1];
    pout[2] = ptmp[2];
}

void Engine3D::localToScreenspace(
    int16_t* coords_3v,
    int16_t* o1,
    int16_t* o2)
{
    int16_t tmppt[4] =
    {
        coords_3v[0],
        coords_3v[1],
        coords_3v[2],
        256
    };

    transform4D(tmppt, modelviewMatrix, tmppt);
    transform4D(tmppt, projectionMatrix, tmppt);

    if (tmppt[3] >= 0)
    {
        *o1 = -1;
        *o2 = -1;
        return;
    }

    int x = (256 * tmppt[0] / tmppt[3]) / 8;
    int y = (256 * tmppt[1] / tmppt[3]) / 8;

    x += _width / 2;
    y += _height / 2;

    *o1 = x;
    *o2 = y;
}

void CNFGTackPixelW( int x, int y )
{
    frontframe[(x+y*FBW)>>2] |= 2<<( (x&3)<<1 );
}

void CNFGTackPixelB( int x, int y )
{
    frontframe[(x+y*FBW)>>2] &= ~(2<<( (x&3)<<1 ));
}

void CNFGTackPixelG( int x, int y )
{
    uint8_t * ffs = &frontframe[(x+y*FBW2)>>1];
    if( x & 1 )
    {
        *ffs = (*ffs & 0x0f) | CNFGLastColor<<4;
    }
    else
    {
        *ffs = (*ffs & 0xf0 ) | CNFGLastColor;
    }
}

void CNFGColor( uint8_t col )
{
    CNFGLastColor = col;
    if( col == 16 )
    {
        LTW = FBW;
        CNFGTackPixel = CNFGTackPixelB;
    }
    else if( col == 17 )
    {
        LTW = FBW;
        CNFGTackPixel = CNFGTackPixelW;
    }
    else
    {
        LTW = FBW/2;
        CNFGTackPixel = CNFGTackPixelG;
    }
}

int LABS( int x )
{
    return (x<0)?-x:x;
}

void Engine3D::line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;

    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;

    int err = dx + dy;

    while (true)
    {
        if (x0 >= 0 && x0 < (int)_width &&
            y0 >= 0 && y0 < (int)_height)
        {
            sp(x0, y0, c);
        }

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;

        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

int16_t verts[] = { 
           0, -256,    0,   
         185, -114,  134,        -70, -114,  217,       -228, -114,    0,        -70, -114, -217,   
         185, -114, -134,         70,  114,  217,       -185,  114,  134,       -185,  114, -134,   
          70,  114, -217,        228,  114,    0,          0,  256,    0,        108, -217,   79,   
         -41, -217,  127,         67, -134,  207,        108, -217,  -79,        217, -134,    0,   
        -134, -217,    0,       -176, -134,  127,        -41, -217, -127,       -176, -134, -127,   
          67, -134, -207,        243,    0,  -79,        243,    0,   79,        150,    0,  207,   
           0,    0,  256,       -150,    0,  207,       -243,    0,   79,       -243,    0,  -79,   
        -150,    0, -207,          0,    0, -256,        150,    0, -207,        176,  134,  127,   
         -67,  134,  207,       -217,  134,    0,        -67,  134, -207,        176,  134, -127,   
         134,  217,    0,         41,  217,  127,       -108,  217,   79,       -108,  217,  -79,   
          41,  217, -127};
uint16_t indices[] = {
          42,  36,     36,   3,      3,  42,     42,  39,     39,  36,      6,  39,     42,   6,     39,   0,   
           0,  36,     48,   3,     36,  48,     36,  45,     45,  48,     15,  48,     45,  15,      0,  45,   
          54,  39,      6,  54,     54,  51,     51,  39,      9,  51,     54,   9,     51,   0,     60,  51,   
           9,  60,     60,  57,     57,  51,     12,  57,     60,  12,     57,   0,     63,  57,     12,  63,   
          63,  45,     45,  57,     63,  15,     69,   3,     48,  69,     48,  66,     66,  69,     30,  69,   
          66,  30,     15,  66,     75,   6,     42,  75,     42,  72,     72,  75,     18,  75,     72,  18,   
           3,  72,     81,   9,     54,  81,     54,  78,     78,  81,     21,  81,     78,  21,      6,  78,   
          87,  12,     60,  87,     60,  84,     84,  87,     24,  87,     84,  24,      9,  84,     93,  15,   
          63,  93,     63,  90,     90,  93,     27,  93,     90,  27,     12,  90,     96,  69,     30,  96,   
          96,  72,     72,  69,     96,  18,     99,  75,     18,  99,     99,  78,     78,  75,     99,  21,   
         102,  81,     21, 102,    102,  84,     84,  81,    102,  24,    105,  87,     24, 105,    105,  90,   
          90,  87,    105,  27,    108,  93,     27, 108,    108,  66,     66,  93,    108,  30,    114,  18,   
          96, 114,     96, 111,    111, 114,     33, 114,    111,  33,     30, 111,    117,  21,     99, 117,   
          99, 114,    114, 117,     33, 117,    120,  24,    102, 120,    102, 117,    117, 120,     33, 120,   
         123,  27,    105, 123,    105, 120,    120, 123,     33, 123,    108, 111,    108, 123,    123, 111,   
        };

void Engine3D::draw3DSegment(int16_t* c1, int16_t* c2) {
    int16_t sx0, sy0;
    int16_t sx1, sy1;

    localToScreenspace(c1, &sx0, &sy0);
    localToScreenspace(c2, &sx1, &sy1);

    if (sx0 < 0 || sx1 < 0)
        return;

    line(
        sx0, sy0,
        sx1, sy1,
        _lastColor
    );
}

void Engine3D::drawGeoSphere() {
    int nrv = sizeof(indices) / sizeof(uint16_t);

    for (int i = 0; i < nrv; i += 2) {
        int16_t* c1 = &verts[indices[i]];
        int16_t* c2 = &verts[indices[i + 1]];

        draw3DSegment(c1, c2);
    }
}

const unsigned short FontCharMap[128] = {
	65535, 0, 10, 20, 32, 44, 56, 68, 70, 65535, 65535, 80, 92, 65535, 104, 114, 
	126, 132, 138, 148, 156, 166, 180, 188, 200, 206, 212, 218, 224, 228, 238, 244, 
	65535, 250, 254, 258, 266, 278, 288, 302, 304, 310, 316, 324, 328, 226, 252, 330, 
	332, 342, 348, 358, 366, 372, 382, 392, 400, 410, 420, 424, 428, 262, 432, 436, 
	446, 460, 470, 486, 496, 508, 516, 522, 536, 542, 548, 556, 568, 572, 580, 586, 
	598, 608, 622, 634, 644, 648, 654, 662, 670, 682, 692, 700, 706, 708, 492, 198, 
	714, 716, 726, 734, 742, 750, 760, 768, 782, 790, 794, 802, 204, 810, 820, 384, 
	828, 836, 844, 850, 860, 864, 872, 880, 890, 894, 902, 908, 916, 920, 928, 934, 
};

const unsigned char FontCharData[949] = {
	0x00, 0x01, 0x20, 0x21, 0x03, 0x23, 0x23, 0x14, 0x14, 0x83, 0x00, 0x01, 0x20, 0x21, 0x04, 0x24, 
	0x24, 0x13, 0x13, 0x84, 0x01, 0x21, 0x21, 0x23, 0x23, 0x14, 0x14, 0x03, 0x03, 0x01, 0x11, 0x92, 
	0x11, 0x22, 0x22, 0x23, 0x23, 0x14, 0x14, 0x03, 0x03, 0x02, 0x02, 0x91, 0x01, 0x21, 0x21, 0x23, 
	0x23, 0x01, 0x03, 0x21, 0x03, 0x01, 0x12, 0x94, 0x03, 0x23, 0x13, 0x14, 0x23, 0x22, 0x22, 0x11, 
	0x11, 0x02, 0x02, 0x83, 0x12, 0x92, 0x12, 0x12, 0x01, 0x21, 0x21, 0x23, 0x23, 0x03, 0x03, 0x81, 
	0x03, 0x21, 0x21, 0x22, 0x21, 0x11, 0x03, 0x14, 0x14, 0x23, 0x23, 0x92, 0x01, 0x10, 0x10, 0x21, 
	0x21, 0x12, 0x12, 0x01, 0x12, 0x14, 0x03, 0xa3, 0x02, 0x03, 0x03, 0x13, 0x02, 0x12, 0x13, 0x10, 
	0x10, 0xa1, 0x01, 0x23, 0x03, 0x21, 0x02, 0x11, 0x11, 0x22, 0x22, 0x13, 0x13, 0x82, 0x00, 0x22, 
	0x22, 0x04, 0x04, 0x80, 0x20, 0x02, 0x02, 0x24, 0x24, 0xa0, 0x01, 0x10, 0x10, 0x21, 0x10, 0x14, 
	0x14, 0x03, 0x14, 0xa3, 0x00, 0x03, 0x04, 0x04, 0x20, 0x23, 0x24, 0xa4, 0x00, 0x20, 0x00, 0x02, 
	0x02, 0x22, 0x10, 0x14, 0x20, 0xa4, 0x01, 0x21, 0x21, 0x23, 0x23, 0x03, 0x03, 0x01, 0x20, 0x10, 
	0x10, 0x14, 0x14, 0x84, 0x03, 0x23, 0x23, 0x24, 0x24, 0x04, 0x04, 0x83, 0x01, 0x10, 0x10, 0x21, 
	0x10, 0x14, 0x14, 0x03, 0x14, 0x23, 0x04, 0xa4, 0x01, 0x10, 0x21, 0x10, 0x10, 0x94, 0x03, 0x14, 
	0x23, 0x14, 0x10, 0x94, 0x02, 0x22, 0x22, 0x11, 0x22, 0x93, 0x02, 0x22, 0x02, 0x11, 0x02, 0x93, 
	0x01, 0x02, 0x02, 0xa2, 0x02, 0x22, 0x22, 0x11, 0x11, 0x02, 0x02, 0x13, 0x13, 0xa2, 0x11, 0x22, 
	0x22, 0x02, 0x02, 0x91, 0x02, 0x13, 0x13, 0x22, 0x22, 0x82, 0x10, 0x13, 0x14, 0x94, 0x10, 0x01, 
	0x20, 0x91, 0x10, 0x14, 0x20, 0x24, 0x01, 0x21, 0x03, 0xa3, 0x21, 0x10, 0x10, 0x01, 0x01, 0x23, 0x23, 0x14, 0x14, 0x03, 0x10, 0x94, 0x00, 0x01, 0x23, 0x24, 0x04, 0x03, 0x03, 0x21, 0x21, 0xa0, 
	0x21, 0x10, 0x10, 0x01, 0x01, 0x12, 0x12, 0x03, 0x03, 0x14, 0x14, 0x23, 0x02, 0xa4, 0x10, 0x91, 
	0x10, 0x01, 0x01, 0x03, 0x03, 0x94, 0x10, 0x21, 0x21, 0x23, 0x23, 0x94, 0x01, 0x23, 0x11, 0x13, 
	0x21, 0x03, 0x02, 0xa2, 0x02, 0x22, 0x11, 0x93, 0x31, 0xc0, 0x03, 0xa1, 0x00, 0x20, 0x20, 0x24, 
	0x24, 0x04, 0x04, 0x00, 0x12, 0x92, 0x01, 0x10, 0x10, 0x14, 0x04, 0xa4, 0x01, 0x10, 0x10, 0x21, 
	0x21, 0x22, 0x22, 0x04, 0x04, 0xa4, 0x00, 0x20, 0x20, 0x24, 0x24, 0x04, 0x12, 0xa2, 0x00, 0x02, 
	0x02, 0x22, 0x20, 0xa4, 0x20, 0x00, 0x00, 0x02, 0x02, 0x22, 0x22, 0x24, 0x24, 0x84, 0x20, 0x02, 0x02, 0x22, 0x22, 0x24, 0x24, 0x04, 0x04, 0x82, 0x00, 0x20, 0x20, 0x21, 0x21, 0x12, 0x12, 0x94, 
	0x00, 0x04, 0x00, 0x20, 0x20, 0x24, 0x04, 0x24, 0x02, 0xa2, 0x00, 0x02, 0x02, 0x22, 0x22, 0x20, 
	0x20, 0x00, 0x22, 0x84, 0x11, 0x11, 0x13, 0x93, 0x11, 0x11, 0x13, 0x84, 0x20, 0x02, 0x02, 0xa4, 0x00, 0x22, 0x22, 0x84, 0x01, 0x10, 0x10, 0x21, 0x21, 0x12, 0x12, 0x13, 0x14, 0x94, 0x21, 0x01, 
	0x01, 0x04, 0x04, 0x24, 0x24, 0x22, 0x22, 0x12, 0x12, 0x13, 0x13, 0xa3, 0x04, 0x01, 0x01, 0x10, 
	0x10, 0x21, 0x21, 0x24, 0x02, 0xa2, 0x00, 0x04, 0x04, 0x14, 0x14, 0x23, 0x23, 0x12, 0x12, 0x02, 
	0x12, 0x21, 0x21, 0x10, 0x10, 0x80, 0x23, 0x14, 0x14, 0x03, 0x03, 0x01, 0x01, 0x10, 0x10, 0xa1, 
	0x00, 0x10, 0x10, 0x21, 0x21, 0x23, 0x23, 0x14, 0x14, 0x04, 0x04, 0x80, 0x00, 0x04, 0x04, 0x24, 
	0x00, 0x20, 0x02, 0x92, 0x00, 0x04, 0x00, 0x20, 0x02, 0x92, 0x21, 0x10, 0x10, 0x01, 0x01, 0x03, 
	0x03, 0x14, 0x14, 0x23, 0x23, 0x22, 0x22, 0x92, 0x00, 0x04, 0x20, 0x24, 0x02, 0xa2, 0x00, 0x20, 
	0x10, 0x14, 0x04, 0xa4, 0x00, 0x20, 0x20, 0x23, 0x23, 0x14, 0x14, 0x83, 0x00, 0x04, 0x02, 0x12, 
	0x12, 0x21, 0x21, 0x20, 0x12, 0x23, 0x23, 0xa4, 0x00, 0x04, 0x04, 0xa4, 0x04, 0x00, 0x00, 0x11, 
	0x11, 0x20, 0x20, 0xa4, 0x04, 0x00, 0x00, 0x22, 0x20, 0xa4, 0x01, 0x10, 0x10, 0x21, 0x21, 0x23, 
	0x23, 0x14, 0x14, 0x03, 0x03, 0x81, 0x00, 0x04, 0x00, 0x10, 0x10, 0x21, 0x21, 0x12, 0x12, 0x82, 
	0x01, 0x10, 0x10, 0x21, 0x21, 0x23, 0x23, 0x14, 0x14, 0x03, 0x03, 0x01, 0x04, 0x93, 0x00, 0x04, 
	0x00, 0x10, 0x10, 0x21, 0x21, 0x12, 0x12, 0x02, 0x02, 0xa4, 0x21, 0x10, 0x10, 0x01, 0x01, 0x23, 
	0x23, 0x14, 0x14, 0x83, 0x00, 0x20, 0x10, 0x94, 0x00, 0x04, 0x04, 0x24, 0x24, 0xa0, 0x00, 0x03, 
	0x03, 0x14, 0x14, 0x23, 0x23, 0xa0, 0x00, 0x04, 0x04, 0x24, 0x14, 0x13, 0x24, 0xa0, 0x00, 0x01, 
	0x01, 0x23, 0x23, 0x24, 0x04, 0x03, 0x03, 0x21, 0x21, 0xa0, 0x00, 0x01, 0x01, 0x12, 0x12, 0x14, 
	0x12, 0x21, 0x21, 0xa0, 0x00, 0x20, 0x20, 0x02, 0x02, 0x04, 0x04, 0xa4, 0x10, 0x00, 0x00, 0x04, 
	0x04, 0x94, 0x01, 0xa3, 0x10, 0x20, 0x20, 0x24, 0x24, 0x94, 0x00, 0x91, 0x02, 0x04, 0x04, 0x24, 
	0x24, 0x22, 0x23, 0x12, 0x12, 0x82, 0x00, 0x04, 0x04, 0x24, 0x24, 0x22, 0x22, 0x82, 0x24, 0x04, 
	0x04, 0x03, 0x03, 0x12, 0x12, 0xa2, 0x20, 0x24, 0x24, 0x04, 0x04, 0x02, 0x02, 0xa2, 0x24, 0x04, 
	0x04, 0x02, 0x02, 0x22, 0x22, 0x23, 0x23, 0x93, 0x04, 0x01, 0x02, 0x12, 0x01, 0x10, 0x10, 0xa1, 
	0x23, 0x12, 0x12, 0x03, 0x03, 0x14, 0x14, 0x23, 0x23, 0x24, 0x24, 0x15, 0x15, 0x84, 0x00, 0x04, 
	0x03, 0x12, 0x12, 0x23, 0x23, 0xa4, 0x11, 0x11, 0x12, 0x94, 0x22, 0x22, 0x23, 0x24, 0x24, 0x15, 
	0x15, 0x84, 0x00, 0x04, 0x03, 0x13, 0x13, 0x22, 0x13, 0xa4, 0x02, 0x04, 0x02, 0x13, 0x12, 0x14, 
	0x12, 0x23, 0x23, 0xa4, 0x02, 0x04, 0x03, 0x12, 0x12, 0x23, 0x23, 0xa4, 0x02, 0x05, 0x04, 0x24, 
	0x24, 0x22, 0x22, 0x82, 0x02, 0x04, 0x04, 0x24, 0x25, 0x22, 0x22, 0x82, 0x02, 0x04, 0x03, 0x12, 
	0x12, 0xa2, 0x22, 0x02, 0x02, 0x03, 0x03, 0x23, 0x23, 0x24, 0x24, 0x84, 0x11, 0x14, 0x02, 0xa2, 0x02, 0x04, 0x04, 0x14, 0x14, 0x23, 0x24, 0xa2, 0x02, 0x03, 0x03, 0x14, 0x14, 0x23, 0x23, 0xa2, 
	0x02, 0x03, 0x03, 0x14, 0x14, 0x12, 0x13, 0x24, 0x24, 0xa2, 0x02, 0x24, 0x04, 0xa2, 0x02, 0x03, 
	0x03, 0x14, 0x22, 0x23, 0x23, 0x85, 0x02, 0x22, 0x22, 0x04, 0x04, 0xa4, 0x20, 0x10, 0x10, 0x14, 
	0x14, 0x24, 0x12, 0x82, 0x10, 0x11, 0x13, 0x94, 0x00, 0x10, 0x10, 0x14, 0x14, 0x04, 0x12, 0xa2, 
	0x01, 0x10, 0x10, 0x11, 0x11, 0xa0, 0x03, 0x04, 0x04, 0x24, 0x24, 0x23, 0x23, 0x12, 0x12, 0x83, 
	0x10, 0x10, 0x11, 0x94, 0x21};

void Engine3D::drawText(const char* text, int scale) {
    if (text == nullptr || scale <= 0)
        return;

    int16_t iox = _penX;
    int16_t ioy = _penY;

    while (*text) {
        uint8_t c = (uint8_t)*text++;

        if (c == '\t') {
            iox += 12 * scale;
            continue;
        }

        if (c == '\n') {
            iox = _penX;
            ioy += 6 * scale;
            continue;
        }

        uint16_t index = FontCharMap[c & 0x7F];

        if (index == 65535) {
            iox += 3 * scale;
            continue;
        }

        const unsigned char* lmap = &FontCharData[index];

        while (true) {
            int x1 = (((lmap[0] & 0x70) >> 4) * scale) + iox;
            int y1 = (( lmap[0] & 0x0F)       * scale) + ioy;

            int x2 = (((lmap[1] & 0x70) >> 4) * scale) + iox;
            int y2 = (( lmap[1] & 0x0F)       * scale) + ioy;

            line(x1, y1, x2, y2, _lastColor);

            bool last = (lmap[1] & 0x80) != 0;
            lmap += 2;

            if (last)
                break;
        }

        iox += 3 * scale;
    }

    _penX = iox;
    _penY = ioy;
}

void Engine3D::drawBox(int x1, int y1, int x2, int y2) {
    uint8_t oldColor = _lastColor;

    _lastColor = _dialogColor;

    drawRectangle(x1, y1, x2, y2);

    _lastColor = oldColor;

    line(x1, y1, x2, y1, _lastColor);
    line(x2, y1, x2, y2, _lastColor);
    line(x2, y2, x1, y2, _lastColor);
    line(x1, y2, x1, y1, _lastColor);
}

void Engine3D::drawRectangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    if (x1 > x2) {
        int16_t t = x1;
        x1 = x2;
        x2 = t;
    }

    if (y1 > y2) {
        int16_t t = y1;
        y1 = y2;
        y2 = t;
    }

    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            if (x >= 0 && x < (int)_width &&
                y >= 0 && y < (int)_height) {
                sp(x, y, _lastColor);
            }
        }
    }
}

int16_t noiseAt( int16_t x, int16_t y ) {
    return ((x*13244321 + y * 33442927));
}

static inline int16_t tdFade( int16_t f ) {
    return f;
}

int16_t fLerp( int16_t a, int16_t b, int16_t t ) {
    int16_t fr = tdFade( t );
    return (a * (256-fr) + b * fr)>>8;
}

static inline int16_t fNoiseAT( int16_t x, int16_t y ) {
    int ix = x;
    int iy = y;
    int16_t fx = x - ix;
    int16_t fy = y - iy;

    int16_t a = noiseAt( ix, iy );
    int16_t b = noiseAt( ix+1, iy );
    int16_t c = noiseAt( ix, iy+1 );
    int16_t d = noiseAt( ix+1, iy+1 );

    int16_t top = fLerp( a, b, fx );
    int16_t bottom = fLerp( c, d, fx );

    return fLerp( top, bottom, fy );
}

int16_t perlin2D( int16_t x, int16_t y ) {
    int ndepth = 5;

    int depth;
    int16_t ret = 0;
    for( depth = 0; depth < ndepth; depth++ ) {
        int16_t nx = (x*256) / (256<<(ndepth-depth-1));
        int16_t ny = (y*256) / (256<<(ndepth-depth-1));
        ret += fNoiseAT( nx, ny ) / (256<<(depth+1));
    }
    return ret;
}