#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <c-periphery/serial.h>
#include <dirent.h>
#include <unistd.h>
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

#define quit(code) do { exit_code = code; goto quit; } while(0)

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
begin_reply(
	cmp_ctx_t* cmp, slipper_ctx_t* slipper,
	uint32_t txid, hakomari_error_t result
)
{
	slipper_error_t error;
	if((error = slipper_begin_write(slipper, SERIAL_TIMEOUT)) != 0)
	{
		fprintf(stderr, "Error sending reply: %s\n", slipper_errorstr(error));
		return false;
	}

	if(!cmp_write_array(cmp, 3))
	{
		fprintf(stderr, "Error sending reply: %s\n", strerror(errno));
		return false;
	}

	if(!cmp_write_u8(cmp, HAKOMARI_FRAME_REP))
	{
		fprintf(stderr, "Error sending reply: %s\n", strerror(errno));
		return false;
	}

	if(!cmp_write_u32(cmp, txid))
	{
		fprintf(stderr, "Error sending reply: %s\n", strerror(errno));
		return false;
	}

	if(!cmp_write_u8(cmp, result))
	{
		fprintf(stderr, "Error sending reply: %s\n", strerror(errno));
		return false;
	}

	return true;
}

static bool
end_reply(cmp_ctx_t* cmp, slipper_ctx_t* slipper)
{
	(void)cmp;
	slipper_error_t error;
	if((error = slipper_end_write(slipper, SERIAL_TIMEOUT)) != 0)
	{
		fprintf(stderr, "Error sending reply: %s\n", slipper_errorstr(error));
		return false;
	}

	return true;
}

static void
handle_unrecognized_query(cmp_ctx_t* cmp, slipper_ctx_t* slipper, uint32_t txid)
{
	fprintf(stderr, "Unrecognized query\n");

	if(!begin_reply(cmp, slipper, txid, HAKOMARI_ERR_DENIED))
	{
		return;
	}

	if(!end_reply(cmp, slipper))
	{
		return;
	}
}

