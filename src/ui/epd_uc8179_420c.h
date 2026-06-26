#pragma once

#include <GxEPD2_EPD.h>

class GxEPD2_420c_NM_UC8179 : public GxEPD2_EPD
{
public:
    static constexpr uint16_t WIDTH  = 400;
    static constexpr uint16_t WIDTH_VISIBLE = WIDTH;
    static constexpr uint16_t HEIGHT = 300;
    static constexpr GxEPD2::Panel panel = GxEPD2::GDEY042Z98;
    static constexpr bool hasColor = true;
    static constexpr bool hasPartialUpdate = false;
    static constexpr bool hasFastPartialUpdate = false;
    static constexpr bool useFastFullUpdate = false;
    static constexpr uint16_t power_on_time = 150;
    static constexpr uint16_t power_off_time = 50;
    static constexpr uint16_t full_refresh_time = 18000;
    static constexpr uint16_t partial_refresh_time = 18000;

    GxEPD2_420c_NM_UC8179(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    void clearScreen(uint8_t value = 0xFF);
    void clearScreen(uint8_t black_value, uint8_t color_value);
    void writeScreenBuffer(uint8_t value = 0xFF);
    void writeScreenBuffer(uint8_t black_value, uint8_t color_value);

    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert, bool mirror_y, bool pgm);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                        int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        bool invert, bool mirror_y, bool pgm);
    void writeImage(const uint8_t* black, const uint8_t* color,
                    int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert, bool mirror_y, bool pgm);
    void writeImage(const uint8_t* black, const uint8_t* color,
                    int16_t x, int16_t y, int16_t w, int16_t h)
    {
        writeImage(black, color, x, y, w, h, false, false, false);
    }
    void writeImagePart(const uint8_t* black, const uint8_t* color,
                        int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        bool invert, bool mirror_y, bool pgm);
    void writeImagePart(const uint8_t* black, const uint8_t* color,
                        int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h)
    {
        writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap,
                       x, y, w, h, false, false, false);
    }
    void writeNative(const uint8_t* data1, const uint8_t* data2,
                     int16_t x, int16_t y, int16_t w, int16_t h,
                     bool invert, bool mirror_y, bool pgm);

    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                   bool invert, bool mirror_y, bool pgm);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                       int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h,
                       bool invert, bool mirror_y, bool pgm);
    void drawImage(const uint8_t* black, const uint8_t* color,
                   int16_t x, int16_t y, int16_t w, int16_t h,
                   bool invert, bool mirror_y, bool pgm);
    void drawImage(const uint8_t* black, const uint8_t* color,
                   int16_t x, int16_t y, int16_t w, int16_t h)
    {
        drawImage(black, color, x, y, w, h, false, false, false);
    }
    void drawImagePart(const uint8_t* black, const uint8_t* color,
                       int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h,
                       bool invert, bool mirror_y, bool pgm);
    void drawImagePart(const uint8_t* black, const uint8_t* color,
                       int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h)
    {
        drawImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap,
                      x, y, w, h, false, false, false);
    }
    void drawNative(const uint8_t* data1, const uint8_t* data2,
                    int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert, bool mirror_y, bool pgm);

    void refresh(bool partial_update_mode = false);
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h);
    void powerOff();
    void hibernate();

private:
    void _writeScreenBuffer(uint8_t command, uint8_t value);
    void _writeImage(uint8_t command, const uint8_t bitmap[],
                     int16_t x, int16_t y, int16_t w, int16_t h,
                     bool invert, bool mirror_y, bool pgm);
    void _writeImagePart(uint8_t command, const uint8_t bitmap[],
                         int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h,
                         bool invert, bool mirror_y, bool pgm);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Update_Full();
};
