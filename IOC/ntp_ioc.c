/*
 * Communicates with NTP server via libntpq static library.
 * The requests must follow BSMP protocol.
 * In order to compile, you need all NTP headers and follow the steps below:
 * As a suggestion, download NTP source code from the official web site. In the root directory,
 * execute:
 *
 * cd libntp
 * make libntp.a
 * cd ../ntpq
 * make libntpq.a
 * gcc -Wall -I. -I..  -I../include -I../lib/isc/include -I../lib/isc/pthreads/include -I../lib/isc/unix/include -I../sntp/libopts -o teste teste.c libntpq.a ../libntp/libntp.a -lssl -lcrypto -lpthread
 *
 * A script with all these steps is provided at the git repository ntp-gpsd-building-scripts.
 *
 * Author: Gustavo CIOTTO PINTON
 */
#include "ntp_ioc.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

#include <stdint.h>

struct global_info *global_context;
pthread_t polling_thread, sys_polling_thread;
struct ntp_global_info *ntp_global_context;

/* Variable list acquired through libntpq's ntpq_doquerylist() */
struct ntpq_var NTPQ_PEER_VARLIST[] = {
		{{ "leap",	 0 }, 1},
		{{ "stratum",0 }, 1},
		{{ "refid",	 0 }, MAX_REFID},
		{{ "offset", 0 }, __SIZEOF_FLOAT__},
		{{ "jitter", 0 }, __SIZEOF_FLOAT__},
		{{ "precision", 0 }, __SIZEOF_FLOAT__},
		{{ "dstadr",	0 }, MAX_REFID}
};

/* Variable list acquired through libntpq's ntpq_read_sysvars() */
struct ntpq_var NTPQ_SYS_VARLIST[] = {
		{{ "version",	 0 }, MAX_REFID},
		{{ "frequency", 0 }, __SIZEOF_FLOAT__},
		{{ "sys_jitter", 0 }, __SIZEOF_FLOAT__},
		{{ "clk_wander", 0 }, __SIZEOF_FLOAT__},
		{{ "clk_jitter", 0 }, __SIZEOF_FLOAT__},
};

/* Variable list acquired through syscalls */
uint8_t OTHER_VARLIST[] = {
		__SIZEOF_LONG__ 	/* Timestamp @ host */
};

/* Restarts the NTPD automatically. restart_ntp_daemon() is called in case of a leap second (-1, +1)
 * is detected. Tests revealed that NTP cannot adjust system's time with the PPS source in such
 * an event. */
void restart_ntp_daemon() {

	/* close connection with the daemon */
	ntpq_closehost();

	system("systemctl restart ntpd");

	/* re-establish connection */
	if (!ntpq_openhost("localhost", AF_INET)) {

		global_context->err_flag = errno;

		printf("Could not re-establish connection with NTPD.\n");
	}
}

/* Acquires system variables */
void* poll_sys_thread() {

	while (!global_context->err_flag) {

		union timestamp_u _t;
		_t.ts_as_ul = (unsigned long) time(NULL); /* System time @ host */

		pthread_mutex_lock(&global_context->var_mutex);

		memset(global_context->bsmp_varlist[TIMESTAMP].data, 0, __SIZEOF_LONG__);

		memcpy(global_context->bsmp_varlist[TIMESTAMP].data, _t.ts_as_bytes, __SIZEOF_LONG__);

		pthread_mutex_unlock(&global_context->var_mutex);

		printf("%lu %d\n", _t.ts_as_ul,  __SIZEOF_LONG__);

		sleep(SYS_POLL_MIN);
	}

	return NULL;
}

/* Polls NTPD via libntpq API. Two sets of variables are retrieved:
 * peer and sys variables. For the first one, we need to call ntpq_doquerylist()
 * for every association entry held by NTPD. The association used as sync source by the server
 * is then selected.  For the second set, a single call ntpq_read_sysvars() is enough.*/
