#define _GNU_SOURCE
#include "hakomari-rpc.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <fcntl.h>

typedef enum hakomari_rpc_frame_type_e
{
	HAKOMARI_RPC_FRAME_REQ = 0,
	HAKOMARI_RPC_FRAME_REP,
} hakomari_rpc_frame_type_t;

static int
hakomari_rpc_set_error(void* server_or_client, int errnum, const char* errorstr)
{
	struct hakomari_rpc_err_s* err = server_or_client;
	err->errnum = errnum;
	err->errorstr = errorstr;
	return err->errnum;
}

static bool
hakomari_rpc_cmp_read(cmp_ctx_t* ctx, void* data, size_t limit)
{
	struct hakomari_rpc_io_ctx_s* io = ctx->buf;
	hakomari_rpc_set_error(io->err, 0, NULL);
	bool result = read(io->fd, data, limit) == (ssize_t)limit;
	hakomari_rpc_set_error(io->err, errno, NULL);
	return result;
}

static size_t
hakomari_rpc_cmp_write(cmp_ctx_t* ctx, const void *data, size_t count)
{
	struct hakomari_rpc_io_ctx_s* io = ctx->buf;
	hakomari_rpc_set_error(io->err, 0, NULL);
	ssize_t bytes_written = write(io->fd, data, count);
	hakomari_rpc_set_error(io->err, errno, NULL);
	return bytes_written > 0 ? bytes_written : 0;
}

static int
hakomari_rpc_set_cmp_error(cmp_ctx_t* ctx)
{
	struct hakomari_rpc_io_ctx_s* io = ctx->buf;

	if(io->err->errnum == 0)
	{
		io->err->errnum = EPROTO;
		io->err->errorstr = cmp_strerror(ctx);
		return EPROTO;
	}
	else
	{
		return io->err->errnum;
	}
}

static void
hakomari_rpc_init_base(struct hakomari_rpc_base_s* base)
{
	*base = (struct hakomari_rpc_base_s){
		.io = {
			.err = &base->err,
			.fd = -1,
		},
		.req = {
			.cmp = &base->cmp,
		}
	};

	cmp_init(
		&base->cmp,
		&base->io,
		hakomari_rpc_cmp_read, NULL, hakomari_rpc_cmp_write
	);
}

static void
hakomari_rpc_disconnect_sock(int sock)
{
	shutdown(sock, SHUT_RDWR);
	close(sock);
}

static void
hakomari_rpc_cleanup_base(struct hakomari_rpc_base_s* base)
{
	if(base->io.fd >= 0)
	{
		hakomari_rpc_disconnect_sock(base->io.fd);
		base->io.fd = -1;
	}
}

static int
hakomari_rpc_begin_rep(hakomari_rpc_req_t* req, bool success)
{
	struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
	if(false
		|| !cmp_write_array(req->cmp, 3)
		|| !cmp_write_u8(req->cmp, HAKOMARI_RPC_FRAME_REP)
		|| !cmp_write_bool(req->cmp, success)
	)
	{
		return hakomari_rpc_set_cmp_error(req->cmp);
	}

	return hakomari_rpc_set_error(io->err, 0, NULL);
}

static int
hakomari_rpc_end_rep(hakomari_rpc_req_t* req)
{
	struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
	hakomari_rpc_disconnect_sock(io->fd);
	io->fd = -1;
	return hakomari_rpc_set_error(io->err, 0, NULL);
}

static int
hakomari_rpc_set_sockopt(int sock)
{
	int err;
	struct timeval timeout = {
		.tv_sec = HAKOMARI_RPC_TIMEOUT / 1000,
		.tv_usec = (HAKOMARI_RPC_TIMEOUT % 1000) * 1000,
	};

	if((err = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) != 0)
	{
		return err;
	}

	if((err = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) != 0)
	{
		return err;
	}

	return 0;
}

const char*
hakomari_rpc_strerror(void* server_or_client)
{
	struct hakomari_rpc_err_s* err = server_or_client;
	return err->errorstr != 0 ? err->errorstr : strerror(err->errnum);
}

