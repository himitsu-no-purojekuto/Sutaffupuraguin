/**
 * This file is part of Hercules.
 * http://herc.ws - http://github.com/HerculesWS/Hercules
 *
 * Copyright (C) 2014-2016  Hercules Dev Team
 * Copyright (C) 2016  Andrei Karas
 *
 * Hercules is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common/hercules.h"
#include "common/socket.h"

#include "login/lclif.h"

#include "char/char.h"

#ifdef DEFAULT_AUTOSAVE_INTERVAL
#undef DEFAULT_AUTOSAVE_INTERVAL
#endif  // DEFAULT_AUTOSAVE_INTERVAL

#include "map/clif.h"
#include "map/pc.h"

#include "plugins/HPMHooking.h"

#include <stdio.h>
#include <stdlib.h>

#include "common/HPMDataCheck.h"

char *packet_names[MAX_PACKET_DB + 1];

HPExport struct hplugin_info pinfo = {
    "packer_logger",    // Plugin name
    SERVER_TYPE_LOGIN | SERVER_TYPE_CHAR | SERVER_TYPE_MAP,     // Which server types this plugin works with?
    "0.1",               // Plugin version
    HPM_VERSION,         // HPM Version (don't change, macro is automatically updated)
};

void dump_data(FILE *file, int fd, int packet_len)
{
    for (int f = 0; f < packet_len; f ++)
    {
        const unsigned int data = (unsigned int)RFIFOB(fd, f);
        fprintf(file, "%05d 0x%02x %5u %c\n", f, data, data, (int)data);
    }
    fprintf(file, "-----------------------------------\n");
}

void show_time(FILE *file)
{
    char timestring[255];
    time_t curtime;
    time(&curtime);
    strftime(timestring, 254, "%m/%d/%Y %H:%M:%S", localtime(&curtime));
    fprintf(file, "%s\n", timestring);
}

void dump_client_packet(FILE *file, int fd, int packet_len)
{
    unsigned int packet_id = RFIFOW(fd, 0);
    fprintf(file, "client packet: 0x%x %u, len: %d\n", packet_id, packet_id, packet_len);
    dump_data(file, fd, packet_len);
}

void dump_client_map_packet(FILE *file, int fd, unsigned int packet_id, int packet_len, const struct s_packet_db *packet)
{
    if (packet_id > 0 && packet_id <= MAX_PACKET_DB)
    {
        if (packet->func == NULL)
            fprintf(file, "client packet: 0x%x %u, len: %5d, Missing function!\n", packet_id, packet_id, packet_len);
        else
            fprintf(file, "client packet: 0x%x %u, len: %5d, function: %s\n", packet_id, packet_id, packet_len, packet_names[packet_id]);
    }
    else
    {
        fprintf(file, "client packet: 0x%x %u, len: %5d, Wrong packet id!\n", packet_id, packet_id, packet_len);
    }
    dump_data(file, fd, packet_len);
}

void dump_client_map_error_packet(FILE *file, int fd, unsigned int packet_id, int packet_len)
{
    fprintf(file, "Unknown client packet: 0x%x %u, len: %5d\n", packet_id, packet_id, packet_len);
    dump_data(file, fd, packet_len);
}

int lclif_parse_pre(int *fdPtr)
{
    const int fd = *fdPtr;

    int packet_len = (int)RFIFOREST(fd);
    if (packet_len < 2)
        return 0;
    char buf[100];
    sprintf(buf, "log/login_%d.log", fd);
    FILE *file = fopen(buf, "a");
    if (file == NULL)
    {
        ShowError("Cant open log file %s\n", buf);
        return 1;
    }
    show_time(file);
    dump_client_packet(file, fd, packet_len);
    fclose(file);
    return 0;
}

int char_parse_char_pre(int *fdPtr)
{
    const int fd = *fdPtr;

    int packet_len = (int)RFIFOREST(fd);
    if (packet_len < 2)
        return 0;
    char buf[100];
    sprintf(buf, "log/char_%d.log", fd);
    FILE *file = fopen(buf, "a");
    if (file == NULL)
    {
        ShowError("Cant open log file %s\n", buf);
        return 1;
    }
    show_time(file);
    dump_client_packet(file, fd, packet_len);
    fclose(file);
    return 0;
}

int clif_parse_pre(int *fdPtr)
{
    const int fd = *fdPtr;

    int rest_len = (int)RFIFOREST(fd);
    if (rest_len < 2)
        return 0;

    struct map_session_data *sd = sockt->session[fd]->session_data;
    unsigned short (*parse_cmd_func)(int fd, struct map_session_data *sd);

    if (sd)
        parse_cmd_func = sd->parse_cmd_func;
    else
        parse_cmd_func = clif->parse_cmd;
    const unsigned int cmd = parse_cmd_func(fd, sd);

    char buf[100];
    sprintf(buf, "log/map_%d.log", fd);
    FILE *file = fopen(buf, "a");
    if (file == NULL)
    {
        ShowError("Cant open log file %s\n", buf);
        fclose(file);
        return 1;
    }

    const struct s_packet_db *packet = clif->packet(cmd);
    if (packet == NULL)
    {
        show_time(file);
        dump_client_map_error_packet(file, fd, cmd, rest_len);
        fclose(file);
        return 1;
    }

    int packet_len = packet->len;
    if (packet_len == -1)
    {
        if (rest_len < 4 || rest_len > 32768)
        {
            fclose(file);
            return 1;
        }
        packet_len = RFIFOW(fd, 2);
    }
    if (rest_len < packet_len)
    {
        fclose(file);
        return 1;
    }

    show_time(file);
    dump_client_map_packet(file, fd, cmd, rest_len, packet);
    fclose(file);
    return 0;
}

#ifdef packet
#undef packet
#endif  // packet
#ifdef MAP_PACKETS_H
#undef MAP_PACKETS_H
#endif  // MAP_PACKETS_H

void load_functions(void)
{
    for (int f = 0; f <= MAX_PACKET_DB; f ++)
        packet_names[f] = "NULL";
#define packet(id, b, ...) \
    if (id > 0 && id <= MAX_PACKET_DB) \
        packet_names[id] = #__VA_ARGS__;
#include "map/packets.h"
}

#undef packet

HPExport void server_preinit(void)
{
    if (SERVER_TYPE == SERVER_TYPE_LOGIN)
    {
        addHookPre(lclif, parse, lclif_parse_pre);
    }
    else if (SERVER_TYPE == SERVER_TYPE_CHAR)
    {
        addHookPre(chr, parse_char, char_parse_char_pre);
    }
    else if (SERVER_TYPE == SERVER_TYPE_MAP)
    {
        addHookPre(clif, parse, clif_parse_pre);
        load_functions();
    }
}

HPExport void plugin_init(void)
{
}
