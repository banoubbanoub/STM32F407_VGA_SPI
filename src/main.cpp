#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <Engine3D.h>

#include "schematic.h"
#include "lenabmp.h"
#include "rectbmp.h"
#include "image_data.h"
#include "robot_bitmap.h"

// ------------------------------------------------------------
// Pin Definitions & Display Configuration
// ------------------------------------------------------------
#define USER_BUTTON_PIN PA0

#define CE_PIN   PB10
#define CSN_PIN  PB12

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  480

// ------------------------------------------------------------
// Terminal Configuration
// ------------------------------------------------------------
#define TEXT_SCALE     2
#define CHAR_WIDTH     7
#define CHAR_HEIGHT    14

#define TERMINAL_LEFT  30
#define TERMINAL_TOP   25

#define TERMINAL_COLS  ((SCREEN_WIDTH - TERMINAL_LEFT * 2) / CHAR_WIDTH)
#define TERMINAL_ROWS  ((SCREEN_HEIGHT - TERMINAL_TOP - 5) / CHAR_HEIGHT)
#define CURSOR_INTERVAL 500



// ------------------------------------------------------------
// Global Objects & Mode State
// ------------------------------------------------------------
//SPIClass SPI_2(PB15, PB14, PB13);  // MOSI, MISO, SCK
RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";

Engine3D engine;

uint8_t currentMode = 0;
const uint8_t TOTAL_MODES = 10; // Modes 0 to 9
bool lastButtonState = LOW;
int16_t framessostate = 0;

// State variables
int counter = 0;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // 50ms debounce time

// Terminal State
char terminal[TERMINAL_ROWS][TERMINAL_COLS + 1];
uint16_t cursorX = 0;
uint16_t cursorY = 0;
bool cursorVisible = true;
uint32_t lastCursorTime = 0;
bool rf24Initialized = false;



bool espConnected = false;
uint32_t lastEspRxTime = 0;
uint32_t lastPingTime = 0;





// ------------------------------------------------------------
// Math & Engine Helpers
// ------------------------------------------------------------
int16_t Height(int x, int y, int l) {
    return engine.cosFixed((x * x + y * y) + l);
}

void SetupMatrix() {
    engine.identity(engine.projectionMatrix);
    engine.identity(engine.modelviewMatrix);
    
    engine.perspective(
        1200,   // Focal length: 2x the old 320-wide value so the scene fills 640 px
        256,    // x/y ratio of 1 keeps pixels square (640x480 is 4:3)
        50,     // Near clipping plane
        8192,   // Far clipping plane
        engine.projectionMatrix
    );

    engine.rotateEuler(engine.projectionMatrix, -20, 0, 0);
}



// ------------------------------------------------------------
// Terminal Functions
// ------------------------------------------------------------
void clearTerminalBuffer() {
    for (uint16_t y = 0; y < TERMINAL_ROWS; y++) {
        for (uint16_t x = 0; x < TERMINAL_COLS; x++) {
            terminal[y][x] = ' ';
        }
        terminal[y][TERMINAL_COLS] = '\0';
    }
    cursorX = 0;
    cursorY = 0;
}

void scrollTerminal() {
    for (uint16_t y = 1; y < TERMINAL_ROWS; y++) {
        memcpy(terminal[y - 1], terminal[y], TERMINAL_COLS + 1);
    }

    for (uint16_t x = 0; x < TERMINAL_COLS; x++) {
        terminal[TERMINAL_ROWS - 1][x] = ' ';
    }
    terminal[TERMINAL_ROWS - 1][TERMINAL_COLS] = '\0';

    cursorY = TERMINAL_ROWS - 1;
}

void terminalNewLine() {
    cursorX = 0;
    cursorY++;

    if (cursorY >= TERMINAL_ROWS) {
        scrollTerminal();
    }
}

void terminalPutChar(char c) {
    if (c == '\r' || c == '\n') {
        terminalNewLine();
        return;
    }

    if (c == 127 || c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            terminal[cursorY][cursorX] = ' ';
        }
        return;
    }

    if (c == '\t') {
        uint16_t spaces = 4 - (cursorX & 3);
        while (spaces--) {
            terminalPutChar(' ');
        }
        return;
    }

    if (c < 32 || c > 126) {
        return;
    }

    if (cursorX >= TERMINAL_COLS) {
        terminalNewLine();
    }

    terminal[cursorY][cursorX] = c;
    cursorX++;

    if (cursorX >= TERMINAL_COLS) {
        terminalNewLine();
    }
}

