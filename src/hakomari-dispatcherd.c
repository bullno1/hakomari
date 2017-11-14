#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <c-periphery/serial.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <cmp.h>
#include <ancillary.h>
#include "hakomari-cfg.h"
#include "hakomari-rpc.h"
#define SLIPPER_API static
#define SLIPPER_IMPLEMENTATION
#include "slipper.h"

#ifndef SERIAL_TIMEOUT
#define SERIAL_TIMEOUT 10000
#endif

#ifndef SERIAL_IO_BUF_SIZE
#define SERIAL_IO_BUF_SIZE 1024
#endif

#ifndef MAX_NUM_ENDPOINTS
#define MAX_NUM_ENDPOINTS 512
#endif

#define quit(value) do { result = value; goto quit; } while(0)

typedef char hakomari_string_t[128];

typedef enum hakomari_frame_type_e
{
	HAKOMARI_FRAME_REQ = 0,
	HAKOMARI_FRAME_REP,
} hakomari_frame_type_t;

struct hakomari_endpoint_s
{
	hakomari_string_t type;
	hakomari_string_t name;
};

struct libhakomari_req_ctx_s
{
	cmp_ctx_t* cmp;
	slipper_ctx_t* slipper;
	hakomari_rpc_client_t* rpc;
	uint32_t txid;
};

static slipper_error_t
slipper_serial_read(
	void* userdata, void* data, size_t* size, slipper_timeout_t timeout
)
{
	unsigned int num_bytes_avail;
	if(serial_input_waiting(userdata, &num_bytes_avail) != 0)
	{
		return SLIPPER_ERR_IO;
	}

	if(true
		&& num_bytes_avail == 0
		&& !serial_poll(userdata, timeout == SLIPPER_INFINITY ? -1 : timeout)
	)
	{
		return SLIPPER_ERR_TIMED_OUT;
	}

	int num_bytes_read = serial_read(userdata, data, *size, 0);

	if(num_bytes_read < 0)
	{
		return SLIPPER_ERR_IO;
	}
	else if(num_bytes_read == 0)
	{
		return SLIPPER_ERR_TIMED_OUT;
	}
	else
	{
		*size = num_bytes_read;
		return SLIPPER_OK;
	}
}

static slipper_error_t
slipper_serial_write(
	void* userdata,
	const void* data, size_t size,
	bool flush, slipper_timeout_t timeout
)
{
	(void)timeout;

	if(serial_write(userdata, data, size) <= 0)
	{
		return SLIPPER_ERR_IO;
	}

	if(flush && serial_flush(userdata) != 0)
	{
		return SLIPPER_ERR_IO;
	}

	return SLIPPER_OK;
}

static bool
cmp_slipper_read(cmp_ctx_t* ctx, void* data, size_t limit)
{
	size_t size = limit;
	if(slipper_read(ctx->buf, data, &size, SERIAL_TIMEOUT) != 0)
	{
		return false;
	}

	return size == limit;
}

static size_t
cmp_slipper_write(cmp_ctx_t* ctx, const void *data, size_t count)
{
	if(slipper_write(ctx->buf, data, count, SERIAL_TIMEOUT) != 0)
	{
		return 0;
	}

	return count;
}

static bool
begin_reply(struct libhakomari_req_ctx_s* req, hakomari_error_t result)
{
	slipper_error_t error;
	if((error = slipper_begin_write(req->slipper, SERIAL_TIMEOUT)) != 0)
	{
		fprintf(stderr, "Error sending reply: %s\n", slipper_errorstr(error));
		return false;
	}

	if(false
		|| !cmp_write_array(req->cmp, 3)
		|| !cmp_write_u8(req->cmp, HAKOMARI_FRAME_REP)
		|| !cmp_write_u32(req->cmp, req->txid)
		|| !cmp_write_u8(req->cmp, result)
	)
	{
		fprintf(stderr, "Error sending reply: %s\n", cmp_strerror(req->cmp));
		return false;
	}

	return true;
}

static bool
end_reply(struct libhakomari_req_ctx_s* req)
{
	slipper_error_t error;
	if((error = slipper_end_write(req->slipper, SERIAL_TIMEOUT)) != 0)
	{
		fprintf(stderr, "Error sending reply: %s\n", slipper_errorstr(error));
		return false;
	}

	return true;
}

