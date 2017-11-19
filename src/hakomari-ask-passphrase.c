#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <linux/limits.h>
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
main(int argc, char* argv[])
{
	int exit_code = EXIT_SUCCESS;
	bool need_confirmation = false;
	int memfd = -1;

	hakomari_rpc_client_t rpc;
	hakomari_rpc_init_client(&rpc);

	char* const* exec_argv = &argv[1];
	if(argc >= 2 && strcmp(argv[1], "-c") == 0)
	{
		need_confirmation = true;
		++exec_argv;
	}

	if(hakomari_rpc_start_client(&rpc, env("HAKOMARI_VAULTD_SOCK", "/run/vault")) != 0)
	{
		fprintf(stderr, "Could not connect to rpc server: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	hakomari_rpc_req_t* req = hakomari_rpc_begin_req(&rpc, "ask-passphrase", 2);
	if(req == NULL)
	{
		fprintf(stderr, "Error starting request: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	const char* endpoint = env("HAKOMARI_ENDPOINT", "");
	if(false
		|| !cmp_write_str(req->cmp, endpoint, strlen(endpoint))
		|| !cmp_write_bool(req->cmp, need_confirmation)
	)
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
		uint32_t array_size;
		uint8_t status_code;
		hakomari_rpc_string_t result_string;
		uint32_t str_len = sizeof(result_string);

		if(false
			|| !cmp_read_array(rep->cmp, &array_size)
			|| array_size != 2
			|| !cmp_read_u8(rep->cmp, &status_code)
			|| !cmp_read_str(rep->cmp, result_string, &str_len)
		)
		{
			fprintf(stderr, "Error reading reply: %s\n", hakomari_rpc_strerror(&rpc));
			quit(EXIT_FAILURE);
		}

		if(status_code == HAKOMARI_OK)
		{
			if(exec_argv[0] == NULL)
			{
				fprintf(stdout, "%s\n", result_string);
				quit(EXIT_SUCCESS);
			}
			else
			{
				hakomari_rpc_stop_client(&rpc);

				memfd = syscall(__NR_memfd_create, "passphrase-file", 0);
				if(memfd < 0)
				{
					fprintf(stdout, "Could not create memfd: %s\n", strerror(errno));
					quit(EXIT_FAILURE);
				}

				if(write(memfd, result_string, str_len) != (ssize_t)str_len)
				{
					fprintf(stdout, "Could not write to memfd: %s\n", strerror(errno));
					quit(EXIT_FAILURE);
				}

				char passphrase_file_path[PATH_MAX];
				int ret = snprintf(
					passphrase_file_path, PATH_MAX,
					"/proc/self/fd/%d", memfd
				);
				if(ret < 0 || ret >= PATH_MAX) { quit(EXIT_FAILURE); }

				if(setenv("HAKOMARI_PASSPHRASE_FILE", passphrase_file_path, 1) < 0)
				{
					fprintf(stdout, "Could not setenv: %s\n", strerror(errno));
					quit(EXIT_FAILURE);
				}

				if(execvpe(exec_argv[0], exec_argv, environ) < 0)
				{
					fprintf(stdout, "execve() failed: %s\n", strerror(errno));
					quit(EXIT_FAILURE);
				}
				// No return
			}
		}
		else
		{
			fprintf(stderr, "%s\n", result_string);
			quit(HAKOMARI_EXIT_CODE_OFFSET + status_code);
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
	if(memfd >= 0) { close(memfd); }
	hakomari_rpc_stop_client(&rpc);
	return exit_code;
}
