/*
 *  The purpose of this program is to serve as a bridge between Stream IOC and GPSD/NTPD daemons.
 *  This file contains two functions:
 *		(i) main(), responsible to initialize all threads (ntpd and gpsd poolers).
 *		(ii) the thread unix_networking_thread, which handles incoming requests and forwards
 *			 demanded fields.
 *
 *	In order to build everything up, use the building scripts in ntp-gps-building-scripts.
 *  Created on: 21 de out de 2016
 *  Author: Gustavo Ciotto Pinton
 */

#include "ntp_ioc.h"
#include "gps_ioc.h"
#include "ioc_globals.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

/* Establishes a new connection via an Unix socket */
void* unix_networking_thread(void* _global_context) {

	struct global_info *global_context = (struct global_info *) _global_context;

	int unix_socket, unix_ioc = 0;

	/* Creates UNIX socket */
	if ((unix_socket = socket(AF_UNIX, SOCK_STREAM , 0)) < 0) {
		global_context->err_flag = errno;
		goto exit;
	}

	struct sockaddr_un unix_addr, unix_remote;
	unix_addr.sun_family = AF_UNIX;
	strcpy(unix_addr.sun_path, "/tmp/ntp.socket");

	/* If socket already exists, unlinks it */
	if (unlink(unix_addr.sun_path) < -1) {

		global_context->err_flag = errno;
		goto exit;
	}

	/* Binds socket to specified path */
	if (bind(unix_socket, (struct sockaddr *) &unix_addr, sizeof(unix_addr)) < 0) {

		global_context->err_flag = errno;
		goto exit;
	}

	/* Waits for a connection */
	if (listen(unix_socket, 1) < 0) {
		global_context->err_flag = errno;
		goto exit;
	}

	/* Loops until any thread reports an error */
	while (!global_context->err_flag) {

		socklen_t ioc_len;
		/* Accepts a new connection request */
		unix_ioc = accept(unix_socket, (struct sockaddr *) &unix_remote, &ioc_len);

		if (unix_ioc < 0) {
			global_context->err_flag = errno;
			break;
		}

		struct bsmp_raw_packet request, response;
		request.data = (uint8_t*) malloc(128 * sizeof(uint8_t));
		response.data = (uint8_t*) malloc(128 * sizeof(uint8_t));

		memset(request.data, 0, 128);
		memset(response.data, 0, 128);

		/* Receives an request in BSMP format */
		while ((request.len = recv(unix_ioc, request.data , 128 , 0)) > 0) {

			printf("%d\n", request.len);

			pthread_mutex_lock(&global_context->var_mutex);

			bsmp_process_packet(&global_context->srv, &request, &response);

			pthread_mutex_unlock(&global_context->var_mutex);

			/* Forwards requested data */
			if (write(unix_ioc, response.data, response.len) < 0) {
				printf ("%s", strerror(errno));
			}

			memset(request.data, 0, 128);
			memset(response.data, 0, 128);
		}

		free(request.data);
		free(response.data);

		if (request.len < 0) {
			global_context->err_flag = errno;
			break;
		}

	}


	/* An error occured */
	exit:
	printf ("%s", strerror(errno));

	close(unix_ioc);
	close(unix_socket);

	return NULL;
}

/* Initializes and deploys all threads */
int main (int argc, char** argv) {

	struct global_info global_context;

	/* Initializes global context */
	pthread_mutex_init(&global_context.var_mutex, NULL);
	global_context.err_flag = 0;
	global_context.bsmp_lenght = VAR_COUNT;
	global_context.bsmp_varlist = (struct bsmp_var *) malloc (global_context.bsmp_lenght* sizeof(struct bsmp_var));
	bsmp_server_init(&global_context.srv);

	/* Initializes ntpd polling threads */
	ntp_register_global_context(&global_context);
	ntp_init();
	ntp_register_bsmp_variables();
	ntp_create_threads();

	/* Initializes gpsd polling threads */
	gps_register_global_context(&global_context);
	gps_init();
	gps_register_bsmp_variables();
	gps_create_threads();

	pthread_t unix_socket_mgmt_thread;
	if (pthread_create(&unix_socket_mgmt_thread, NULL, unix_networking_thread, (void*) &global_context)) {
		printf ("(pthread_create) %s\n", strerror(errno));
		global_context.err_flag = errno;
	}

	/* Starts all threads */
	pthread_join(unix_socket_mgmt_thread, NULL);
	ntp_join_threads();
	gps_join_threads();

	pthread_mutex_destroy(&global_context.var_mutex);

	return 0;
}
