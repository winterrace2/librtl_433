#include <stdbool.h>

#include "librtl_433.h"
#include "data.h"
#include "data_printer_csv.h"
#include "data_printer_json.h"
#include "data_printer_udp.h"
#include "data_printer_kv.h"
#include "data_printer_ext.h"
#include "redir_print.h"
#include "pulse_demod.h"

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

r_device *flex_create_device(char *spec); // maybe put this in some header file?

int dm_state_init(dm_state **out_dm, rtl_433_t *rtl) {
    if (!out_dm || !rtl) return RTL_433_ERROR_INVALID_PARAM;

    dm_state *dm = (dm_state*) malloc(sizeof(dm_state));
    if (dm) {
        dm->rtl = rtl;
        memset(&dm->am_buf, 0, sizeof(dm->am_buf));
        memset(&dm->buf, 0, sizeof(dm->buf));
        memset(&dm->u8_buf, 0, sizeof(dm->u8_buf));
        memset(&dm->f32_buf, 0, sizeof(dm->f32_buf));
        dm->sample_size = 0; // Todo: prüfen, ob korrektes default
        dm->pulse_detect = pulse_detect_create();
        memset(&dm->lowpass_filter_state, 0, sizeof(dm->lowpass_filter_state));
        memset(&dm->demod_FM_state, 0, sizeof(dm->demod_FM_state));
        dm->enable_FM_demod = 0;
        dm->samp_grab = NULL;
        dm->am_analyze = (rtl->cfg->analyze_am ? am_analyze_create() : NULL);
        memset(&dm->load_info, 0, sizeof(dm->load_info));
        list_initialize(&dm->dumper);
        list_ensure_size(&dm->dumper, 32);
        dm->in_filename = NULL;
        dm->report_time = rtl->cfg->report_time_preference;
        list_initialize(&dm->r_devs);
        list_ensure_size(&dm->r_devs, 100);
        list_initialize(&dm->output_handler);
        list_ensure_size(&dm->output_handler, 16);
        memset(&dm->pulse_data, 0, sizeof(dm->pulse_data));
        memset(&dm->fsk_pulse_data, 0, sizeof(dm->fsk_pulse_data));
        dm->frame_event_count = 0;
        dm->frame_start_ago = 0;
        dm->frame_end_ago = 0;
        memset(&dm->now, 0, sizeof(dm->now));
        dm->sample_file_pos = 0;

        if (dm->am_analyze) {
            dm->am_analyze->level_limit = &dm->rtl->cfg->level_limit;
            dm->am_analyze->frequency = &dm->rtl->center_frequency;
            dm->am_analyze->samp_rate = &dm->rtl->cfg->samp_rate;
            dm->am_analyze->sample_size = &dm->sample_size;
            dm->am_analyze->override_short = dm->rtl->cfg->override_short;
            dm->am_analyze->override_long = dm->rtl->cfg->override_long;
        }
        if (rtl->cfg->grab_mode)
            dm->samp_grab = samp_grab_create(SIGNAL_GRABBER_BUFFER);
        if (dm->samp_grab) {
            dm->samp_grab->frequency   = &dm->rtl->center_frequency;
            dm->samp_grab->samp_rate   = &dm->rtl->cfg->samp_rate;
            dm->samp_grab->sample_size = &dm->sample_size;
        }
        if (dm->report_time == REPORT_TIME_DEFAULT) {
            if (rtl->cfg->in_files.len)
                dm->report_time = REPORT_TIME_SAMPLES;
            else
                dm->report_time = REPORT_TIME_DATE;
        }
        if (rtl->cfg->report_time_utc) {
#ifdef _WIN32
            putenv("TZ=UTC+0");
            _tzset();
#else
            int r = setenv("TZ", "UTC", 1);
            if (r != 0)
                rtl433_fprintf(stderr, "Unable to set TZ to UTC; error code: %d\n", r);
#endif
        }

    }
    *out_dm = dm;
    return 0;
}

int dm_state_destroy(dm_state *dm){
    if (!dm) return RTL_433_ERROR_INVALID_PARAM;

    for (void **iter = dm->dumper.elems; iter && *iter; ++iter) {
        file_info_t const *dumper = *iter;
        fclose(dumper->file);
    }
    list_free_elems(&dm->dumper, free);

    if (dm->samp_grab) {
        samp_grab_free(dm->samp_grab);
        dm->samp_grab = NULL;
    }

    sdr_deactivate(dm->rtl->dev);

    list_free_elems(&dm->r_devs, free);
    list_free_elems(&dm->output_handler, (list_elem_free_fn)data_output_free);

    if(dm->pulse_detect) pulse_detect_free(dm->pulse_detect);

    if (dm->am_analyze)
        am_analyze_free(dm->am_analyze);

    free(dm);
    return 0;
}