static bool
reply_with_error(struct libhakomari_req_ctx_s* req, hakomari_error_t error)
{
	if(!begin_reply(req, error)) { return false; }
	if(!end_reply(req)) { return false; }

	return true;
}

static int
memfd_create(const char* name, unsigned int flags)
{
	int memfd = syscall(__NR_memfd_create, name, flags);
	if(memfd < 0)
	{
		fprintf(stderr, "Error creating memfd: %s\n", strerror(errno));
		return memfd;
	}

	fprintf(stdout, "Created memfd: %d\n", memfd);

	return memfd;
}

static hakomari_rpc_req_t*
begin_rpc(
	hakomari_rpc_client_t* rpc,
	const char* sock_dir,
	const char* type, const char* name,
	const char* method, unsigned int num_args
)
{
	hakomari_rpc_stop_client(rpc);

	hakomari_string_t path;
	int n = snprintf(path, sizeof(path), "%s/%s/%s", sock_dir, name, type);
	if(n < 0 || (unsigned int)n > sizeof(path)) { return NULL; }

	if(hakomari_rpc_start_client(rpc, path) != 0)
	{
		fprintf(
			stderr, "Error connecting to %s: %s\n",
			path, hakomari_rpc_strerror(rpc)
		);
		return NULL;
	}

	hakomari_rpc_req_t* req = hakomari_rpc_begin_req(rpc, method, num_args);
	if(req == NULL)
	{
		fprintf(stderr, "Error sending request: %s\n", hakomari_rpc_strerror(rpc));
		return NULL;
	}

	return req;
}

static hakomari_rpc_rep_t*
end_rpc(hakomari_rpc_client_t* rpc, hakomari_rpc_req_t* req)
{
	hakomari_rpc_rep_t* rep = hakomari_rpc_end_req(req);
	if(rep == NULL)
	{
		fprintf(stderr, "Error waiting for reply: %s\n", hakomari_rpc_strerror(rpc));
		return NULL;
	}

	uint32_t size;
	if(!rep->success)
	{
		hakomari_string_t error;
		size = sizeof(error);
		if(!cmp_read_str(rep->cmp, error, &size))
		{
			fprintf(stderr, "Error reading error: %s\n", cmp_strerror(rep->cmp));
			return NULL;
		}

		fprintf(stderr, "Server replied with error: %s\n", error);
		return NULL;
	}

	return rep;
}

static hakomari_error_t
endpoint_rpc(
	hakomari_rpc_client_t* rpc, const char* type,
	const char* query,
	unsigned int num_args,
	const char* args[],
	unsigned int num_fds,
	int fds[]
)
{
	hakomari_error_t result = HAKOMARI_OK;

	hakomari_rpc_req_t* req = begin_rpc(
		rpc, HAKOMARI_ENDPOINT_PATH, "endpoint", type, query, num_args
	);
	if(req == NULL) { quit(HAKOMARI_ERR_IO); }

	for(unsigned int i = 0; i < num_args; ++i)
	{
		if(!cmp_write_str(req->cmp, args[i], strlen(args[i])))
		{
			fprintf(stderr, "Error sending request: %s\n", hakomari_rpc_strerror(rpc));
			quit(HAKOMARI_ERR_IO);
		}
	}

	struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;

	if(num_fds > 0 && ancil_send_fds(io->fd, fds, num_fds) < 0)
	{
		fprintf(stderr, "Error sending fd: %s\n", strerror(errno));
		quit(HAKOMARI_ERR_IO);
	}

	hakomari_rpc_rep_t* rep = end_rpc(rpc, req);
	if(rep == NULL) { quit(HAKOMARI_ERR_IO); }

	uint8_t status_code;
	if(!cmp_read_u8(rep->cmp, &status_code))
	{
		fprintf(stderr, "Error reading reply: %s\n", cmp_strerror(rep->cmp));
		quit(HAKOMARI_ERR_IO);
	}

	result = (hakomari_error_t)status_code;

quit:
	hakomari_rpc_stop_client(rpc);
	return result;
}

