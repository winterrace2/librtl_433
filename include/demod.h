#ifndef RTL_433_DEMOD_H
	#define RTL_433_DEMOD_H

    #include "compat_time.h"
    #include "baseband.h"
    #include "samp_grab.h"
	#include "am_analyze.h"
	#include "data_printer_ext.h"

#define MINIMAL_BUF_LENGTH      512
#define MAXIMAL_BUF_LENGTH      (256 * 16384)
#define SIGNAL_GRABBER_BUFFER   (12 * DEFAULT_BUF_LENGTH)

typedef struct _dm_state{
		rtl_433_t *rtl;		// pointer to rtl_433 instance that created this object (no need to free this here)
		int16_t am_buf[MAXIMAL_BUF_LENGTH];  // AM demodulated signal (for OOK decoding)
		union {
			// These buffers aren't used at the same time, so let's use a union to save some memory
			int16_t fm[MAXIMAL_BUF_LENGTH];  // FM demodulated signal (for FSK decoding)
			uint16_t temp[MAXIMAL_BUF_LENGTH];  // Temporary buffer (to be optimized out..)
		} buf;
		uint8_t u8_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
		float f32_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
		int sample_size; // CU8: 1, CS16: 2
		pulse_detect_t *pulse_detect;
		FilterState lowpass_filter_state;
		DemodFM_State demod_FM_state;
		int enable_FM_demod;
		samp_grab_t *samp_grab;   // (only allocated if cfg->grab_mode != 0; created by dm_state_init, freed by dm_state_destroy)
		am_analyze_t *am_analyze; // (only allocated if cfg->analyze_am != 0; created by dm_state_init, freed by dm_state_destroy)
		file_info_t load_info;   
		list_t dumper;			  
		char const *in_filename; // contains a pointer to the name of the current input file

		time_mode_t report_time;

		/* Protocol states */
		list_t r_devs; // elements are alloced in register_protocol, freed in destructor

		list_t output_handler;

		pulse_data_t    pulse_data;
		pulse_data_t    fsk_pulse_data;
		unsigned frame_event_count;
		unsigned frame_start_ago;
		unsigned frame_end_ago;
		struct timeval now;
		float sample_file_pos;
} dm_state;

//	public:
int dm_state_init(dm_state **out_dm, rtl_433_t *rtl);
int dm_state_destroy(dm_state *dm);

int	add_dumper(dm_state *dm, char const *spec, int overwrite);
int registerNonflexDevices(dm_state *dm);
int registerFlexDevices(dm_state *dm, list_t *flex_specs);
int Perform_AM_Demodulation(dm_state *dm, unsigned char *iq_buf, unsigned long n_samples);
int Perform_FM_Demodulation(dm_state *dm, unsigned char *iq_buf, unsigned long n_samples);

int run_ook_demods(dm_state *dm);
int run_fsk_demods(dm_state *dm);

int	ReadFromFiles(dm_state *dm);
int dumpSamplesToFile(dm_state *dm, unsigned char *iq_buf, unsigned long n_samples);
void update_protocols(dm_state *dm, r_cfg_t *cfg);

void start_outputs(dm_state *dm, char const **well_known);
int	add_json_output(dm_state *dm, char *param, int allow_overwrite);
int	add_csv_output(dm_state *dm, char *param, int allow_overwrite);
int	add_kv_output(dm_state *dm, char *param, int allow_overwrite);
int	add_syslog_output(dm_state *dm, char *host, char *port);
int add_ext_output(dm_state *dm, void *extcb);

// private:
static void data_acquired_handler(r_device *r_dev, data_t *data, extdata_t *ext);
static void update_protocol(r_cfg_t *cfg, r_device *r_dev);
static int register_protocol(dm_state *dm, r_device* t_dev, char *arg);
static char const **determine_csv_fields(dm_state *dm, char const **well_known, int *num_fields);
static FILE *fopen_output(char *param, int allow_overwrite);

#endif // RTL_433_DEMOD_H