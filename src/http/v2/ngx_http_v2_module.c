
/*
 * Copyright (C) Nginx, Inc.
 * Copyright (C) Valentin V. Bartenev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_v2_module.h>


static ngx_int_t ngx_http_v2_add_variables(ngx_conf_t *cf);

static ngx_int_t ngx_http_v2_fingerprint_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_http_v2_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_http_v2_module_init(ngx_cycle_t *cycle);

static void *ngx_http_v2_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_v2_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_v2_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_v2_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
static void *ngx_http_v2_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_v2_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);

static char *ngx_http_v2_recv_buffer_size(ngx_conf_t *cf, void *post,
    void *data);
static char *ngx_http_v2_pool_size(ngx_conf_t *cf, void *post, void *data);
static char *ngx_http_v2_preread_size(ngx_conf_t *cf, void *post, void *data);
static char *ngx_http_v2_streams_index_mask(ngx_conf_t *cf, void *post,
    void *data);
static char *ngx_http_v2_chunk_size(ngx_conf_t *cf, void *post, void *data);
static char *ngx_http_v2_obsolete(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_conf_deprecated_t  ngx_http_v2_recv_timeout_deprecated = {
    ngx_conf_deprecated, "http2_recv_timeout", "client_header_timeout"
};

static ngx_conf_deprecated_t  ngx_http_v2_idle_timeout_deprecated = {
    ngx_conf_deprecated, "http2_idle_timeout", "keepalive_timeout"
};

static ngx_conf_deprecated_t  ngx_http_v2_max_requests_deprecated = {
    ngx_conf_deprecated, "http2_max_requests", "keepalive_requests"
};

static ngx_conf_deprecated_t  ngx_http_v2_max_field_size_deprecated = {
    ngx_conf_deprecated, "http2_max_field_size", "large_client_header_buffers"
};

static ngx_conf_deprecated_t  ngx_http_v2_max_header_size_deprecated = {
    ngx_conf_deprecated, "http2_max_header_size", "large_client_header_buffers"
};


static ngx_conf_post_t  ngx_http_v2_recv_buffer_size_post =
    { ngx_http_v2_recv_buffer_size };
static ngx_conf_post_t  ngx_http_v2_pool_size_post =
    { ngx_http_v2_pool_size };
static ngx_conf_post_t  ngx_http_v2_preread_size_post =
    { ngx_http_v2_preread_size };
static ngx_conf_post_t  ngx_http_v2_streams_index_mask_post =
    { ngx_http_v2_streams_index_mask };
static ngx_conf_post_t  ngx_http_v2_chunk_size_post =
    { ngx_http_v2_chunk_size };


static ngx_command_t  ngx_http_v2_commands[] = {

    { ngx_string("http2"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v2_srv_conf_t, enable),
      NULL },

    { ngx_string("http2_recv_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_v2_main_conf_t, recv_buffer_size),
      &ngx_http_v2_recv_buffer_size_post },

    { ngx_string("http2_pool_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v2_srv_conf_t, pool_size),
      &ngx_http_v2_pool_size_post },

    { ngx_string("http2_max_concurrent_streams"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v2_srv_conf_t, concurrent_streams),
      NULL },

    { ngx_string("http2_max_concurrent_pushes"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_v2_obsolete,
      0,
      0,
      NULL },

    { ngx_string("http2_max_requests"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_v2_obsolete,
      0,
      0,
      &ngx_http_v2_max_requests_deprecated },

    { ngx_string("http2_max_field_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_v2_obsolete,
      0,
      0,
      &ngx_http_v2_max_field_size_deprecated },

    { ngx_string("http2_max_header_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_v2_obsolete,
      0,
      0,
      &ngx_http_v2_max_header_size_deprecated },

    { ngx_string("http2_body_preread_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v2_srv_conf_t, preread_size),
      &ngx_http_v2_preread_size_post },

    { ngx_string("http2_streams_index_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v2_srv_conf_t, streams_index_mask),
      &ngx_http_v2_streams_index_mask_post },

    { ngx_string("http2_recv_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_v2_obsolete,
      0,
      0,
      &ngx_http_v2_recv_timeout_deprecated },

    { ngx_string("http2_idle_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_v2_obsolete,
      0,
      0,
      &ngx_http_v2_idle_timeout_deprecated },

    { ngx_string("http2_chunk_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_v2_loc_conf_t, chunk_size),
      &ngx_http_v2_chunk_size_post },

    { ngx_string("http2_push_preload"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_v2_obsolete,
      0,
      0,
      NULL },

    { ngx_string("http2_push"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_v2_obsolete,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_v2_module_ctx = {
    ngx_http_v2_add_variables,             /* preconfiguration */
    NULL,                                  /* postconfiguration */

    ngx_http_v2_create_main_conf,          /* create main configuration */
    ngx_http_v2_init_main_conf,            /* init main configuration */

    ngx_http_v2_create_srv_conf,           /* create server configuration */
    ngx_http_v2_merge_srv_conf,            /* merge server configuration */

    ngx_http_v2_create_loc_conf,           /* create location configuration */
    ngx_http_v2_merge_loc_conf             /* merge location configuration */
};


