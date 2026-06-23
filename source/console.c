/*
    console.c - console helper routines

    Copyright (C) 2025  Retro Rewind Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <gctypes.h>
#include <gccore.h>

#include "update/update.h"
#include "console.h"
#include "util.h"
#include "version.h"

/* 100 = 100% */
int rrc_con_progress_percent = 0;
char *rrc_con_current_action;
struct rrc_version cached_version = {-1, -1, -1};

int rrc_con_get_cols()
{
    int cols, rows;
    CON_GetMetrics(&cols, &rows);
    return cols;
}

int rrc_con_get_rows()
{
    int cols, rows;
    CON_GetMetrics(&cols, &rows);
    return rows;
}

void rrc_con_set_action(char *action)
{
    rrc_con_current_action = action;
}

void rrc_con_set_progress_percent(int progress)
{
    rrc_con_progress_percent = progress;
}

void rrc_con_cursor_seek_to(int row, int column)
{
    printf("\x1b[%i;%iH", row, column);
};

void rrc_con_update(char *action, int progress_percent)
{
    rrc_con_set_action(action);
    rrc_con_set_progress_percent(progress_percent);
    rrc_con_print_state();
}

void rrc_con_cursor_seek_to_row_centered(int row, int text_len)
{

    int off = (rrc_con_get_cols() / 2) - text_len;
    rrc_con_cursor_seek_to(row, off);
}

void rrc_con_print_text_centered(int row, char *text)
{
    rrc_con_clear_line(row);
    rrc_con_cursor_seek_to_row_centered(row, strlen(text) / 2);
    printf("%s", text);
}

int rrc_con_centered_text_start_column(char *text)
{
    return (rrc_con_get_cols() / 2) - (strlen(text) / 2);
}

void rrc_con_display_version()
{
    if (cached_version.major == -1)
    {
        struct rrc_result version_result = rrc_update_get_current_version(&cached_version);
        rrc_result_error_check_error_fatal(version_result);
    }

    char vertext[32];

#if defined(RRC_BETA) && RRC_BETA >= 1
    snprintf(vertext, 32, "RR: %i.%i.%i (RRBETA BUILD)", cached_version.major, cached_version.minor, cached_version.patch);
#else
    snprintf(vertext, 32, "RR: %i.%i.%i", cached_version.major, cached_version.minor, cached_version.patch);
#endif

    char internal_vertext[32];
    struct rrc_version internal_version = RRC_INTERNAL_VERSION;
    snprintf(internal_vertext, sizeof(internal_vertext), "Channel: %i.%i.%i", internal_version.major, internal_version.minor, internal_version.patch);

    rrc_con_cursor_seek_to(rrc_con_get_rows() - 2, rrc_con_get_cols() - strlen(vertext));
    printf("%s", vertext);
    rrc_con_cursor_seek_to(rrc_con_get_rows() - 1, rrc_con_get_cols() - strlen(internal_vertext));
    printf("%s", internal_vertext);
}

void rrc_con_display_progress_bar()
{
    printf(RRC_CON_ANSI_CLR);
    int inner_width = (rrc_con_get_cols()) - (RRC_CON_EDGE_PAD * 2);
    bool progress_bar[inner_width];
    for (int i = 0; i < inner_width; i++)
    {
        int percent_along = ((i * 100) / inner_width);
        int is_filled = percent_along < rrc_con_progress_percent;

        if (is_filled)
            progress_bar[i] = true;
        else
            progress_bar[i] = false;
    }

    rrc_con_clear_line(_RRC_PROGRESS_ROW);
    rrc_con_cursor_seek_to(_RRC_PROGRESS_ROW, RRC_CON_EDGE_PAD);
    putc('[', stdout);

    bool now_empty = false;
    for (int i = 0; i < inner_width; i++)
    {
        if (!now_empty && progress_bar[i] == true)
        {
            putc('-', stdout);
        }
        else
        {
            now_empty = true;
            putc(' ', stdout);
        }
    }

    putc(']', stdout);
    rrc_con_clear_line(_RRC_PROGRESS_ROW + 1);
    rrc_con_cursor_seek_to(_RRC_PROGRESS_ROW + 1, RRC_CON_EDGE_PAD);
    printf("%i%c", rrc_con_progress_percent, '%');
}

void rrc_con_display_action()
{
    // clear two lines in case an action overflowed the line
    rrc_con_clear_line(_RRC_ACTION_ROW + 1);
    rrc_con_clear_line(_RRC_ACTION_ROW);
    rrc_con_cursor_seek_to(_RRC_ACTION_ROW, RRC_CON_EDGE_PAD);
    printf("%s\n", rrc_con_current_action);
}

void rrc_con_print_state()
{
    rrc_con_display_version();
    rrc_con_display_progress_bar();
    rrc_con_display_action();
}

void rrc_con_clear_line(int row)
{
    int cols, rows;
    CON_GetMetrics(&cols, &rows);

    rrc_con_cursor_seek_to(row, 0);

    for (int i = 0; i < cols; i++)
    {
        printf(" ");
        fflush(stdout);
    }
}

void rrc_con_clear(bool keep_version)
{
    int cols, rows;

    CON_GetMetrics(&cols, &rows);
    for (int i = 0; i < rows - 1; i++)
    {
        rrc_con_clear_line(i);
    }

    if (keep_version)
    {
        rrc_con_display_version();
    }
}