void
hakomari_rpc_init_server(hakomari_rpc_server_t* server)
{
	*server = (hakomari_rpc_server_t){
		.sock = -1,
		.lock = -1,
		.sig = -1,
		.stopping = false
	};
	hakomari_rpc_init_base(&server->base);
}

int
hakomari_rpc_start_server(
	hakomari_rpc_server_t* server, const char* sock_path, const char* lock_path
)
{
	server->lock = open(lock_path, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
	if(server->lock < 0)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	if(flock(server->lock, LOCK_EX | LOCK_NB) != 0)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	if(unlink(sock_path) != 0 && errno != ENOENT)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	server->sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(server->sock < 0)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	int status = fcntl(server->sock, F_GETFL);
	if(status < 0)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	if(fcntl(server->sock, F_SETFL, status | O_NONBLOCK) != 0)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path));
	mode_t previous = umask(S_ISUID | S_ISGID | S_IXUSR | S_IXGRP | S_IRWXO);
	int bind_result = bind(server->sock, (struct sockaddr*)&addr, sizeof(addr));
	umask(previous);

	if(bind_result != 0)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	if(listen(server->sock, 1) != 0)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);

	server->sig = signalfd(-1, &mask, 0);
	if(server->sig < 0)
	{
		return hakomari_rpc_set_error(server, errno, NULL);
	}

	server->stopping = false;

	return 0;
}

int
hakomari_rpc_stop_server(hakomari_rpc_server_t* server)
{
	if(server->sig >= 0)
	{
		close(server->sig);
		server->sig = -1;
	}

	if(server->sock >= 0)
	{
		struct sockaddr_un addr;
		socklen_t size = sizeof(addr);
		if(getsockname(server->sock, (struct sockaddr*)&addr, &size) == 0)
		{
			unlink(addr.sun_path);
		}

		close(server->sock);
		server->sock = -1;
	}

	if(server->lock >= 0)
	{
		close(server->lock);
		server->lock = -1;
	}

	hakomari_rpc_cleanup_base(&server->base);

	server->stopping = true;

	return hakomari_rpc_set_error(server, errno, NULL);
}

hakomari_rpc_req_t*
hakomari_rpc_next_req(hakomari_rpc_server_t* server)
{
	hakomari_rpc_req_t* result = NULL;

	sigset_t mask, old;
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	if(sigprocmask(SIG_BLOCK, &mask, &old) != 0)
	{
		hakomari_rpc_set_error(server, errno, NULL);
		goto end;
	}

	struct pollfd pfds[2];
	pfds[0].fd = server->sock;
	pfds[0].events = POLLIN;
	pfds[1].fd = server->sig;
	pfds[1].events = POLLIN;

	while(true)
	{
		if(server->base.io.fd >=0)
		{
			hakomari_rpc_disconnect_sock(server->base.io.fd);
			server->base.io.fd = -1;
		}

		if(poll(pfds, 2, -1) < 0)
		{
			hakomari_rpc_set_error(server, errno, NULL);
			goto end;
		}

		if((pfds[1].revents & POLLIN) > 0)
		{
			server->stopping = true;
			goto end;
		}

		if((pfds[0].revents & POLLIN) > 0)
		{
			int client = server->base.io.fd = accept(server->sock, NULL, NULL);
			if(client < 0)
			{
				continue;
			}

			if(hakomari_rpc_set_sockopt(client) != 0)
			{
				continue;
			}

			uint32_t size;
			hakomari_rpc_req_t* req = &server->base.req;
			if(!cmp_read_array(req->cmp, &size) || size != 3)
			{
				continue;
			}

			uint8_t frame_type;
			if(!cmp_read_u8(req->cmp, &frame_type) || frame_type != HAKOMARI_RPC_FRAME_REQ)
			{
				continue;
			}

			size = sizeof(req->method);
			if(!cmp_read_str(req->cmp, req->method, &size))
			{
				continue;
			}

			if(!cmp_read_array(req->cmp, &req->num_args))
			{
				continue;
			}

			result = &server->base.req;
			goto end;
		}
	}

end:
	sigprocmask(SIG_BLOCK, &old, NULL);

	return result;
}

