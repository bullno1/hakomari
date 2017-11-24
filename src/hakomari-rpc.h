#ifndef HAKOMARI_RPC_H
#define HAKOMARI_RPC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <poll.h>
#include <cmp.h>

#ifndef HAKOMARI_RPC_TIMEOUT
#define HAKOMARI_RPC_TIMEOUT 10000
#endif

#ifndef HAKOMARI_RPC_STRLEN
#define HAKOMARI_RPC_STRLEN 1024
#endif

typedef struct hakomari_rpc_server_s hakomari_rpc_server_t;
typedef struct hakomari_rpc_client_s hakomari_rpc_client_t;
typedef struct hakomari_rpc_req_s hakomari_rpc_req_t;
typedef struct hakomari_rpc_rep_s hakomari_rpc_rep_t;
typedef char hakomari_rpc_string_t[HAKOMARI_RPC_STRLEN];

struct hakomari_rpc_err_s
{
	int errnum;
	const char* errorstr;
};

struct hakomari_rpc_io_ctx_s
{
	struct hakomari_rpc_err_s* err;
	int fd;
};

struct hakomari_rpc_req_s
{
	uint32_t num_args;
	hakomari_rpc_string_t method;
	cmp_ctx_t* cmp;
};

struct hakomari_rpc_rep_s
{
	bool success;
	cmp_ctx_t* cmp;
};

struct hakomari_rpc_base_s
{
	struct hakomari_rpc_err_s err;
	struct hakomari_rpc_io_ctx_s io;
	cmp_ctx_t cmp;
	hakomari_rpc_req_t req;
};

struct hakomari_rpc_server_s
{
	struct hakomari_rpc_base_s base;
	int sock;
	int lock;
	int sig;
	bool stopping;
};

struct hakomari_rpc_client_s
{
	struct hakomari_rpc_base_s base;
	hakomari_rpc_rep_t rep;
};

// Common

const char*
hakomari_rpc_strerror(void* server_or_client);

// Server

void
hakomari_rpc_init_server(hakomari_rpc_server_t* server);

int
hakomari_rpc_start_server(
	hakomari_rpc_server_t* server, const char* sock_path, const char* lock_path
);

int
hakomari_rpc_stop_server(hakomari_rpc_server_t* server);

hakomari_rpc_req_t*
hakomari_rpc_next_req(hakomari_rpc_server_t* server);

int
hakomari_rpc_reply_error(hakomari_rpc_req_t* req, hakomari_rpc_string_t str);

int
hakomari_rpc_begin_result(hakomari_rpc_req_t* req);

int
hakomari_rpc_end_result(hakomari_rpc_req_t* req);

// Client

void
hakomari_rpc_init_client(hakomari_rpc_client_t* client);

int
hakomari_rpc_start_client(hakomari_rpc_client_t* client, const char* sock_path);

int
hakomari_rpc_stop_client(hakomari_rpc_client_t* client);

hakomari_rpc_req_t*
hakomari_rpc_begin_req(
	hakomari_rpc_client_t* client,
	const hakomari_rpc_string_t method,
	unsigned int num_args
);

hakomari_rpc_rep_t*
hakomari_rpc_end_req(hakomari_rpc_req_t* req);

#endif
