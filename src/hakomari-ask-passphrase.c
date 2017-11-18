#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "hakomari-cfg.h"
#include "hakomari-rpc.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)

static const char*
env(const char* name, const char* default_value)
{
	const char* value = getenv(name);
	return value != NULL ? value : default_value;
}

int
main()
{
	int exit_code = EXIT_SUCCESS;

	hakomari_rpc_client_t rpc;
	hakomari_rpc_init_client(&rpc);

	if(hakomari_rpc_start_client(&rpc, env("HAKOMARI_VAULTD_SOCK", "/run/vault")) != 0)
	{
		fprintf(stderr, "Could not connect to rpc server: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	hakomari_rpc_req_t* req = hakomari_rpc_begin_req(&rpc, "ask-passphrase", 1);
	if(req == NULL)
	{
		fprintf(stderr, "Error starting request: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	const char* endpoint = env("HAKOMARI_ENDPOINT", "");
	if(!cmp_write_str(req->cmp, endpoint, strlen(endpoint)))
	{
		fprintf(stderr, "Error sending request: %s\n", cmp_strerror(req->cmp));
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
		cmp_object_t obj;
		if(!cmp_read_object(rep->cmp, &obj))
		{
			fprintf(stderr, "Error receiving reply: %s\n", cmp_strerror(req->cmp));
			quit(EXIT_FAILURE);
		}

		switch(obj.type)
		{
			case CMP_TYPE_NIL:
				quit(HAKOMARI_EXIT_CODE_OFFSET + HAKOMARI_ERR_AUTH_REQUIRED);
				break;
			case CMP_TYPE_STR8:
			case CMP_TYPE_STR16:
			case CMP_TYPE_STR32:
			case CMP_TYPE_FIXSTR:
				{
					hakomari_rpc_string_t passphrase;
					if(obj.as.str_size > sizeof(passphrase) - 1)
					{
						fprintf(stderr, "Format error");
						quit(EXIT_FAILURE);
					}

					struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
					if(read(io->fd, passphrase, obj.as.str_size) != (ssize_t)obj.as.str_size)
					{
						fprintf(stderr, "Error reading reply: %s\n", strerror(errno));
						quit(EXIT_FAILURE);
					}

					passphrase[obj.as.str_size] = '\0';
					fprintf(stdout, "%s\n", passphrase);
				}
				break;
			default:
				fprintf(stderr, "Format error: %d\n", obj.type);
				quit(EXIT_FAILURE);
				break;
		}
	}
	else
	{
		hakomari_rpc_string_t str;
		uint32_t size = sizeof(str);
		if(!cmp_read_str(rep->cmp, str, &size))
		{
			fprintf(stderr, "Error reading error: %s\n", cmp_strerror(rep->cmp));
			quit(EXIT_FAILURE);
		}

		fprintf(stderr, "%s\n", str);
		quit(EXIT_FAILURE);
	}

quit:
	hakomari_rpc_stop_client(&rpc);
	return exit_code;
}