int add_dumper(dm_state *dm, char const *spec, int overwrite) {
    if (!dm) {
        rtl433_fprintf(stderr, "add_dumper: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    file_info_t *dumper = calloc(1, sizeof(*dumper));

    parse_file_info(spec, dumper);
    if (strcmp(dumper->path, "-") == 0) { /* Write samples to stdout */
        dumper->file = stdout;
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
    }
    else {
        if (access(dumper->path, F_OK) == 0 && !overwrite) {
            rtl433_fprintf(stderr, "Output file %s already exists, exiting\n", spec);
            free(dumper);
            return -1; // exit(1);
        }
        dumper->file = fopen(dumper->path, "wb");
        if (!dumper->file) {
            rtl433_fprintf(stderr, "Failed to open %s\n", spec);
            free(dumper);
            return -1; // exit(1);
        }
    }

    list_push(&dm->dumper, dumper);
    if (dumper->format == VCD_LOGIC) {
        pulse_data_print_vcd_header(dumper->file, dm->rtl->cfg->samp_rate);
    }
    if (dumper->format == PULSE_OOK) {
        pulse_data_print_pulse_header(dumper->file);
    }
    return 0;
}

int registerNonflexDevices(dm_state *dm) {
    if (!dm) return RTL_433_ERROR_INVALID_PARAM;

    r_device r_devices[] = {
#define DECL(name) name,
        DEVICES
#undef DECL
    };
    uint32_t num_r_devices = sizeof(r_devices) / sizeof(*r_devices);
    for (uint32_t i = 0; i < num_r_devices; i++) {
        r_devices[i].protocol_num = i + 1;
        if (dm->rtl->cfg->active_prots.len) {
            if (r_devices[i].disabled != 2) r_devices[i].disabled = (dm->rtl->cfg->active_prots.elems[i] != NULL ? 0 : 1);
        }
    }
    for (uint32_t i = 0; i < num_r_devices; i++) {
        if (!r_devices[i].disabled) {
            if (!register_protocol(dm, &r_devices[i], ""))
                return 0;
        }
    }
    return 1;
}

int registerFlexDevices(dm_state *dm, list_t *flex_specs) {
    for (void **iter = flex_specs->elems; iter && *iter; ++iter) {
        char *spec = *iter;
        r_device *r_dev = flex_create_device(spec);
        if (!r_dev) {
            return 0;
        }
        if (!register_protocol(dm, r_dev, ""))
            return 0;
    }
    return 1;
}

int run_ook_demods(dm_state *dm) {
    if (!dm) return 0;

    int p_events = 0;

    for (void **iter = dm->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        switch (r_dev->modulation) {
        case OOK_PULSE_PCM_RZ:
            p_events += pulse_demod_pcm(&dm->pulse_data, r_dev);
            break;
        case OOK_PULSE_PPM:
            p_events += pulse_demod_ppm(&dm->pulse_data, r_dev);
            break;
        case OOK_PULSE_PWM:
            p_events += pulse_demod_pwm(&dm->pulse_data, r_dev);
            break;
        case OOK_PULSE_MANCHESTER_ZEROBIT:
            p_events += pulse_demod_manchester_zerobit(&dm->pulse_data, r_dev);
            break;
        case OOK_PULSE_PIWM_RAW:
            p_events += pulse_demod_piwm_raw(&dm->pulse_data, r_dev);
            break;
        case OOK_PULSE_PIWM_DC:
            p_events += pulse_demod_piwm_dc(&dm->pulse_data, r_dev);
            break;
        case OOK_PULSE_DMC:
            p_events += pulse_demod_dmc(&dm->pulse_data, r_dev);
            break;
        case OOK_PULSE_PWM_OSV1:
            p_events += pulse_demod_osv1(&dm->pulse_data, r_dev);
            break;
            // FSK decoders
        case FSK_PULSE_PCM:
        case FSK_PULSE_PWM:
            break;
        case FSK_PULSE_MANCHESTER_ZEROBIT:
            p_events += pulse_demod_manchester_zerobit(&dm->pulse_data, r_dev);
            break;
        default:
            rtl433_fprintf(stderr, "Unknown modulation %d in protocol!\n", r_dev->modulation);
        }
    }

    if (!p_events && dm->rtl->cfg->report_unknown && dm->pulse_data.num_pulses > 10) { // unknown OOK signal (no matching device demodulator) - pass to GUI as unknown signal if it has a significant length
		extdata_t ext = {
			.bitbuffer = NULL,
			.pulses = &dm->pulse_data,
			.pulseexc_startidx = 0,
			.pulseexc_len = 0,
			.mod = UNKNOWN_MODULATION_TYPE,
			.samprate = dm->rtl->cfg->samp_rate,
			.freq = dm->rtl->center_frequency
        };
        r_device pseudo = {
            .name = "pseudo device",
            .ctx = dm->rtl,
            .modulation = UNKNOWN_MODULATION_TYPE,
            .decode_fn = NULL,
            .disabled = 2,
            .fields = NULL,
        };
        data_acquired_handler(&pseudo, NULL, &ext);
    }
    return p_events;
}

int run_fsk_demods(dm_state *dm) {
    if (!dm) return 0;

    int p_events = 0;
    for (void **iter = dm->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        switch (r_dev->modulation) {
            // OOK decoders
            case OOK_PULSE_PCM_RZ:
            case OOK_PULSE_PPM:
            case OOK_PULSE_PWM:
            case OOK_PULSE_MANCHESTER_ZEROBIT:
            case OOK_PULSE_PIWM_RAW:
            case OOK_PULSE_PIWM_DC:
            case OOK_PULSE_DMC:
            case OOK_PULSE_PWM_OSV1:
                break;
            case FSK_PULSE_PCM:
                p_events += pulse_demod_pcm(&dm->fsk_pulse_data, r_dev);
                break;
            case FSK_PULSE_PWM:
                p_events += pulse_demod_pwm(&dm->fsk_pulse_data, r_dev);
                break;
            case FSK_PULSE_MANCHESTER_ZEROBIT:
                p_events += pulse_demod_manchester_zerobit(&dm->fsk_pulse_data, r_dev);
                break;
            default:
                rtl433_fprintf(stderr, "Unknown modulation %d in protocol!\n", r_dev->modulation);
        }
    } // for demodulators
    return p_events;
}

int Perform_AM_Demodulation(dm_state *dm, unsigned char *iq_buf, unsigned long n_samples) {
    if (!dm) return RTL_433_ERROR_INVALID_PARAM;

    // AM demodulation
    //    envelope_detect(iq_buf, dm->buf.temp, len/2);
    //    baseband_low_pass_filter(dm->buf.temp, dm->am_buf, len/2, &dm->lowpass_filter_state);
    if (dm->sample_size == 1) { // CU8
        envelope_detect(iq_buf, dm->buf.temp, n_samples);
        //magnitude_true_cu8(iq_buf, dm->buf.temp, n_samples);
        //magnitude_est_cu8(iq_buf, dm->buf.temp, n_samples);
    }
    else { // CS16
           //magnitude_true_cs16((int16_t *)iq_buf, dm->buf.temp, n_samples);
        magnitude_est_cs16((int16_t *)iq_buf, dm->buf.temp, n_samples);
    }
    baseband_low_pass_filter(dm->buf.temp, dm->am_buf, n_samples, &dm->lowpass_filter_state);

    return 0;
}

int Perform_FM_Demodulation(dm_state *dm, unsigned char *iq_buf, unsigned long n_samples) {
    if (!dm) return RTL_433_ERROR_INVALID_PARAM;

    // FM demodulation
    if (dm->enable_FM_demod) {
        //        baseband_demod_FM(iq_buf, demod->buf.fm, len/2, &demod->demod_FM_state);
        if (dm->sample_size == 1) { // CU8
            baseband_demod_FM(iq_buf, dm->buf.fm, n_samples, &dm->demod_FM_state);
        }
        else { // CS16
            baseband_demod_FM_cs16((int16_t *)iq_buf, dm->buf.fm, n_samples, &dm->demod_FM_state);
        }
    }
    return 0;
}

int ReadFromFiles(dm_state *dm) {
    if (!dm) return RTL_433_ERROR_INVALID_PARAM;

    FILE *in_file;

    unsigned char *test_mode_buf = malloc(DEFAULT_BUF_LENGTH * sizeof(unsigned char));
    float *test_mode_float_buf = malloc(DEFAULT_BUF_LENGTH / sizeof(int16_t) * sizeof(float));
    if (!test_mode_buf || !test_mode_float_buf)
    {
        rtl433_fprintf(stderr, "Couldn't allocate read buffers!\n");
        return -1;
    }

    if (dm->rtl->cfg->duration > 0) {
        time(&dm->rtl->stop_time);
        dm->rtl->stop_time += dm->rtl->cfg->duration;
    }

    for (void **iter = dm->rtl->cfg->in_files.elems; iter && *iter; ++iter) {
        dm->in_filename = *iter;
        parse_file_info(dm->in_filename, &dm->load_info);
        if (strcmp(dm->load_info.path, "-") == 0) { /* read samples from stdin */
            in_file = stdin;
            dm->in_filename = "<stdin>";
        } else {
            in_file = fopen(dm->load_info.path, "rb");
            if (!in_file) {
                rtl433_fprintf(stderr, "Opening file: %s failed!\n", dm->in_filename);
                return -1;
            }
        }
        rtl433_fprintf(stderr, "Test mode active. Reading samples from file: %s\n", dm->in_filename);  // Essential information (not quiet)
        if (dm->load_info.format == CU8_IQ
            || dm->load_info.format == S16_AM
            || dm->load_info.format == S16_FM) {
            dm->sample_size = sizeof(uint8_t); // CU8, AM, FM
        } else {
            dm->sample_size = sizeof(int16_t); // CF32, CS16
        }
        if (dm->rtl->cfg->verbosity) {
            rtl433_fprintf(stderr, "Input format: %s\n", file_info_string(&dm->load_info));
        }
        dm->sample_file_pos = 0.0;

		// special case for pulse data file-inputs
		if (dm->load_info.format == PULSE_OOK) {
			while (!dm->rtl->do_exit) {
				pulse_data_load(in_file, &dm->pulse_data);
				if (!dm->pulse_data.num_pulses)
					break;

				if (dm->pulse_data.fsk_f2_est) {
					run_fsk_demods(dm);
				}
				else {
					run_ook_demods(dm);
				}
			}

			if (in_file != stdin)
				fclose(in_file = stdin);

			continue;
		}

		// default case for file-inputs
        int n_blocks = 0;
        unsigned long n_read;
        do {
            if (dm->load_info.format == CF32_IQ) {
                n_read = fread(test_mode_float_buf, sizeof(float), DEFAULT_BUF_LENGTH / 2, in_file);
                // clamp float to [-1,1] and scale to Q0.15
                for(unsigned long n = 0; n < n_read; n++) {
                    int s_tmp = test_mode_float_buf[n] * INT16_MAX;
                    if (s_tmp < -INT16_MAX)
                        s_tmp = -INT16_MAX;
                    else if (s_tmp > INT16_MAX)
                        s_tmp = INT16_MAX;
                    test_mode_buf[n] = (int16_t)s_tmp;
                }
            } else {
                n_read = fread(test_mode_buf, 1, DEFAULT_BUF_LENGTH, in_file);
            }
            if (n_read == 0) break;  // sdr_callback() will Segmentation Fault with len=0
            dm->sample_file_pos = ((float)n_blocks * DEFAULT_BUF_LENGTH + n_read) / dm->rtl->cfg->samp_rate / 2 / dm->sample_size;
            n_blocks++; // this assumes n_read == DEFAULT_BUF_LENGTH
            sdr_callback(test_mode_buf, n_read, dm->rtl);
        } while (n_read != 0 && !dm->rtl->do_exit);

        // Call a last time with cleared samples to ensure EOP detection
        if (dm->sample_size == 1) // CU8
            memset(test_mode_buf, 128, DEFAULT_BUF_LENGTH); // 128 is 0 in unsigned data
        else // CF32, CS16
            memset(test_mode_buf, 0, DEFAULT_BUF_LENGTH);
        dm->sample_file_pos = ((float)n_blocks + 1) * DEFAULT_BUF_LENGTH / dm->rtl->cfg->samp_rate / 2 / dm->sample_size;
        sdr_callback(test_mode_buf, DEFAULT_BUF_LENGTH, dm->rtl);

        //Always classify a signal at the end of the file
        if (dm->am_analyze)
            am_analyze_classify(dm->am_analyze);
        if (dm->rtl->cfg->verbosity) {
            rtl433_fprintf(stderr, "Test mode file issued %d packets\n", n_blocks);
        }

        if (in_file != stdin)
            fclose(in_file = stdin);
    }

    free(test_mode_buf);
    free(test_mode_float_buf);
    return 0;
}

int dumpSamplesToFile(dm_state *dm, unsigned char *iq_buf, unsigned long n_samples) {
    int res = 1;

    for (void **iter = dm->dumper.elems; iter && *iter; ++iter) {
        file_info_t const *dumper = *iter;
        if (!dumper->file
                || dumper->format == VCD_LOGIC
                || dumper->format == PULSE_OOK)
            continue;

        uint8_t* out_buf = iq_buf;  // Default is to dump IQ samples
        unsigned long out_len = n_samples * 2 * dm->sample_size;

        if (dumper->format == CU8_IQ) {
            if (dm->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((uint8_t *)dm->buf.temp)[n] = (((int16_t *)iq_buf)[n] >> 8) + 128; // scale Q0.15 to Q0.7
                out_buf = (uint8_t *)dm->buf.temp;
                out_len = n_samples * 2 * sizeof(uint8_t);
            }
        }
        else if (dumper->format == CS16_IQ) {
            if (dm->sample_size == 1) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((int16_t *)dm->buf.temp)[n] = (iq_buf[n] - 128) << 8; // scale Q0.7 to Q0.15
                out_buf = (uint8_t *)dm->buf.temp; // this buffer is too small if out_block_size is large
                out_len = n_samples * 2 * sizeof(int16_t);
            }
        }
        else if (dumper->format == S16_AM) {
            out_buf = (uint8_t *)dm->am_buf;
            out_len = n_samples * sizeof(int16_t);

        }
        else if (dumper->format == S16_FM) {
            out_buf = (uint8_t *)dm->buf.fm;
            out_len = n_samples * sizeof(int16_t);

        }
        else if (dumper->format == F32_AM) {
            for (unsigned long n = 0; n < n_samples; ++n)
                dm->f32_buf[n] = dm->am_buf[n] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)dm->f32_buf;
            out_len = n_samples * sizeof(float);

        }
        else if (dumper->format == F32_FM) {
            for (unsigned long n = 0; n < n_samples; ++n)
                dm->f32_buf[n] = dm->buf.fm[n] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)dm->f32_buf;
            out_len = n_samples * sizeof(float);

        }
        else if (dumper->format == F32_I) {
            if (dm->sample_size == 1)
                for (unsigned long n = 0; n < n_samples; ++n)
                    dm->f32_buf[n] = (iq_buf[n * 2] - 128) * (1.0 / 0x80); // scale from Q0.7
            else
                for (unsigned long n = 0; n < n_samples; ++n)
                    dm->f32_buf[n] = ((int16_t *)iq_buf)[n * 2] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)dm->f32_buf;
            out_len = n_samples * sizeof(float);

        }
        else if (dumper->format == F32_Q) {
            if (dm->sample_size == 1)
                for (unsigned long n = 0; n < n_samples; ++n)
                    dm->f32_buf[n] = (iq_buf[n * 2 + 1] - 128) * (1.0 / 0x80); // scale from Q0.7
            else
                for (unsigned long n = 0; n < n_samples; ++n)
                    dm->f32_buf[n] = ((int16_t *)iq_buf)[n * 2 + 1] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)dm->f32_buf;
            out_len = n_samples * sizeof(float);

        }
        else if (dumper->format == U8_LOGIC) { // state data
            out_buf = dm->u8_buf;
            out_len = n_samples;
        }

        if (fwrite(out_buf, 1, out_len, dumper->file) != out_len) {
            res = 0;
        }
    }
    return res;
}

