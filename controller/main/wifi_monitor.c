#include "wifi_monitor.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/gpio.h"

#include "config.h"
#include "lc72130_bus.h"
#include "lc72130_emulator.h"
#include "frequency_test.h"
#include "ota_update.h"
#include "radio_link.h"
#include "rds_clock.h"

static const char *TAG = "WIFI_MONITOR";
static httpd_handle_t server;

typedef struct {
    radio_metadata_t metadata;
    char station[65];
    char now_playing[321];
    char genre[49];
    char bitrate[33];
    char stream_url[385];
    char response[1600];
    bool clock_synced;
    char clock_utc[24];
} status_workspace_t;

static void json_escape(char *destination, size_t destination_size, const char *source)
{
    size_t output = 0;
    for (size_t input = 0; source[input] != '\0' && output + 1 < destination_size; input++) {
        unsigned char character = source[input];
        if ((character == '"' || character == '\\') && output + 2 < destination_size) {
            destination[output++] = '\\';
            destination[output++] = character;
        } else if (character >= 0x20) {
            destination[output++] = character;
        }
    }
    destination[output] = '\0';
}

/* Minimal in-place '%XX'/'+' decoder for query-string test parameters. */
static void url_decode(char *value)
{
    char *write = value;
    for (char *read = value; *read != '\0'; read++, write++) {
        if (*read == '+') {
            *write = ' ';
        } else if (*read == '%' && isxdigit((unsigned char)read[1]) && isxdigit((unsigned char)read[2])) {
            char hex[3] = {read[1], read[2], '\0'};
            *write = (char)strtol(hex, NULL, 16);
            read += 2;
        } else {
            *write = *read;
        }
    }
    *write = '\0';
}