static void
enumerate_endpoints(struct libhakomari_req_ctx_s* libreq)
{
	struct hakomari_endpoint_s endpoints[MAX_NUM_ENDPOINTS];

	if(slipper_end_read(libreq->slipper, SLIPPER_INFINITY) != SLIPPER_OK)
	{
		fprintf(stderr, "Error reading frame end: %s\n", strerror(errno));
		return;
	}

	DIR* dir = opendir(HAKOMARI_ENDPOINT_PATH);
	if(dir == NULL)
	{
		fprintf(stderr, "Error opening endpoint sock dir: %s\n", strerror(errno));
		return;
	}

	uint32_t num_endpoints = 0;
	int out_memfd = -1;

	struct dirent* dirent;
	while((dirent = readdir(dir)) != NULL)
	{
		if(false
			|| dirent->d_type != DT_DIR
			|| strcmp(dirent->d_name, ".") == 0
			|| strcmp(dirent->d_name, "..") == 0
		)
		{
			continue;
		}

		struct hakomari_endpoint_s* endpoint = &endpoints[num_endpoints++];
		if(num_endpoints > MAX_NUM_ENDPOINTS) { break; }

		strncpy(endpoint->type, "@provider", sizeof(hakomari_string_t));
		strncpy(endpoint->name, dirent->d_name, sizeof(hakomari_string_t));

		if(out_memfd >= 0) { close(out_memfd); }
		out_memfd = memfd_create("enumerate-output", MFD_ALLOW_SEALING);
		if(out_memfd < 0) { continue; }

		if(endpoint_rpc(
			libreq->rpc, dirent->d_name, "enumerate", 0, NULL, 1, &out_memfd
		) != HAKOMARI_OK)
		{
			continue;
		}

		if(lseek(out_memfd, 0, SEEK_SET) < 0)
		{
			fprintf(stderr, "lseek() failed: %s\n", strerror(errno));
			continue;
		}

		// TODO: seal file
		FILE* memfile = fdopen(out_memfd, "r");
		if(memfile == NULL)
		{
			fprintf(stderr, "fdopen() failed: %s\n", strerror(errno));
			continue;
		}

		while(num_endpoints < MAX_NUM_ENDPOINTS)
		{
			struct hakomari_endpoint_s* endpoint = &endpoints[num_endpoints];
			strncpy(endpoint->type, dirent->d_name, sizeof(hakomari_string_t));

			if(fgets(endpoint->name, sizeof(endpoint->name), memfile) == NULL)
			{
				break;
			}

			size_t len = strlen(endpoint->name);
			if(len <= 1) { continue; }

			// Delete '\n'
			endpoint->name[len - 1] = '\0';
			++num_endpoints;
		}

		close(out_memfd);
		out_memfd = -1;
	}

	if(!begin_reply(libreq, HAKOMARI_OK)) { goto end; }

	fprintf(stderr, "Found %d endpoints\n", num_endpoints);
	if(!cmp_write_array(libreq->cmp, num_endpoints))
	{
		fprintf(stderr, "Error writing reply: %s\n", cmp_strerror(libreq->cmp));
		goto end;
	}

	for(uint32_t i = 0; i < num_endpoints; ++i)
	{
		if(false
			|| !cmp_write_map(libreq->cmp, 2)
			|| !cmp_write_str(libreq->cmp, "type", sizeof("type") - 1)
			|| !cmp_write_str(libreq->cmp, endpoints[i].type, strlen(endpoints[i].type))
			|| !cmp_write_str(libreq->cmp, "name", sizeof("name") - 1)
			|| !cmp_write_str(libreq->cmp, endpoints[i].name, strlen(endpoints[i].name))
		)
		{
			fprintf(stderr, "Error writing reply: %s\n", cmp_strerror(libreq->cmp));
			goto end;
		}
	}

	if(!end_reply(libreq)) { goto end; }
end:
	if(out_memfd >= 0) { close(out_memfd); }
	if(dir) { closedir(dir); }
}

