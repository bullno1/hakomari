#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ancillary.h>
#include "hakomari-cfg.h"
#include "hakomari-rpc.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)
#define HAKOMARI_INPUT_FILENO 3
#define HAKOMARI_OUTPUT_FILENO 4

static char* script_env[] = {
	"HAKOMARI_OK=64",
	"HAKOMARI_ERR_INVALID=65",
	"HAKOMARI_ERR_MEMORY=66",
	"HAKOMARI_ERR_AUTH_REQUIRED=67",
	"HAKOMARI_ERR_DENIED=68",
	"HAKOMARI_ERR_IO=69",
	"HAKOMARI_INPUT=/proc/self/fd/3",
	"HAKOMARI_OUTPUT=/proc/self/fd/4",
	NULL
};

static int
exec_script(
	const char* script_dir, const char* script, unsigned int num_args, char* args[],
	const char* working_dir, int input_fd, int output_fd
)
{
	fprintf(stdout, "Executing %s/%s", script_dir, script);
	for(unsigned int i = 0; i < num_args; ++i)
	{
		fprintf(stdout, " '%s'", args[i]);
	}
	fprintf(stdout, "\n");

	char argv0[PATH_MAX];

	int ret = snprintf(argv0, PATH_MAX, "%s/%s", script_dir, script);
	if(ret < 0 || ret >= PATH_MAX) { return -1; }

	char* argv[num_args + 2];
	argv[0] = argv0;
	memcpy(&argv[1], args, sizeof(char*) * num_args);
	argv[num_args + 1] = NULL;

	pid_t pid = vfork();

	if(pid < 0)
	{
		fprintf(stderr, "vfork() failed: %s\n", strerror(errno));
		return pid;
	}

	if(pid == 0) // child
	{
		if(dup2(input_fd, HAKOMARI_INPUT_FILENO) < 0)
		{
			_exit(EXIT_FAILURE);
		}

		if(dup2(output_fd, HAKOMARI_OUTPUT_FILENO) < 0)
		{
			_exit(EXIT_FAILURE);
		}

		if(chdir(working_dir) < 0)
		{
			_exit(EXIT_FAILURE);
		}

		if(execve(argv0, argv, script_env) < 0)
		{
			_exit(EXIT_FAILURE);
		}
	}
	else // parent
	{
		int status;
		if(waitpid(pid, &status, 0) < 0)
		{
			fprintf(stderr, "waitpid() failed: %s\n", strerror(errno));
			return -1;
		}

		return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
	}
}

static void
handle_script_result(int ret, hakomari_rpc_req_t* req)
{
	if(ret < 0)
	{
		fprintf(stderr, "Error running script: %s\n", strerror(errno));
		hakomari_rpc_reply_error(req, "exec-error");
	}

	if(hakomari_rpc_begin_result(req) != 0)
	{
		fprintf(stderr, "Error sending reply: %s\n", hakomari_rpc_strerror(req->cmp));
		return;
	}

	if(ret != 0 && ret < HAKOMARI_EXIT_CODE_OFFSET)
	{
		ret = HAKOMARI_ERR_IO;
	}
	else if(ret >= HAKOMARI_EXIT_CODE_OFFSET)
	{
		ret -= HAKOMARI_EXIT_CODE_OFFSET;
	}

	if(!cmp_write_u8(req->cmp, ret))
	{
		fprintf(stderr, "Error sending reply: %s\n", hakomari_rpc_strerror(req->cmp));
		return;
	}

	if(hakomari_rpc_end_result(req) != 0)
	{
		fprintf(stderr, "Error sending reply: %s\n", hakomari_rpc_strerror(req->cmp));
		return;
	}
}