void initRF24() {
    /*
    SPI_2.begin();
    clearTerminalBuffer();

    if (!radio.begin(&SPI_2)) {
        const char *err = "RF24 INIT FAILED!";
        while (*err) terminalPutChar(*err++);
        rf24Initialized = false;
        return;
    }

    radio.openReadingPipe(0, address);
    radio.setPALevel(RF24_PA_LOW);
    radio.startListening();
    rf24Initialized = true;

    terminalPutChar('>');
    terminalPutChar(' ');
    const char *msg = "Ready";
    while (*msg) terminalPutChar(*msg++);
    terminalNewLine();
    terminalPutChar('>');
    terminalPutChar(' ');
    */
}

void DrawTerminalMode() {
    if (rf24Initialized && radio.available()) {
        char receivedChar;
        radio.read(&receivedChar, sizeof(receivedChar));
        terminalPutChar(receivedChar);
    }

    uint32_t now = millis();
    if ((now - lastCursorTime) >= CURSOR_INTERVAL) {
        lastCursorTime = now;
        cursorVisible = !cursorVisible;
    }

    engine.setColor(1);
    engine.setPen(60, 5);
    engine.drawText("WIRELESS TERMINAL", 2);

    for (uint16_t y = 0; y < TERMINAL_ROWS; y++) {
        engine.setPen(TERMINAL_LEFT, TERMINAL_TOP + y * CHAR_HEIGHT);
        engine.drawText(terminal[y], TEXT_SCALE);
    }

    if (cursorVisible) {
        int cursorPixelX = TERMINAL_LEFT + cursorX * CHAR_WIDTH;
        int cursorPixelY = TERMINAL_TOP + cursorY * CHAR_HEIGHT;

        engine.line(
            cursorPixelX,
            cursorPixelY + CHAR_HEIGHT - 2,
            cursorPixelX + CHAR_WIDTH - 1,
            cursorPixelY + CHAR_HEIGHT - 2,
            1
        );
    }
}

// ------------------------------------------------------------
// Display Modes
// ------------------------------------------------------------
void DrawMesh() {
    SetupMatrix();
    engine.rotateEuler(engine.modelviewMatrix, 0, 0, framessostate);

    engine.setColor(1);
    engine.setPen(80, 10);
    engine.drawText("Dynamic 3D Mesh", 2);

    int o = -framessostate * 2;

    for (int y = -18; y < 18; y++) {
        for (int x = -18; x < 18; x++) {
            int t  = Height(x,     y,     o) * 2 + 2000;
            int nx = Height(x + 1, y,     o) * 2 + 2000;
            int ny = Height(x,     y + 1, o) * 2 + 2000;
            
            int16_t p0[3] = { (int16_t)(x * 120),       (int16_t)(y * 120),       (int16_t)t };
            int16_t p1[3] = { (int16_t)((x + 1) * 120), (int16_t)(y * 120),       (int16_t)nx };
            int16_t p2[3] = { (int16_t)(x * 120),       (int16_t)((y + 1) * 120), (int16_t)ny };

            engine.draw3DSegment(p0, p1);
            engine.draw3DSegment(p0, p2);
        }
    }

    if (framessostate > 300) framessostate = 0;
}

void DrawSphere() {
    SetupMatrix();
    engine.rotateEuler(engine.modelviewMatrix, framessostate, 0, 0);

    engine.setColor(1);
    engine.setPen(20, 10);
    engine.drawText("Matrix-based 3D engine.", 2);

    // At 640x480 about +-2100 world units are visible at z=1000; draw only that grid, centred.
    for (int y = 3; y >= 0; y--) {
        for (int x = 0; x < 8; x++) {
            engine.modelviewMatrix[11] = 1000 + engine.sinFixed((x + y) * 40 + framessostate * 2);
            engine.modelviewMatrix[3]  = 500 * x - 1750;
            engine.modelviewMatrix[7]  = 600 * y - 900;
            engine.drawGeoSphere();
        }
    }

    if (framessostate > 300) framessostate = 0;
}