/** Pass the data structure to all output handlers. Frees data afterwards. */
static void data_acquired_handler(r_device *r_dev, data_t *data, extdata_t *ext) {
    if (!r_dev || !r_dev->ctx || !r_dev->ctx->demod) {
        rtl433_fprintf(stderr, "data_acquired_handler: missing context (internal error).\n");
        return;
    }
    rtl_433_t *rtl = r_dev->ctx;

    // The data_acquired_handler also gets called with unset data if we just received an unknown signal.
    // This is only used to pass extended pulse details to an external callback-
    // If this is unused, we can return immediately.
    int use_ext = (rtl->cfg->outputs_configured & OUTPUT_EXT); // is external callback being used?
    int unknown_dev = 0; // did we get called for an unknown signal?
    if (r_dev->modulation == UNKNOWN_MODULATION_TYPE) {
        if (!use_ext || !ext) return;
        unknown_dev = 1;
    }

    if (!unknown_dev) {
        if (rtl->cfg->conversion_mode == CONVERT_SI) {
            for (data_t *d = data; d; d = d->next) {
                // Convert double type fields ending in _F to _C
                if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_F")) {
                    *(double*)d->value = fahrenheit2celsius(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_F", "_C");
                    free(d->key);
                    d->key = new_label;
                    char *pos;
                    if (d->format && (pos = strrchr(d->format, 'F'))) {
                        *pos = 'C';
                    }
                }
                // Convert double type fields ending in _mph to _kph
                else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mph")) {
                    *(double*)d->value = mph2kmph(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_mph", "_kph");
                    free(d->key);
                    d->key = new_label;
                    char *new_format_label = str_replace(d->format, "mph", "kph");
                    free(d->format);
                    d->format = new_format_label;
                }
                // Convert double type fields ending in _mph to _kph
                else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_inch")) {
                    *(double*)d->value = inch2mm(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_inch", "_mm");
                    free(d->key);
                    d->key = new_label;
                    char *new_format_label = str_replace(d->format, "inch", "mm");
                    free(d->format);
                    d->format = new_format_label;
                }
                // Convert double type fields ending in _inHg to _hPa
                else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_inHg")) {
                    *(double*)d->value = inhg2hpa(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_inHg", "_hPa");
                    free(d->key);
                    d->key = new_label;
                    char *new_format_label = str_replace(d->format, "inHg", "hPa");
                    free(d->format);
                    d->format = new_format_label;
                }
                // Convert double type fields ending in _PSI to _kPa
                else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_PSI")) {
                    *(double*)d->value = psi2kpa(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_PSI", "_kPa");
                    free(d->key);
                    d->key = new_label;
                    char *new_format_label = str_replace(d->format, "PSI", "kPa");
                    free(d->format);
                    d->format = new_format_label;
                }
            }
        }
        if (rtl->cfg->conversion_mode == CONVERT_CUSTOMARY) {
            for (data_t *d = data; d; d = d->next) {
                // Convert double type fields ending in _C to _F
                if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_C")) {
                    *(double*)d->value = celsius2fahrenheit(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_C", "_F");
                    free(d->key);
                    d->key = new_label;
                    char *pos;
                    if (d->format && (pos = strrchr(d->format, 'C'))) {
                        *pos = 'F';
                    }
                }
                // Convert double type fields ending in _kph to _mph
                else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_kph")) {
                    *(double*)d->value = kmph2mph(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_kph", "_mph");
                    free(d->key);
                    d->key = new_label;
                    char *new_format_label = str_replace(d->format, "kph", "mph");
                    free(d->format);
                    d->format = new_format_label;
                }
                // Convert double type fields ending in _mm to _inch
                else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mm")) {
                    *(double*)d->value = mm2inch(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_mm", "_inch");
                    free(d->key);
                    d->key = new_label;
                    char *new_format_label = str_replace(d->format, "mm", "inch");
                    free(d->format);
                    d->format = new_format_label;
                }
                // Convert double type fields ending in _hPa to _inHg
                else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_hPa")) {
                    *(double*)d->value = hpa2inhg(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_hPa", "_inHg");
                    free(d->key);
                    d->key = new_label;
                    char *new_format_label = str_replace(d->format, "hPa", "inHg");
                    free(d->format);
                    d->format = new_format_label;
                }
                // Convert double type fields ending in _kPa to _PSI
                else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_kPa")) {
                    *(double*)d->value = kpa2psi(*(double*)d->value);
                    char *new_label = str_replace(d->key, "_kPa", "_PSI");
                    free(d->key);
                    d->key = new_label;
                    char *new_format_label = str_replace(d->format, "kPa", "PSI");
                    free(d->format);
                    d->format = new_format_label;
                }
            }
        }

        // prepend "description" if requested
        if (rtl->cfg->report_description) {
            data = data_prepend(data,
                    "description", "Description", DATA_STRING, r_dev->name,
                    NULL);
        }

        // prepend "protocol" if requested
        if (rtl->cfg->report_protocol && r_dev->protocol_num) {
            data = data_prepend(data,
                "protocol", "Protocol", DATA_INT, r_dev->protocol_num,
                NULL);
        }

        if (rtl->cfg->report_meta && rtl->demod->fsk_pulse_data.fsk_f2_est) {
            data_append(data,
                "mod", "Modulation", DATA_STRING, "FSK",
                "freq1", "Freq1", DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, rtl->demod->fsk_pulse_data.freq1_hz / 1000000.0,
                "freq2", "Freq2", DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, rtl->demod->fsk_pulse_data.freq2_hz / 1000000.0,
                "rssi", "RSSI", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, rtl->demod->fsk_pulse_data.rssi_db,
                "snr", "SNR", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, rtl->demod->fsk_pulse_data.snr_db,
                "noise", "Noise", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, rtl->demod->fsk_pulse_data.noise_db,
                NULL);
        }
        else if (rtl->cfg->report_meta) {
            data_append(data,
                "mod", "Modulation", DATA_STRING, "ASK",
                "freq", "Freq", DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, rtl->demod->pulse_data.freq1_hz / 1000000.0,
                "rssi", "RSSI", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, rtl->demod->pulse_data.rssi_db,
                "snr", "SNR", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, rtl->demod->pulse_data.snr_db,
                "noise", "Noise", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, rtl->demod->pulse_data.noise_db,
                NULL);
        }
    }
    else { // unknown_dev
        data = data_make("model", "", DATA_STRING, "unknown device", NULL);
    }

    // always prepend "time"
    char time_str[LOCAL_TIME_BUFLEN];
    time_pos_str(rtl, 0, time_str);
    data = data_prepend(data,
        "time", "", DATA_STRING, time_str,
        NULL);

    // prepend "tag" if available
    if (rtl->cfg->output_tag) {
        char const *output_tag = rtl->cfg->output_tag;
        if (rtl->demod->in_filename && !strcmp("PATH", rtl->cfg->output_tag)) {
            output_tag = rtl->demod->in_filename;
        }
        else if (rtl->demod->in_filename && !strcmp("FILE", rtl->cfg->output_tag)) {
            output_tag = file_basename(rtl->demod->in_filename);
        }
        data = data_prepend(data,
            "tag", "Tag", DATA_STRING, output_tag,
            NULL);
    }

    // if output via external callback is activated we have to pass an extended data_t structure (data_ext_t) to the output handlers
    if (use_ext) {
        // ensure that there's not yet another user of the data pointer since the pointer will change when we replace the first entry right now
        if (data->retain > 0) {
            rtl433_fprintf(stderr, "data_acquired_handler: Unexpected condition, data object is already shared.\n");
            data_free(data);
            return;
        }
        // alloc a new, extended base object for the linked list and copy the old base object (incl. next pointer)
        data_ext_t *extdata = (data_ext_t*)malloc(sizeof(data_ext_t)); 
        extdata->data = *data;
        extdata->ext = *ext;
        // free old base object and replace with new one
        free(data); // note: we intentionally do not call data_free in order to keep the rest of the list
        data = &extdata->data;
    }

    for (size_t i = 0; i < rtl->demod->output_handler.len; ++i) { // list might contain NULLs
        data_output_t *handler = (data_output_t*)rtl->demod->output_handler.elems[i];
        if (!unknown_dev || handler->ext_callback) { // don't call output handlers without external callback with extended input from unknown devices
            data_output_print(rtl->demod->output_handler.elems[i], data);
        }
    }
    data_free(data);
}

