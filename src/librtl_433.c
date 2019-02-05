/*
 * rtl_433, turns your Realtek RTL2832 based DVB dongle into a 433.92MHz generic data receiver
 * Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>
 *
 * Based on rtl_sdr
 *
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <errno.h>
//#include <signal.h>

#include "librtl_433.h"
#include "librtl_433_devices.h"
#include "sdr.h"
#include "baseband.h"
#include "pulse_detect.h"
#include "pulse_analyze.h"
#include "pulse_demod.h"
#include "decoder.h"
#include "r_util.h"
#include "redir_print.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#ifdef _MSC_VER
#define F_OK 0
#endif
#endif
#ifndef _MSC_VER
#include <unistd.h>
#endif
 
RTL_433_API int getDevCount() {
    r_device* dev_pointers[] = {
        #define DECL(name) &name,
        DEVICES
        #undef DECL
    };

    return (sizeof(dev_pointers) / sizeof(*dev_pointers));
}

RTL_433_API int getDev(int idx, r_device **dev) {
    if (!dev) {
        rtl433_fprintf(stderr, "getDev: mandatory parameter 'dev' is not set.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    *dev = NULL;

    r_device* dev_pointers[] = {
#define DECL(name) &name,
        DEVICES
#undef DECL
    };

    int dev_num = (sizeof(dev_pointers) / sizeof(*dev_pointers));
    if (idx < 0 || idx >= dev_num) {
        rtl433_fprintf(stderr, "getDev: Requested device id %d is invalid.\n", idx);
        return 0;
    }
    *dev = dev_pointers[idx];
    return 1;
}

RTL_433_API SdrDriverType getDriverType() {
#ifdef RTLSDR
    return SDRDRV_RTLSDR;
#else
#ifdef SOAPYSDR
    return SDRDRV_SOAPYSDR;
#else
    return SDRDRV_NONE;
#endif
#endif
}

static void print_version(void) {
    rtl433_fprintf(stderr, "rtl_433 " VERSION " inputs file rtl_tcp"
#ifdef RTLSDR
            " RTL-SDR"
#endif
#ifdef SOAPYSDR
            " SoapySDR"
#endif
            "\n");
}

RTL_433_API int rtl_433_init(rtl_433_t **out_rtl) {
    if (!out_rtl) {
        rtl433_fprintf(stderr, "rtl_433_init: mandatory parameter is not set.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    rtl_433_t *rtl = (rtl_433_t*) malloc(sizeof(rtl_433_t));
    if (rtl) {
#ifndef _WIN32
        memset(&rtl->sigact, 0, sizeof(rtl->sigact));
#endif
        rtl->cfg = r_create_cfg();
        if (!rtl->cfg) {
            free(rtl);
            return RTL_433_ERROR_OUTOFMEM;
        }
        r_init_cfg(rtl->cfg);
        rtl->dev = NULL;
        rtl->do_exit = 0;
        rtl->do_exit_async = 0;
        rtl->rawtime_old = 0;
        rtl->stop_time = 0;
        rtl->bytes_to_read_left = 0;
        rtl->input_pos = 0;
        rtl->demod = NULL;
        rtl->center_frequency = 0;
        baseband_init(); /* initialize tables */
    }
    *out_rtl = rtl;
    print_version();
    return 0;
}

