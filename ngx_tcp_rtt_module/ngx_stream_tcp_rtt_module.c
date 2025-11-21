#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include <ngx_proxy_protocol.h>
#include <netinet/tcp.h>      /* For TCP_INFO */

/* TCP info structure for getting RTT */
struct tcp_info_l {
    __u8    tcpi_state;
    __u8    tcpi_ca_state;
    __u8    tcpi_retransmits;
    __u8    tcpi_probes;
    __u8    tcpi_backoff;
    __u8    tcpi_options;
    __u8    tcpi_snd_wscale : 4, tcpi_rcv_wscale : 4;
    __u8    tcpi_delivery_rate_app_limited:1, tcpi_fastopen_client_fail:2;

    __u32   tcpi_rto;
    __u32   tcpi_ato;
    __u32   tcpi_snd_mss;
    __u32   tcpi_rcv_mss;

    __u32   tcpi_unacked;
    __u32   tcpi_sacked;
    __u32   tcpi_lost;
    __u32   tcpi_retrans;
    __u32   tcpi_fackets;

    /* Times. */
    __u32   tcpi_last_data_sent;
    __u32   tcpi_last_ack_sent;
    __u32   tcpi_last_data_recv;
    __u32   tcpi_last_ack_recv;

    /* Metrics. */
    __u32   tcpi_pmtu;
    __u32   tcpi_rcv_ssthresh;
    __u32   tcpi_rtt;
    __u32   tcpi_rttvar;
    __u32   tcpi_snd_ssthresh;
    __u32   tcpi_snd_cwnd;
    __u32   tcpi_advmss;
    __u32   tcpi_reordering;

    __u32   tcpi_rcv_rtt;
    __u32   tcpi_rcv_space;

    __u32   tcpi_total_retrans;

    __u64   tcpi_pacing_rate;
    __u64   tcpi_max_pacing_rate;
    __u64   tcpi_bytes_acked;
    __u64   tcpi_bytes_received;
    __u32   tcpi_segs_out;
    __u32   tcpi_segs_in;

    __u32   tcpi_notsent_bytes;
    __u32   tcpi_min_rtt;
    __u32   tcpi_data_segs_in;
    __u32   tcpi_data_segs_out;

    __u64   tcpi_delivery_rate;
    
    __u64   tcpi_busy_time;
    __u64   tcpi_rwnd_limited;
    __u64   tcpi_sndbuf_limited;

    __u32   tcpi_delivered;
    __u32   tcpi_delivered_ce;

    __u64   tcpi_bytes_sent;
    __u64   tcpi_bytes_retrans;
    __u32   tcpi_dsack_dups;
    __u32   tcpi_reord_seen;

    __u32   tcpi_rcv_ooopack;

    __u32   tcpi_snd_wnd;
};

/* Stream module configuration */
typedef struct {
    ngx_flag_t  enable_tcp_rtt;
} ngx_stream_tcp_rtt_srv_conf_t;

/* Stream session context */
typedef struct {
    ngx_msec_t  tcp_rtt;
    ngx_flag_t  tcp_rtt_set;
} ngx_stream_tcp_rtt_ctx_t;

/* Forward declarations */
static ngx_int_t ngx_stream_tcp_rtt_handler(ngx_stream_session_t *s);
static void *ngx_stream_tcp_rtt_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_tcp_rtt_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_stream_tcp_rtt_init(ngx_conf_t *cf);

/* Stream module commands */
static ngx_command_t ngx_stream_tcp_rtt_commands[] = {
    { ngx_string("tcp_rtt"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_tcp_rtt_srv_conf_t, enable_tcp_rtt),
      NULL },

    ngx_null_command
};

/* Stream module context */
static ngx_stream_module_t ngx_stream_tcp_rtt_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_stream_tcp_rtt_init,               /* postconfiguration */
    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */
    ngx_stream_tcp_rtt_create_srv_conf,    /* create server configuration */
    ngx_stream_tcp_rtt_merge_srv_conf      /* merge server configuration */
};

/* Stream module */
ngx_module_t ngx_stream_tcp_rtt_module = {
    NGX_MODULE_V1,
    &ngx_stream_tcp_rtt_module_ctx,        /* module context */
    ngx_stream_tcp_rtt_commands,           /* module directives */
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

/* Helper function to get TCP RTT */
static ngx_msec_t
ngx_get_tcp_rtt(ngx_connection_t *c)
{
    struct tcp_info_l  tcp_info;
    socklen_t          tcp_info_len;

    tcp_info_len = sizeof(tcp_info);
    
    if (getsockopt(c->fd, IPPROTO_TCP, TCP_INFO, &tcp_info, &tcp_info_len) == -1) {
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                       "tcp_rtt: failed to get TCP_INFO");
        return 0;
    }

    /* tcpi_rtt is in microseconds, convert to milliseconds */
    return tcp_info.tcpi_rtt / 1000;
}

/* Stream configuration functions */
static void *
ngx_stream_tcp_rtt_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_tcp_rtt_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_tcp_rtt_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable_tcp_rtt = NGX_CONF_UNSET;

    return conf;
}

static char *
ngx_stream_tcp_rtt_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_stream_tcp_rtt_srv_conf_t *prev = parent;
    ngx_stream_tcp_rtt_srv_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable_tcp_rtt, prev->enable_tcp_rtt, 0);

    return NGX_CONF_OK;
}

/* Stream module handler */
static ngx_int_t
ngx_stream_tcp_rtt_handler(ngx_stream_session_t *s)
{
    ngx_stream_tcp_rtt_srv_conf_t  *sscf;
    ngx_stream_tcp_rtt_ctx_t       *ctx;
    ngx_connection_t                *c;
    ngx_str_t                       rtt_str;
    u_char                          rtt_buf[NGX_INT_T_LEN];

    sscf = ngx_stream_get_module_srv_conf(s, ngx_stream_tcp_rtt_module);
    
    if (!sscf->enable_tcp_rtt) {
        return NGX_DECLINED;
    }

    c = s->connection;

    /* Get TCP RTT */
    ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_stream_tcp_rtt_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ctx->tcp_rtt = ngx_get_tcp_rtt(c);
    if (ctx->tcp_rtt > 0) {
        ctx->tcp_rtt_set = 1;

        /* Add TCP RTT to proxy protocol v2 TLV */
        rtt_str.len = ngx_sprintf(rtt_buf, "%M", ctx->tcp_rtt) - rtt_buf;
        rtt_str.data = rtt_buf;

        if (ngx_proxy_protocol_set_tlv(c, NGX_PROXY_PROTOCOL_TLV_TCP_RTT, &rtt_str) != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,
                          "tcp_rtt: failed to set TCP RTT TLV");
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0,
                           "tcp_rtt: added TCP RTT %M ms to proxy protocol", ctx->tcp_rtt);
        }
    }

    ngx_stream_set_ctx(s, ctx, ngx_stream_tcp_rtt_module);

    return NGX_DECLINED;
}

/* Stream module initialization */
static ngx_int_t
ngx_stream_tcp_rtt_init(ngx_conf_t *cf)
{
    ngx_stream_core_main_conf_t  *cmcf;
    ngx_stream_handler_pt         *h;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_tcp_rtt_handler;

    return NGX_OK;
}