int
hakomari_rpc_reply_error(hakomari_rpc_req_t* req, hakomari_rpc_string_t str)
{
	struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
	if(hakomari_rpc_begin_rep(req, false) != 0)
	{
		return io->err->errnum;
	}

	if(!cmp_write_str(req->cmp, str, strlen(str)))
	{
		return io->err->errnum;
	}

	return hakomari_rpc_end_rep(req);
}

int
hakomari_rpc_begin_result(hakomari_rpc_req_t* req)
{
	return hakomari_rpc_begin_rep(req, true);
}

int
hakomari_rpc_end_result(hakomari_rpc_req_t* req)
{
	return hakomari_rpc_end_rep(req);
}

void
hakomari_rpc_init_client(hakomari_rpc_client_t* client)
{
	*client = (hakomari_rpc_client_t){
		.rep = {
			.cmp = &client->base.cmp
		}
	};

	hakomari_rpc_init_base(&client->base);
}

int
hakomari_rpc_start_client(hakomari_rpc_client_t* client, const char* sock_path)
{

	client->base.io.fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(client->base.io.fd < 0)
	{
		return hakomari_rpc_set_error(client, errno, NULL);
	}

	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path));
	if(connect(client->base.io.fd, &addr, sizeof(addr)) != 0)
	{
		return hakomari_rpc_set_error(client, errno, NULL);
	}

	if(hakomari_rpc_set_sockopt(client->base.io.fd) != 0)
	{
		return hakomari_rpc_set_error(client, errno, NULL);
	}

	return hakomari_rpc_set_error(client, 0, NULL);
}

int
hakomari_rpc_stop_client(hakomari_rpc_client_t* client)
{

	hakomari_rpc_cleanup_base(&client->base);
	return hakomari_rpc_set_error(client, errno, NULL);
}

hakomari_rpc_req_t*
hakomari_rpc_begin_req(
	hakomari_rpc_client_t* client,
	const hakomari_rpc_string_t method,
	unsigned int num_args
)
{
	hakomari_rpc_req_t* req = &client->base.req;
	if(false
		|| !cmp_write_array(req->cmp, 3)
		|| !cmp_write_u8(req->cmp, HAKOMARI_RPC_FRAME_REQ)
		|| !cmp_write_str(req->cmp, method, strlen(method))
		|| !cmp_write_array(req->cmp, num_args)
	)
	{
		hakomari_rpc_set_cmp_error(req->cmp);
		return NULL;
	}

	hakomari_rpc_set_error(client, 0, NULL);
	return req;
}

hakomari_rpc_rep_t*
hakomari_rpc_end_req(hakomari_rpc_req_t* req)
{
	struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
	hakomari_rpc_client_t* client = (hakomari_rpc_client_t*)io->err;
	hakomari_rpc_rep_t* rep = &client->rep;

	uint32_t size;
	if(!cmp_read_array(rep->cmp, &size))
	{
		hakomari_rpc_set_cmp_error(rep->cmp);
		return NULL;
	}

	if(size != 3)
	{
		hakomari_rpc_set_error(client, EPROTO, NULL);
		return NULL;
	}

	uint8_t frame_type;
	if(!cmp_read_u8(rep->cmp, &frame_type))
	{
		hakomari_rpc_set_cmp_error(rep->cmp);
		return NULL;
	}

	if(frame_type != HAKOMARI_RPC_FRAME_REP)
	{
		hakomari_rpc_set_error(client, EPROTO, "Invalid frame type");
		return NULL;
	}

	if(!cmp_read_bool(rep->cmp, &client->rep.success))
	{
		hakomari_rpc_set_cmp_error(rep->cmp);
		return NULL;
	}

	return &client->rep;
}