static const char dashboard_html[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>LC72130 Monitor</title><style>"
    ":root{font-family:ui-monospace,monospace;color:#17211b;background:#edf1e8}"
    "body{max-width:900px;margin:0 auto;padding:28px 18px}"
    "h1{font-size:24px;margin:0 0 6px}h2{font-size:17px;margin:28px 0 10px}small{color:#617066}"
    "main{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}"
    "section{background:#fff;border:1px solid #c9d2c7;border-radius:6px;padding:16px}"
    "label{display:block;color:#617066;font-size:12px;text-transform:uppercase}"
    "strong{display:block;font-size:25px;margin-top:8px;overflow-wrap:anywhere}.wide{grid-column:1/-1}.text{font-size:17px}"
    ".ok{color:#167146}.bad{color:#a33b2b}"
    "form{display:flex;gap:8px;margin-top:20px}input{font:inherit;padding:10px;border:1px solid #9aa69c;border-radius:4px}"
    "button{font:inherit;padding:10px 14px;border:0;border-radius:4px;background:#176b45;color:white;cursor:pointer}"
    "select{font:inherit;padding:8px;border:1px solid #9aa69c;border-radius:4px}"
    ".overrides{display:flex;flex-wrap:wrap;gap:16px;align-items:end}.overrides div{display:flex;flex-direction:column;gap:4px}"
    "table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #c9d2c7;border-radius:6px;overflow:hidden;font-size:13px}"
    "th,td{padding:6px 10px;text-align:left;border-bottom:1px solid #e2e7de}th{background:#f3f5f0;text-transform:uppercase;font-size:11px;color:#617066}"
    "td.bad{background:#fbeceA}"
    "</style></head><body><h1>LC72130 Monitor</h1><small id=updated>Connecting...</small>"
    "<h2>Tuner</h2><main id=stats></main><h2>Radio ESP / RDS</h2><main id=rds></main>"
    "<h2>Manual Overrides</h2><div class=overrides>"
    "<div><label>ST GPIO2</label><button id=stToggle type=button>Toggle</button></div>"
    "<div><label>AST GPIO3</label><button id=astToggle type=button>Toggle</button></div>"
    "<div><label>SI GPIO9</label><button id=siToggle type=button>Toggle</button></div>"
    "<div><label>D-IN GPIO18</label><button id=dinPulse type=button>Normal HIGH &rarr; Pulse LOW</button></div>"
    "</div>"
    "<h2>Raw RDS Groups (GPIO11 RDS-D / GPIO12 RDS-C)</h2>"
    "<table><thead><tr><th>#</th><th>Type</th><th>PI</th><th>Block B</th><th>Block C</th>"
    "<th>Block D</th><th>Addr</th><th>Text / Clock Time</th><th>CRC</th></tr></thead><tbody id=rdsRaw></tbody></table>"
    "<form id=test><input id=mhz type=number min=76 max=108 step=.05 value=106.00 "
    "aria-label=\"Test frequency in MHz\"><button type=submit>Simulate frequency</button></form><script>"
    "const labels={frequency_mhz:'Frequency',pll_locked:'PLL',tuner_ready:'Tuner',frames:'Frames',"
    "frame_errors:'Frame errors',transactions:'Transactions',read_requests:'Read requests',uptime_seconds:'Uptime'};"
    "const rdsLabels={rds_ps:'RDS PS (0A)',now_playing:'RadioText (2A)',genre:'Genre',pty:'PTY code',"
    "bitrate:'Bitrate',stream_url:'Stream URL',state:'Playback state',playing:'Playing',"
    "st_output:'ST GPIO2',ast_output:'AST GPIO3',radio_ip:'Radio ESP API',"
    "clock_synced:'Clock synced (NTP)',clock_utc:'Clock Time (4A, UTC)'};"
    "const show=(k,v)=>{if(k==='frequency_mhz')return Number(v).toFixed(2)+' MHz';"
    "if(k==='pll_locked')return v?'LOCKED':'UNLOCKED';if(k==='tuner_ready')return v?'READY':'NOT READY';"
    "if(k==='uptime_seconds')return v+' s';return v};"
    "const render=(root,map,d)=>{root.replaceChildren(...Object.keys(map).map(k=>{const s=document.createElement('section');"
    "if(k==='now_playing'||k==='stream_url')s.className='wide';const l=document.createElement('label');l.textContent=map[k];"
    "const v=document.createElement('strong');v.className=(typeof d[k]==='boolean'?(d[k]?'ok':'bad'):'')+"
    "((k==='now_playing'||k==='stream_url')?' text':'');v.textContent=d[k]===true?'YES':d[k]===false?'NO':show(k,d[k])||'--';"
    "s.append(l,v);return s}))};"
    "async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});const d=await r.json();"
    "render(stats,labels,d);render(rds,rdsLabels,d);"
    "stToggle.textContent=d.st_override==='on'?'ST: ON':'ST: OFF';"
    "astToggle.textContent=d.ast_override==='on'?'AST: ON':'AST: OFF';"
    "siToggle.textContent=d.si_override==='on'?'SI: ON':'SI: OFF';"
    "updated.textContent='Updated '+new Date().toLocaleTimeString()}catch(e){updated.textContent='Monitor unavailable'};}"
    "async function refreshRds(){try{const r=await fetch('/api/rds-raw',{cache:'no-store'});const groups=await r.json();"
    "rdsRaw.replaceChildren(...groups.map(g=>{const tr=document.createElement('tr');"
    "const cells=[g.group,g.type,g.pi,g.block_b,g.block_c,g.block_d,g.address,g.clock_text||g.text,g.valid?'OK':'FAIL'];"
    "cells.forEach((c,i)=>{const td=document.createElement('td');td.textContent=c;"
    "if(i===8&&!g.valid)td.className='bad';tr.append(td)});return tr}))}catch(e){}}"
    "test.onsubmit=async e=>{e.preventDefault();const r=await fetch('/api/test-frequency?mhz='+mhz.value,{method:'POST'});"
    "updated.textContent=r.ok?'Test frequency sent':await r.text();refresh()};"
    "stToggle.onclick=async()=>{const r=await fetch('/api/status',{cache:'no-store'});const d=await r.json();"
    "await fetch('/api/control-st?state='+(d.st_override==='on'?'off':'on'),{method:'POST'});refresh()};"
    "astToggle.onclick=async()=>{const r=await fetch('/api/status',{cache:'no-store'});const d=await r.json();"
    "await fetch('/api/control-ast?state='+(d.ast_override==='on'?'off':'on'),{method:'POST'});refresh()};"
    "siToggle.onclick=async()=>{const r=await fetch('/api/status',{cache:'no-store'});const d=await r.json();"
    "await fetch('/api/control-si?state='+(d.si_override==='on'?'off':'on'),{method:'POST'});refresh()};"
    "dinPulse.onclick=async()=>{await fetch('/api/control-din?state=pulse',{method:'POST'});refresh()};"
    "refresh();refreshRds();setInterval(refresh,2000);setInterval(refreshRds,2000);</script></body></html>";

static esp_err_t dashboard_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html");
    return httpd_resp_send(request, dashboard_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    const lc72130_state_t *state = lc72130_emulator_get_state();
    status_workspace_t *workspace = calloc(1, sizeof(*workspace));
    if (workspace == NULL) {
        return httpd_resp_send_500(request);
    }
    radio_link_get_metadata(&workspace->metadata);
    char radio_ip[46] = "";
    radio_link_get_radio_ip(radio_ip, sizeof(radio_ip));
    json_escape(workspace->station, sizeof(workspace->station), workspace->metadata.station);
    json_escape(workspace->now_playing, sizeof(workspace->now_playing), workspace->metadata.now_playing);
    json_escape(workspace->genre, sizeof(workspace->genre), workspace->metadata.genre);
    json_escape(workspace->bitrate, sizeof(workspace->bitrate), workspace->metadata.bitrate);
    json_escape(workspace->stream_url, sizeof(workspace->stream_url), workspace->metadata.stream_url);
    workspace->clock_synced = rds_clock_get_utc_string(workspace->clock_utc, sizeof(workspace->clock_utc));
    int length = snprintf(workspace->response, sizeof(workspace->response),
                          "{\"frequency_mhz\":%.2f,\"pll_locked\":%s,\"tuner_ready\":%s,"
                          "\"frames\":%lu,\"frame_errors\":%lu,\"transactions\":%lu,"
                          "\"read_requests\":%lu,\"uptime_seconds\":%lld,"
                          "\"station\":\"%.32s\",\"rds_ps\":\"%.8s\","
                          "\"now_playing\":\"%.160s\",\"genre\":\"%.24s\","
                          "\"pty\":%u,\"bitrate\":\"%.16s\",\"stream_url\":\"%.192s\","
                          "\"state\":\"%s\",\"playing\":%s,\"st_output\":%s,\"ast_output\":%s,"
                          "\"radio_ip\":\"%.45s\",\"din_last_response\":%u,\"din_state\":%s,"
                          "\"din_ready\":%s,\"din_init_error\":\"%s\","
                          "\"st_override\":\"%s\",\"ast_override\":\"%s\","
                          "\"si_override\":\"%s\",\"si_output\":%s,\"din_override\":\"%s\","
                          "\"radio_link_init_error\":\"%s\",\"rds_clock_state\":%s,"
                          "\"rds_data_state\":%s,\"rds_isr_ticks\":%lu,\"rds_running\":%s,"
                          "\"clock_synced\":%s,\"clock_utc\":\"%s\",\"rds_pi\":\"0x%04X\"}",
                          state->current_frequency_mhz,
                          state->pll_locked ? "true" : "false",
                          state->tuner_ready ? "true" : "false",
                          (unsigned long)lc72130_bus_get_frame_count(),
                          (unsigned long)lc72130_bus_get_error_count(),
                          (unsigned long)lc72130_emulator_get_transaction_count(),
                          (unsigned long)lc72130_emulator_get_read_count(),
                          esp_timer_get_time() / 1000000,
                          workspace->station, workspace->station, workspace->now_playing,
                          workspace->genre, workspace->metadata.pty, workspace->bitrate,
                          workspace->stream_url,
                          rds_output_playback_state_name(workspace->metadata.playback_state),
                          workspace->metadata.playback_state == RADIO_PLAYBACK_PLAYING ? "true" : "false",
                          gpio_get_level(GPIO_ST) ? "true" : "false",
                          gpio_get_level(GPIO_AST) ? "true" : "false",
                          radio_ip,
                          state->last_din_response_sent,
                          lc72130_bus_get_din_state() ? "true" : "false",
                          lc72130_bus_din_hardware_ready() ? "true" : "false",
                          esp_err_to_name(lc72130_bus_get_din_init_error()),
                          radio_link_override_name(radio_link_get_st_override()),
                          radio_link_override_name(radio_link_get_ast_override()),
                          radio_link_override_name(radio_link_get_si_override()),
                          gpio_get_level(GPIO_SI) ? "true" : "false",
                          lc72130_bus_din_override_name(lc72130_bus_get_din_override()),
                          esp_err_to_name(radio_link_get_init_error()),
                          rds_output_get_clock_state() ? "true" : "false",
                          rds_output_get_data_state() ? "true" : "false",
                          (unsigned long)rds_output_get_isr_tick_count(),
                          rds_output_is_running() ? "true" : "false",
                          workspace->clock_synced ? "true" : "false",
                          workspace->clock_utc, rds_output_get_pi());

    if (length < 0 || length >= sizeof(workspace->response)) {
        free(workspace);
        return httpd_resp_send_500(request);
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(request, workspace->response, length);
    free(workspace);
    return err;
}

static esp_err_t rds_raw_handler(httpd_req_t *request)
{
    rds_decoded_group_t *groups = calloc(RDS_DECODED_GROUP_COUNT, sizeof(rds_decoded_group_t));
    char *response = malloc(8192);
    if (groups == NULL || response == NULL) {
        free(groups);
        free(response);
        return httpd_resp_send_500(request);
    }

    size_t count = 0;
    rds_output_decode_groups(groups, RDS_DECODED_GROUP_COUNT, &count);

    size_t offset = 0;
    offset += snprintf(response + offset, 8192 - offset, "[");
    for (size_t i = 0; i < count; i++) {
        char text[9];
        char clock_text[48];
        json_escape(text, sizeof(text), groups[i].text);
        json_escape(clock_text, sizeof(clock_text), groups[i].clock_text);
        offset += snprintf(response + offset, 8192 - offset,
                           "%s{\"group\":%u,\"type\":\"%u%c\",\"pi\":\"0x%04X\","
                           "\"block_b\":\"0x%04X\",\"block_c\":\"0x%04X\",\"block_d\":\"0x%04X\","
                           "\"address\":%u,\"text\":\"%s\",\"clock_text\":\"%s\",\"valid\":%s}",
                           i == 0 ? "" : ",", (unsigned)i, groups[i].group_type, groups[i].version,
                           groups[i].pi, groups[i].block_b, groups[i].block_c, groups[i].block_d,
                           groups[i].address, text, clock_text, groups[i].valid ? "true" : "false");
        if (offset >= 8192) {
            break;
        }
    }
    offset += snprintf(response + offset, offset < 8192 ? 8192 - offset : 0, "]");

    free(groups);
    if (offset >= 8192) {
        free(response);
        return httpd_resp_send_500(request);
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(request, response, offset);
    free(response);
    return err;
}

static esp_err_t test_frequency_handler(httpd_req_t *request)
{
    char query[48];
    char value[20];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "mhz", value, sizeof(value)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Missing mhz parameter");
    }

    char *end;
    float frequency_mhz = strtof(value, &end);
    if (*value == '\0' || *end != '\0' ||
        frequency_test_simulate(frequency_mhz) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Frequency must be between 76.00 and 108.00 MHz");
    }

    return httpd_resp_sendstr(request, "Frequency queued for simulation and forwarding");
}

static esp_err_t test_radio_ip_handler(httpd_req_t *request)
{
    char query[64];
    char ip[46];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "ip", ip, sizeof(ip)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Missing ip parameter");
    }

    char line[64];
    snprintf(line, sizeof(line), "IP: [%s]", ip);
    if (radio_link_process_metadata_line(line) != ESP_OK) {
        return httpd_resp_send_500(request);
    }
    return httpd_resp_sendstr(request, "Radio ESP IP cached; polling will begin on the next cycle");
}

static esp_err_t test_radio_metadata_handler(httpd_req_t *request)
{
    char now_playing[161] = "Artist - Title";
    char query[256];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "now_playing", now_playing, sizeof(now_playing)) == ESP_OK) {
            url_decode(now_playing);
        }
    }

    char now_playing_line[192];
    snprintf(now_playing_line, sizeof(now_playing_line), "NOW_PLAYING: [%s]", now_playing);

    const char *lines[] = {
        "STATION: [RADIO538]",
        now_playing_line,
        "GENRE: [Pop]",
        "BITRATE: [128 KBPS]",
        "STREAM_URL: [playerservices.streamtheworld.com/...]",
        "PLAYING: [TRUE]",
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        if (radio_link_process_metadata_line(lines[i]) != ESP_OK) {
            return httpd_resp_send_500(request);
        }
    }
    return httpd_resp_sendstr(request, "Sample Radio ESP metadata applied to RDS");
}