static void update_protocol(r_cfg_t *cfg, r_device *r_dev)
{
    float samples_per_us = cfg->samp_rate / 1.0e6;

    r_dev->f_short_width = 1.0 / (r_dev->short_width * samples_per_us);
    r_dev->f_long_width  = 1.0 / (r_dev->long_width * samples_per_us);
    r_dev->s_short_width = r_dev->short_width * samples_per_us;
    r_dev->s_long_width  = r_dev->long_width * samples_per_us;
    r_dev->s_reset_limit = r_dev->reset_limit * samples_per_us;
    r_dev->s_gap_limit   = r_dev->gap_limit * samples_per_us;
    r_dev->s_sync_width  = r_dev->sync_width * samples_per_us;
    r_dev->s_tolerance   = r_dev->tolerance * samples_per_us;

    r_dev->verbose      = cfg->verbosity > 0 ? cfg->verbosity - 1 : 0;
    r_dev->verbose_bits = cfg->verbose_bits;
}

static void free_protocol(r_device *r_dev)
{
    // free(r_dev->name);
    free(r_dev->decode_ctx);
    free(r_dev);
}

void update_protocols(dm_state *dm, r_cfg_t *cfg)
{
    for (void **iter = dm->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        update_protocol(cfg, r_dev);
    }
}