void* poll_ntp_thread() {

	while (!global_context->err_flag) {

		/* rstatus holds the priority of an association held by the NTP server.*/
		u_short rstatus = 0, max_rstatus = 0;
		int max_rstatus_index = -1;

		/* Requests peer variables for every association */
		for (int w = 0; w < ntp_global_context->assoc_lenght; w++) {

			/* peer_varlist is a list with all fields which will be requested to the NTPD */
			ntpq_doquerylist(ntp_global_context->assocs[w].peer_varlist, 2,
					ntp_global_context->assocs[w].assod_id, 0,
					&rstatus,
					&ntp_global_context->assocs[w].ntpq_input_size, &ntp_global_context->assocs[w].ntpq_input_data);

			/* CTL_PEER_STATVAL(rstatus) & 0x7 is the way it's done in libntpq.c */
			ntp_global_context->assocs[w].rstatus = CTL_PEER_STATVAL(rstatus) & 0x7;

			/* Saves biggest priority and association index (currently used as sync source) */
			if (max_rstatus <= ntp_global_context->assocs[w].rstatus) {

				max_rstatus = ntp_global_context->assocs[w].rstatus;
				max_rstatus_index = w;
				printf("rstatus:%d w:%d\n",  ntp_global_context->assocs[w].rstatus, w);
			}

		}

		printf ("%d: \n", ntp_global_context->assocs[max_rstatus_index].rstatus);

		/* Updates BSMP variables using the attributes of the association used as sync source */
		char value[100];
		for (int j = 0; j < SIZE_OF_ARRAY(NTPQ_PEER_VARLIST); j++) {

			/* strstr returns the first appearance of NTPQ_PEER_VARLIST[j]._libntqp_var.name inside
			 * the result of ntpq_doquerylist() */
			char 	*_c = strstr(ntp_global_context->assocs[max_rstatus_index].ntpq_input_data,
					NTPQ_PEER_VARLIST[j]._libntqp_var.name),
							*_i = strstr(_c, "=") + 1;

			memset(value, 0, 100);

			/* Selects all characters belonging to the variable */
			for (int _index = 0; *(_i + _index) != ',' && *(_i + _index) != '\0'; _index++)
				value[_index] = *(_i + _index);

			printf ("%s %s\n", NTPQ_PEER_VARLIST[j]._libntqp_var.name, value);

			union float_u _f;
			int _j = j + NTP_OFFSET;

			pthread_mutex_lock(&global_context->var_mutex);

			uint8_t _leap;

			/* Parses value according to the variable type */
			switch (_j) {
			case LEAP:
				_leap = (uint8_t) atoi (value);
				/* Saves occurrence of a LEAP second. */
				if (_leap == 1 || _leap == 2) {
					ntp_global_context->leap_steps = LEAP_MAX_STEPS_RECOVERY;
					ntp_global_context->leap_flag = 1;
				}
			case STRATUM:
				global_context->bsmp_varlist[_j].data[0] = (uint8_t) atoi (value);
				break;
			case PRECISION:
				_f.float_as_float = powf(2, atof(value));
				memcpy(global_context->bsmp_varlist[_j].data, _f.float_as_bytes, __SIZEOF_FLOAT__);
				break;
			case OFFSET:
				/* restarts NTPD daemon if the server does not recover in less than LEAP_MAX_STEPS_RECOVERY
				 * pollings */
				if (ntp_global_context->leap_flag) {
					float _offset = atof(value);
					ntp_global_context->leap_steps--;
					if (_offset > 10 && ntp_global_context->leap_steps < 0){
						restart_ntp_daemon();
						ntp_global_context->leap_steps = LEAP_MAX_STEPS_RECOVERY;
						ntp_global_context->leap_flag = 0;
					}
				}

			case JITTER:
				_f.float_as_float = atof(value);
				memcpy(global_context->bsmp_varlist[_j].data, _f.float_as_bytes, __SIZEOF_FLOAT__);
				break;
			case SRCADR:
			case REFID:
				memset(global_context->bsmp_varlist[_j].data, 0, MAX_REFID);
				global_context->bsmp_varlist[_j].info.size = strlen(value);
				memcpy(global_context->bsmp_varlist[_j].data, value, strlen(value));
				break;
			}

			pthread_mutex_unlock(&global_context->var_mutex);
		}

		char _buffer[500];
		ntpq_read_sysvars(_buffer, 500);

		for (int j = 0; j < SIZE_OF_ARRAY(NTPQ_SYS_VARLIST); j++) {

			char 	*_c = strstr(_buffer, NTPQ_SYS_VARLIST[j]._libntqp_var.name),
					*_i = strstr(_c, "=") + 1;

			memset(value, 0, 100);

			for (int _index = 0; *(_i + _index) != ',' && *(_i + _index) != '\0'; _index++)
				value[_index] = *(_i + _index);

			printf ("%s %s\n", NTPQ_SYS_VARLIST[j]._libntqp_var.name, value);

			union float_u _f;

			int _j = j + SIZE_OF_ARRAY(NTPQ_PEER_VARLIST) + NTP_OFFSET;

			pthread_mutex_lock(&global_context->var_mutex);

			switch (_j) {
			case FREQUENCY:
			case SYS_JITTER:
			case CLK_WANDER:
			case CLK_JITTER:
				_f.float_as_float = atof(value);
				memcpy(global_context->bsmp_varlist[_j].data, _f.float_as_bytes, __SIZEOF_FLOAT__);
				break;
			case SERVER_NTP_VERSION:
				memset(global_context->bsmp_varlist[_j].data, 0, MAX_REFID);
				global_context->bsmp_varlist[_j].info.size = NTP_VERSION_LENGHT;
				memcpy(global_context->bsmp_varlist[_j].data, value + 6, NTP_VERSION_LENGHT);
				printf("data = %s\n", global_context->bsmp_varlist[_j].data);
				break;
			}

			pthread_mutex_unlock(&global_context->var_mutex);
		}

		sleep(NTPQ_POLL_MIN);
	}

	return NULL;
}