static esp_err_t test_playback_state_handler(httpd_req_t *request)
{
    char query[32];
    char value[16];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "state", value, sizeof(value)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Missing state parameter (idle, search, true)");
    }

    char line[32];
    snprintf(line, sizeof(line), "PLAYING: [%s]", value);
    if (radio_link_process_metadata_line(line) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Unrecognized state");
    }
    return httpd_resp_sendstr(request, "Playback state applied");
}

static esp_err_t now_playing_mock_handler(httpd_req_t *request)
{
    /* Self-test fixture only: mimics the Radio ESP's /api/now-playing contract
     * so the controller's own HTTP+JSON poll path can be exercised via loopback
     * before real Radio ESP hardware is reachable. */
    static const char body[] =
        "{\"station\":\"LOOPBACK\",\"now_playing\":\"Poll Self-Test\","
        "\"genre\":\"Pop\",\"bitrate\":\"64 KBPS\",\"stream_url\":\"http://example.invalid/stream\","
        "\"state\":\"playing\"}";
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t parse_on_off(const char *value, bool *enabled_out)
{
    if (strcasecmp(value, "on") == 0) {
        *enabled_out = true;
    } else if (strcasecmp(value, "off") == 0) {
        *enabled_out = false;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t control_st_handler(httpd_req_t *request)
{
    char query[32];
    char value[16];
    bool enabled;
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "state", value, sizeof(value)) != ESP_OK ||
        parse_on_off(value, &enabled) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "state must be on or off");
    }
    radio_link_set_st_override(enabled ? GPIO_OVERRIDE_FORCE_ON : GPIO_OVERRIDE_FORCE_OFF);
    return httpd_resp_sendstr(request, "ST output applied");
}