static void
create_or_destroy_endpoint(struct libhakomari_req_ctx_s* libreq, const char* query)
{
	uint8_t result = HAKOMARI_ERR_IO;
	bool read_finished = false;
	bool pipe_ok = true;
	uint32_t size;
	if(!cmp_read_map(libreq->cmp, &size))
	{
		fprintf(stderr, "Format error: %s\n", cmp_strerror(libreq->cmp));
		quit(HAKOMARI_ERR_INVALID);
	}

	if(size != 2)
	{
		fprintf(stderr, "Format error\n");
		quit(HAKOMARI_ERR_INVALID);
	}

	hakomari_string_t key, type, name;
	memset(type, 0, sizeof(type));
	memset(name, 0, sizeof(name));

	for(uint32_t i = 0; i < 2; ++i)
	{
		size = sizeof(key);
		if(!cmp_read_str(libreq->cmp, key, &size))
		{
			fprintf(stderr, "Format error: %s\n", cmp_strerror(libreq->cmp));
			quit(HAKOMARI_ERR_INVALID);
		}

		char* value = NULL;
		if(strcmp(key, "type") == 0)
		{
			value = type;
		}
		else if(strcmp(key, "name") == 0)
		{
			value = name;
		}
		else
		{
			fprintf(stderr, "Format error\n");
			quit(HAKOMARI_ERR_INVALID);
		}

		size = sizeof(hakomari_string_t);
		if(!cmp_read_str(libreq->cmp, value, &size))
		{
			fprintf(stderr, "Format error: %s\n", cmp_strerror(libreq->cmp));
			quit(HAKOMARI_ERR_INVALID);
		}
	}

	if(slipper_end_read(libreq->slipper, SERIAL_TIMEOUT) != SLIPPER_OK)
	{
		fprintf(stderr, "Error reading payload: %s\n", strerror(errno));
		pipe_ok = false;
		quit(HAKOMARI_ERR_IO);
	}

	read_finished = true;

	const char* args[] = { name };
	result = endpoint_rpc(libreq->rpc, type, query, 1, args, 0, NULL);
quit:
	if(!read_finished && slipper_end_read(libreq->slipper, SERIAL_TIMEOUT) != SLIPPER_OK)
	{
		fprintf(stderr, "Error reading payload: %s\n", strerror(errno));
		pipe_ok = false;
	}

	if(pipe_ok) { reply_with_error(libreq, result); }
}

static void
query_endpoint(
	struct libhakomari_req_ctx_s* libreq,
	const char* type, const char* name,
	const char* query
)
{
	uint8_t result = HAKOMARI_ERR_IO;
	bool read_finished = false;
	bool send_reply = true;
	int in_memfd, out_memfd;
	in_memfd = out_memfd = -1;

	in_memfd = memfd_create("query-input", MFD_ALLOW_SEALING);
	if(in_memfd < 0) { quit(HAKOMARI_ERR_IO); }

	out_memfd = memfd_create("query-output", MFD_ALLOW_SEALING);
	if(out_memfd < 0) { quit(HAKOMARI_ERR_IO); }

	char io_buf[2048];
	while(true)
	{
		size_t size = sizeof(io_buf);
		if(slipper_read(libreq->slipper, io_buf, &size, SERIAL_TIMEOUT) != SLIPPER_OK)
		{
			fprintf(stderr, "Error reading payload: %s\n", strerror(errno));
			quit(HAKOMARI_ERR_IO);
		}

		if(size == 0) { break; }

		if(write(in_memfd, io_buf, size) < 0)
		{
			fprintf(stderr, "Error writing payload: %s\n", strerror(errno));
			quit(HAKOMARI_ERR_IO);
		}
	}

	if(lseek(in_memfd, 0, SEEK_SET) < 0)
	{
		fprintf(stderr, "lseek() failed: %s\n", strerror(errno));
		quit(HAKOMARI_ERR_IO);
	}

	if(slipper_end_read(libreq->slipper, SERIAL_TIMEOUT) != SLIPPER_OK)
	{
		fprintf(stderr, "Error reading payload: %s\n", strerror(errno));
		send_reply = false;
		quit(HAKOMARI_ERR_IO);
	}

	read_finished = true;

	const char* args[] = { name, query };
	int fds[] = { in_memfd, out_memfd };
	result = endpoint_rpc(libreq->rpc, type, "query", 2, args, 2, fds);

	if(lseek(out_memfd, 0, SEEK_SET) < 0)
	{
		fprintf(stderr, "lseek() failed: %s\n", strerror(errno));
		quit(HAKOMARI_ERR_IO);
	}

	send_reply = false;
	if(!begin_reply(libreq, result)) { quit(HAKOMARI_ERR_IO); }

	while(true)
	{
		ssize_t bytes_read = read(out_memfd, io_buf, sizeof(io_buf));
		if(bytes_read < 0)
		{
			fprintf(stderr, "read() failed: %s\n", strerror(errno));
			quit(HAKOMARI_ERR_IO);
		}

		if(bytes_read == 0) { break; }

		if(slipper_write(
			libreq->slipper, io_buf, bytes_read, SERIAL_TIMEOUT
		) != SLIPPER_OK)
		{
			fprintf(stderr, "Error writing payload: %s\n", strerror(errno));
			quit(HAKOMARI_ERR_IO);
		}
	}

	if(!end_reply(libreq)) { quit(HAKOMARI_ERR_IO); }

quit:
	if(in_memfd >= 0) { close(in_memfd); }
	if(out_memfd >= 0) { close(out_memfd); }

	if(!read_finished && slipper_end_read(libreq->slipper, SERIAL_TIMEOUT) != SLIPPER_OK)
	{
		fprintf(stderr, "Error reading payload: %s\n", strerror(errno));
		send_reply = false;
	}

	if(send_reply) { reply_with_error(libreq, result); }
}