void Draw3DEngine() {
    SetupMatrix();
    engine.rotateEuler(engine.modelviewMatrix, framessostate, 0, 0);

    int sphereset = (framessostate / 120);
    if (sphereset > 2) sphereset = 2;

    for (int y = -sphereset; y <= sphereset; y++) {
        for (int x = -sphereset; x <= sphereset; x++) {
            if (y == 2) continue;
            engine.modelviewMatrix[11] = 1000 + engine.sinFixed((x + y) * 40 + framessostate * 2);
            engine.modelviewMatrix[3]  = 500 * x;
            engine.modelviewMatrix[7]  = 500 * y + 800;
            engine.drawGeoSphere();
        }
    }

    if (framessostate > 300) framessostate = 0;
}

void Lines_on_double_buffered_232x220() {
    engine.setColor(1);
    engine.setPen(40, 10);
    engine.drawText("Lines on double-buffered 232x220.", 2);

    if (framessostate > 60) {
        for (int i = 0; i < 350; i++) {
            engine.setColor((i % 15) + 1);
            engine.line(
                rand() % SCREEN_WIDTH, rand() % (SCREEN_HEIGHT - 30) + 30,
                rand() % SCREEN_WIDTH, rand() % (SCREEN_HEIGHT - 30) + 30,
                (i % 15) + 1
            );
        }
    }

    if (framessostate > 300) framessostate = 0;
}

void Direct_modulation() {
    // 3x font: 9 px per char, 18 px per row -> about 68 columns x 25 rows on 640x480.
    static const char *const lines[] = {
        "STM32F407 DISCOVERY - VGA 640x480 MONO GREEN",
        "============================================",
        "",
        "VIDEO TIMING (all done by hardware, CPU stays free):",
        "",
        "TIM4 CH1 (PB6): PWM, 31.47 kHz line rate, 3.81 us",
        "  low pulse = HSYNC",
        "TIM4 update IRQ: counts 525 lines, drives VSYNC (PB7)",
        "TIM4 CH2 compare IRQ: start of active video, kicks DMA",
        "",
        "PIXEL OUTPUT:",
        "",
        "DMA2 Stream3 copies 80 bytes per line from RAM to SPI1",
        "SPI1 (PA7 MOSI) shifts 1 bit per pixel at 25.2 MHz",
        "SYSCLK 201.6 MHz: HSE 8 MHz, PLL M=5 N=252 P=2",
        "Frame buffer: 640x480 x 1 bit = 38400 bytes in SRAM",
        "Back buffer is copied during V-blank, so no tearing",
        "",
        "WIRING: PA7 -> 270 ohm -> VGA pin 2 (green)",
        "  PB6 -> pin 13 HSYNC, PB7 -> pin 14 VSYNC, GND -> 5-8,10",
        "",
        "Engine3D draws lines, text, 3D meshes and bitmaps.",
        "Press the USER button (PA0) to change demo mode."
    };
    const int rows = sizeof(lines) / sizeof(lines[0]);
    const uint32_t MS_PER_CHAR = 40;
    const uint32_t HOLD_MS = 5000;

    static uint32_t t0 = 0;
    if (framessostate == 0) t0 = millis();

    int total = 0;
    for (int i = 0; i < rows; i++) total += strlen(lines[i]) + 1;

    uint32_t elapsed = millis() - t0;
    int shown = elapsed / MS_PER_CHAR;
    if (elapsed > (uint32_t)total * MS_PER_CHAR + HOLD_MS) framessostate = -1; // restart typing

    engine.setColor(1);
    for (int i = 0; i < rows && shown > 0; i++) {
        char tmp[80];
        int len = strlen(lines[i]);
        int n = (shown < len) ? shown : len;
        strncpy(tmp, lines[i], n);
        tmp[n] = '\0';

        engine.setPen(10, 10 + i * 18);
        engine.drawText(tmp, 3);
        shown -= len + 1;
    }
}

static void schemBox(int x0, int y0, int x1, int y1) {
    engine.line(x0, y0, x1, y0, 1);
    engine.line(x1, y0, x1, y1, 1);
    engine.line(x1, y1, x0, y1, 1);
    engine.line(x0, y1, x0, y0, 1);
}

