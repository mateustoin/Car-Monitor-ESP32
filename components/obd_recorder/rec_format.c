#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "obd_recorder.h"

int rec_format_line(char *out, size_t cap, uint32_t t_ms, const char *cmd, const char *resp)
{
    if (!out || !cmd || !resp) return -1;
    int n = snprintf(out, cap, "%lu\t%s\t", (unsigned long)t_ms, cmd);
    if (n < 0 || (size_t)n >= cap) return -1;
    size_t len = (size_t)n;
    for (const char *p = resp; *p; p++) {
        if (len + 2 > cap) return -1;      /* espaço para o '\n' e o '\0' */
        out[len++] = (*p == '\r' || *p == '\n') ? '|' : *p;
    }
    if (len + 2 > cap) return -1;
    out[len++] = '\n';
    out[len] = '\0';
    return (int)len;
}

bool rec_parse_line(const char *line, uint32_t *t_ms, char *cmd, size_t cmd_cap,
                    char *resp, size_t resp_cap)
{
    if (!line || !t_ms || !cmd || !resp || cmd_cap == 0 || resp_cap == 0) return false;
    char *end;
    unsigned long ts = strtoul(line, &end, 10);
    if (end == line || *end != '\t') return false;
    const char *c = end + 1;
    const char *tab = strchr(c, '\t');
    if (!tab) return false;
    size_t clen = (size_t)(tab - c);
    if (clen >= cmd_cap) return false;
    const char *r = tab + 1;
    size_t rlen = strcspn(r, "\n");
    if (rlen >= resp_cap) return false;
    memcpy(cmd, c, clen);
    cmd[clen] = '\0';
    for (size_t i = 0; i < rlen; i++) resp[i] = (r[i] == '|') ? '\r' : r[i];
    resp[rlen] = '\0';
    *t_ms = (uint32_t)ts;
    return true;
}

int rec_trip_index(const char *name)
{
    if (!name || strncmp(name, "trip_", 5) != 0) return -1;
    const char *p = name + 5;
    int n = 0, digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 6) { n = n * 10 + (*p - '0'); p++; digits++; }
    if (digits == 0 || strcmp(p, ".tsv") != 0) return -1;
    return n;
}
