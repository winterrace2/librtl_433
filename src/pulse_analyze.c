/**
 * Pulse detection functions
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>

#include "librtl_433.h"
#include "decoder.h"
#include "pulse_analyze.h"
#include "pulse_demod.h"
#include "redir_print.h"

#define MAX_HIST_BINS 16

 /// Histogram data for single bin
typedef struct {
	unsigned count;
	int sum;
	int mean;
	int min;
	int max;
} hist_bin_t;

/// Histogram data for all bins
typedef struct {
	unsigned bins_count;
	hist_bin_t bins[MAX_HIST_BINS];
} histogram_t;

/// Generate a histogram (unsorted)
void histogram_sum(histogram_t *hist, int const *data, unsigned len, float tolerance)
{
	unsigned bin;    // Iterator will be used outside for!

	for (unsigned n = 0; n < len; ++n) {
		// Search for match in existing bins
		for (bin = 0; bin < hist->bins_count; ++bin) {
			int bn = data[n];
			int bm = hist->bins[bin].mean;
			if (abs(bn - bm) < (tolerance * max(bn, bm))) {
				hist->bins[bin].count++;
				hist->bins[bin].sum += data[n];
				hist->bins[bin].mean = hist->bins[bin].sum / hist->bins[bin].count;
				hist->bins[bin].min = min(data[n], hist->bins[bin].min);
				hist->bins[bin].max = max(data[n], hist->bins[bin].max);
				break;    // Match found! Data added to existing bin
			}
		}
		// No match found? Add new bin
		if (bin == hist->bins_count && bin < MAX_HIST_BINS) {
			hist->bins[bin].count = 1;
			hist->bins[bin].sum = data[n];
			hist->bins[bin].mean = data[n];
			hist->bins[bin].min = data[n];
			hist->bins[bin].max = data[n];
			hist->bins_count++;
		} // for bin
	} // for data
}

/// Delete bin from histogram
void histogram_delete_bin(histogram_t *hist, unsigned index)
{
	hist_bin_t const zerobin = { 0 };
	if (hist->bins_count < 1) return;    // Avoid out of bounds
										 // Move all bins afterwards one forward
	for (unsigned n = index; n < hist->bins_count - 1; ++n) {
		hist->bins[n] = hist->bins[n + 1];
	}
	hist->bins_count--;
	hist->bins[hist->bins_count] = zerobin;    // Clear previously last bin
}


/// Swap two bins in histogram
void histogram_swap_bins(histogram_t *hist, unsigned index1, unsigned index2)
{
	hist_bin_t    tempbin;
	if ((index1 < hist->bins_count) && (index2 < hist->bins_count)) {        // Avoid out of bounds
		tempbin = hist->bins[index1];
		hist->bins[index1] = hist->bins[index2];
		hist->bins[index2] = tempbin;
	}
}


/// Sort histogram with mean value (order lowest to highest)
void histogram_sort_mean(histogram_t *hist)
{
	if (hist->bins_count < 2) return;        // Avoid underflow
											 // Compare all bins (bubble sort)
	for (unsigned n = 0; n < hist->bins_count - 1; ++n) {
		for (unsigned m = n + 1; m < hist->bins_count; ++m) {
			if (hist->bins[m].mean < hist->bins[n].mean) {
				histogram_swap_bins(hist, m, n);
			}
		}
	}
}


/// Sort histogram with count value (order lowest to highest)
void histogram_sort_count(histogram_t *hist)
{
	if (hist->bins_count < 2) return;        // Avoid underflow
											 // Compare all bins (bubble sort)
	for (unsigned n = 0; n < hist->bins_count - 1; ++n) {
		for (unsigned m = n + 1; m < hist->bins_count; ++m) {
			if (hist->bins[m].count < hist->bins[n].count) {
				histogram_swap_bins(hist, m, n);
			}
		}
	}
}


/// Fuse histogram bins with means within tolerance
void histogram_fuse_bins(histogram_t *hist, float tolerance)
{
	if (hist->bins_count < 2) return;        // Avoid underflow
											 // Compare all bins
	for (unsigned n = 0; n < hist->bins_count - 1; ++n) {
		for (unsigned m = n + 1; m < hist->bins_count; ++m) {
			int bn = hist->bins[n].mean;
			int bm = hist->bins[m].mean;
			// if within tolerance
			if (abs(bn - bm) < (tolerance * max(bn, bm))) {
				// Fuse data for bin[n] and bin[m]
				hist->bins[n].count += hist->bins[m].count;
				hist->bins[n].sum += hist->bins[m].sum;
				hist->bins[n].mean = hist->bins[n].sum / hist->bins[n].count;
				hist->bins[n].min = min(hist->bins[n].min, hist->bins[m].min);
				hist->bins[n].max = max(hist->bins[n].max, hist->bins[m].max);
				// Delete bin[m]
				histogram_delete_bin(hist, m);
				m--;    // Compare new bin in same place!
			}
		}
	}
}

/// Print a histogram
void histogram_print(histogram_t const *hist, uint32_t samp_rate)
{
	for (unsigned n = 0; n < hist->bins_count; ++n) {
		rtl433_fprintf(stderr, " [%2u] count: %4u,  width: %4.0f us [%.0f;%.0f]\t(%4i S)\n", n,
			hist->bins[n].count,
			hist->bins[n].mean * 1e6 / samp_rate,
			hist->bins[n].min * 1e6 / samp_rate,
			hist->bins[n].max * 1e6 / samp_rate,
			hist->bins[n].mean);
	}
}

#define TOLERANCE (0.2f) // 20% tolerance should still discern between the pulse widths: 0.33, 0.66, 1.0

/// Analyze the statistics of a pulse data structure and print result
void pulse_analyzer(pulse_data_t *data, rtl_433_t *ctx)
{
	double to_ms = 1e3 / data->sample_rate;
	double to_us = 1e6 / data->sample_rate;
	// Generate pulse period data
	int pulse_total_period = 0;
	pulse_data_t pulse_periods = { 0 };
	pulse_periods.num_pulses = data->num_pulses;
	for (unsigned n = 0; n < pulse_periods.num_pulses; ++n) {
		pulse_periods.pulse[n] = data->pulse[n] + data->gap[n];
		pulse_total_period += data->pulse[n] + data->gap[n];
	}
	pulse_total_period -= data->gap[pulse_periods.num_pulses - 1];

	histogram_t hist_pulses = { 0 };
	histogram_t hist_gaps = { 0 };
	histogram_t hist_periods = { 0 };

	// Generate statistics
	histogram_sum(&hist_pulses, data->pulse, data->num_pulses, TOLERANCE);
	histogram_sum(&hist_gaps, data->gap, data->num_pulses - 1, TOLERANCE);                      // Leave out last gap (end)
	histogram_sum(&hist_periods, pulse_periods.pulse, pulse_periods.num_pulses - 1, TOLERANCE); // Leave out last gap (end)

																								// Fuse overlapping bins
	histogram_fuse_bins(&hist_pulses, TOLERANCE);
	histogram_fuse_bins(&hist_gaps, TOLERANCE);
	histogram_fuse_bins(&hist_periods, TOLERANCE);

	rtl433_fprintf(stderr, "Analyzing pulses...\n");
	rtl433_fprintf(stderr, "Total count: %4u,  width: %4.2f ms\t\t(%5i S)\n",
		data->num_pulses, pulse_total_period * to_ms, pulse_total_period);
	rtl433_fprintf(stderr, "Pulse width distribution:\n");
	histogram_print(&hist_pulses, data->sample_rate);
	rtl433_fprintf(stderr, "Gap width distribution:\n");
	histogram_print(&hist_gaps, data->sample_rate);
	rtl433_fprintf(stderr, "Pulse period distribution:\n");
	histogram_print(&hist_periods, data->sample_rate);
	rtl433_fprintf(stderr, "Level estimates [high, low]: %6i, %6i\n",
		data->ook_high_estimate, data->ook_low_estimate);
	rtl433_fprintf(stderr, "RSSI: %.1f dB SNR: %.1f dB Noise: %.1f dB\n",
		data->rssi_db, data->snr_db, data->noise_db);
	rtl433_fprintf(stderr, "Frequency offsets [F1, F2]:  %6i, %6i\t(%+.1f kHz, %+.1f kHz)\n",
		data->fsk_f1_est, data->fsk_f2_est,
		(float)data->fsk_f1_est / INT16_MAX * data->sample_rate / 2.0 / 1000.0,
		(float)data->fsk_f2_est / INT16_MAX * data->sample_rate / 2.0 / 1000.0);

	rtl433_fprintf(stderr, "Guessing modulation: ");
	r_device device = { .name = "Analyzer Device",.ctx = ctx, 0 };
	histogram_sort_mean(&hist_pulses); // Easier to work with sorted data
	histogram_sort_mean(&hist_gaps);
	if (hist_pulses.bins[0].mean == 0) {
		histogram_delete_bin(&hist_pulses, 0);
	} // Remove FSK initial zero-bin

	  // Attempt to find a matching modulation
	if (data->num_pulses == 1) {
		rtl433_fprintf(stderr, "Single pulse detected. Probably Frequency Shift Keying or just noise...\n");
	}
	else if (hist_pulses.bins_count == 1 && hist_gaps.bins_count == 1) {
		rtl433_fprintf(stderr, "Un-modulated signal. Maybe a preamble...\n");
	}
	else if (hist_pulses.bins_count == 1 && hist_gaps.bins_count > 1) {
		rtl433_fprintf(stderr, "Pulse Position Modulation with fixed pulse width\n");
		device.modulation = OOK_PULSE_PPM;
		device.s_short_width = hist_gaps.bins[0].mean;
		device.s_long_width = hist_gaps.bins[1].mean;
		device.s_gap_limit = hist_gaps.bins[1].max + 1;                        // Set limit above next lower gap
		device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
	}
	else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 1) {
		rtl433_fprintf(stderr, "Pulse Width Modulation with fixed gap\n");
		device.modulation = OOK_PULSE_PWM;
		device.s_short_width = hist_pulses.bins[0].mean;
		device.s_long_width = hist_pulses.bins[1].mean;
		device.s_tolerance = (device.s_long_width - device.s_short_width) * 0.4;
		device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
	}
	else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 1) {
		rtl433_fprintf(stderr, "Pulse Width Modulation with fixed period\n");
		device.modulation = OOK_PULSE_PWM;
		device.s_short_width = hist_pulses.bins[0].mean;
		device.s_long_width = hist_pulses.bins[1].mean;
		device.s_tolerance = (device.s_long_width - device.s_short_width) * 0.4;
		device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
	}
	else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 3) {
		rtl433_fprintf(stderr, "Manchester coding\n");
		device.modulation = OOK_PULSE_MANCHESTER_ZEROBIT;
		device.s_short_width = min(hist_pulses.bins[0].mean, hist_pulses.bins[1].mean); // Assume shortest pulse is half period
		device.s_long_width = 0;                                                       // Not used
		device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1;        // Set limit above biggest gap
	}
	else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count >= 3) {
		rtl433_fprintf(stderr, "Pulse Width Modulation with multiple packets\n");
		device.modulation = OOK_PULSE_PWM;
		device.s_short_width = hist_pulses.bins[0].mean;
		device.s_long_width = hist_pulses.bins[1].mean;
		device.s_gap_limit = hist_gaps.bins[1].max + 1; // Set limit above second gap
		device.s_tolerance = (device.s_long_width - device.s_short_width) * 0.4;
		device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
	}
	else if ((hist_pulses.bins_count >= 3 && hist_gaps.bins_count >= 3)
		&& (abs(hist_pulses.bins[1].mean - 2 * hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean / 8)    // Pulses are multiples of shortest pulse
		&& (abs(hist_pulses.bins[2].mean - 3 * hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean / 8)
		&& (abs(hist_gaps.bins[0].mean - hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean / 8)    // Gaps are multiples of shortest pulse
		&& (abs(hist_gaps.bins[1].mean - 2 * hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean / 8)
		&& (abs(hist_gaps.bins[2].mean - 3 * hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean / 8)) {
		rtl433_fprintf(stderr, "Pulse Code Modulation (Not Return to Zero)\n");
		device.modulation = FSK_PULSE_PCM;
		device.s_short_width = hist_pulses.bins[0].mean;        // Shortest pulse is bit width
		device.s_long_width = hist_pulses.bins[0].mean;        // Bit period equal to pulse length (NRZ)
		device.s_reset_limit = hist_pulses.bins[0].mean * 1024; // No limit to run of zeros...
	}
	else if (hist_pulses.bins_count == 3) {
		rtl433_fprintf(stderr, "Pulse Width Modulation with sync/delimiter\n");
		// Re-sort to find lowest pulse count index (is probably delimiter)
		histogram_sort_count(&hist_pulses);
		int p1 = hist_pulses.bins[1].mean;
		int p2 = hist_pulses.bins[2].mean;
		device.modulation = OOK_PULSE_PWM;
		device.s_short_width = p1 < p2 ? p1 : p2;                                // Set to shorter pulse width
		device.s_long_width = p1 < p2 ? p2 : p1;                                // Set to longer pulse width
		device.s_sync_width = hist_pulses.bins[0].mean;                         // Set to lowest count pulse width
		device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
	}
	else {
		rtl433_fprintf(stderr, "No clue...\n");
	}

	// Demodulate (if detected)
	if (device.modulation) {
		rtl433_fprintf(stderr, "Attempting demodulation... short_width: %.0f, long_width: %.0f, reset_limit: %.0f, sync_width: %.0f\n",
			device.s_short_width * to_us, device.s_long_width * to_us,
			device.s_reset_limit * to_us, device.s_sync_width * to_us);
		switch (device.modulation) {
		case FSK_PULSE_PCM:
			rtl433_fprintf(stderr, "Use a flex decoder with -X 'n=name,m=FSK_PCM,s=%.0f,l=%.0f,r=%.0f'\n",
				device.s_short_width * to_us, device.s_long_width * to_us, device.s_reset_limit * to_us);
			pulse_demod_pcm(data, &device);
			break;
		case OOK_PULSE_PPM:
			rtl433_fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_PPM,s=%.0f,l=%.0f,g=%.0f,r=%.0f'\n",
				device.s_short_width * to_us, device.s_long_width * to_us,
				device.s_gap_limit * to_us, device.s_reset_limit * to_us);
			data->gap[data->num_pulses - 1] = device.s_reset_limit + 1; // Be sure to terminate package
			pulse_demod_ppm(data, &device);
			break;
		case OOK_PULSE_PWM:
			rtl433_fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_PWM,s=%.0f,l=%.0f,r=%.0f,g=%.0f,t=%.0f,y=%.0f'\n",
				device.s_short_width * to_us, device.s_long_width * to_us, device.s_reset_limit * to_us,
				device.s_gap_limit * to_us, device.s_tolerance * to_us, device.s_sync_width * to_us);
			data->gap[data->num_pulses - 1] = device.s_reset_limit + 1; // Be sure to terminate package
			pulse_demod_pwm(data, &device);
			break;
		case OOK_PULSE_MANCHESTER_ZEROBIT:
			rtl433_fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_MC_ZEROBIT,s=%.0f,l=%.0f,r=%.0f'\n",
				device.s_short_width * to_us, device.s_long_width * to_us, device.s_reset_limit * to_us);
			data->gap[data->num_pulses - 1] = device.s_reset_limit + 1; // Be sure to terminate package
			pulse_demod_manchester_zerobit(data, &device);
			break;
		default:
			rtl433_fprintf(stderr, "Unsupported\n");
		}
	}

	rtl433_fprintf(stderr, "\n");
}
