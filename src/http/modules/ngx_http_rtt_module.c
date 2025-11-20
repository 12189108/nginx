
/*
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#if (NGX_LINUX)
#include <netinet/tcp.h>
#endif


typedef struct {
    ngx_msec_t  ssl_handshake_start;
    ngx_msec_t  ssl_handshake_end;
} ngx_http_rtt_ctx_t;


static ngx_int_t ngx_http_rtt_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_rtt_get_tcp_info(ngx_connection_t *c, 
    ngx_uint_t *rtt);
static ngx_int_t ngx_http_rtt_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_rtt_tcp_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_rtt_tls_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_rtt_postconfiguration(ngx_conf_t *cf);


static ngx_http_module_t  ngx_http_rtt_module_ctx = {
    ngx_http_rtt_add_variables,            /* preconfiguration */
    ngx_http_rtt_postconfiguration,        /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_rtt_module = {
    NGX_MODULE_V1,
    &ngx_http_rtt_module_ctx,              /* module context */
    NULL,                                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t  ngx_http_rtt_vars[] = {

    { ngx_string("tcp_rtt"), NULL,
      ngx_http_rtt_tcp_variable, 0,
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("tls_handshake_rtt"), NULL,
      ngx_http_rtt_tls_variable, 0,
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    ngx_http_null_variable
};


static ngx_int_t
ngx_http_rtt_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_rtt_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_rtt_tcp_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char       *p;
    ngx_uint_t    rtt;

    rtt = 0;

    if (r->connection->proxy_protocol
        && r->connection->proxy_protocol->tcp_rtt > 0)
    {
        rtt = r->connection->proxy_protocol->tcp_rtt;
    } else {
        if (ngx_http_rtt_get_tcp_info(r->connection, &rtt) != NGX_OK) {
            v->not_found = 1;
            return NGX_OK;
        }
    }

    if (rtt == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    p = ngx_pnalloc(r->pool, NGX_INT32_LEN);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->len = ngx_sprintf(p, "%ui", rtt) - p;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = p;

    return NGX_OK;
}


static ngx_int_t
ngx_http_rtt_tls_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char               *p;
    ngx_uint_t            tls_rtt;
    ngx_http_rtt_ctx_t   *ctx;

    if (r->connection->proxy_protocol
        && r->connection->proxy_protocol->tls_rtt > 0)
    {
        tls_rtt = r->connection->proxy_protocol->tls_rtt;

        p = ngx_pnalloc(r->pool, NGX_INT32_LEN);
        if (p == NULL) {
            return NGX_ERROR;
        }

        v->len = ngx_sprintf(p, "%ui", tls_rtt) - p;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = p;

        return NGX_OK;
    }

#if (NGX_HTTP_SSL)
    if (r->connection->ssl == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_rtt_module);
    if (ctx == NULL || ctx->ssl_handshake_end == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    tls_rtt = ctx->ssl_handshake_end - ctx->ssl_handshake_start;

    p = ngx_pnalloc(r->pool, NGX_INT32_LEN);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->len = ngx_sprintf(p, "%M", tls_rtt) - p;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = p;

    return NGX_OK;
#else
    v->not_found = 1;
    return NGX_OK;
#endif
}


static ngx_int_t
ngx_http_rtt_get_tcp_info(ngx_connection_t *c, ngx_uint_t *rtt)
{
#if (NGX_LINUX)
    struct tcp_info  ti;
    socklen_t        len;

    len = sizeof(struct tcp_info);

    if (getsockopt(c->fd, IPPROTO_TCP, TCP_INFO, &ti, &len) == -1) {
        ngx_log_error(NGX_LOG_WARN, c->log, ngx_socket_errno,
                      "getsockopt(TCP_INFO) failed");
        return NGX_ERROR;
    }

    *rtt = ti.tcpi_rtt;
    return NGX_OK;

#elif (NGX_DARWIN)
    struct tcp_connection_info  ti;
    socklen_t                   len;

    len = sizeof(struct tcp_connection_info);

    if (getsockopt(c->fd, IPPROTO_TCP, TCP_CONNECTION_INFO, &ti, &len) == -1) {
        ngx_log_error(NGX_LOG_WARN, c->log, ngx_socket_errno,
                      "getsockopt(TCP_CONNECTION_INFO) failed");
        return NGX_ERROR;
    }

    *rtt = ti.tcpi_rttcur;
    return NGX_OK;

#else
    return NGX_DECLINED;
#endif
}


static ngx_int_t
ngx_http_rtt_handler(ngx_http_request_t *r)
{
    ngx_uint_t           rtt;
    ngx_http_rtt_ctx_t  *ctx;

#if (NGX_HTTP_SSL)
    if (r->connection->ssl) {
        ctx = ngx_http_get_module_ctx(r, ngx_http_rtt_module);
        if (ctx == NULL) {
            ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_rtt_ctx_t));
            if (ctx == NULL) {
                return NGX_ERROR;
            }

            ngx_http_set_ctx(r, ctx, ngx_http_rtt_module);
        }

        if (ctx->ssl_handshake_end == 0) {
            ctx->ssl_handshake_end = ngx_current_msec;
        }

        if (r->connection->proxy_protocol == NULL) {
            r->connection->proxy_protocol = ngx_pcalloc(r->connection->pool,
                                                        sizeof(ngx_proxy_protocol_t));
            if (r->connection->proxy_protocol == NULL) {
                return NGX_ERROR;
            }
        }

        if (ctx->ssl_handshake_start > 0 && ctx->ssl_handshake_end > 0) {
            r->connection->proxy_protocol->tls_rtt = 
                (ctx->ssl_handshake_end - ctx->ssl_handshake_start) * 1000;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "TLS handshake RTT: %ui microseconds",
                           r->connection->proxy_protocol->tls_rtt);
        }
    }
#endif

    if (r->connection->proxy_protocol == NULL) {
        r->connection->proxy_protocol = ngx_pcalloc(r->connection->pool,
                                                    sizeof(ngx_proxy_protocol_t));
        if (r->connection->proxy_protocol == NULL) {
            return NGX_ERROR;
        }
    }

    if (r->connection->proxy_protocol->tcp_rtt == 0) {
        if (ngx_http_rtt_get_tcp_info(r->connection, &rtt) == NGX_OK) {
            r->connection->proxy_protocol->tcp_rtt = rtt;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "TCP RTT: %ui microseconds", rtt);
        }
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_rtt_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_rtt_handler;

    return NGX_OK;
}
