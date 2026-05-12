#include "radio.h"

// LoRa is the replacement for the classic AX.25 modem/PHY here.
// AX.25 frames, including their FCS, are handed to radio::Driver as packet bytes.
