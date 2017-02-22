/*
 * Header file for GPSD polling threads.
 *
 * Created on: 21 de out de 2016
 * Author: Gustavo Ciotto Pinton
 */

#ifndef GPS_IOC_H_
#define GPS_IOC_H_

#include <gps.h>
#include <stdlib.h>
#include "ioc_globals.h"

#define GPS_MAX_WAITING_TIME 5e6 /* in microseconds */
#define GPSD_POLL_MIN 1

struct gps_var {
	int id;
	uint16_t size;
};


void gps_register_global_context(struct global_info* _global_pointer);
int gps_init ();
int gps_register_bsmp_variables();
void gps_create_threads();
void gps_join_threads();

#endif /* GPS_IOC_H_ */