void ntp_register_global_context(struct global_info* g_pointer) {
	global_context = g_pointer;
}

/* Registers NTPD variables into the BSMP server. The order in which the variables appear
 * in NTPQ_PEER_VARLIST, NTPQ_SYS_VARLIST and OTHER_VARLIST is important because it
 * determines which BSMP ID they will receive  */
int ntp_register_bsmp_variables() {

	if (!global_context) {
		global_context->err_flag = -1;
		return -1;
	}

	uint8_t ntpq_var_size  = SIZE_OF_ARRAY(NTPQ_PEER_VARLIST),
		sys_var_size = SIZE_OF_ARRAY(NTPQ_SYS_VARLIST),
		other_var_size = SIZE_OF_ARRAY(OTHER_VARLIST);

	int _i;
	for (int i = 0; i < ntpq_var_size; i++){

		/* NTP_OFFSET is 0 */
		_i = i + NTP_OFFSET;

		global_context->bsmp_varlist[_i].info.writable = 0;
		global_context->bsmp_varlist[_i].info.size = NTPQ_PEER_VARLIST[i]._bsmp_len;
		global_context->bsmp_varlist[_i].data = (uint8_t*) malloc (NTPQ_PEER_VARLIST[i]._bsmp_len);
		memset(global_context->bsmp_varlist[_i].data, 0, global_context->bsmp_varlist[i].info.size);

		bsmp_register_variable(&global_context->srv, &global_context->bsmp_varlist[_i]);
	}

	for (int i = 0; i < sys_var_size; i++){

		/* Must follow right index for bsmp_varlist */
		_i = i + ntpq_var_size + NTP_OFFSET;

		global_context->bsmp_varlist[_i].info.writable = 0;
		global_context->bsmp_varlist[_i].info.size = NTPQ_SYS_VARLIST[i]._bsmp_len;
		global_context->bsmp_varlist[_i].data = (uint8_t*) malloc (NTPQ_SYS_VARLIST[i]._bsmp_len);
		memset(global_context->bsmp_varlist[_i].data, 0, global_context->bsmp_varlist[_i].info.size);

		bsmp_register_variable(&global_context->srv, &global_context->bsmp_varlist[_i]);
	}

	for (int i = 0; i < other_var_size; i++) {

		_i = i + ntpq_var_size + sys_var_size + NTP_OFFSET;

		global_context->bsmp_varlist[_i].info.writable = 0;
		global_context->bsmp_varlist[_i].info.size = OTHER_VARLIST[i];
		global_context->bsmp_varlist[_i].data = (uint8_t*) malloc (OTHER_VARLIST[i]);
		memset(global_context->bsmp_varlist[_i].data, 0, global_context->bsmp_varlist[_i].info.size);

		bsmp_register_variable(&global_context->srv, &global_context->bsmp_varlist[_i]);
	}

	return 0;
}