static void schemText(int x, int y, const char *s, int scale = 2) {
    engine.setPen(x, y);
    engine.drawText(s, scale);
}

// Draws the board-to-VGA-connector wiring on the 640x480 screen.
void DrawSchematic() {
    engine.setColor(1);
    schemText(150, 15, "VGA OUTPUT SCHEMATIC", 3);

    // Discovery board on the left, DB15 connector on the right
    schemBox(30, 70, 210, 290);
    schemText(50, 80, "STM32F407");
    schemText(50, 95, "DISCOVERY");

    schemBox(430, 70, 610, 290);
    schemText(470, 80, "VGA DB15");
    schemText(470, 95, "(MONITOR)");

    const int yG = 140, yH = 190, yV = 230, yD = 270;

    // PA7 (SPI1 MOSI) -> 270R -> pin 2 green
    schemText(60, yG - 5, "PA7");
    engine.line(210, yG, 270, yG, 1);
    schemBox(270, yG - 10, 340, yG + 10);
    schemText(285, yG - 28, "270R");
    engine.line(340, yG, 430, yG, 1);
    schemText(440, yG - 5, "2 GREEN");

    // PB6 (TIM4 CH1 PWM) -> pin 13 HSYNC
    schemText(60, yH - 5, "PB6");
    engine.line(210, yH, 430, yH, 1);
    schemText(440, yH - 5, "13 HSYNC");

    // PB7 (GPIO) -> pin 14 VSYNC
    schemText(60, yV - 5, "PB7");
    engine.line(210, yV, 430, yV, 1);
    schemText(440, yV - 5, "14 VSYNC");

    // GND -> pins 5-8 and 10; unused red/blue pins 1 and 3 also go to GND for mono green
    schemText(60, yD - 5, "GND");
    engine.line(210, yD, 430, yD, 1);
    schemText(440, yD - 5, "5-8,10 GND");

    schemText(30, 320, "PA7 SPI1 MOSI: 1 bit per pixel, 25.2 MHz");
    schemText(30, 345, "PB6 TIM4 CH1 PWM: 31.47 kHz, active low");
    schemText(30, 370, "PB7 GPIO: VSYNC, 60 Hz, active low");
    schemText(30, 405, "270R + 75R monitor input = about 0.7 V at full green");
    schemText(30, 430, "Tie pins 1 and 3 (red, blue) to GND for mono green");
}

void DrawImage(uint16_t imge_number) {
    // Store image pointers in an array for clean indexing (0 to 9)
  const uint8_t* image_list[10] = {
    image_1_ntsc, // counter = 0
    image_1_ntsc, // counter = 1
    image_2_ntsc, // counter = 2
    image_3_ntsc, // counter = 3
    image_4_ntsc, // counter = 4
    image_5_ntsc, // counter = 5
    image_6_ntsc, // counter = 6
    image_7_ntsc, // counter = 7
    image_8_ntsc, // counter = 8 
    image_9_ntsc  // counter = 9 
  };

  engine.clear();

  // Safety check to prevent out-of-bounds array reads (prevents crash)
  if (imge_number < 10) {
    //engine.bitmap(0, 0, image_list[imge_number], 0, 320, 200);
    engine.LoadBitmap(image_list[imge_number], 8000); // Assuming each image is 8000 bytes   image_list[imge_number]
  } else {
    //engine.bitmap(0, 0, image_1_ntsc, 0, 320, 200);
    engine.LoadBitmap(image_1_ntsc, 8000); // Fallback to first image if out of bounds //image_1_ntsc
  }
  //engine.delay(100); // Optional delay for visual effect
}

void DrawRobot() {
    engine.LoadBitmap(robot_bitmap, sizeof(robot_bitmap));

    // The bitmap has a stray vertical line at x=18 (and a short blob at the bottom); the robot starts at x>=20.
    uint8_t *fb = engine.getBackBuffer();
    for (int row = 0; row < 480; row++) {
        uint8_t *line = fb + row * 80;
        if (row >= 440) {
            line[0] = line[1] = line[2] = 0;
        } else {
            line[2] &= ~0x20;
        }
    }
}

void TVlogo() {
    engine.clear();
    engine.intro();
}


//////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
/////////////////////////////////////////////

