#ifndef RTL_433_CONFIG_H
#define RTL_433_CONFIG_H

#include "list.h"

#define MAX_GAINSTR_LEN 100
#define MAX_SDRSET_LEN 100
#define MAX_TESTDATA_LEN 160
#define MAX_PATHLEN 300

// valid options for r_cfg_t->conversion_mode
typedef enum {
	CONVERT_NATIVE,
	CONVERT_SI,
	CONVERT_CUSTOMARY
} conversion_mode_t;

typedef enum {
    REPORT_TIME_DEFAULT,
    REPORT_TIME_DATE,
    REPORT_TIME_SAMPLES,
    REPORT_TIME_UNIX,
    REPORT_TIME_ISO,
    REPORT_TIME_OFF,
} time_mode_t;

// valid options for r_cfg_t->grab_mode
typedef enum {
	GRAB_DISABLED = 0,
	GRAB_ALL_DEVICES = 1,		// All devices
	GRAB_UNKNOWN_DEVICES = 2,	// Unknown devices only
	GRAB_KNOWN_DEVICES = 3		// Known devices only
} GrabMode;

// valid bits for overwrite_modes bit mask:
#define OVR_SUBJ_SAMPLES  1	 // allow to overwrite file with all captured samples (written by rtlsdr_callback)
#define OVR_SUBJ_SIGNALS  2  // allow to overwrite files with samples belonging to detected signals (by pwm_analyze)
#define OVR_SUBJ_DEC_KV   4  // allows to overwrite files with decoded output in KV format
#define OVR_SUBJ_DEC_CSV  8  // allows to overwrite files with decoded output in CSV format
#define OVR_SUBJ_DEC_JSON 16 // allows to overwrite files with decoded output in JSON format

// valid bits for outputs_configured mask:
#define OUTPUT_KV     1 // textual key value output
#define OUTPUT_CSV    2 // CSV output
#define OUTPUT_JSON   4 // JSON output
#define OUTPUT_UDP    8 // syslog output
#define OUTPUT_MQTT  16 // syslog output
#define OUTPUT_EXT  128 // extended output to external callback

typedef struct r_cfg { // following explanations contain the former command line switches in brackets
	int verbosity;										///< [-v] 0=normal, 1=verbose, 2=verbose decoders, 3=debug decoders, 4=trace decoding.
	char dev_query[40];									///< [-d] RTL-SDR: USB device index or ":"+serial. SoapySDR: device query. Leavy empty for no preference (first device).
	char gain_str[MAX_GAINSTR_LEN];						///< [-g] gain (leave stringempty for auto gain).
	char settings_str[MAX_SDRSET_LEN];					///< [-t] soapy-sdr antenna settings etc.
	uint32_t frequency[MAX_FREQS];						///< [-f] list of target frequencies.
	int frequencies;									///< [-f] number of target frequencies.
	int hop_time;										///< [-H] Hop interval for polling of multiple frequencies.
	int ppm_error;										///< [-p] Correct rtl-sdr tuner frequency offset error.
	uint32_t samp_rate;									///< [-s] Sample rate.
	uint32_t out_block_size;							///< [-b] Output block size for RTL-SDR.
	list_t active_prots;								///< [-R] [-G] nth element set to a non-NULL argument (might be "") if corresponding protocol should be used (do this for all entries to "register_all"). Empty list to use defaults.
	list_t flex_specs;									///< [-X] list of specs of general purpose decoders.
	uint32_t level_limit;								///< [-l] Change detection level used to determine pulses [0-16384] (0 = auto).
	uint32_t override_short;							///< [-z] Override short value in data decoder (only effective on -a).
	uint32_t override_long;								///< [-x] Override short value in data decoder (only effective on -a).
	uint32_t bytes_to_read;								///< [-n] Specify number of samples to take (0 = no restriction).
	int analyze_am;										///< [-a] 1 for Analyze mode. Print a textual description of the signal.
	int analyze_pulses;									///< [-A] 1 for Pulse Analyzer. Enable pulse analysis and decode attempt.
	char test_data[MAX_TESTDATA_LEN];					///< [-y] demodulated test data (e.g. "{25}fb2dd58") to verify decoding of with enabled devices.
	GrabMode grab_mode;									///< [-S] Signal auto save. Creates one file per signal.
	char output_path_sigdmp[MAX_PATHLEN];				///<      directory to which the grabbed signals should be written, has to include trailing slash. (empty string for working dir).
	list_t in_files;									///< [-r] input file to read data from (instead of a receiver).
	char out_filename[MAX_PATHLEN];						///< [-w, -W, deprecated: <filename>] output file to Save data stream to  ('-' dumps samples to stdout).
	unsigned char overwrite_modes;						///< [-w/W] mask allowing to overwrite different kinds of output files.
	unsigned char outputs_configured;					///< [-F] bit mask of formats in which decoded output shall be produced.
	char output_path_csv[MAX_PATHLEN];					///< [-F] target file for CSV output.
	char output_path_json[MAX_PATHLEN];					///< [-F] target file for JSON output.
	char output_path_kv[MAX_PATHLEN];					///< [-F] target file for KV output.
	char output_udp_host[100];							///< [-F] target host for syslog output.
	char output_udp_port[10];							///< [-F] target port for syslog output.
	char output_mqtt_host[100];							///< [-F] target host for MQTT output.
	char output_mqtt_port[10];							///< [-F] target port for MQTT output.
	char output_mqtt_opts[100];							///< [-F] options for MQTT output.
	void *output_extcallback;							///< [-F] target callback function for extended external output.
	int report_unknown;									///< flag to enable/disable passing of unknown signals to output_extcallback.
	int report_meta;									///< [-M time|reltime|notime|hires|utc|protocol|level|bits] Add various meta data to every output line.
	time_mode_t report_time_preference;					///< [-M time|reltime|notime|hires|utc|protocol|level|bits] Add various meta data to every output line.
	int report_time_hires;								///< [-M time|reltime|notime|hires|utc|protocol|level|bits] Add various meta data to every output line.
	int report_time_utc;								///< [-M time|reltime|notime|hires|utc|protocol|level|bits] Add various meta data to every output line.
	int report_description;								///< [-M time|reltime|notime|hires|utc|protocol|level|bits] Add various meta data to every output line.
    int report_stats;
    int stats_interval;
    int stats_now;
    time_t stats_time;
	int report_protocol;								///< [-M time|reltime|notime|hires|utc|protocol|level|bits] Add various meta data to every output line.
	int verbose_bits;									///< [-M time|reltime|notime|hires|utc|protocol|level|bits] Add various meta data to every output line.
	char *output_tag;									///< [-K FILE|PATH|<tag>] Add an expanded token or fixed tag to every output line.
	int new_model_keys;									///< [-M newmodel] Use "newmodel" to transition to new model keys. This will become the default someday
	conversion_mode_t conversion_mode;					///< [-C] Convert units in decoded output.
	uint32_t duration;									///< [-T] Specify number of seconds to run.
	int stop_after_successful_events_flag;				///< [-E] 1 for stopping after outputting successful event(s).
} r_cfg_t;

void r_init_cfg(r_cfg_t *cfg); // Fills a config with all default elements
r_cfg_t *r_create_cfg(void);
void r_free_cfg(r_cfg_t *cfg);

#endif /* RTL_433_CONFIG_H */