/* Frees all structures  */
void ntp_clean_context() {

	if (!ntp_global_context)
		return;

	for (int w = 0; w < ntp_global_context->assoc_lenght; w++)
		free(ntp_global_context->assocs[w].peer_varlist);

	free(ntp_global_context->sys_varlist);
	free(ntp_global_context->assocs);

	free(ntp_global_context);
}

/* Initialization function. It identifies all NTPD associations and allocates
 * memory for its structures  */
int ntp_init () {

	ntp_global_context = (struct ntp_global_info *) malloc (sizeof(struct ntp_global_info));

	if (!ntpq_openhost("localhost", AF_INET)) {

		global_context->err_flag = errno;

		printf("Could not open NTP host\n");
		return -1;
	}

	u_short assocs_ids[MAX_ASSOCS];

	/* Asks NTPD the number of associations used (or candidate) as sync sources  */
	uint8_t assoc_number = ntpq_get_assocs();

	ntp_global_context->leap_flag = 0;

	ntp_global_context->assoc_lenght = assoc_number;
	ntp_global_context->assocs = (struct association_info *) malloc (assoc_number * sizeof(struct association_info));

	/* Reads info of all associations */
	ntpq_read_associations(assocs_ids, assoc_number);
	uint8_t ntpq_var_size  = SIZE_OF_ARRAY(NTPQ_PEER_VARLIST),
		sys_var_size = SIZE_OF_ARRAY(NTPQ_SYS_VARLIST);

	/* Allocates memory for the structures describing the associations (peer variables) */
	for (int w = 0; w < assoc_number; w++) {

		ntp_global_context->assocs[w].assod_id = assocs_ids[w];
		ntp_global_context->assocs[w].peer_varlist = (struct ntpq_varlist*) malloc((ntpq_var_size + 1) * sizeof (struct ntpq_varlist));

		for (int i = 0; i < ntpq_var_size; i++){
			ntp_global_context->assocs[w].peer_varlist[i].name = NTPQ_PEER_VARLIST[i]._libntqp_var.name;
			ntp_global_context->assocs[w].peer_varlist[i].value = (char*) malloc (NTPQ_PEER_VARLIST[i]._bsmp_len);
		}

		/* NTPD query functions demand a 'NULL' entry */
		ntp_global_context->assocs[w].peer_varlist[ntpq_var_size].name = 0;
		ntp_global_context->assocs[w].peer_varlist[ntpq_var_size].value = 0;
	}

	/* Allocates structures for sys variables. They depend only on the host. */
	ntp_global_context->sys_varlist = (struct ntpq_varlist *) malloc ( (sys_var_size + 1)* sizeof(struct bsmp_var));

	for (int w = 0; w < sys_var_size; w++) {
		ntp_global_context->sys_varlist[w].name = NTPQ_SYS_VARLIST[w]._libntqp_var.name;
		ntp_global_context->sys_varlist[2].value = (char*) malloc (NTPQ_SYS_VARLIST[w]._bsmp_len);
	}

	/* NTPD query functions demand a 'NULL' entry */
	ntp_global_context->sys_varlist[sys_var_size].name = 0;
	ntp_global_context->sys_varlist[sys_var_size].value = 0;

	if (global_context->err_flag) {

		ntpq_closehost();
		printf("%s", strerror(global_context->err_flag));

		ntp_clean_context();
	}

	return global_context->err_flag;
}

void ntp_create_threads() {

	if (pthread_create(&polling_thread, NULL, poll_ntp_thread, NULL)) {
		printf ("(pthread_create) %s\n", strerror(errno));
		global_context->err_flag = errno;
	}

	if (pthread_create(&sys_polling_thread, NULL, poll_sys_thread, NULL)) {
		printf ("(pthread_create) %s\n", strerror(errno));
		global_context->err_flag = errno;
	}

}

void ntp_join_threads() {

	pthread_join(polling_thread, NULL);
	pthread_join(sys_polling_thread, NULL);
}
