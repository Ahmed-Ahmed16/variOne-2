#include <Arduino.h>
#include "qrcode.h"
#include "qrencode.h"

int offsetsX;
int offsetsY;
int screenwidth;
int screenheight;
int multiply = 2;

QRcode::QRcode(tft_display *tft)
{
  this->tft = tft;
}

void QRcode::init()
{
  screenwidth = tft->width();
  screenheight = tft->height();
  // Reserve a caption band at the bottom on small screens so the QR centers
  // ABOVE it and never overlaps text. WD is the module count (VERSION 7 -> 45);
  // QR size is WD*multiply, so we only shrink `multiply`, never clip modules.
  int usableHeight = screenheight;
  if (screenheight <= 160)
  {
    usableHeight = screenheight - QR_CAPTION_BAND;
    if (usableHeight < WD)
      usableHeight = screenheight;
  }
  int min = screenwidth;
  if (usableHeight < min)
    min = usableHeight;
  multiply = min / WD;
  if (multiply < 1)
    multiply = 1;
  offsetsX = (screenwidth - (WD * multiply)) / 2;
  offsetsY = (usableHeight - (WD * multiply)) / 2;
}

void QRcode::render(int x, int y, int color)
{
  x = (x * multiply) + offsetsX;
  y = (y * multiply) + offsetsY;
  if (color == 1)
  {
    tft->drawPixel(x, y, TFT_BLACK);
    if (multiply > 1)
    {
      tft->fillRect(x, y, multiply, multiply, TFT_BLACK);
    }
  }
  else
  {
    tft->drawPixel(x, y, TFT_WHITE);
    if (multiply > 1)
    {
      tft->fillRect(x, y, multiply, multiply, TFT_WHITE);
    }
  }
}

void QRcode::create(String message)
{
  // create QR code
  tft->fillScreen(TFT_WHITE);
  message.toCharArray((char *)strinbuf, 260);
  qrencode();
  // print QR Code
  for (byte x = 0; x < WD; x += 2)
  {
    for (byte y = 0; y < WD; y++)
    {
      if (QRBIT(x, y) && QRBIT((x + 1), y))
      {
        // black square on top of black square
        render(x, y, 1);
        render((x + 1), y, 1);
      }
      if (!QRBIT(x, y) && QRBIT((x + 1), y))
      {
        // white square on top of black square
        render(x, y, 0);
        render((x + 1), y, 1);
      }
      if (QRBIT(x, y) && !QRBIT((x + 1), y))
      {
        // black square on top of white square
        render(x, y, 1);
        render((x + 1), y, 0);
      }
      if (!QRBIT(x, y) && !QRBIT((x + 1), y))
      {
        // white square on top of white square
        render(x, y, 0);
        render((x + 1), y, 0);
      }
    }
  }

  // Mirror the QR to the USB screen-mirror (varione-mirror.html) as ONE compact text
  // line. The per-module fill-rects (45x45 = 2025) overflow the 64-entry draw-log, so a
  // QR can't stream as draw ops. fillScreen(WHITE) above already streamed the white
  // background; here we send only the black-module matrix as hex on Serial — the same
  // USB CDC the mirror reads (serialDevice wraps &Serial). This lib has no project
  // headers, so we can't cheaply gate on logging-active; the line is harmless when no
  // mirror is attached (just a ">>QR:" hex line the serial console / CLI ignore).
  {
    static const char H[] = "0123456789ABCDEF";
    String qr = ">>QR:" + String(offsetsX) + "," + String(offsetsY) + "," + String(multiply) +
                "," + String((int)WD) + ",";
    String hex;
    hex.reserve(((WD * WD + 7) / 8) * 2);
    uint8_t cur = 0;
    int bit = 0;
    for (int yy = 0; yy < WD; yy++)
    {
      for (int xx = 0; xx < WD; xx++)
      {
        cur = (uint8_t)((cur << 1) | (QRBIT(xx, yy) ? 1 : 0));
        if (++bit == 8)
        {
          hex += H[cur >> 4];
          hex += H[cur & 0x0F];
          cur = 0;
          bit = 0;
        }
      }
    }
    if (bit)
    {
      cur = (uint8_t)(cur << (8 - bit));
      hex += H[cur >> 4];
      hex += H[cur & 0x0F];
    }
    qr += hex;
    Serial.println(qr);
  }
}
