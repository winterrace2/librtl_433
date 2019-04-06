#include "librtl_433.h"
#include "list.h"
 
void r_init_cfg(r_cfg_t *cfg) {
    cfg->verbosity = 0;
    cfg->dev_query[0] = 0;
    memset(cfg->gain_str, 0, sizeof(cfg->gain_str));
    memset(cfg->settings_str, 0, sizeof(cfg->settings_str));
    for (int a = 0; a < MAX_FREQS; a++) cfg->frequency[a] = 0;
    cfg->frequencies = 0;
    cfg->hop_time = DEFAULT_HOP_TIME;
    cfg->ppm_error = 0;
    cfg->samp_rate = DEFAULT_SAMPLE_RATE;
    cfg->out_block_size = DEFAULT_BUF_LENGTH;
    list_initialize(&cfg->active_prots);
    list_initialize(&cfg->flex_specs);
    list_ensure_size(&cfg->flex_specs, 5);
    cfg->level_limit = DEFAULT_LEVEL_LIMIT;
    cfg->override_short = 0;
    cfg->override_long = 0;
    cfg->bytes_to_read = 0;
    cfg->analyze_am = 0;
    cfg->analyze_pulses = 0;
    memset(cfg->test_data, 0, sizeof(cfg->test_data));
    cfg->grab_mode = GRAB_DISABLED;
    memset(cfg->output_path_sigdmp, 0, sizeof(cfg->output_path_sigdmp));
    list_initialize(&cfg->in_files);
    list_ensure_size(&cfg->in_files, 100);
    memset(cfg->out_filename, 0, sizeof(cfg->out_filename));
    cfg->overwrite_modes = 0;
    cfg->outputs_configured = 0;
    memset(cfg->output_path_csv, 0, sizeof(cfg->output_path_csv));
    memset(cfg->output_path_json, 0, sizeof(cfg->output_path_json));
    memset(cfg->output_path_kv, 0, sizeof(cfg->output_path_kv));
    strcpy(cfg->output_udp_host, "localhost");
    strcpy(cfg->output_udp_port, "514");
	strcpy(cfg->output_mqtt_host, "localhost");
	strcpy(cfg->output_mqtt_port, "1883");
	memset(cfg->output_mqtt_opts, 0, sizeof(cfg->output_mqtt_opts));
	cfg->output_extcallback = NULL;
    cfg->report_unknown = 0;
    cfg->report_meta = 0;
    cfg->report_time_preference = REPORT_TIME_DEFAULT;
    cfg->report_time_hires = 0;
    cfg->report_time_utc = 0;
    cfg->report_protocol = 0;
    cfg->verbose_bits = 0;
    cfg->output_tag = NULL;
    cfg->new_model_keys = 0;
    cfg->conversion_mode = CONVERT_NATIVE;
    cfg->duration = 0;
    cfg->stop_after_successful_events_flag = 0;
}

r_cfg_t *r_create_cfg(void)
{
    r_cfg_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        rtl433_fprintf(stderr, "Could not create cfg!\n");
        return NULL;
    }

    r_init_cfg(cfg);
    return cfg;
}

void r_free_cfg(r_cfg_t *cfg)
{
    list_free_elems(&cfg->active_prots, free);
    list_free_elems(&cfg->flex_specs, free);
    list_free_elems(&cfg->in_files, free);
    free(cfg);
}