ngx_module_t  ngx_http_v2_module = {
    NGX_MODULE_V1,
    &ngx_http_v2_module_ctx,               /* module context */
    ngx_http_v2_commands,                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    ngx_http_v2_module_init,               /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t  ngx_http_v2_vars[] = {

    { ngx_string("http2"), NULL,
      ngx_http_v2_variable, 0, 0, 0 },
    { ngx_string("http2_fingerprint"), NULL,
      ngx_http_v2_fingerprint_variable, 0, 0, 0 },
      ngx_http_null_variable
};


static ngx_int_t
ngx_http_v2_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_v2_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}

void
calculate_fingerprint(ngx_http_request_t *r, ngx_str_t* out)
{
    size_t cur = 0;
    ngx_list_part_t *part;
    ngx_http_v2_setting_t* setting;
    ngx_http_v2_fingerprint_priority_t* priority;

    ngx_http_v2_fingerprint_t* fp = r->stream->connection->fp;
    ngx_list_part_t* setting_start = &fp->settings->part;
    ngx_list_part_t* prio_start = &fp->priorities->part;

    size_t setting_count = 0;
    size_t priority_count = 0;
    size_t header_count = 0;

    part = &fp->settings->part;
    setting = part->elts;

    // the amount of semicolons and colons in between kv pairs - 1 at the end
    size_t len = 0;
    while (part) {
        setting_count++;
        setting = part->elts;
        len += num_of_digits(setting->name)
            + 1
            + num_of_digits(setting->value) +
            (part->next != NULL ? 1 : 0);
        part = part->next;
    }
    // |12345| <- plus 2 pipes
    // |00| if there's no window_update
    size_t window_update_length = (fp->window_update == 0 ? 2 : num_of_digits(fp->window_update)) + 2;
    len += window_update_length;

    part = &fp->priorities->part;

    // not counting pipes here, it's handled by header order
    while (part) {
        priority = part->elts;
        priority_count++;
        // 3 = num of colons
        len += 3
            + num_of_digits(priority->stream_id)
            + num_of_digits(priority->exclusivity)
            + num_of_digits(priority->dependent_stream_id)
            + num_of_digits(priority->weight)
            + (part->next != NULL ? 1 : 0 );

        if (part->next == NULL) {
            break;
        }
        part = part->next;
    }
    if (priority_count == 0) {
        len+= 1;
    }
    // each header is one character separated by commas plus a pipe
    part = &fp->pseudo_headers->part;
    while (part) {
        header_count++;
        part = part->next;
    }

    len += (2 * header_count) + 1 ;

    out->data = ngx_pnalloc(r->pool, len);
    out->len = len;

    part = setting_start;
    while (part) {
        setting = part->elts;
        size_t part_len = num_of_digits(setting->name) + 1 + num_of_digits(setting->value);
        if (part->next != NULL) {
            // semicolon
            part_len += 1;
            ngx_snprintf(out->data + cur, part_len, "%ui:%ui;", setting->name, setting->value);
        } else {
            ngx_snprintf(out->data + cur, part_len, "%ui:%ui", setting->name, setting->value);
        }
        cur += part_len;
        part = part->next;
    }

    /// Window Update
    if (fp->window_update != 0) {
        ngx_snprintf(out->data + cur, window_update_length, "|%ui|", fp->window_update);
    } else {
        ngx_snprintf(out->data + cur, window_update_length, "|00|");
    }
    cur += window_update_length;

    /// Priority
    if (priority_count == 0) {
        ngx_snprintf(out->data + (cur++), 1, "0");
    } else {
        part = prio_start;
        while (part) {
            ngx_http_v2_fingerprint_priority_t* p = part->elts;
            // 3 = num of colons
            size_t part_len = 3
                + num_of_digits(p->stream_id)
                + num_of_digits(p->exclusivity)
                + num_of_digits(p->dependent_stream_id)
                + num_of_digits(p->weight);
            if (part->next != NULL) {
                // extra comma
                part_len += 1;
                ngx_snprintf(out->data + cur, part_len, "%ui:%ui:%ui:%ui,",
                    p->stream_id,
                    p->exclusivity,
                    p->dependent_stream_id,
                    p->weight
                );
            } else {
                ngx_snprintf(out->data + cur, part_len, "%ui:%ui:%ui:%ui",
                    p->stream_id,
                    p->exclusivity,
                    p->dependent_stream_id,
                    p->weight
                );
            }
            cur += part_len;

            if (part->next == NULL) {
                break;
            }
            part = part->next;
        }
    }
    // Pseudo headers
    ngx_list_part_t* headers = &fp->pseudo_headers->part;
    if (header_count > 0) {
        ngx_snprintf(out->data + (cur++), 1, "|");
        while (headers) {
            ngx_str_t* header = headers->elts;
            // nginx already strips out the leading colon of pseudo headers
            int header_prefix = (header->data)[0];
            len = 1;
            if (headers->next != NULL) {
                // extra comma
                len += 1;
                ngx_snprintf(out->data + cur, len, "%c,", header_prefix);
            } else {
                ngx_snprintf(out->data + cur, len, "%c", header_prefix);
            }
            cur += len;
            headers = headers->next;
        }
    }

    out->len = cur;
}