static hakomari_rpc_rep_t*
vault_rpc(
	hakomari_rpc_client_t* rpc,
	const char* type, const char* name,
	const char* method
)
{
	hakomari_rpc_req_t* req = begin_rpc(
		rpc, HAKOMARI_ENDPOINT_PATH, "vault", type, method, 1
	);
	if(req == NULL) { return NULL; }

	if(!cmp_write_str(req->cmp, name, strlen(name)))
	{
		fprintf(stderr, "Error sending request: %s\n", cmp_strerror(req->cmp));
		return NULL;
	}

	return end_rpc(rpc, req);
}

static void
get_passphrase_screen(
	struct libhakomari_req_ctx_s* libreq,
	const char* type, const char* name
)
{
	uint8_t result = HAKOMARI_ERR_IO;
	hakomari_rpc_client_t* rpc = libreq->rpc;
	bool send_reply = true;

	if(slipper_end_read(libreq->slipper, SERIAL_TIMEOUT) != SLIPPER_OK)
	{
		fprintf(stderr, "Error reading payload: %s\n", strerror(errno));
		send_reply = false;
		quit(HAKOMARI_ERR_IO);
	}

	hakomari_rpc_rep_t* rep = vault_rpc(rpc, type, name, "get-passphrase-screen");
	if(rep == NULL) { quit(HAKOMARI_ERR_IO); }

	send_reply = false;
	if(!begin_reply(libreq, HAKOMARI_OK)) { quit(HAKOMARI_ERR_IO); }

	// Copy result to serial
	char io_buf[2048];
	struct hakomari_rpc_io_ctx_s* io = rep->cmp->buf;

	while(true)
	{
		ssize_t bytes_read = read(io->fd, io_buf, sizeof(io_buf));
		if(bytes_read < 0)
		{
			fprintf(stderr, "Error reading reply: %s\n", strerror(errno));
			quit(HAKOMARI_ERR_IO);
		}

		if(bytes_read == 0) { break; }

		if(slipper_write(libreq->slipper, io_buf, bytes_read, SERIAL_TIMEOUT) != SLIPPER_OK)
		{
			quit(HAKOMARI_ERR_IO);
		}
	}

	if(!end_reply(libreq)) { quit(HAKOMARI_ERR_IO); }

quit:
	hakomari_rpc_stop_client(rpc);
	if(send_reply) { reply_with_error(libreq, result); }
}

