#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hakomari-cfg.h"
#include "hakomari-rpc.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)

int
main(int argc, const char* argv[])
{
	(void)argc;
	int exit_code = EXIT_SUCCESS;

	hakomari_rpc_client_t rpc;
	hakomari_rpc_init_client(&rpc);

	if(argc != 2)
	{
		fprintf(stderr, "Usage: hakomari-show <message>\n");
		quit(EXIT_FAILURE);
	}

	if(hakomari_rpc_start_client(&rpc, HAKOMARI_DISPLAYD_SOCK_PATH) != 0)
	{
		fprintf(stderr, "Could not connect to rpc server: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	hakomari_rpc_req_t* req = hakomari_rpc_begin_req(&rpc, "show", 1);
	if(req == NULL)
	{
		fprintf(stderr, "Error starting request: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	if(!cmp_write_str(req->cmp, argv[1], strlen(argv[1])))
	{
		fprintf(stderr, "Error while making request: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	hakomari_rpc_rep_t* rep = hakomari_rpc_end_req(req);
	if(rep == NULL)
	{
		fprintf(stderr, "Error while waiting for reply: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	if(rep->success)
	{
		cmp_read_nil(rep->cmp);
		quit(EXIT_SUCCESS);
	}
	else
	{
		hakomari_rpc_string_t str;
		uint32_t size = sizeof(str);
		if(!cmp_read_str(rep->cmp, str, &size))
		{
			fprintf(stderr, "Error reading error: %s\n", hakomari_rpc_strerror(&rpc));
			quit(EXIT_FAILURE);
		}

		fprintf(stderr, "%s\n", str);
		quit(EXIT_FAILURE);
	}

quit:
	hakomari_rpc_stop_client(&rpc);
	return exit_code;
}
