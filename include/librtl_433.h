#ifndef LIBRTL_433_H
#define LIBRTL_433_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

typedef struct _rtl_433 rtl_433_t;

#include "librtl_433_export.h"
#include "sdr.h"
#include "decoder.h"
#include "fileformat.h"
#include "redir_print.h"
#include "data_printer_ext.h"

#define MAX_FREQS               32
#define DEFAULT_BUF_LENGTH      (16 * 32 * 512) // librtlsdr default

#include "librtl_433_devices.h"
#include "config.h"
#include "demod.h"

#define RTL_433_ERROR_INVALID_PARAM -99
#define RTL_433_ERROR_INTERNAL -98
#define RTL_433_ERROR_OUTOFMEM -97

#define DEFAULT_SAMPLE_RATE     250000
#define DEFAULT_FREQUENCY       433920000
#define DEFAULT_HOP_TIME        (60*10)
#define DEFAULT_ASYNC_BUF_NUMBER    0 // Force use of default value (librtlsdr default: 15)

/*
 * Theoretical high level at I/Q saturation is 128x128 = 16384 (above is ripple)
 * 0 = automatic adaptive level limit, else fixed level limit
 * 8000 = previous fixed default
 */
#define DEFAULT_LEVEL_LIMIT     0

#ifdef GIT_VERSION
#define STR_VALUE(arg) #arg
#define STR_EXPAND(s) STR_VALUE(s)
#define VERSION "version " STR_EXPAND(GIT_VERSION) " branch " STR_EXPAND(GIT_BRANCH) " at " STR_EXPAND(GIT_TIMESTAMP)
#else
#define VERSION "version unknown"
#endif

typedef enum { // used by getDriverType()
	SDRDRV_NONE = 2,
	SDRDRV_RTLSDR = 1,
	SDRDRV_SOAPYSDR  = 2
} SdrDriverType;

// buffer to hold localized timestamp YYYY-MM-DD HH:MM:SS
#define LOCAL_TIME_BUFLEN	32

typedef struct _rtl_433 {
	#ifndef _WIN32
		struct sigaction sigact;
	#endif

		r_cfg_t *cfg;

		sdr_dev_t *dev;
		int do_exit;									// set to 1 in order to quit processing
		int do_exit_async;								// set to 1 in order to quit 1 processing round (async frequency hopping)
		time_t rawtime_old;								// used by async mode to check multiple frequencies hopping time
		time_t stop_time;								// used by async mode to check duration
		uint32_t bytes_to_read_left;					// rest of bytes_to_read (value is initialized with cfg->bytes_to_read on startup)
		uint64_t input_pos;
		dm_state *demod;
		uint32_t center_frequency;
} rtl_433_t;

//public
RTL_433_API int rtl_433_init(rtl_433_t **out_rtl);
RTL_433_API int rtl_433_destroy(rtl_433_t *rtl);
RTL_433_API int start(rtl_433_t *rtl, struct sigaction *sigact);
RTL_433_API int stop_signal(rtl_433_t *rtl);
RTL_433_API int getDevCount();
RTL_433_API int getDev(int idx, r_device **dev);
RTL_433_API SdrDriverType getDriverType();

void sdr_callback(unsigned char *iq_buf, uint32_t len, void *ctx);
char *time_pos_str(rtl_433_t *rtl, unsigned samples_ago, char *buf);
void calc_rssi_snr(rtl_433_t *rtl);

//private:
static int InitSdr(rtl_433_t *rtl);
static int ReadRtlAsync(rtl_433_t *rtl, struct sigaction *sigact);

#ifdef __cplusplus
}
#endif


#endif /* RTL_433_H */
