#ifndef ENGINE3D_H
#define ENGINE3D_H

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <VGA.h>
#include <arduino.h>

#define WHITE         1
#define BLACK         0
#define INVERT        2

#define clear_screen()      fill(0)
#define invert(color)       fill(2)

class Engine3D {

private:
    void init(uint8_t* vram, uint16_t width, uint16_t height);

public:
    VGA_class* _vga;
    Engine3D() : _vga(&::VGA), _screen(nullptr), _backBuffer(nullptr), _doubleBuffered(false), _ownsBackBuffer(false) {}; // Constructor
    ~Engine3D(); // Destructor (frees allocated backbuffer)

    // System Control & Buffer Management
    void begin(uint8_t spino = 1, uint8_t* extram = nullptr);
    void end() { _vga->end(); }; 
    void adjust(int16_t cnt, int16_t hcnt = 0, int16_t vcnt = 0);
   
    void bitmap(uint16_t x, uint16_t y, const unsigned char * bmp, uint16_t i = 0, uint16_t width = 0, uint16_t lines = 0);
    void bitmap8(uint8_t x, uint8_t y, const unsigned char * bmp, uint16_t i = 0, uint8_t width = 0, uint8_t lines = 0) 
         { bitmap((uint8_t)x,(uint8_t)y,bmp,i,width,lines); };

    void delayMicroseconds(uint32_t x) {::delayMicroseconds(x);} // delayMicroseconds (microseconds)
    void delay(uint32_t x) {::delay(x);};                       // delay (milliseconds)
    uint16_t hres() {return _width;} ;                          // Get horizontal pixel count
    uint16_t vres() {return _height;} ;                         // Get vertical pixel count

    void LoadBitmap(const uint8_t* pImg, uint16_t imgSize = 0);
    void render_ntsc_line(uint16_t line_number, uint8_t *line_buffer);
    void Schematic(uint16_t x, uint16_t y, const unsigned char * bmp, uint16_t i, uint16_t width, uint16_t lines);
    void intro();
    void intro_1();

    // Double Buffering Controls
    bool setDoubleBuffering(bool enable, uint8_t* externalBuffer = nullptr);
    void display(); // Pushes off-screen back buffer to active screen VRAM
    inline void flip() { display(); } // Alias for display()
    uint8_t* getBackBuffer() { return _doubleBuffered ? _backBuffer : _screen; }

    // Context & Color Management
    void setPen(int x, int y);
    void setColor(uint8_t col);
    void setBGColor(uint8_t col);
    void setDialogColor(uint8_t col);

    // 2D Drawing Primitives
    void drawPixel(uint16_t x, uint16_t y, uint8_t c);
    void drawSegment(int x0, int y0, int x1, int y1);
    void drawText(const char* text, int scale);
    void drawBox(int x1, int y1, int x2, int y2);
    void drawRectangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2);
    void multiply(int16_t* fin1, int16_t* fin2, int16_t* fout);

    // 3D Math & Matrix Transformations
    void identity(int16_t* matrix);
    void translate(int16_t* matrix, int16_t x, int16_t y, int16_t z);
    void scale(int16_t* f, int16_t x, int16_t y, int16_t z);
    void rotateEuler(int16_t* matrix, int16_t x, int16_t y, int16_t z);

    void makeTranslateMatrix(int x, int y, int z, int16_t* out);
    void makeXRotationMatrix(uint8_t angle, int16_t* f);
    void makeYRotationMatrix(uint8_t angle, int16_t* f);
    
    void perspective(int fovx, int aspect, int zNear, int zFar, int16_t* out);

    void multiplyMatrix(int16_t* fin1, int16_t* fin2, int16_t* fout);
    void pointTransform(int16_t* pin, int16_t* f, int16_t* pout);
    void transform(int16_t* pin, int16_t* f, int16_t* pout);
    void localToScreenspace(int16_t* coords_3v, int16_t* o1, int16_t* o2);

    // 3D Drawing Primitives
    void draw3DSegment(int16_t* c1, int16_t* c2);
    void drawGeoSphere();
    void clear();
    void fill(uint8_t color);

    // Math & Noise Helpers
    static int labs(int x);
    static int16_t sinFixed(uint8_t iv);
    static int16_t cosFixed(uint8_t iv);
    static int16_t perlin2D(int16_t x, int16_t y);
    static int16_t fLerp(int16_t a, int16_t b, int16_t t);
    static int16_t noiseAt(int16_t x, int16_t y);
    static int16_t fNoiseAT(int16_t x, int16_t y);

    void line(int x0, int y0, int x1, int y1, uint8_t c);

    // Public State Accessors
    int frameCount;
    int16_t projectionMatrix[16];
    int16_t modelviewMatrix[16];

private:

    // Renders to off-screen buffer if double buffering is enabled, otherwise directly to screen
    inline void sp(uint16_t x, uint16_t y, uint8_t c) {
        if (x >= _width || y >= _height)
            return;

        uint32_t offset = (x >> 3) + (y * _hres);
        uint8_t mask = 0x80 >> (x & 7);
        uint8_t* targetBuffer = _doubleBuffered ? _backBuffer : _screen;

        if (!targetBuffer) return;

        if (c == WHITE)
            targetBuffer[offset] |= mask;
        else if (c == BLACK)
            targetBuffer[offset] &= ~mask;
        else if (c == INVERT)
            targetBuffer[offset] ^= mask;
    }

    // Display & Buffering state
    uint8_t*  _screen;          // Active display/front buffer VRAM
    uint8_t*  _backBuffer;      // Off-screen frame buffer
    bool      _doubleBuffered;  // Double buffering active state
    bool      _ownsBackBuffer;  // Tracks heap allocation ownership

    uint16_t  _width;
    uint16_t  _height;
    uint16_t  _hres;            // Horizontal byte stride (80 bytes for 640px)
    uint16_t  _vres;            // Vertical pixel count (480 lines)

    uint16_t _cursorX;
    uint16_t _cursorY;

    int _penX;
    int _penY;

    uint8_t _bgColor;
    uint8_t _lastColor;
    uint8_t _dialogColor;

    uint8_t _mode;
    const unsigned char* _font;
};

#endif // ENGINE3D_H