static ngx_int_t
ngx_http_v2_fingerprint_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t fp = ngx_null_string;
    if (r->connection == NULL) {
        return NGX_OK;
    }
    if (r->stream) {
        // prevent data from being collected form the same connection again
        r->stream->connection->fp->fingerprinted = 1;
        calculate_fingerprint(r, &fp);

        v->data = fp.data;
        v->len = fp.len;
        v->valid = 1;
        v->no_cacheable = 1;
        v->not_found = 0;
        return NGX_OK;
    }
    *v = ngx_http_variable_null_value;
    return NGX_OK;
}

static ngx_int_t
ngx_http_v2_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{

    if (r->stream) {
#if (NGX_HTTP_SSL)

        if (r->connection->ssl) {
            v->len = sizeof("h2") - 1;
            v->valid = 1;
            v->no_cacheable = 0;
            v->not_found = 0;
            v->data = (u_char *) "h2";

            return NGX_OK;
        }

#endif
        v->len = sizeof("h2c") - 1;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = (u_char *) "h2c";

        return NGX_OK;
    }

    *v = ngx_http_variable_null_value;

    return NGX_OK;
}


static ngx_int_t
ngx_http_v2_module_init(ngx_cycle_t *cycle)
{
    return NGX_OK;
}


static void *
ngx_http_v2_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_v2_main_conf_t  *h2mcf;

    h2mcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_v2_main_conf_t));
    if (h2mcf == NULL) {
        return NULL;
    }

    h2mcf->recv_buffer_size = NGX_CONF_UNSET_SIZE;

    return h2mcf;
}


static char *
ngx_http_v2_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_v2_main_conf_t *h2mcf = conf;

    ngx_conf_init_size_value(h2mcf->recv_buffer_size, 256 * 1024);

    return NGX_CONF_OK;
}


