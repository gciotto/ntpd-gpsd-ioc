/*
 * ioc_globals.h
 *
 *  Created on: 21 de out de 2016
 *      Author: gciotto
 */

#ifndef IOC_GLOBALS_H_
#define IOC_GLOBALS_H_

#include <bsmp/bsmp.h>
#include <bsmp/server.h>
#include <pthread.h>

#define SIZE_OF_ARRAY(_array) (sizeof(_array) / sizeof(_array[0]))

#define NTP_OFFSET LEAP
#define GPS_OFFSET FIX_MODE

union timestamp_u {
	unsigned long ts_as_ul;
	uint8_t ts_as_bytes[__SIZEOF_LONG__];
};

union float_u {
	float float_as_float;
	uint8_t float_as_bytes[__SIZEOF_FLOAT__];
};

union double_u {
	double double_as_double;
	uint8_t double_as_bytes[__SIZEOF_DOUBLE__];
};

union int_u {
	int int_as_int;
	uint8_t int_as_bytes[__SIZEOF_INT__];
};

enum VAR_ID {
	LEAP,
	STRATUM,
	REFID,
	OFFSET,
	JITTER,
	PRECISION,
	SRCADR,
	SERVER_NTP_VERSION,
	FREQUENCY,
	SYS_JITTER,
	CLK_WANDER,
	CLK_JITTER,
	TIMESTAMP,

	FIX_MODE,
	FIX_STATUS,
	FIX_TIMESTAMP,
	LATITUDE,
	LONGITUDE,
	ALTITUDE,
	N_SATTELITES_IN_USE,
	SATTELITES_IN_USE,

	VAR_COUNT
};

struct global_info {

	pthread_mutex_t var_mutex;

	bsmp_server_t srv;
	struct bsmp_var *bsmp_varlist;
	uint8_t bsmp_lenght;

	int err_flag;
};


#endif /* IOC_GLOBALS_H_ */