static int register_protocol(dm_state *dm, r_device *r_dev, char *arg)
{
    if (!dm) return RTL_433_ERROR_INVALID_PARAM;

    r_device *p;
    if (r_dev->create_fn) {
        p = r_dev->create_fn(arg);
    }
    else {
        if (arg && *arg) {
			rtl433_fprintf(stderr, "Protocol [%d] \"%s\" does not take arguments \"%s\"!\n", r_dev->protocol_num, r_dev->name, arg);
        }
        p = malloc(sizeof (*p));
        *p = *r_dev; // copy
    }

    update_protocol(dm->rtl->cfg, p);

    p->output_fn = data_acquired_handler;
//  p->output_ctx = cfg;
    p->ctx = dm->rtl;

    list_push(&dm->r_devs, p);

    if (dm->rtl->cfg->verbosity) {
        rtl433_fprintf(stderr, "Registering protocol [%d] \"%s\"\n", r_dev->protocol_num, r_dev->name);
    }

    return 1;
}

// find the fields output for CSV
static char const **determine_csv_fields(dm_state *dm, char const **well_known, int *num_fields) {
    if (!dm) {
        rtl433_fprintf(stderr, "determine_csv_fields: missing context (internal error)!\n");
        return NULL;
    }

    list_t field_list = { 0 };
    list_ensure_size(&field_list, 100);

    // always add well-known fields
    list_push_all(&field_list, (void **)well_known);

    list_t *r_devs = &dm->r_devs;
    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (!r_dev->disabled) {
            if (r_dev->fields)
                list_push_all(&field_list, (void **)r_dev->fields);
            else
                rtl433_fprintf(stderr, "rtl_433: warning: %d \"%s\" does not support CSV output\n",
                    r_dev->protocol_num, r_dev->name);
        }
    }

    if (num_fields)
        *num_fields = field_list.len;
    return (char const **)field_list.elems;
}

