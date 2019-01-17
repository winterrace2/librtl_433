#ifndef INCLUDE_DECODER_H_
#define INCLUDE_DECODER_H_

#include <string.h>
#include "librtl_433.h"
#include "librtl_433_devices.h"
#include "bitbuffer.h"
#include "data.h"
#include "util.h"
#include "decoder_util.h"
#include "redir_print.h"

/* Supported modulation types */
#define UNKNOWN_MODULATION_TYPE         0

#define OOK_DEMOD_MIN_VAL               3  // Dummy. OOK demodulation must start at this value
#define OOK_PULSE_MANCHESTER_ZEROBIT    3  // Manchester encoding. Hardcoded zerobit. Rising Edge = 0, Falling edge = 1
#define OOK_PULSE_PCM_RZ                4  // Pulse Code Modulation with Return-to-Zero encoding, Pulse = 0, No pulse = 1
#define OOK_PULSE_PPM                   5  // Pulse Position Modulation. Short gap = 0, Long = 1
#define OOK_PULSE_PWM                   6  // Pulse Width Modulation with precise timing parameters
#define OOK_PULSE_PIWM_RAW              8  // Level shift for each bit. Short interval = 1, Long = 0
#define OOK_PULSE_PIWM_DC               11 // Level shift for each bit. Short interval = 1, Long = 0
#define OOK_PULSE_DMC                   9  // Level shift within the clock cycle.
#define OOK_PULSE_PWM_OSV1              10 // Pulse Width Modulation. Oregon Scientific v1
#define OOK_DEMOD_MAX_VAL               11 // Dummy. OOK demodulation must end at this value

#define FSK_DEMOD_MIN_VAL               16 // Dummy. FSK demodulation must start at this value
#define FSK_PULSE_PCM                   16 // FSK, Pulse Code Modulation
#define FSK_PULSE_PWM                   17 // FSK, Pulse Width Modulation. Short pulses = 1, Long = 0
#define FSK_PULSE_MANCHESTER_ZEROBIT    18 // FSK, Manchester encoding
#define FSK_DEMOD_MAX_VAL               18 // Dummy. FSK demodulation must ends at this value

#endif /* INCLUDE_DECODER_H_ */
