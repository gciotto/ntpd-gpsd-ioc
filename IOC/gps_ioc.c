#include "gps_ioc.h"

#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

struct gps_global_info {
	struct gps_data_t gpsdata;
};

struct global_info* global_context;
struct gps_global_info *gps_global_context;
pthread_t gps_poll_thread;

struct gps_var GPS_VARLIST [] = {
		{FIX_MODE, 1},
		{FIX_STATUS, 1},
		{FIX_TIMESTAMP, __SIZEOF_LONG__},
		{LATITUDE, __SIZEOF_DOUBLE__},
		{LONGITUDE, __SIZEOF_DOUBLE__},
		{ALTITUDE, __SIZEOF_DOUBLE__},
		{N_SATTELITES_IN_USE, __SIZEOF_INT__},
		{SATTELITES_IN_USE, MAXCHANNELS * __SIZEOF_INT__}
};

void gps_register_global_context(struct global_info* _global_pointer) {
	global_context = _global_pointer;
}

int gps_init () {

	printf("GPS INIT\n");

	gps_global_context = (struct gps_global_info *) malloc (sizeof(struct gps_global_info));

	if ( (global_context->err_flag = gps_open("localhost", DEFAULT_GPSD_PORT, &gps_global_context->gpsdata))) {

		printf("GPS INIT FAILED %s\n", gps_errstr(global_context->err_flag));
		return global_context->err_flag;
	}

	gps_stream(&gps_global_context->gpsdata, WATCH_ENABLE, NULL);

	return 0;

}

int gps_register_bsmp_variables() {

	if (!global_context) {
		global_context->err_flag = -1;
		return -1;
	}

	uint8_t gpsd_var_size  = SIZE_OF_ARRAY(GPS_VARLIST);
	int _w;

	for (int w = 0; w < gpsd_var_size; w++) {
		_w = w + GPS_OFFSET;

		global_context->bsmp_varlist[_w].info.writable = 0;
		global_context->bsmp_varlist[_w].info.size = GPS_VARLIST[w].size;
		global_context->bsmp_varlist[_w].data = (uint8_t*) malloc (GPS_VARLIST[w].size);

		memset(global_context->bsmp_varlist[_w].data, 0, global_context->bsmp_varlist[_w].info.size);

		bsmp_register_variable(&global_context->srv, &global_context->bsmp_varlist[_w]);
	}

	return 0;
}

void* poll_gps_thread (void* n) {

	printf("Entrou GPS Thread\n");

	while (!global_context->err_flag) {

		if (!gps_waiting(&gps_global_context->gpsdata, GPS_MAX_WAITING_TIME)) {
			global_context->err_flag = -1;
			printf("(gps_waiting) %s", strerror(errno));
		}

		else {

			gps_read(&gps_global_context->gpsdata);

			global_context->bsmp_varlist[FIX_STATUS].data[0] = (uint8_t) gps_global_context->gpsdata.status;

			if (gps_global_context->gpsdata.status) {

				global_context->bsmp_varlist[FIX_MODE].data[0] = (uint8_t) gps_global_context->gpsdata.fix.mode;

				union double_u _d;
				union int_u _i;

				union timestamp_u _t;
		                _t.ts_as_ul = (unsigned long) gps_global_context->gpsdata.fix.time;
				memcpy (global_context->bsmp_varlist[FIX_TIMESTAMP].data, _t.ts_as_bytes, __SIZEOF_LONG__);

//				printf("Timestamp: %lu\n", _t.ts_as_ul);

				_d.double_as_double = gps_global_context->gpsdata.fix.altitude;
				memcpy (global_context->bsmp_varlist[ALTITUDE].data, _d.double_as_bytes, __SIZEOF_DOUBLE__);

//				printf("Altitude: %f\n", _d.double_as_double);

				_d.double_as_double = gps_global_context->gpsdata.fix.latitude;
				memcpy (global_context->bsmp_varlist[LATITUDE].data, _d.double_as_bytes, __SIZEOF_DOUBLE__);

				_d.double_as_double = gps_global_context->gpsdata.fix.longitude;
				memcpy (global_context->bsmp_varlist[LONGITUDE].data, _d.double_as_bytes, __SIZEOF_DOUBLE__);

				_i.int_as_int = gps_global_context->gpsdata.satellites_used;
				memcpy (global_context->bsmp_varlist[N_SATTELITES_IN_USE].data, _i.int_as_bytes, __SIZEOF_INT__);

				memset (global_context->bsmp_varlist[SATTELITES_IN_USE].data, 0, MAXCHANNELS * __SIZEOF_INT__);

				for (int i = 0; i < gps_global_context->gpsdata.satellites_used; i++) {
					_i.int_as_int = gps_global_context->gpsdata.used[i];
					memcpy(global_context->bsmp_varlist[SATTELITES_IN_USE].data + i *__SIZEOF_INT__, _i.int_as_bytes,
							__SIZEOF_INT__);

					printf("Satellite no %d = %d\n", i, _i.int_as_int);
				}

//				printf("Sattelites: %s\n", global_context->bsmp_varlist[SATTELITES_IN_USE].data);

			}

		}

//		sleep(GPSD_POLL_MIN);
	}

	gps_stream(&gps_global_context->gpsdata, WATCH_DISABLE, NULL);
	gps_close(&gps_global_context->gpsdata);

	return NULL;
}

void gps_join_threads() {

	pthread_join(gps_poll_thread, NULL);
}

void gps_create_threads() {

	if (pthread_create(&gps_poll_thread, NULL, poll_gps_thread, NULL)) {
		printf ("(pthread_create) %s\n", strerror(errno));
		global_context->err_flag = errno;
	}

	printf("OK");

}