static FILE *fopen_output(char *param, int allow_overwrite) {
    if (!param || !*param || *param == '-') {
        return stdout;
    }

    FILE *file = NULL;
    if (access(param, F_OK) != 0 || allow_overwrite) {
        file = fopen(param, "a");
        if (!file) rtl433_fprintf(stderr, "rtl_433: failed to open output file\n");
    }
    return file;
}

int add_json_output(dm_state *dm, char *param, int allow_overwrite) {
    if (!dm) {
        rtl433_fprintf(stderr, "add_json_output: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    FILE *file = fopen_output(param, allow_overwrite);
    if (file) {
        list_push(&dm->output_handler, data_output_json_create(file));
    }
    else {
        rtl433_fprintf(stderr, "add_json_output: failed to open output JSON file %s", param);
    }

    return 0;
}

int add_csv_output(dm_state *dm, char *param, int allow_overwrite) {
    if (!dm) {
        rtl433_fprintf(stderr, "add_csv_output: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    FILE *file = fopen_output(param, allow_overwrite);
    if (file) {
        list_push(&dm->output_handler, data_output_csv_create(file));
    }
    else {
        rtl433_fprintf(stderr, "add_csv_output: failed to open output CSV file %s", param);
    }

    return 0;
}

void start_outputs(dm_state *dm, char const **well_known) {
    if (!dm) {
        rtl433_fprintf(stderr, "start_outputs: invalid parameters (internal error).\n");
        return;
    }

    int num_output_fields;
    const char **output_fields = determine_csv_fields(dm, well_known, &num_output_fields);
    for (size_t i = 0; i < dm->output_handler.len; ++i) { // list might contain NULLs
        data_output_start(dm->output_handler.elems[i], output_fields, num_output_fields);
    }
    free(output_fields);
}

int add_kv_output(dm_state *dm, char *param, int allow_overwrite) {
    if (!dm) {
        rtl433_fprintf(stderr, "add_kv_output: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    FILE *file = fopen_output(param, (dm->rtl->cfg->overwrite_modes & OVR_SUBJ_DEC_KV));
    if (file) {
        list_push(&dm->output_handler, data_output_kv_create(file));
    }
    else {
        rtl433_fprintf(stderr, "add_kv_output: failed to open output TXT file %s", param);
    }

    return 0;
}

int add_syslog_output(dm_state *dm, char *host, char *port) {
    if (!dm) {
        rtl433_fprintf(stderr, "add_syslog_output: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    rtl433_fprintf(stderr, "Syslog UDP datagrams to %s port %s\n", host, port);
    list_push(&dm->output_handler, data_output_syslog_create(host, port));

    return 0;
}

int add_ext_output(dm_state *dm, void *extcb) {
    if (!dm) {
        rtl433_fprintf(stderr, "add_ext_output: missing context.\n");
        return RTL_433_ERROR_INVALID_PARAM;
    }

    rtl433_fprintf(stderr, "Output to external callback function at address %p\n", extcb);
    list_push(&dm->output_handler, data_output_extcb_create(extcb));
    return 0;
}