static esp_err_t control_ast_handler(httpd_req_t *request)
{
    char query[32];
    char value[16];
    bool enabled;
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "state", value, sizeof(value)) != ESP_OK ||
        parse_on_off(value, &enabled) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "state must be on or off");
    }
    radio_link_set_ast_override(enabled ? GPIO_OVERRIDE_FORCE_ON : GPIO_OVERRIDE_FORCE_OFF);
    return httpd_resp_sendstr(request, "AST output applied");
}

static esp_err_t control_si_handler(httpd_req_t *request)
{
    char query[32];
    char value[16];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "state", value, sizeof(value)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "state must be on or off");
    }

    bool enabled;
    if (parse_on_off(value, &enabled) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "state must be on or off");
    }

    radio_link_set_si_override(enabled ? GPIO_OVERRIDE_FORCE_ON : GPIO_OVERRIDE_FORCE_OFF);
    return httpd_resp_sendstr(request, "SI output applied");
}

static esp_err_t control_din_handler(httpd_req_t *request)
{
    char query[32];
    char value[16];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "state", value, sizeof(value)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "state must be high or pulse");
    }

    if (strcasecmp(value, "pulse") == 0) {
        if (lc72130_bus_pulse_din_low(200) != ESP_OK) {
            httpd_resp_set_status(request, "400 Bad Request");
            return httpd_resp_sendstr(request, "D-IN pulse rejected (check EMULATOR mode and hardware readiness)");
        }
        return httpd_resp_sendstr(request, "D-IN pulsed LOW for 200 ms, then returned to HIGH");
    }

    din_override_t mode;
    if (strcasecmp(value, "high") == 0) {
        mode = DIN_OVERRIDE_FORCE_HIGH;
    } else if (strcasecmp(value, "auto") == 0) {
        mode = DIN_OVERRIDE_AUTO;
    } else if (strcasecmp(value, "low") == 0) {
        mode = DIN_OVERRIDE_FORCE_LOW;
    } else {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "state must be high or pulse");
    }

    if (lc72130_bus_set_din_override(mode) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "D-IN override rejected (check EMULATOR mode and hardware readiness)");
    }
    return httpd_resp_sendstr(request, "D-IN override applied");
}

