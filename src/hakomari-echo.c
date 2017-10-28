#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hakomari-rpc.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)

int
main(int argc, char* argv[])
{
	int exit_code = EXIT_SUCCESS;
	hakomari_rpc_client_t rpc;
	hakomari_rpc_init_client(&rpc);

	if(argc < 3)
	{
		fprintf(stderr, "Usage: hakomari-echo <sock-path> <method> [args]\n");
		quit(EXIT_FAILURE);
	}

	if(hakomari_rpc_start_client(&rpc, argv[1]) != 0)
	{
		fprintf(stderr, "Could not connect to rpc server: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	hakomari_rpc_req_t* req = hakomari_rpc_begin_req(&rpc, argv[2], argc - 3);
	if(req == NULL)
	{
		fprintf(stderr, "Error starting request: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	for(int i = 3; i < argc; ++i)
	{
		if(!cmp_write_str(req->cmp, argv[i], strlen(argv[i])))
		{
			fprintf(stderr, "Error sending request: %s\n", hakomari_rpc_strerror(&rpc));
			quit(EXIT_FAILURE);
		}
	}

	hakomari_rpc_rep_t* rep = hakomari_rpc_end_req(req);
	if(rep == NULL)
	{
		fprintf(stderr, "Error while waiting for reply: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	hakomari_rpc_string_t str;
	uint32_t size = sizeof(str);
	if(!cmp_read_str(rep->cmp, str, &size))
	{
		fprintf(stderr, "Error reading result: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	fprintf(rep->success ? stdout : stderr, "%s\n", str);
	quit(rep->success ? EXIT_SUCCESS : EXIT_FAILURE);

quit:
	hakomari_rpc_stop_client(&rpc);

	return exit_code;
}
