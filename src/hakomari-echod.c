#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hakomari-rpc.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)

int
main(int argc, const char* argv[])
{
	int exit_code = EXIT_SUCCESS;
	hakomari_rpc_server_t rpc;
	hakomari_rpc_init_server(&rpc);

	if(argc != 3)
	{
		fprintf(stderr, "Usage: hakomari-echod <sock-path> <lock-path>\n");
		quit(EXIT_FAILURE);
	}

	fprintf(stdout, "Starting server\n");
	if(hakomari_rpc_start_server(&rpc, argv[1], argv[2]) != 0)
	{
		fprintf(stderr, "Could not start server: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	while(true)
	{
		fprintf(stdout, "Waiting for request\n");
		hakomari_rpc_req_t* req = hakomari_rpc_next_req(&rpc);
		if(req == NULL)
		{
			if(rpc.stopping)
			{
				quit(EXIT_SUCCESS);
			}
			else
			{
				fprintf(stderr, "Error getting next request: %s\n", hakomari_rpc_strerror(&rpc));
				quit(EXIT_FAILURE);
			}
		}

		fprintf(stdout, "Got request\n");

		if(strcmp(req->method, "echo") == 0 && req->num_args == 1)
		{
			hakomari_rpc_string_t arg;
			uint32_t size = sizeof(arg);
			if(!cmp_read_str(req->cmp, arg, &size))
			{
				fprintf(stderr, "Error reading argument: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}

			if(hakomari_rpc_begin_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}

			if(!cmp_write_str(req->cmp, arg, strlen(arg)))
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}

			if(hakomari_rpc_end_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}
		}
		else
		{
			hakomari_rpc_reply_error(req, "invalid-method");
		}
	}

quit:
	hakomari_rpc_stop_server(&rpc);

	return exit_code;
}