static void
input_passphrase(
	struct libhakomari_req_ctx_s* libreq,
	const char* type, const char* name
)
{
	uint8_t result = HAKOMARI_ERR_IO;
	hakomari_rpc_client_t* rpc = libreq->rpc;
	bool read_finished = false;
	bool send_reply = true;

	hakomari_rpc_req_t* req = begin_rpc(
		rpc, HAKOMARI_ENDPOINT_PATH, "vault", type, "input-passphrase", 1
	);
	if(req == NULL) { quit(HAKOMARI_ERR_IO); }

	if(!cmp_write_str(req->cmp, name, strlen(name)))
	{
		fprintf(stderr, "Error sending request: %s\n", cmp_strerror(req->cmp));
		quit(HAKOMARI_ERR_IO);
	}

	bool inputing = true;
	while(inputing)
	{
		cmp_object_t obj;
		if(!cmp_read_object(libreq->cmp, &obj))
		{
			fprintf(stderr, "Error reading payload: %s\n", cmp_strerror(req->cmp));
			quit(HAKOMARI_ERR_IO);
		}

		uint32_t x, y;
		bool down;
		switch(obj.type)
		{
			case CMP_TYPE_NIL:
				if(!cmp_write_nil(req->cmp))
				{
					fprintf(stderr, "Error sending payload: %s\n", cmp_strerror(req->cmp));
					quit(HAKOMARI_ERR_IO);
				}
				inputing = false;
				continue;
			case CMP_TYPE_ARRAY16:
			case CMP_TYPE_ARRAY32:
			case CMP_TYPE_FIXARRAY:
				if(obj.as.array_size != 3) { quit(HAKOMARI_ERR_INVALID); }

				if(false
					|| !cmp_read_uint(libreq->cmp, &x)
					|| !cmp_read_uint(libreq->cmp, &y)
					|| !cmp_read_bool(libreq->cmp, &down)
				)
				{
					fprintf(stderr, "Error reading payload: %s\n", cmp_strerror(libreq->cmp));
					quit(HAKOMARI_ERR_IO);
				}

				if(false
					|| !cmp_write_array(req->cmp, 3)
					|| !cmp_write_uint(req->cmp, x)
					|| !cmp_write_uint(req->cmp, y)
					|| !cmp_write_bool(req->cmp, down)
				)
				{
					fprintf(stderr, "Error sending payload: %s\n", cmp_strerror(req->cmp));
					quit(HAKOMARI_ERR_IO);
				}
				break;
			default:
				quit(HAKOMARI_ERR_INVALID);
		}
	}

	hakomari_rpc_rep_t* rep = end_rpc(rpc, req);
	if(rep == NULL) { quit(HAKOMARI_ERR_IO); }

	uint8_t status_code;
	if(!cmp_read_u8(rep->cmp, &status_code))
	{
		fprintf(stderr, "Error reading reply: %s\n", cmp_strerror(rep->cmp));
		quit(HAKOMARI_ERR_IO);
	}

	result = (hakomari_error_t)status_code;

quit:
	hakomari_rpc_stop_client(rpc);

	if(!read_finished && slipper_end_read(libreq->slipper, SERIAL_TIMEOUT) != SLIPPER_OK)
	{
		fprintf(stderr, "Error reading payload: %s\n", strerror(errno));
		send_reply = false;
	}

	if(send_reply) { reply_with_error(libreq, result); }
}

