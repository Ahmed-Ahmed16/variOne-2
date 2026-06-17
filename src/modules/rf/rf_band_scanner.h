#ifndef __RF_BAND_SCANNER_H__
#define __RF_BAND_SCANNER_H__

// rf_band_scanner: all-band CC1101 sweep tool. Continuously sweeps the three
// CC1101 sub-bands (~300-348, ~387-464, ~779-928 MHz) sampling RSSI at each
// step and reports the strongest frequency seen. VariOne addition to the RF
// menu. Cancellable with BACK (EscPress). See rf_utils.* for the shared CC1101
// helpers reused here (initRfModule/setMHZ/getRssi/deinitRfModule).

void rf_band_scanner();

#endif