float A = 0.0f;
float B = 0.0f;
uint8_t sceneIndex = 0;
// CCM RAM (64 KB, unused by the linker): 4-bit depth per 2x2 pixel cell, 320x240 cells.
static uint8_t* const depthBuffer = (uint8_t*)0x10000000;
#define DEPTH_BYTES (320 * 240 / 2)

// Depth-tested 2x2 dot; the depth map is half resolution to fit CCM RAM.
static void plotDepth(int px, int py, uint8_t depth) {
    uint32_t idx = (uint32_t)(py >> 1) * 320 + (px >> 1);
    uint8_t shift = (idx & 1) * 4;
    uint8_t nib = depth >> 4;
    uint8_t cur = (depthBuffer[idx >> 1] >> shift) & 0xF;
    if (nib > cur) {
        depthBuffer[idx >> 1] = (depthBuffer[idx >> 1] & ~(0xF << shift)) | (nib << shift);
        engine.drawPixel(px, py, 1);
        engine.drawPixel(px + 1, py, 1);
        engine.drawPixel(px, py + 1, 1);
        engine.drawPixel(px + 1, py + 1, 1);
    }
}

void DrawPlanetLikePlasma() {
    memset(depthBuffer, 0, DEPTH_BYTES);

    engine.clear();

    const float cx = 320.0f;
    const float cy = 240.0f;
    const float R = 55.0f;

    for (float theta = 0.0f; theta < 6.2831853f; theta += 0.06f) {
        for (float phi = 0.0f; phi < 6.2831853f; phi += 0.05f) {
            float x = R * cosf(theta) * cosf(phi);
            float y = R * sinf(theta);
            float z = R * cosf(theta) * sinf(phi);

            float xr = x * cosf(A) - z * sinf(A);
            float zr = x * sinf(A) + z * cosf(A);
            float yr = y * cosf(B) - zr * sinf(B);
            float zr2 = y * sinf(B) + zr * cosf(B);

            int px = (int)(cx + xr * 3.4f);
            int py = (int)(cy + yr * 3.4f);

            if (px < 0 || px >= 640 || py < 0 || py >= 480) continue;

            float depth = zr2 + 180.0f;
            if (depth <= 0.0f) continue;
            if (depth > 255.0f) depth = 255.0f;

            plotDepth(px, py, (uint8_t)depth);
        }
    }

    A += 0.06f;
    B += 0.04f;
    engine.display();
}

void DrawShellOrbit() {
    memset(depthBuffer, 0, DEPTH_BYTES);

    engine.clear();

    const float cx = 320.0f;
    const float cy = 240.0f;

    for (float theta = 0.0f; theta < 6.2831853f; theta += 0.045f) {
        float r = 22.0f + 18.0f * sinf(theta * 3.0f + A);
        float x = cx + cosf(theta + A) * r * 2.0f;
        float y = cy + sinf(theta * 2.0f + B) * (r * 2.6f);

        for (int i = 0; i < 6; i++) {
            float px = x + cosf(theta * 5.0f + i) * 16.0f;
            float py = y + sinf(theta * 4.0f + i) * 16.0f;
            int ix = (int)px;
            int iy = (int)py;

            if (ix >= 0 && ix < 640 && iy >= 0 && iy < 480) {
                plotDepth(ix, iy, (uint8_t)(128 + 64.0f * sinf(theta + A)));
            }
        }
    }

    A += 0.08f;
    B += 0.05f;
    engine.display();
}

void DrawLatticeSphere() {
    memset(depthBuffer, 0, DEPTH_BYTES);

    engine.clear();

    const float cx = 320.0f;
    const float cy = 240.0f;
    const float R = 58.0f;

    for (float theta = -1.5f; theta < 1.5f; theta += 0.08f) {
        for (float phi = -3.0f; phi < 3.0f; phi += 0.09f) {
            float x = R * cosf(theta) * cosf(phi + A);
            float y = R * sinf(theta) * 0.9f;
            float z = R * cosf(theta) * sinf(phi + A);

            float xr = x * cosf(B) - z * sinf(B);
            float zr = x * sinf(B) + z * cosf(B);
            float yr = y * cosf(A) - zr * sinf(A);
            float zr2 = y * sinf(A) + zr * cosf(A);

            int px = (int)(cx + xr * 3.6f);
            int py = (int)(cy + yr * 3.6f);

            if (px < 0 || px >= 640 || py < 0 || py >= 480) continue;

            float depth = zr2 + 150.0f;
            if (depth <= 0.0f) continue;
            if (depth > 255.0f) depth = 255.0f;

            plotDepth(px, py, (uint8_t)depth);
        }
    }

    A += 0.05f;
    B += 0.07f;
    engine.display();
}