static void *
ngx_http_v2_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_v2_srv_conf_t  *h2scf;

    h2scf = ngx_pcalloc(cf->pool, sizeof(ngx_http_v2_srv_conf_t));
    if (h2scf == NULL) {
        return NULL;
    }

    h2scf->enable = NGX_CONF_UNSET;

    h2scf->pool_size = NGX_CONF_UNSET_SIZE;

    h2scf->concurrent_streams = NGX_CONF_UNSET_UINT;

    h2scf->preread_size = NGX_CONF_UNSET_SIZE;

    h2scf->streams_index_mask = NGX_CONF_UNSET_UINT;

    return h2scf;
}


static char *
ngx_http_v2_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_v2_srv_conf_t *prev = parent;
    ngx_http_v2_srv_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);

    ngx_conf_merge_size_value(conf->pool_size, prev->pool_size, 4096);

    ngx_conf_merge_uint_value(conf->concurrent_streams,
                              prev->concurrent_streams, 128);

    ngx_conf_merge_size_value(conf->preread_size, prev->preread_size, 65536);

    ngx_conf_merge_uint_value(conf->streams_index_mask,
                              prev->streams_index_mask, 32 - 1);

    return NGX_CONF_OK;
}


static void *
ngx_http_v2_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_v2_loc_conf_t  *h2lcf;

    h2lcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_v2_loc_conf_t));
    if (h2lcf == NULL) {
        return NULL;
    }

    h2lcf->chunk_size = NGX_CONF_UNSET_SIZE;

    return h2lcf;
}


static char *
ngx_http_v2_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_v2_loc_conf_t *prev = parent;
    ngx_http_v2_loc_conf_t *conf = child;

    ngx_conf_merge_size_value(conf->chunk_size, prev->chunk_size, 8 * 1024);

    return NGX_CONF_OK;
}


static char *
ngx_http_v2_recv_buffer_size(ngx_conf_t *cf, void *post, void *data)
{
    size_t *sp = data;

    if (*sp <= NGX_HTTP_V2_STATE_BUFFER_SIZE) {
        return "value is too small";
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_v2_pool_size(ngx_conf_t *cf, void *post, void *data)
{
    size_t *sp = data;

    if (*sp < NGX_MIN_POOL_SIZE) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the pool size must be no less than %uz",
                           NGX_MIN_POOL_SIZE);

        return NGX_CONF_ERROR;
    }

    if (*sp % NGX_POOL_ALIGNMENT) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the pool size must be a multiple of %uz",
                           NGX_POOL_ALIGNMENT);

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_v2_preread_size(ngx_conf_t *cf, void *post, void *data)
{
    size_t *sp = data;

    if (*sp > NGX_HTTP_V2_MAX_WINDOW) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the maximum body preread buffer size is %uz",
                           NGX_HTTP_V2_MAX_WINDOW);

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_v2_streams_index_mask(ngx_conf_t *cf, void *post, void *data)
{
    ngx_uint_t *np = data;

    ngx_uint_t  mask;

    mask = *np - 1;

    if (*np == 0 || (*np & mask)) {
        return "must be a power of two";
    }

    *np = mask;

    return NGX_CONF_OK;
}


static char *
ngx_http_v2_chunk_size(ngx_conf_t *cf, void *post, void *data)
{
    size_t *sp = data;

    if (*sp == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the http2 chunk size cannot be zero");

        return NGX_CONF_ERROR;
    }

    if (*sp > NGX_HTTP_V2_MAX_FRAME_SIZE) {
        *sp = NGX_HTTP_V2_MAX_FRAME_SIZE;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_v2_obsolete(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_conf_deprecated_t  *d = cmd->post;

    if (d) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"%s\" directive is obsolete, "
                           "use the \"%s\" directive instead",
                           d->old_name, d->new_name);

    } else {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"%V\" directive is obsolete, ignored",
                           &cmd->name);
    }

    return NGX_CONF_OK;
}
