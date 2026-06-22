#include <display/tft.h>

// On small TFTs (<=160px, e.g. ST7735 128x160) init() centers the QR in the
// area ABOVE this bottom band so a caption drawn in the band never overlaps the
// code. Callers draw their caption in the bottom QR_CAPTION_BAND px.
#define QR_CAPTION_BAND 20

class QRcode
{
	private:
		tft_display *tft;
		void render(int x, int y, int color);

	public:
		QRcode(tft_display *display);
		void init();
		void create(String message);	
};
