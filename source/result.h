/*
    result.h - Error and success handling

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

#ifndef RRC_RESULT_H
#define RRC_RESULT_H

#include <gctypes.h>
#include <errno.h>
#include <string.h>
#include <curl/curl.h>

/* The amount of time in seconds to display fatal errors before exiting */
#define RRC_RESULT_FATAL_SPLASH_TIME_SEC 20

enum rrc_result_error_source
{
    ESOURCE_CURL,
    ESOURCE_ERRNO,
    ESOURCE_ZIP,
    /* Corruption detected in settingsfile during parsing.
       This should ideally never happen unless the user manually edited it and the detection is only a best-effort,
       but if we do detect it we can ask the user if they want to reset the file to its defaults. */
    ESOURCE_CORRUPTED_SETTINGSFILE,
    ESOURCE_UPDATE_MISC,
    ESOURCE_CORRUPTED_VERSIONFILE,
    /* Misc SD card errors: locked, not inserted etc. */
    ESOURCE_SD_CARD,
    /* Failure to initialise network */
    ESOURCE_WIISOCKET_INIT,
    ESOURCE_CORRUPTED_RR_XML,
    /* Version mismatch (currently for runtime-ext and channel) */
    ESOURCE_VERSION_MISMATCH
};

/* Because each library uses their own set of error codes, we need to support all
   of them and be able to stringify all of them for display if they occur. */
union rrc_result_error_inner
{
    /* defined if curl has an error */
    CURLcode ccode;
    /* defined for any libc errors */
    int errnocode;
    /* defined for ZIP errors */
    int ziperr;
    /* code returned by wiisocket_init */
    int wiisocket_init_code;
};

/* heap allocated error structure */
struct rrc_result_error
{
    /* the relevant union member */
    enum rrc_result_error_source source;
    /* additional source data of the error, activate member indicated by `source` enum */
    union rrc_result_error_inner inner;
    /* dynamically sized string containing some extra context */
    char context[];
};

/* Primary result struct. Denotes either success or failure of a routine.
   Success is considered a no-op in most cases, and errors are handled in different
   ways depending on the context and severity.
   Errors have two main escape routes:
      - Fatal errors cannot be recovered from. The most serious of errors will
        display themselves on-screen, and then a fixed-length delay will happen
        before exiting. This is because we need to handle cases where the
        controller is not initialized, as this prevents us from reading the
        controller in order to, for example, support pressing A to exit etc.
        Some examples of fatal errors include failure to initialise SD card,
        failure to initialise DVD drive, can't init controllers, etc.
      - Normal errors are errors that can be recovered from. When a normal error
        occurs, the user is displayed an error prompt which they can dismiss.
        Some examples include failure to download updates, failed to save settings
        etc.
        It is implementation defined exactly what happens when a normal error occurs.
        See `rrc_result_error_check_error_normal' for details.
    This result struct should rarely (or never) be interfaced with directly.
    Instead, use the plethora of helper functions in order to process and handle
    errors. For example, to check for error and react accordingly, call the
    functions `rrc_result_check_error_fatal' or `rrc_error_check_error_normal',
    depending on the context of the error condition. For example, early stage
    SD card failure would use the former function. See their documentaton for
    more details.
    Result structs can be created with `rrc_result_create_*' (depending on source).
    Result structures are designed to be stack-allocated (i.e., not using `malloc').
*/
struct rrc_result
{
    /* NULL if this result represents the 'ok' state, otherwise a heap allocated error structure.
       This allows it to fit in a single register and is useful because it gets passed around a lot,
       and is also still very fast to create in the common happy ok path. */
    struct rrc_result_error *err;
};

#define TRY(x)                        \
    do                                \
    {                                 \
        struct rrc_result res = x;    \
        if (rrc_result_is_error(res)) \
            return res;               \
    } while (0);

extern const struct rrc_result rrc_result_success;

struct rrc_result rrc_result_create_error_curl(CURLcode error, const char *context);

struct rrc_result rrc_result_create_error_errno(int eno, const char *context);

struct rrc_result rrc_result_create_error_sdcard(int eno, const char *context);

struct rrc_result rrc_result_create_error_zip(int error, const char *context);

struct rrc_result rrc_result_create_error_corrupted_settingsfile(const char *context);

struct rrc_result rrc_result_create_error_corrupted_versionfile(const char *context);

struct rrc_result rrc_result_create_error_misc_update(const char *context);

struct rrc_result rrc_result_create_error_corrupted_rr_xml(const char *context);

struct rrc_result rrc_result_create_error_version_mismatch(const char *context);

/* Returns true if this result is an error, false otherwise. */
inline bool rrc_result_is_error(struct rrc_result result)
{
    return result.err != NULL;
}

/* Returns the context associated with this result, or NULL if there is no error */
inline const char *rrc_result_context(struct rrc_result result)
{
    return rrc_result_is_error(result) ? result.err->context : NULL;
}

/* Returns a statically allocated string with contextual information related to
   the inner error code. NULL if unknown or not an error. The returned string
   should not be freed. */
char *rrc_result_strerror(struct rrc_result result);

/* Check this result for error condition, and if it is in an erroneous state, supply
   a prompt with the error details. This prompt can be dismissed.

   It is down to the particular implementation at which this error happens to decide
   how exactly to handle the error condition. It is likely that this is propagated
   back to the call site of thie work (such as the settings page) and no other
   action is taken.

   The xfb parameter is required for prompt display.

   This function **consumes** the result and must not be used or freed explicitly again.
   */
void rrc_result_error_check_error_normal(struct rrc_result result, void *xfb);

/* Check this result for error condition, and if it is in an erroneous state, display
   an error message, wait a set period of time, and exit.

   Unlike normal errors, this can be called from anywhere because either it does
   nothing (success) or never returns (error). */
void rrc_result_error_check_error_fatal(struct rrc_result result);

/**
 * Frees any heap allocated data associated with this result struct.
 *
 * Must be called exactly once (unless another function is called first that consumes this;
 * `rrc_result_error_*` functions also implicitly free the result and you must not free it again).
 */
void rrc_result_free(struct rrc_result result);

#endif