int
main(int argc, const char* argv[])
{
	int exit_code = EXIT_SUCCESS;
	int devnull = -1;
	hakomari_rpc_server_t rpc;
	hakomari_rpc_init_server(&rpc);

	if(argc != 5)
	{
		fprintf(stderr, "Usage: hakomari-endpointd <sock-path> <lock-path> <script-dir> <data-dir>\n");
		quit(EXIT_FAILURE);
	}

	fprintf(stdout, "Starting RPC server\n");
	if(hakomari_rpc_start_server(&rpc, argv[1], argv[2]) != 0)
	{
		fprintf(stderr, "Error starting RPC server: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	char script_dir[PATH_MAX];
	if(realpath(argv[3], script_dir) == NULL)
	{
		fprintf(stderr, "Could not resolve script dir: %s\n", strerror(errno));
		quit(EXIT_FAILURE);
	}

	char data_dir[PATH_MAX];
	if(realpath(argv[4], data_dir) == NULL)
	{
		fprintf(stderr, "Could not resolve data dir: %s\n", strerror(errno));
		quit(EXIT_FAILURE);
	}

	devnull = open("/dev/null", O_RDWR);
	if(devnull < 0)
	{
		fprintf(stderr, "Could not open /dev/null: %s\n", strerror(errno));
		quit(EXIT_FAILURE);
	}

	while(true)
	{
		hakomari_rpc_req_t* req = hakomari_rpc_next_req(&rpc);
		if(req == NULL)
		{
			if(rpc.stopping)
			{
				fprintf(stdout, "Shutting down\n");
				quit(EXIT_SUCCESS);
			}
			else
			{
				fprintf(stderr, "Error getting next request: %s\n", hakomari_rpc_strerror(&rpc));
				quit(EXIT_FAILURE);
			}
		}

		uint32_t size;
		hakomari_rpc_string_t name, query;
		memset(name, 0, sizeof(name));
		memset(query, 0, sizeof(query));

		char* args[] = { name, query };

		if(req->num_args >= 1)
		{
			size = sizeof(name);
			if(!cmp_read_str(req->cmp, name, &size))
			{
				fprintf(stderr, "Error reading request: %s\n", cmp_strerror(req->cmp));
				continue;
			}
		}

		if(req->num_args >= 2)
		{
			size = sizeof(query);
			if(!cmp_read_str(req->cmp, query, &size))
			{
				fprintf(stderr, "Error reading request: %s\n", cmp_strerror(req->cmp));
				continue;
			}
		}

		if(true
			&& (strcmp(req->method, "create") == 0 || strcmp(req->method, "destroy") == 0)
			&& req->num_args == 1
		)
		{
			int ret = exec_script(
				script_dir, req->method, 1, args,
				data_dir,
				devnull, devnull
			);

			handle_script_result(ret, req);
		}
		else if(strcmp(req->method, "query") == 0 && req->num_args == 2)
		{
			struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
			int fds[2] = { -1, -1 };
			int num_fds;
			if((num_fds = ancil_recv_fds(io->fd, fds, 2)) < 0)
			{
				fprintf(stderr, "Error receiving fd: %s\n", strerror(errno));
				continue;
			}

			if(num_fds != 2)
			{
				close(fds[0]);
				close(fds[1]);

				fprintf(stderr, "Wrong number of fds received\n");
				continue;
			}

			int ret = exec_script(
				script_dir, "query", 2, args,
				data_dir,
				fds[0], fds[1]
			);

			close(fds[0]);
			close(fds[1]);

			handle_script_result(ret, req);
		}
		else if(strcmp(req->method, "enumerate") == 0 && req->num_args == 0)
		{
			struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
			int output_fd;
			if(ancil_recv_fd(io->fd, &output_fd) < 0)
			{
				fprintf(stderr, "Error receiving fd: %s\n", strerror(errno));
				continue;
			}

			int ret = exec_script(
				script_dir, "enumerate", 0, args,
				data_dir,
				devnull, output_fd
			);

			close(output_fd);

			handle_script_result(ret, req);
		}
		else
		{
			hakomari_rpc_reply_error(req, "invalid-method");
		}
	}

quit:
	if(devnull >= 0) { close(devnull); }
	hakomari_rpc_stop_server(&rpc);
	return exit_code;
}
