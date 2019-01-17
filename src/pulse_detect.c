/**
 * Pulse detection functions
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "pulse_detect.h"
#include "util.h"
#include "decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "redir_print.h"

void pulse_data_clear(pulse_data_t *data) {
	*data = (pulse_data_t const) {0};
}


void pulse_data_print(pulse_data_t const *data) {
	rtl433_fprintf(stderr, "Pulse data: %u pulses\n", data->num_pulses);
	for(unsigned n = 0; n < data->num_pulses; ++n) {
		rtl433_fprintf(stderr, "[%3u] Pulse: %4u, Gap: %4u, Period: %4u\n", n, data->pulse[n], data->gap[n], data->pulse[n] + data->gap[n]);
	}
}

static void *bounded_memset(void *b, int c, int64_t size, int64_t offset, int64_t len)
{
	if (offset < 0) {
		len += offset; // reduce len by negative offset
		offset = 0;
	}
	if (offset + len > size) {
		len = size - offset; // clip excessive len
	}
	if (len > 0)
		memset((char *)b + offset, c, (size_t)len);
	return b;
}

void pulse_data_dump_raw(uint8_t *buf, unsigned len, uint64_t buf_offset, pulse_data_t const *data, uint8_t bits)
{
	int64_t pos = data->offset - buf_offset;
	for (unsigned n = 0; n < data->num_pulses; ++n) {
		bounded_memset(buf, 0x01 | bits, len, pos, data->pulse[n]);
		pos += data->pulse[n];
		bounded_memset(buf, 0x01, len, pos, data->gap[n]);
		pos += data->gap[n];
	}
}

void pulse_data_print_vcd_header(FILE *file, uint32_t sample_rate)
{
	char time_str[LOCAL_TIME_BUFLEN];
	char *timescale;
	if (sample_rate <= 500000)
		timescale = "1 us";
	else
		timescale = "100 ns";
	fprintf(file, "$date %s $end\n", local_time_str(0, time_str));
	fprintf(file, "$version rtl_433 0.1.0 $end\n");
	fprintf(file, "$comment Acquisition at %s Hz $end\n", nice_freq(sample_rate));
	fprintf(file, "$timescale %s $end\n", timescale);
	fprintf(file, "$scope module rtl_433 $end\n");
	fprintf(file, "$var wire 1 / FRAME $end\n");
	fprintf(file, "$var wire 1 ' AM $end\n");
	fprintf(file, "$var wire 1 \" FM $end\n");
	fprintf(file, "$upscope $end\n");
	fprintf(file, "$enddefinitions $end\n");
	fprintf(file, "#0 0/ 0' 0\"\n");
}

void pulse_data_print_vcd(FILE *file, pulse_data_t const *data, int ch_id, uint32_t sample_rate)
{
	float scale;
	if (sample_rate <= 500000)
		scale = 1000000 / sample_rate; // unit: 1 us
	else
		scale = 10000000 / sample_rate; // unit: 100 ns
	uint64_t pos = data->offset;
	for (unsigned n = 0; n < data->num_pulses; ++n) {
		if (n == 0)
			fprintf(file, "#%.f 1/ 1%c\n", pos * scale, ch_id);
		else
			fprintf(file, "#%.f 1%c\n", pos * scale, ch_id);
		pos += data->pulse[n];
		fprintf(file, "#%.f 0%c\n", pos * scale, ch_id);
		pos += data->gap[n];
	}
	if (data->num_pulses > 0)
		fprintf(file, "#%.f 0/\n", pos * scale);
}


// OOK adaptive level estimator constants
#define OOK_HIGH_LOW_RATIO	8			// Default ratio between high and low (noise) level
#define OOK_MIN_HIGH_LEVEL	1000		// Minimum estimate of high level
#define OOK_MAX_HIGH_LEVEL	(128*128)	// Maximum estimate for high level (A unit phasor is 128, anything above is overdrive)
#define OOK_MAX_LOW_LEVEL	(OOK_MAX_HIGH_LEVEL/2)	// Maximum estimate for low level
#define OOK_EST_HIGH_RATIO	64			// Constant for slowness of OOK high level estimator
#define OOK_EST_LOW_RATIO	1024		// Constant for slowness of OOK low level (noise) estimator (very slow)

// FSK adaptive frequency estimator constants
#define FSK_DEFAULT_FM_DELTA	6000	// Default estimate for frequency delta
#define FSK_EST_RATIO		32			// Constant for slowness of FSK estimators


/// Internal state data for pulse_FSK_detect()
typedef struct {
	unsigned int fsk_pulse_length;		// Counter for internal FSK pulse detection
	enum {
		PD_FSK_STATE_INIT	= 0,	// Initial frequency estimation
		PD_FSK_STATE_F1		= 1,	// High frequency (pulse)
		PD_FSK_STATE_F2		= 2,	// Low frequency (gap)
		PD_FSK_STATE_ERROR	= 3		// Error - stay here until cleared
	} fsk_state;

	int fm_f1_est;			// Estimate for the F1 frequency for FSK
	int fm_f2_est;			// Estimate for the F2 frequency for FSK

} pulse_FSK_state_t;


/// Demodulate Frequency Shift Keying (FSK) sample by sample
///
/// Function is stateful between calls
/// Builds estimate for initial frequency. When frequency deviates more than a
/// threshold value it will determine whether the deviation is positive or negative
/// to classify it as a pulse or gap. It will then transition to other state (F1 or F2)
/// and build an estimate of the other frequency. It will then transition back and forth when current
/// frequency is closer to other frequency estimate.
/// Includes spurious suppression by coalescing pulses when pulse/gap widths are too short.
/// Pulses equal higher frequency (F1) and Gaps equal lower frequency (F2)
/// @param fm_n: One single sample of FM data
/// @param *fsk_pulses: Will return a pulse_data_t structure for FSK demodulated data
/// @param *s: Internal state
void pulse_FSK_detect(int16_t fm_n, pulse_data_t *fsk_pulses, pulse_FSK_state_t *s) {
	int const fm_f1_delta = abs(fm_n - s->fm_f1_est);	// Get delta from F1 frequency estimate
	int const fm_f2_delta = abs(fm_n - s->fm_f2_est);	// Get delta from F2 frequency estimate
	s->fsk_pulse_length++;

	switch(s->fsk_state) {
		case PD_FSK_STATE_INIT:		// Initial frequency - High or low?
			// Initial samples?
			if (s->fsk_pulse_length < PD_MIN_PULSE_SAMPLES) {
				s->fm_f1_est = s->fm_f1_est/2 + fm_n/2;		// Quick initial estimator
			// Above default frequency delta?
			} else if (fm_f1_delta > (FSK_DEFAULT_FM_DELTA/2)) {
				// Positive frequency delta - Initial frequency was low (gap)
				if (fm_n > s->fm_f1_est) {
					s->fsk_state = PD_FSK_STATE_F1;
					s->fm_f2_est = s->fm_f1_est;	// Switch estimates
					s->fm_f1_est = fm_n;			// Prime F1 estimate
					fsk_pulses->pulse[0] = 0;		// Initial frequency was a gap...
					fsk_pulses->gap[0] = s->fsk_pulse_length;		// Store gap width
					fsk_pulses->num_pulses++;
					s->fsk_pulse_length = 0;
				// Negative Frequency delta - Initial frequency was high (pulse)
				} else {
					s->fsk_state = PD_FSK_STATE_F2;
					s->fm_f2_est = fm_n;	// Prime F2 estimate
					fsk_pulses->pulse[0] = s->fsk_pulse_length;	// Store pulse width
					s->fsk_pulse_length = 0;
				}
			// Still below threshold
			} else {
				s->fm_f1_est += fm_n/FSK_EST_RATIO - s->fm_f1_est/FSK_EST_RATIO;	// Slow estimator
			}
			break;
		case PD_FSK_STATE_F1:		// Pulse high at F1 frequency
			// Closer to F2 than F1?
			if (fm_f1_delta > fm_f2_delta) {
				s->fsk_state = PD_FSK_STATE_F2;
				// Store if pulse is not too short (suppress spurious)
				if (s->fsk_pulse_length >= PD_MIN_PULSE_SAMPLES) {
					fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store pulse width
					s->fsk_pulse_length = 0;
				// Else rewind to last gap
				} else {
					s->fsk_pulse_length += fsk_pulses->gap[fsk_pulses->num_pulses-1];	// Restore counter
					fsk_pulses->num_pulses--;		// Rewind one pulse
					// Are we back to initial frequency? (Was initial frequency a gap?)
					if ((fsk_pulses->num_pulses == 0) && (fsk_pulses->pulse[0] == 0)) {
						s->fm_f1_est = s->fm_f2_est;	// Switch back estimates
						s->fsk_state = PD_FSK_STATE_INIT;
					}
				}
			// Still below threshold
			} else {
				s->fm_f1_est += fm_n/FSK_EST_RATIO - s->fm_f1_est/FSK_EST_RATIO;	// Slow estimator
			}
			break;
		case PD_FSK_STATE_F2:		// Pulse gap at F2 frequency
			// Freq closer to F1 than F2 ?
			if (fm_f2_delta > fm_f1_delta) {
				s->fsk_state = PD_FSK_STATE_F1;
				// Store if pulse is not too short (suppress spurious)
				if (s->fsk_pulse_length >= PD_MIN_PULSE_SAMPLES) {
					fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store gap width
					fsk_pulses->num_pulses++;	// Go to next pulse
					s->fsk_pulse_length = 0;
					// When pulse buffer is full go to error state
					if (fsk_pulses->num_pulses >= PD_MAX_PULSES) {
						rtl433_fprintf(stderr, "pulse_FSK_detect(): Maximum number of pulses reached!\n");
						s->fsk_state = PD_FSK_STATE_ERROR;
					}
				// Else rewind to last pulse
				} else {
					s->fsk_pulse_length += fsk_pulses->pulse[fsk_pulses->num_pulses];	// Restore counter
					// Are we back to initial frequency?
					if (fsk_pulses->num_pulses == 0) {
						s->fsk_state = PD_FSK_STATE_INIT;
					}
				}
			// Still below threshold
			} else {
				s->fm_f2_est += fm_n/FSK_EST_RATIO - s->fm_f2_est/FSK_EST_RATIO;	// Slow estimator
			}
			break;
		case PD_FSK_STATE_ERROR:		// Stay here until cleared
			break;
		default:
			rtl433_fprintf(stderr, "pulse_FSK_detect(): Unknown FSK state!!\n");
			s->fsk_state = PD_FSK_STATE_ERROR;
	} // switch(s->fsk_state)
}


/// Wrap up FSK modulation and store last data at End Of Package
///
/// @param fm_n: One single sample of FM data
/// @param *fsk_pulses: Pulse_data_t structure for FSK demodulated data
/// @param *s: Internal state
void pulse_FSK_wrap_up(pulse_data_t *fsk_pulses, pulse_FSK_state_t *s) {
	if (fsk_pulses->num_pulses < PD_MAX_PULSES) {	// Avoid overflow
		s->fsk_pulse_length++;
		if(s->fsk_state == PD_FSK_STATE_F1) {
			fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store last pulse
			fsk_pulses->gap[fsk_pulses->num_pulses] = 0;	// Zero gap at end
		} else {
			fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store last gap
		}
		fsk_pulses->num_pulses++;
	}
}


/// Internal state data for pulse_pulse_package()
struct pulse_detect {
	enum {
		PD_OOK_STATE_IDLE		= 0,
		PD_OOK_STATE_PULSE		= 1,
		PD_OOK_STATE_GAP_START	= 2,
		PD_OOK_STATE_GAP		= 3
	} ook_state;
	int pulse_length;		// Counter for internal pulse detection
	int max_pulse;			// Size of biggest pulse detected

	int data_counter;		// Counter for how much of data chunk is processed
	int lead_in_counter;	// Counter for allowing initial noise estimate to settle

	int ook_low_estimate;		// Estimate for the OOK low level (base noise level) in the envelope data
	int ook_high_estimate;		// Estimate for the OOK high level

	pulse_FSK_state_t	FSK_state;

};

pulse_detect_t *pulse_detect_create()
{
	return calloc(1, sizeof(pulse_detect_t));
}

void pulse_detect_free(pulse_detect_t *pulse_detect)
{
	free(pulse_detect);
}

/// Demodulate On/Off Keying (OOK) and Frequency Shift Keying (FSK) from an envelope signal
PulseDetectionResult pulse_detect_package(pulse_detect_t *pulse_detect, int16_t const *envelope_data, int16_t const *fm_data, int len, uint16_t level_limit, uint32_t samp_rate, uint64_t sample_offset, pulse_data_t *pulses, pulse_data_t *fsk_pulses)
{
	int const samples_per_ms = samp_rate / 1000;
	pulse_detect_t *s = pulse_detect;
	s->ook_high_estimate = max(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);	// Be sure to set initial minimum level

	if (s->data_counter == 0) {
		// age the pulse_data if this is a fresh buffer
		pulses->start_ago += len;
		fsk_pulses->start_ago += len;
	}

	// Process all new samples
	while(s->data_counter < len) {
		// Calculate OOK detection threshold and hysteresis
		int16_t const am_n = envelope_data[s->data_counter];
		int16_t ook_threshold = s->ook_low_estimate + (s->ook_high_estimate - s->ook_low_estimate) / 2;
		if (level_limit != 0) ook_threshold = level_limit;	// Manual override
		int16_t const ook_hysteresis = ook_threshold / 8;	// ±12%

		// OOK State machine
		switch (s->ook_state) {
			case PD_OOK_STATE_IDLE:
				if (am_n > (ook_threshold + ook_hysteresis)	// Above threshold?
					&& s->lead_in_counter > OOK_EST_LOW_RATIO	// Lead in counter to stabilize noise estimate
				) {
					// Initialize all data
					pulse_data_clear(pulses);
					pulse_data_clear(fsk_pulses);
					pulses->offset = sample_offset + s->data_counter;
					fsk_pulses->offset = sample_offset + s->data_counter;
					pulses->start_ago = len - s->data_counter;
					fsk_pulses->start_ago = len - s->data_counter;
					s->pulse_length = 0;
					s->max_pulse = 0;
					s->FSK_state = (pulse_FSK_state_t){0};
					s->ook_state = PD_OOK_STATE_PULSE;
				} else {	// We are still idle..
					// Estimate low (noise) level
					int const ook_low_delta = am_n - s->ook_low_estimate;
					s->ook_low_estimate += ook_low_delta / OOK_EST_LOW_RATIO;
					s->ook_low_estimate += ((ook_low_delta > 0) ? 1 : -1);	// Hack to compensate for lack of fixed-point scaling
					// Calculate default OOK high level estimate
					s->ook_high_estimate = OOK_HIGH_LOW_RATIO * s->ook_low_estimate;	// Default is a ratio of low level
					s->ook_high_estimate = max(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);
					s->ook_high_estimate = min(s->ook_high_estimate, OOK_MAX_HIGH_LEVEL);
					if (s->lead_in_counter <= OOK_EST_LOW_RATIO) s->lead_in_counter++;		// Allow initial estimate to settle
				}
				break;
			case PD_OOK_STATE_PULSE:
				s->pulse_length++;
				// End of pulse detected?
				if (am_n < (ook_threshold - ook_hysteresis)) {	// Gap?
					// Check for spurious short pulses
					if (s->pulse_length < PD_MIN_PULSE_SAMPLES) {
						s->ook_state = PD_OOK_STATE_IDLE;
					} else {
						// Continue with OOK decoding
						pulses->pulse[pulses->num_pulses] = s->pulse_length;	// Store pulse width
						s->max_pulse = max(s->pulse_length, s->max_pulse);	// Find largest pulse
						s->pulse_length = 0;
						s->ook_state = PD_OOK_STATE_GAP_START;
					}
				// Still pulse
				} else {
					// Calculate OOK high level estimate
					s->ook_high_estimate += am_n / OOK_EST_HIGH_RATIO - s->ook_high_estimate / OOK_EST_HIGH_RATIO;
					s->ook_high_estimate = max(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);
					s->ook_high_estimate = min(s->ook_high_estimate, OOK_MAX_HIGH_LEVEL);
					// Estimate pulse carrier frequency
					pulses->fsk_f1_est += fm_data[s->data_counter] / OOK_EST_HIGH_RATIO - pulses->fsk_f1_est / OOK_EST_HIGH_RATIO;
				}
				// FSK Demodulation
				if(pulses->num_pulses == 0) {	// Only during first pulse
					pulse_FSK_detect(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
				}
				break;
			case PD_OOK_STATE_GAP_START:	// Beginning of gap - it might be a spurious gap
				s->pulse_length++;
				// Pulse detected again already? (This is a spurious short gap)
				if (am_n > (ook_threshold + ook_hysteresis)) {	// New pulse?
					s->pulse_length += pulses->pulse[pulses->num_pulses];	// Restore counter
					s->ook_state = PD_OOK_STATE_PULSE;
				// Or this gap is for real?
				} else if (s->pulse_length >= PD_MIN_PULSE_SAMPLES) {
					s->ook_state = PD_OOK_STATE_GAP;
					// Determine if FSK modulation is detected
					if(fsk_pulses->num_pulses > PD_MIN_PULSES) {
						// Store last pulse/gap
						pulse_FSK_wrap_up(fsk_pulses, &s->FSK_state);
						// Store estimates
						fsk_pulses->fsk_f1_est = s->FSK_state.fm_f1_est;
						fsk_pulses->fsk_f2_est = s->FSK_state.fm_f2_est;
						fsk_pulses->ook_low_estimate = s->ook_low_estimate;
						fsk_pulses->ook_high_estimate = s->ook_high_estimate;
						pulses->end_ago = len - s->data_counter;
						fsk_pulses->end_ago = len - s->data_counter;
						s->ook_state = PD_OOK_STATE_IDLE;	// Ensure everything is reset
						return PULSEDETECTION_FSK /*2*/;	// FSK package detected!!!
					}
				} // if
				// FSK Demodulation (continue during short gap - we might return...)
				if(pulses->num_pulses == 0) {	// Only during first pulse
					pulse_FSK_detect(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
				}
				break;
			case PD_OOK_STATE_GAP:
				s->pulse_length++;
				// New pulse detected?
				if (am_n > (ook_threshold + ook_hysteresis)) {	// New pulse?
					pulses->gap[pulses->num_pulses] = s->pulse_length;	// Store gap width
					pulses->num_pulses++;	// Next pulse

					// EOP if too many pulses
					if (pulses->num_pulses >= PD_MAX_PULSES) {
						s->ook_state = PD_OOK_STATE_IDLE;
						// Store estimates
						pulses->ook_low_estimate = s->ook_low_estimate;
						pulses->ook_high_estimate = s->ook_high_estimate;
						pulses->end_ago = len - s->data_counter;
						return PULSEDETECTION_OOK /*1*/;	// End Of Package!!
					}

					s->pulse_length = 0;
					s->ook_state = PD_OOK_STATE_PULSE;
				}

				// EOP if gap is too long
				if (((s->pulse_length > (PD_MAX_GAP_RATIO * s->max_pulse))	// gap/pulse ratio exceeded
					&& (s->pulse_length > (PD_MIN_GAP_MS * samples_per_ms)))	// Minimum gap exceeded
					|| (s->pulse_length > (PD_MAX_GAP_MS * samples_per_ms))	// maximum gap exceeded
				) {
					pulses->gap[pulses->num_pulses] = s->pulse_length;	// Store gap width
					pulses->num_pulses++;	// Store last pulse
					s->ook_state = PD_OOK_STATE_IDLE;
					// Store estimates
					pulses->ook_low_estimate = s->ook_low_estimate;
					pulses->ook_high_estimate = s->ook_high_estimate;
					pulses->end_ago = len - s->data_counter;
					return PULSEDETECTION_OOK /*1*/;	// End Of Package!!
				}
				break;
			default:
				rtl433_fprintf(stderr, "demod_OOK(): Unknown state!!\n");
				s->ook_state = PD_OOK_STATE_IDLE;
		} // switch
		s->data_counter++;
	} // while

	s->data_counter = 0;
	return PULSEDETECTION_OUTOFDATA /*0*/;	// Out of data
}