static void
enumerate_endpoints(
	cmp_ctx_t* cmp, slipper_ctx_t* slipper, hakomari_rpc_client_t* rpc
)
{
	(void)slipper;
	struct hakomari_endpoint_s endpoints[MAX_NUM_ENDPOINTS];

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
		if(dirent->d_type != DT_SOCK) { continue; }

		struct hakomari_endpoint_s* endpoint = &endpoints[num_endpoints++];
		if(num_endpoints > MAX_NUM_ENDPOINTS) { break; }

		strncpy(endpoint->type, "@provider", sizeof(hakomari_string_t));
		strncpy(endpoint->name, dirent->d_name, sizeof(hakomari_string_t));

		hakomari_rpc_stop_client(rpc);
		char* path;
		if(asprintf(&path, "%s/%s", HAKOMARI_ENDPOINT_PATH, dirent->d_name) <= 0)
		{
			fprintf(stderr, "Error building path to endpoint socket: %s\n", strerror(errno));
			continue;
		}

		int error = hakomari_rpc_start_client(rpc, path);
		free(path);

		if(error != 0)
		{
			fprintf(stderr, "Error connecting to endpoint: %s\n", hakomari_rpc_strerror(rpc));
			continue;
		}

		hakomari_rpc_req_t* req = hakomari_rpc_begin_req(rpc, "enumerate", 0);
		if(req == NULL)
		{
			fprintf(stderr, "Error sending request: %s\n", hakomari_rpc_strerror(rpc));
			continue;
		}

		struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
		if(out_memfd >= 0) { close(out_memfd); }

		out_memfd = syscall(__NR_memfd_create, "output", MFD_ALLOW_SEALING);
		if(out_memfd < 0)
		{
			fprintf(stderr, "Error creating memfd: %s\n", strerror(errno));
			continue;
		}
		fprintf(stdout, "Created memfd: %d\n", out_memfd);

		if(ancil_send_fd(io->fd, out_memfd) < 0)
		{
			fprintf(stderr, "Error sending fd: %s\n", strerror(errno));
			continue;
		}

		hakomari_rpc_rep_t* rep = hakomari_rpc_end_req(req);
		if(rep == NULL)
		{
			fprintf(stderr, "Error waiting for reply: %s\n", hakomari_rpc_strerror(rpc));
			continue;
		}

		uint32_t size;
		if(!rep->success)
		{
			hakomari_string_t error;
			size = sizeof(error);
			if(!cmp_read_str(rep->cmp, error, &size))
			{
				fprintf(stderr, "Error reading error: %s\n", cmp_strerror(rep->cmp));
				continue;
			}

			fprintf(stderr, "%s replied with error: %s\n", dirent->d_name, error);
			continue;
		}

		uint8_t status_code;
		if(!cmp_read_u8(rep->cmp, &status_code))
		{
			fprintf(stderr, "Error reading reply: %s\n", cmp_strerror(rep->cmp));
			continue;
		}

		if(status_code != (uint8_t)HAKOMARI_OK)
		{
			fprintf(stderr, "%s replied with error: %d\n", dirent->d_name, status_code);
			continue;
		}

		// TODO: seal file
		FILE* memfile = fdopen(out_memfd, "r");
		if(memfile == NULL)
		{
			fprintf(stderr, "fdopen() failed: %s\n", strerror(errno));
			continue;
		}

		if(fseek(memfile, 0, SEEK_SET) != 0)
		{
			fprintf(stderr, "fseek() failed: %s\n", strerror(errno));
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

	fprintf(stderr, "Found %d endpoints\n", num_endpoints);
	if(!cmp_write_array(cmp, num_endpoints))
	{
		fprintf(stderr, "Error writing reply: %s\n", cmp_strerror(cmp));
		goto end;
	}

	for(uint32_t i = 0; i < num_endpoints; ++i)
	{
		if(!cmp_write_map(cmp, 2))
		{
			fprintf(stderr, "Error writing reply: %s\n", cmp_strerror(cmp));
			goto end;
		}

		if(!cmp_write_str(cmp, "type", sizeof("type") - 1))
		{
			fprintf(stderr, "Error writing reply: %s\n", cmp_strerror(cmp));
			goto end;
		}

		if(!cmp_write_str(cmp, endpoints[i].type, strlen(endpoints[i].type)))
		{
			fprintf(stderr, "Error writing reply: %s\n", cmp_strerror(cmp));
			goto end;
		}

		if(!cmp_write_str(cmp, "name", sizeof("name") - 1))
		{
			fprintf(stderr, "Error writing reply: %s\n", cmp_strerror(cmp));
			goto end;
		}

		if(!cmp_write_str(cmp, endpoints[i].name, strlen(endpoints[i].name)))
		{
			fprintf(stderr, "Error writing reply: %s\n", cmp_strerror(cmp));
			goto end;
		}
	}

end:
	if(out_memfd >= 0) { close(out_memfd); }
	hakomari_rpc_stop_client(rpc);
	if(dir) { closedir(dir); }
}

int
main(int argc, const char* argv[])
{
	int exit_code = EXIT_SUCCESS;
	bool serial_opened = false;
	serial_t serial;
	slipper_ctx_t slipper;
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
	slipper_init(&slipper, &slipper_cfg);
	cmp_ctx_t cmp;
	cmp_init(&cmp, &slipper, cmp_slipper_read, NULL, cmp_slipper_write);

	while(true)
	{
		if(serial_flush(&serial) != 0)
		{
			fprintf(stderr, "Error flushing serial: %s\n", serial_errmsg(&serial));
			quit(EXIT_FAILURE);
		}

		slipper_error_t error;
		if((error = slipper_begin_read(&slipper, SLIPPER_INFINITY)) != 0)
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
		if(!cmp_read_u8(&cmp, &frame_type) || frame_type != HAKOMARI_FRAME_REQ)
		{
			continue;
		}

		uint32_t txid;
		if(!cmp_read_u32(&cmp, &txid))
		{
			continue;
		}

		hakomari_string_t query;
		size = sizeof(query);
		if(!cmp_read_str(&cmp, query, &size))
		{
			continue;
		}

		cmp_object_t obj;
		if(!cmp_read_object(&cmp, &obj))
		{
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
					continue;
				}

				size = sizeof(endpoint_type);
				if(!cmp_read_str(&cmp, endpoint_type, &size))
				{
					continue;
				}

				size = sizeof(endpoint_name);
				if(!cmp_read_str(&cmp, endpoint_name, &size))
				{
					continue;
				}
				break;
			default:
				continue;
		}

		if((error = slipper_end_read(&slipper, SLIPPER_INFINITY)) != 0)
		{
			fprintf(stderr, "Error reading frame end: %s\n", strerror(errno));
			quit(EXIT_FAILURE);
		}

		if(target_endpoint)
		{
			fprintf(
				stdout, "Query: %s(%s, %s) (txid=%d)\n",
				query, endpoint_type, endpoint_name, txid
			);

			if(false)
			{
			}
			else
			{
				handle_unrecognized_query(&cmp, &slipper, txid);
			}
		}
		else
		{
			fprintf(stdout, "Query: %s (txid=%d)\n", query, txid);

			if(strcmp(query, "enumerate") == 0)
			{
				if(!begin_reply(&cmp, &slipper, txid, HAKOMARI_OK))
				{
					continue;
				}

				enumerate_endpoints(&cmp, &slipper, &rpc);

				if(!end_reply(&cmp, &slipper))
				{
					continue;
				}
			}
			else
			{
				handle_unrecognized_query(&cmp, &slipper, txid);
			}
		}
	}

quit:
	if(serial_opened) { serial_close(&serial); }
	return exit_code;
}