void DrawPulseShell() {
    memset(depthBuffer, 0, DEPTH_BYTES);

    engine.clear();

    const float cx = 320.0f;
    const float cy = 240.0f;

    for (float theta = 0.0f; theta < 6.2831853f; theta += 0.035f) {
        for (float phi = 0.0f; phi < 6.2831853f; phi += 0.06f) {
            float r = 34.0f + 18.0f * sinf(theta * 2.0f + A) + 10.0f * cosf(phi * 3.0f + B);
            float x = r * cosf(theta) * cosf(phi);
            float y = r * sinf(theta) * 0.8f;
            float z = r * cosf(theta) * sinf(phi);

            float xr = x * cosf(A) - z * sinf(A);
            float zr = x * sinf(A) + z * cosf(A);
            float yr = y * cosf(B) - zr * sinf(B);
            float zr2 = y * sinf(B) + zr * cosf(B);

            int px = (int)(cx + xr * 3.8f);
            int py = (int)(cy + yr * 3.8f);

            if (px < 0 || px >= 640 || py < 0 || py >= 480) continue;

            float depth = zr2 + 180.0f;
            if (depth <= 0.0f) continue;
            if (depth > 255.0f) depth = 255.0f;

            plotDepth(px, py, (uint8_t)depth);
        }
    }

    A += 0.07f;
    B += 0.06f;
    engine.display();
}


// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
    FLASH->ACR |= FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    pinMode(USER_BUTTON_PIN, INPUT);


    engine.begin();
    engine.setDoubleBuffering(true);
   // SetupMatrix();
}

// ------------------------------------------------------------
// Main Loop
// ------------------------------------------------------------
void loop() {
    
    bool currentButtonState = digitalRead(USER_BUTTON_PIN);
    if (currentButtonState == HIGH && lastButtonState == LOW) {
        currentMode = (currentMode + 1) % TOTAL_MODES;
        framessostate = 0;

        if (currentMode == 7) {
            initRF24();
        } else if (currentMode == 9) {
            
            lastPingTime = millis();
        }

        delay(50); // Debounce
    }
    lastButtonState = currentButtonState;

    engine.clear();

    switch (currentMode) {
        case 0:
            DrawMesh();
            break;
        case 1:
            DrawSphere();
            break;
        case 2:
            Draw3DEngine();
            break;
        case 3:
            Lines_on_double_buffered_232x220();
            break;
        case 4:
            Direct_modulation();
            break;
        case 5:
            DrawSchematic();
            break;
        case 6:
            DrawRobot();
            break;
        case 7:
            //DrawTerminalMode();
            break;
        case 8:
            TVlogo();
            break;
        case 9:
         
            break;
        default:
            engine.setColor(1);
            engine.setPen(60, 10);
            engine.drawText("Mode not implemented.", 2);
            break;
    }
            
engine.display(); 

framessostate++;


            /*
 int currentButtonState = digitalRead(USER_BUTTON_PIN);

  // Check if state changed
  if (currentButtonState != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    static int buttonState = HIGH;

    if (currentButtonState != buttonState) {
      buttonState = currentButtonState;

      // Execute action only on button press (falling edge)
      if (buttonState == LOW) {
        counter++;
        if (counter > 9) {
          counter = 0; // Reset after 9
        }

        // Draw and update display ONLY when counter changes
       // DrawImage(counter);
        //engine.display(); 
      }
    }
  }
    DrawImage(counter);
        engine.display(); 
        //engine.delay(100); // Optional delay for visual effect
  lastButtonState = currentButtonState;
  
  /*
    switch (sceneIndex) {
        case 0:
            DrawPlanetLikePlasma();
            break;
        case 1:
            DrawShellOrbit();
            break;
        case 2:
            DrawLatticeSphere();
            break;
        default:
            DrawPulseShell();
            break;
    }

    if (++sceneIndex >= 4) sceneIndex = 0;

    delay(20);
    */

}