static esp_err_t start_http_server(void)
{
    if (server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 18;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t dashboard_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = dashboard_handler,
    };
    const httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
    };
    const httpd_uri_t test_frequency_uri = {
        .uri = "/api/test-frequency",
        .method = HTTP_POST,
        .handler = test_frequency_handler,
    };
    const httpd_uri_t test_radio_metadata_uri = {
        .uri = "/api/test-radio-metadata",
        .method = HTTP_POST,
        .handler = test_radio_metadata_handler,
    };
    const httpd_uri_t ota_uri = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = ota_update_handler,
    };
    const httpd_uri_t test_playback_state_uri = {
        .uri = "/api/test-playback-state",
        .method = HTTP_POST,
        .handler = test_playback_state_handler,
    };
    const httpd_uri_t test_radio_ip_uri = {
        .uri = "/api/test-radio-ip",
        .method = HTTP_POST,
        .handler = test_radio_ip_handler,
    };
    const httpd_uri_t now_playing_mock_uri = {
        .uri = "/api/now-playing",
        .method = HTTP_GET,
        .handler = now_playing_mock_handler,
    };
    const httpd_uri_t control_st_uri = {
        .uri = "/api/control-st",
        .method = HTTP_POST,
        .handler = control_st_handler,
    };
    const httpd_uri_t control_ast_uri = {
        .uri = "/api/control-ast",
        .method = HTTP_POST,
        .handler = control_ast_handler,
    };
    const httpd_uri_t control_si_uri = {
        .uri = "/api/control-si",
        .method = HTTP_POST,
        .handler = control_si_handler,
    };
    const httpd_uri_t control_din_uri = {
        .uri = "/api/control-din",
        .method = HTTP_POST,
        .handler = control_din_handler,
    };
    const httpd_uri_t rds_raw_uri = {
        .uri = "/api/rds-raw",
        .method = HTTP_GET,
        .handler = rds_raw_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &dashboard_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &test_frequency_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &test_radio_metadata_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ota_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &test_playback_state_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &test_radio_ip_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &now_playing_mock_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &control_st_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &control_ast_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &control_si_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &control_din_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &rds_raw_uri));
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u); reconnecting", disconnected->reason);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        ESP_LOGI(TAG, "Remote monitor: http://" IPSTR "/", IP2STR(&got_ip->ip_info.ip));
        esp_err_t err = start_http_server();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        }
        rds_clock_init();
    }
}

esp_err_t wifi_monitor_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize TCP/IP");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Failed to initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    wifi_event_handler, NULL),
                        TAG, "Failed to register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    wifi_event_handler, NULL),
                        TAG, "Failed to register IP event handler");

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_LC72130_WIFI_SSID,
            .password = CONFIG_LC72130_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG,
                        "Failed to set Wi-Fi configuration");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start Wi-Fi");
    ESP_LOGI(TAG, "Connecting to Wi-Fi network %s", CONFIG_LC72130_WIFI_SSID);
    return ESP_OK;
}