int
main(int argc, const char* argv[])
{
	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IOLBF, 1024);
	setvbuf(stderr, NULL, _IOLBF, 1024);

	int result = EXIT_SUCCESS;
	bool serial_opened = false;
	serial_t serial;
	hakomari_rpc_client_t rpc;
	hakomari_rpc_init_client(&rpc);

	if(argc != 2)
	{
		fprintf(stderr, "Usage: hakomari-dispatcherd <serial-dev>\n");
		quit(EXIT_FAILURE);
	}

	if(serial_open_advanced(&serial, argv[1], 115200, 8, PARITY_NONE, 1, false, true) != 0)
	{
		fprintf(stderr, "Error connecting to serial: %s\n", serial_errmsg(&serial));
		quit(EXIT_FAILURE);
	}

	serial_opened = true;

	uint8_t serial_io_buf[SERIAL_IO_BUF_SIZE];
	slipper_cfg_t slipper_cfg = {
		.serial = {
			.userdata = &serial,
			.read = slipper_serial_read,
			.write = slipper_serial_write,
		},
		.memory_size = sizeof(serial_io_buf),
		.memory = serial_io_buf
	};
	slipper_ctx_t slipper;
	slipper_init(&slipper, &slipper_cfg);

	cmp_ctx_t cmp;
	cmp_init(&cmp, &slipper, cmp_slipper_read, NULL, cmp_slipper_write);

	struct libhakomari_req_ctx_s libreq = {
		.slipper = &slipper,
		.cmp = &cmp,
		.rpc = &rpc,
	};

	while(true)
	{
		if(serial_flush(&serial) != 0)
		{
			fprintf(stderr, "Error flushing serial: %s\n", serial_errmsg(&serial));
			quit(EXIT_FAILURE);
		}

		if(slipper_begin_read(&slipper, SLIPPER_INFINITY) != SLIPPER_OK)
		{
			fprintf(stderr, "Error reading frame start: %s\n", strerror(errno));
			quit(EXIT_FAILURE);
		}

		uint32_t size;
		if(!cmp_read_array(&cmp, &size))
		{
			fprintf(stderr, "Error reading message header: %s\n", cmp_strerror(&cmp));
			continue;
		}

		uint8_t frame_type;
		if(!cmp_read_u8(&cmp, &frame_type))
		{
			fprintf(stderr, "Format error: %s\n", cmp_strerror(libreq.cmp));
			continue;
		}

		if(frame_type != HAKOMARI_FRAME_REQ)
		{
			fprintf(stderr, "Format error\n");
			continue;
		}

		if(!cmp_read_u32(&cmp, &libreq.txid))
		{
			fprintf(stderr, "Format error: %s\n", cmp_strerror(libreq.cmp));
			continue;
		}

		hakomari_string_t query;
		size = sizeof(query);
		if(!cmp_read_str(&cmp, query, &size))
		{
			fprintf(stderr, "Format error: %s\n", cmp_strerror(libreq.cmp));
			continue;
		}

		cmp_object_t obj;
		if(!cmp_read_object(&cmp, &obj))
		{
			fprintf(stderr, "Format error: %s\n", cmp_strerror(libreq.cmp));
			continue;
		}

		hakomari_string_t endpoint_type, endpoint_name;
		bool target_endpoint;
		switch(obj.type)
		{
			case CMP_TYPE_NIL:
				target_endpoint = false;
				break;
			case CMP_TYPE_ARRAY16:
			case CMP_TYPE_ARRAY32:
			case CMP_TYPE_FIXARRAY:
				target_endpoint = true;
				if(obj.as.array_size != 2)
				{
					fprintf(stderr, "Format error\n");
					continue;
				}

				size = sizeof(endpoint_type);
				if(!cmp_read_str(&cmp, endpoint_type, &size))
				{
					fprintf(stderr, "Format error\n");
					continue;
				}

				size = sizeof(endpoint_name);
				if(!cmp_read_str(&cmp, endpoint_name, &size))
				{
					fprintf(stderr, "Format error\n");
					continue;
				}
				break;
			default:
				continue;
		}

		if(target_endpoint)
		{
			fprintf(
				stdout, "Query: %s(%s, %s) (txid=%d)\n",
				query, endpoint_type, endpoint_name, libreq.txid
			);

			if(strcmp(query, "@get-passphrase-screen") == 0)
			{
				get_passphrase_screen(&libreq, endpoint_type, endpoint_name);
			}
			else if(strcmp(query, "@input-passphrase") == 0)
			{
				input_passphrase(&libreq, endpoint_type, endpoint_name);
			}
			else
			{
				query_endpoint(&libreq, endpoint_type, endpoint_name, query);
			}
		}
		else
		{
			fprintf(stdout, "Query: %s (txid=%d)\n", query, libreq.txid);

			if(strcmp(query, "@enumerate") == 0)
			{
				enumerate_endpoints(&libreq);
			}
			else if(strcmp(query, "@create") == 0 || strcmp(query, "@destroy") == 0)
			{
				create_or_destroy_endpoint(&libreq, query + 1);
			}
			else
			{
				fprintf(stderr, "Unrecognized query\n");
				reply_with_error(&libreq, HAKOMARI_ERR_INVALID);
			}
		}
	}

quit:
	if(serial_opened) { serial_close(&serial); }
	return result;
}
