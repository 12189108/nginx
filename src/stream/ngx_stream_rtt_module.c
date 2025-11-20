
/*
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

#if (NGX_LINUX)
#include <netinet/tcp.h>
#endif


static ngx_int_t ngx_stream_rtt_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_rtt_get_tcp_info(ngx_connection_t *c, 
    ngx_uint_t *rtt);
static ngx_int_t ngx_stream_rtt_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_stream_rtt_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_rtt_postconfiguration(ngx_conf_t *cf);


static ngx_stream_module_t  ngx_stream_rtt_module_ctx = {
    ngx_stream_rtt_add_variables,          /* preconfiguration */
    ngx_stream_rtt_postconfiguration,      /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL                                   /* merge server configuration */
};


ngx_module_t  ngx_stream_rtt_module = {
    NGX_MODULE_V1,
    &ngx_stream_rtt_module_ctx,            /* module context */
    NULL,                                  /* module directives */
    NGX_STREAM_MODULE,                     /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_stream_variable_t  ngx_stream_rtt_vars[] = {

    { ngx_string("tcp_rtt"), NULL,
      ngx_stream_rtt_variable, 0,
      NGX_STREAM_VAR_NOCACHEABLE, 0 },

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};


static ngx_int_t
ngx_stream_rtt_add_variables(ngx_conf_t *cf)
{
    ngx_stream_variable_t  *var, *v;

    for (v = ngx_stream_rtt_vars; v->name.len; v++) {
        var = ngx_stream_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_stream_rtt_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    u_char       *p;
    ngx_uint_t    rtt;

    rtt = 0;

    if (s->connection->proxy_protocol
        && s->connection->proxy_protocol->tcp_rtt > 0)
    {
        rtt = s->connection->proxy_protocol->tcp_rtt;
    } else {
        if (ngx_stream_rtt_get_tcp_info(s->connection, &rtt) != NGX_OK) {
            v->not_found = 1;
            return NGX_OK;
        }
    }

    if (rtt == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    p = ngx_pnalloc(s->connection->pool, NGX_INT32_LEN);
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
ngx_stream_rtt_get_tcp_info(ngx_connection_t *c, ngx_uint_t *rtt)
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
ngx_stream_rtt_handler(ngx_stream_session_t *s)
{
    ngx_uint_t  rtt;

    if (s->connection->proxy_protocol == NULL) {
        s->connection->proxy_protocol = ngx_pcalloc(s->connection->pool,
                                                    sizeof(ngx_proxy_protocol_t));
        if (s->connection->proxy_protocol == NULL) {
            return NGX_ERROR;
        }
    }

    if (ngx_stream_rtt_get_tcp_info(s->connection, &rtt) == NGX_OK) {
        s->connection->proxy_protocol->tcp_rtt = rtt;

        ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                       "stream TCP RTT: %ui microseconds", rtt);
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_stream_rtt_postconfiguration(ngx_conf_t *cf)
{
    ngx_stream_handler_pt        *h;
    ngx_stream_core_main_conf_t  *cmcf;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_PREREAD_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_rtt_handler;

    return NGX_OK;
}
