#ifndef VARIONE_VEMO_STATUS_H
#define VARIONE_VEMO_STATUS_H

#include <Arduino.h>

namespace VariOneUI {

enum class VemoStatus {
    Scan,
    Success,
    Error,
};

void showVemoStatus(const String &message, VemoStatus status = VemoStatus::Scan, bool waitKeyPress = false);

} // namespace VariOneUI

#endif // VARIONE_VEMO_STATUS_H