RTL_433_API int rtl_433_destroy(rtl_433_t *rtl) {
    if (!rtl) {
        rtl433_fprintf(stderr, "rtl_433_destroy: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }
    r_free_cfg(rtl->cfg);
    free(rtl);
    return 0;
}

// well-known fields "time", "msg" and "codes" are used to output general decoder messages
// well-known field "bits" is only used when verbose bits (-M bits) is requested
// well-known field "tag" is only used when output tagging is requested
static char const *well_known_default[] = { "time", "msg", "codes", NULL };
static char const *well_known_with_tag[] = {"time", "msg", "codes", "tag", NULL};
static char const *well_known_with_bits[] = {"time", "msg", "codes", "bits", NULL};
static char const *well_known_with_bits_tag[] = {"time", "msg", "codes", "bits", "tag", NULL};
static char const **well_known_output_fields(r_cfg_t *cfg)
{
    if (cfg->output_tag && cfg->verbose_bits)
        return well_known_with_bits_tag;
    else if (cfg->output_tag)
        return well_known_with_tag;
    else if (cfg->verbose_bits)
        return well_known_with_bits;
    else
        return well_known_default;
}

RTL_433_API int start(rtl_433_t *rtl, struct sigaction *sigact){
    if (!rtl) {
        rtl433_fprintf(stderr, "start: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }
    if (rtl->demod) {
        rtl433_fprintf(stderr, "start: called with active demod context. Stop it first!\n");
        return RTL_433_ERROR_INTERNAL;
    }

    int r = 0; // 0 = failed, 1 = success

    // Initialize all object variables that might change during runtime (rtl->cfg remains unchanged)
    rtl->do_exit = 0;
    rtl->do_exit_async = 0;
    rtl->bytes_to_read_left = rtl->cfg->bytes_to_read;
    rtl->input_pos = 0;

    dm_state_init(&rtl->demod, rtl);
    if (!rtl->demod) {
        rtl433_fprintf(stderr, "start(): Could not initialize demod (internal error)");
        return r;
    }
        
    // Activate output modules
    if (rtl->cfg->outputs_configured &  OUTPUT_JSON) add_json_output(rtl->demod, rtl->cfg->output_path_json, ((rtl->cfg->overwrite_modes & OVR_SUBJ_DEC_JSON) != 0));
    if (rtl->cfg->outputs_configured &  OUTPUT_CSV)  add_csv_output(rtl->demod, rtl->cfg->output_path_csv, ((rtl->cfg->overwrite_modes & OVR_SUBJ_DEC_CSV) != 0));
    if (rtl->cfg->outputs_configured &  OUTPUT_KV)   add_kv_output(rtl->demod, rtl->cfg->output_path_kv, ((rtl->cfg->overwrite_modes & OVR_SUBJ_DEC_KV) != 0));
    if (rtl->cfg->outputs_configured &  OUTPUT_UDP)  add_syslog_output(rtl->demod, rtl->cfg->output_udp_host, rtl->cfg->output_udp_port);
    if (rtl->cfg->outputs_configured &  OUTPUT_EXT)  add_ext_output(rtl->demod, rtl->cfg->output_extcallback);
        
    // Check dumper and activate, if required
    if (!rtl->cfg->out_filename[0] || add_dumper(rtl->demod, rtl->cfg->out_filename, ((rtl->cfg->overwrite_modes & OVR_SUBJ_SAMPLES) != 0)) >= 0) {
        // Register flex devices
        if (registerFlexDevices(rtl->demod, &rtl->cfg->flex_specs)) { // Info: In the original code, flex devices are also registered first.
            // Register non-flex devices
            if (registerNonflexDevices(rtl->demod)) { // loads and registers the non-flex devices
                rtl433_fprintf(stderr, "Registered %zu out of %d device decoding protocols",
                    rtl->demod->r_devs.len, getDevCount());

                // check if we need FM demod // todo: move to demod function?
                for (void **iter = rtl->demod->r_devs.elems; iter && *iter; ++iter) {
                    r_device *r_dev = *iter;
                    if (r_dev->modulation >= FSK_DEMOD_MIN_VAL) {
                        rtl->demod->enable_FM_demod = 1;
                        break;
                    }
                }

                if (!rtl->cfg->verbosity) {
                    // print registered decoder ranges
                    rtl433_fprintf(stderr, " [");
                    for (void **iter = rtl->demod->r_devs.elems; iter && *iter; ++iter) {
                        r_device *r_dev = *iter;
                        unsigned num = r_dev->protocol_num;
                        if (num == 0)
                            continue;
                        while (iter[1]
                            && r_dev->protocol_num + 1 == ((r_device *)iter[1])->protocol_num)
                            r_dev = *++iter;
                        if (num == r_dev->protocol_num)
                            rtl433_fprintf(stderr, " %d", num);
                        else
                            rtl433_fprintf(stderr, " %d-%d", num, r_dev->protocol_num);
                    }
                    rtl433_fprintf(stderr, " ]");
                }
                rtl433_fprintf(stderr, "\n");

                start_outputs(rtl->demod, well_known_output_fields(rtl->cfg));

                if (rtl->cfg->out_block_size < MINIMAL_BUF_LENGTH || rtl->cfg->out_block_size > MAXIMAL_BUF_LENGTH) {
                    rtl433_fprintf(stderr, "Output block size wrong value, falling back to default\n");
                    rtl433_fprintf(stderr, "Minimal length: %u\n", MINIMAL_BUF_LENGTH);
                    rtl433_fprintf(stderr, "Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
                    rtl->cfg->out_block_size = DEFAULT_BUF_LENGTH;
                }

                // Special case for test data (work on test data, if present)
                if (rtl->cfg->test_data[0]) {
                    for (void **iter = rtl->demod->r_devs.elems; iter && *iter; ++iter) {
                        r_device *r_dev = *iter;
                        if (rtl->cfg->verbosity)
                            rtl433_fprintf(stderr, "Verifying test data with device %s.\n", r_dev->name);
                        r += pulse_demod_string(rtl->cfg->test_data, r_dev);
                    }
                }
                // Special case for in files (work on input files, if specified)
                else if (rtl->cfg->in_files.len) {
                    r = ReadFromFiles(rtl->demod);
                }
                // Normal case, no test data, no in files (use live SDR)
                else if(InitSdr(rtl)) {
                    // prepare stop_time if required
                    if (rtl->cfg->duration > 0) {
                        time(&rtl->stop_time);
                        rtl->stop_time += rtl->cfg->duration;
                    }

                    /* Reset endpoint before we start reading from it (mandatory) */
                    if(sdr_reset(rtl->dev, rtl->cfg->verbosity)< 0)
                        rtl433_fprintf(stderr, "WARNING: Failed to reset buffers.\n");
                    if (sdr_activate(rtl->dev) < 0)
                        rtl433_fprintf(stderr, "WARNING: Failed to activate SDR.\n");

                    r = ReadRtlAsync(rtl, sigact);
                    if (!rtl->do_exit) rtl433_fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

                    sdr_close(rtl->dev);
                }
            }
        }
    }

    dm_state_destroy(rtl->demod);
    rtl->demod = NULL;
    return r >= 0 ? r : -r;
}

void sdr_callback(unsigned char *iq_buf, uint32_t len, void *ctx)
{
    rtl_433_t *rtl = (rtl_433_t*) ctx;

    if (!rtl || !rtl->demod) {
        rtl433_fprintf(stderr, "sdr_callback: missing context (internal error)!\n");
        return;
    }

    char time_str[LOCAL_TIME_BUFLEN];

    for (size_t i = 0; i < rtl->demod->output_handler.len; ++i) { // list might contain NULLs
        data_output_poll(rtl->demod->output_handler.elems[i]);
    }
    
    if (rtl->do_exit || rtl->do_exit_async)
        return;

    if ((rtl->bytes_to_read_left > 0) && (rtl->bytes_to_read_left <= len)) {
        len = rtl->bytes_to_read_left;
        rtl->do_exit = 1;
        sdr_stop(rtl->dev);
    }

    get_time_now(&rtl->demod->now);

    unsigned long n_samples = len / 2 / rtl->demod->sample_size;

    // age the frame position if there is one
    if (rtl->demod->frame_start_ago)
        rtl->demod->frame_start_ago += n_samples;
    if (rtl->demod->frame_end_ago)
        rtl->demod->frame_end_ago += n_samples;

#ifndef _WIN32
    alarm(3); // require callback to run every 3 second, abort otherwise
#endif

    if (rtl->demod->samp_grab) {
        samp_grab_push(rtl->demod->samp_grab, iq_buf, len);
    }

    Perform_AM_Demodulation(rtl->demod, iq_buf, n_samples); // fills demod->am_buf
    Perform_FM_Demodulation(rtl->demod, iq_buf, n_samples); // fills demod->buf.fm

    // Handle special input formats
    if (rtl->demod->load_info.format == S16_AM) { // The IQ buffer is really AM demodulated data
        memcpy(rtl->demod->am_buf, iq_buf, len);
    }
    else if (rtl->demod->load_info.format == S16_FM) { // The IQ buffer is really FM demodulated data
                                                   // we would need AM for the envelope too
        memcpy(rtl->demod->buf.fm, iq_buf, len);
    }

    int d_events = 0; // Sensor events successfully detected
    if (rtl->demod->r_devs.len || rtl->cfg->analyze_pulses || rtl->demod->dumper.len || rtl->demod->samp_grab) {
        // Detect a package and loop through demodulators with pulse data
        PulseDetectionResult package_type = PULSEDETECTION_OOK/*1*/;  // Just to get us started
        
        for (void **iter = rtl->demod->dumper.elems; iter && *iter; ++iter) {
            file_info_t const *dumper = *iter;
            if (dumper->format == U8_LOGIC) {
                memset(rtl->demod->u8_buf, 0, n_samples);
                break;
            }
        }
        while (package_type != PULSEDETECTION_OUTOFDATA) {
            int p_events = 0;  // Sensor events successfully detected per package
            package_type = pulse_detect_package(rtl->demod->pulse_detect, rtl->demod->am_buf, rtl->demod->buf.fm, n_samples, rtl->cfg->level_limit, rtl->cfg->samp_rate, rtl->input_pos, &rtl->demod->pulse_data, &rtl->demod->fsk_pulse_data);
            if (package_type != PULSEDETECTION_OUTOFDATA) {
                // new package: set a first frame start if we are not tracking one already
                if (!rtl->demod->frame_start_ago)
                    rtl->demod->frame_start_ago = rtl->demod->pulse_data.start_ago;
                // always update the last frame end
                rtl->demod->frame_end_ago = rtl->demod->pulse_data.end_ago;
            }
            if (package_type == PULSEDETECTION_OOK /*1*/) {
                calc_rssi_snr(rtl, &rtl->demod->pulse_data);
                if (rtl->cfg->analyze_pulses) rtl433_fprintf(stderr, "Detected OOK package\t%s\n", time_pos_str(rtl, rtl->demod->pulse_data.start_ago, time_str));

                p_events += run_ook_demods(rtl->demod);

                for (void **iter = rtl->demod->dumper.elems; iter && *iter; ++iter) {
                    file_info_t const *dumper = *iter;
                    if (dumper->format == VCD_LOGIC) pulse_data_print_vcd(dumper->file, &rtl->demod->pulse_data, '\'', rtl->cfg->samp_rate);
                    if (dumper->format == U8_LOGIC) pulse_data_dump_raw(rtl->demod->u8_buf, n_samples, rtl->input_pos, &rtl->demod->pulse_data, 0x02);
                    if (dumper->format == PULSE_OOK) pulse_data_dump(dumper->file, &rtl->demod->pulse_data);
                }

                if (rtl->cfg->verbosity > 2) pulse_data_print(&rtl->demod->pulse_data);
                if (rtl->cfg->analyze_pulses && (rtl->cfg->grab_mode <= GRAB_ALL_DEVICES || (rtl->cfg->grab_mode == GRAB_UNKNOWN_DEVICES && p_events == 0) || (rtl->cfg->grab_mode == GRAB_KNOWN_DEVICES && p_events > 0))) {
                    pulse_analyzer(&rtl->demod->pulse_data, rtl->cfg->samp_rate, rtl);
                }

            }
            else if (package_type == PULSEDETECTION_FSK/*2*/) {
                calc_rssi_snr(rtl, &rtl->demod->fsk_pulse_data);
                if (rtl->cfg->analyze_pulses) rtl433_fprintf(stderr, "Detected FSK package\t %s\n", time_pos_str(rtl, rtl->demod->fsk_pulse_data.start_ago, time_str));

                p_events += run_fsk_demods(rtl->demod);

                for (void **iter = rtl->demod->dumper.elems; iter && *iter; ++iter) {
                    file_info_t const *dumper = *iter;
                    if (dumper->format == VCD_LOGIC) pulse_data_print_vcd(dumper->file, &rtl->demod->fsk_pulse_data, '"', rtl->cfg->samp_rate);
                    if (dumper->format == U8_LOGIC) pulse_data_dump_raw(rtl->demod->u8_buf, n_samples, rtl->input_pos, &rtl->demod->fsk_pulse_data, 0x04);
                    if (dumper->format == PULSE_OOK) pulse_data_dump(dumper->file, &rtl->demod->fsk_pulse_data);
                }

                if (rtl->cfg->verbosity > 2) pulse_data_print(&rtl->demod->fsk_pulse_data);
                if (rtl->cfg->analyze_pulses && (rtl->cfg->grab_mode <= GRAB_ALL_DEVICES || (rtl->cfg->grab_mode == GRAB_UNKNOWN_DEVICES && p_events == 0) || (rtl->cfg->grab_mode == GRAB_KNOWN_DEVICES && p_events > 0))) {
                    pulse_analyzer(&rtl->demod->fsk_pulse_data, rtl->cfg->samp_rate, rtl);
                }
            } // if (package_type == ...
            d_events += p_events;
        } // while (package_type)...

        // add event counter to the frames currently tracked
        rtl->demod->frame_event_count += d_events;
        // end frame tracking if older than a whole buffer
        if (rtl->demod->frame_start_ago && rtl->demod->frame_end_ago > n_samples) {
            if (rtl->demod->samp_grab) {
                if (rtl->cfg->grab_mode == GRAB_ALL_DEVICES
                        || (rtl->cfg->grab_mode == GRAB_UNKNOWN_DEVICES && rtl->demod->frame_event_count == 0)
                        || (rtl->cfg->grab_mode == GRAB_KNOWN_DEVICES && rtl->demod->frame_event_count > 0)) {
                    unsigned frame_pad = n_samples / 8; // this could also be a fixed value, e.g. 10000 samples
                    unsigned start_padded = rtl->demod->frame_start_ago + frame_pad;
                    unsigned end_padded = rtl->demod->frame_end_ago - frame_pad;
                    unsigned len_padded = start_padded - end_padded;
                    samp_grab_write(rtl->demod->samp_grab, len_padded, end_padded, rtl->cfg->output_path_sigdmp, ((rtl->cfg->overwrite_modes & OVR_SUBJ_SIGNALS) != 0));
                }
            }
            rtl->demod->frame_start_ago = 0;
            rtl->demod->frame_event_count = 0;
        }

        // dump partial pulse_data for this buffer
        for (void **iter = rtl->demod->dumper.elems; iter && *iter; ++iter) {
            file_info_t const *dumper = *iter;
            if (dumper->format == U8_LOGIC) {
                pulse_data_dump_raw(rtl->demod->u8_buf, n_samples, rtl->input_pos, &rtl->demod->pulse_data, 0x02);
                pulse_data_dump_raw(rtl->demod->u8_buf, n_samples, rtl->input_pos, &rtl->demod->fsk_pulse_data, 0x04);
                break;
            }
        }

        if (rtl->cfg->stop_after_successful_events_flag && (d_events > 0)) {
            rtl->do_exit = rtl->do_exit_async = 1;
            sdr_stop(rtl->dev);
        }
    } // if (rtl->cfg->analyze...

    if (rtl->demod->am_analyze) {
        am_analyze(rtl->demod->am_analyze, rtl->demod->am_buf, n_samples, rtl->cfg->verbosity > 1/*, NULL, NULL, 0*/);
    }

    if (!dumpSamplesToFile(rtl->demod, iq_buf, n_samples)) {
        rtl433_fprintf(stderr, "Short write, samples lost, exiting!\n");
        sdr_stop(rtl->dev);
    }

    rtl->input_pos += n_samples;

    if (rtl->bytes_to_read_left > 0) rtl->bytes_to_read_left -= len;

    time_t rawtime;
    time(&rawtime);
    if (rtl->cfg->frequencies > 1 && difftime(rawtime, rtl->rawtime_old) > rtl->cfg->hop_time) {
        rtl->rawtime_old = rawtime;
        rtl->do_exit_async = 1;
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        sdr_stop(rtl->dev);
    }
    if (rtl->cfg->duration > 0 && rawtime >= rtl->stop_time) {
        rtl->do_exit_async = rtl->do_exit = 1;
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        sdr_stop(rtl->dev);
        rtl433_fprintf(stderr, "Time expired, exiting!\n");
    }
}

static int InitSdr(rtl_433_t *rtl) {
    if (!rtl || !rtl->demod) {
        rtl433_fprintf(stderr, "InitSdr: missing context (internal error).\n");
        return RTL_433_ERROR_INTERNAL;
    }

    int r;
    
    r = sdr_open(&rtl->dev, &rtl->demod->sample_size, (rtl->cfg->dev_query[0]?rtl->cfg->dev_query:NULL), rtl->cfg->verbosity);
    if (r < 0) {
        rtl433_fprintf(stderr, "InitSdr: sdr_open failed.\n");
        return 0;
    }
    /* Set the sample rate */
    r = sdr_set_sample_rate(rtl->dev, rtl->cfg->samp_rate, 1); // always verbose
    if (r < 0) {
        return 0; // error has already been printed by sdr_set_sample_rate
    }

    if (rtl->cfg->verbosity || rtl->cfg->level_limit)
        rtl433_fprintf(stderr, "Bit detection level set to %d%s.\n", rtl->cfg->level_limit, (rtl->cfg->level_limit ? "" : " (Auto)"));

    /* Enable automatic gain if gain_str empty (or 0 for RTL-SDR), set manual gain otherwise */
    r = sdr_set_tuner_gain(rtl->dev, rtl->cfg->gain_str, 1); // always verbose

    if(rtl->cfg->ppm_error)
        r = sdr_set_freq_correction(rtl->dev, rtl->cfg->ppm_error, 1); // always verbose

    return 1;
}


static int ReadRtlAsync(rtl_433_t *rtl, struct sigaction *sigact) {
    if (!rtl || !rtl->demod) {
        rtl433_fprintf(stderr, "ReadRtlAsync: missing context (internal error).\n");
        return RTL_433_ERROR_INTERNAL;
    }

    int r = 0;
    int frequency_index = 0;

    if (rtl->cfg->frequencies == 0) {
        rtl->cfg->frequency[0] = DEFAULT_FREQUENCY;
        rtl->cfg->frequencies = 1;
    }
    else {
        time(&rtl->rawtime_old);
    }
    if (rtl->cfg->verbosity) {
        rtl433_fprintf(stderr, "Reading samples in async mode...\n");
    }
    uint32_t samp_rate = rtl->cfg->samp_rate;
    while (!rtl->do_exit) {
        /* Set the frequency */
        rtl->center_frequency = rtl->cfg->frequency[frequency_index];
        r = sdr_set_center_freq(rtl->dev, rtl->center_frequency, 1); // always verbose

        if (samp_rate != rtl->cfg->samp_rate) {
            r = sdr_set_sample_rate(rtl->dev, rtl->cfg->samp_rate, 1); // always verbose
            update_protocols(rtl->demod, rtl->cfg);
            samp_rate = rtl->cfg->samp_rate;
        }


#ifndef _WIN32
        if (sigact) {
            signal(SIGALRM, sigact->sa_handler);
            alarm(3); // require callback to run every 3 second, abort otherwise
        }
#endif
        r = sdr_start(rtl->dev, sdr_callback, (void *)rtl, DEFAULT_ASYNC_BUF_NUMBER, rtl->cfg->out_block_size);
        if (r < 0) {
            rtl433_fprintf(stderr, "WARNING: async read failed (%i).\n", r);
            break;
        }
#ifndef _WIN32
        if (sigact) {
            alarm(0); // cancel the watchdog timer
        }
#endif
        rtl->do_exit_async = 0;
        frequency_index = (frequency_index + 1) % rtl->cfg->frequencies;
    }
    return r;
}

static void calc_rssi_snr(rtl_433_t *rtl, pulse_data_t *pulse_data){
    if (!rtl || !rtl->demod || !pulse_data) {
        rtl433_fprintf(stderr, "calc_rssi_snr: missing context (internal error).\n");
        return;
    }

    float asnr = (float)pulse_data->ook_high_estimate / ((float)pulse_data->ook_low_estimate + 1);
    float foffs1 = (float)pulse_data->fsk_f1_est / INT16_MAX * rtl->cfg->samp_rate / 2.0;
    float foffs2 = (float)pulse_data->fsk_f2_est / INT16_MAX * rtl->cfg->samp_rate / 2.0;
    pulse_data->freq1_hz = (foffs1 + rtl->center_frequency);
    pulse_data->freq2_hz = (foffs2 + rtl->center_frequency);
    // NOTE: for (CU8) amplitude is 10x (because it's squares)
    if (rtl->demod->sample_size == 1) { // amplitude (CU8)
        pulse_data->rssi_db = 10.0f * log10f(pulse_data->ook_high_estimate) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->noise_db = 10.0f * log10f(pulse_data->ook_low_estimate + 1) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->snr_db = 10.0f * log10f(asnr);
    }
    else { // magnitude (CS16)
        pulse_data->rssi_db = 20.0f * log10f(pulse_data->ook_high_estimate) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->noise_db = 20.0f * log10f(pulse_data->ook_low_estimate + 1) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->snr_db = 20.0f * log10f(asnr);
    }
}

char *time_pos_str(rtl_433_t *rtl, unsigned samples_ago, char *buf)
{
    if (rtl->demod->report_time == REPORT_TIME_SAMPLES) {
        double s_per_sample = 1.0 / rtl->cfg->samp_rate;
        return sample_pos_str(rtl->demod->sample_file_pos - samples_ago * s_per_sample, buf);
    }
    else {
        struct timeval ago = rtl->demod->now;
        double us_per_sample = 1e6 / rtl->cfg->samp_rate;
        unsigned usecs_ago   = samples_ago * us_per_sample;
        while (ago.tv_usec < (int)usecs_ago) {
            ago.tv_sec -= 1;
            ago.tv_usec += 1000000;
        }
        ago.tv_usec -= usecs_ago;
         if (rtl->cfg->report_time_hires)
            return usecs_time_str(&ago, buf);
        else
            return local_time_str(ago.tv_sec, buf);
    }
}

RTL_433_API int stop_signal(rtl_433_t *rtl){
    if (!rtl) {
        rtl433_fprintf(stderr, "stop_signal: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    rtl->do_exit = 1;
    sdr_stop(rtl->dev);

    return 0;
}
