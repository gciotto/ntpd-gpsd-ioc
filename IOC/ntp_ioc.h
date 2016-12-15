/*
 * ntp_ioc.h
 *
 *  Created on: 21 de out de 2016
 *      Author: gciotto
 */

#ifndef NTP_IOC_H_
#define NTP_IOC_H_

/* NTP header includes */
#include "config.h"
#include "ntp_config.h"
#include "libntpq.h"
#include "ntp_control.h"

#include "ioc_globals.h"

#define MAX_ASSOCS 5
#define NTPQ_POLL_MIN 4
#define SYS_POLL_MIN 1
#define MAX_REFID 15
#define NTP_VERSION_LENGHT 7

struct association_info {
	u_short assod_id;
	struct ntpq_varlist *peer_varlist;
	const char* ntpq_input_data;
	size_t ntpq_input_size;
	u_short rstatus;
};

struct ntp_global_info {
	struct association_info *assocs;
	struct ntpq_varlist *sys_varlist;
	uint8_t assoc_lenght;
};

struct ntpq_var {
	struct ntpq_varlist _libntqp_var;
	uint8_t _bsmp_len;
};

int ntp_init();
void ntp_register_global_context(struct global_info* g_pointer);
int ntp_register_bsmp_variables();
void ntp_clean_context();
void ntp_join_threads();
void ntp_create_threads();

#endif /* NTP_IOC_H_ */
