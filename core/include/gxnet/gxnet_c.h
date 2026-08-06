/* SPDX-License-Identifier: MIT
 *
 * C ABI for the GxNet library.
 *
 * Provided for hosts that cannot consume the C++ interface directly: a Windows
 * DLL loaded by an ERP runtime, a service written in another language, a plain
 * C tool. Everything is string in, string out, with no ownership transfer, so
 * the boundary stays trivial to bind against.
 */
#ifndef GXNET_C_H
#define GXNET_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXNET_OK 0
#define GXNET_ERR_ARGUMENT (-1)
#define GXNET_ERR_PARSE (-2)
#define GXNET_ERR_BUFFER (-3)
#define GXNET_ERR_NOT_FOUND (-4)

typedef struct gxnet_token_info {
    char token[8];  /* "GW7D" */
    char name[80];  /* "GGW_UNIQUE_DATEN" */
    char since[16]; /* "15.20", empty if unknown */
    int reserved;   /* non-zero if listed without a description */
    int group_code; /* high nibble of the class code, -1 if unknown */
    int type_code;  /* low nibble of the class code, -1 if unknown */
    int arity;      /* payload fields consumed: 0 or 1 */
} gxnet_token_info_t;

/* Number of entries in the bundled subfunction table. */
int gxnet_registry_size(void);

/* Looks a subfunction up by token ("GW7D") or symbolic name
 * ("GGW_UNIQUE_DATEN"). Returns GXNET_OK or GXNET_ERR_NOT_FOUND. */
int gxnet_lookup_token(const char* token, gxnet_token_info_t* out);
int gxnet_lookup_name(const char* name, gxnet_token_info_t* out);

/* Validates a telegram.
 *
 * header_line     required, e.g. "A!PV04|PW02|GL19|LX02"
 * data_line       optional, may be NULL or "" for read requests
 * device_version  optional, e.g. "16.40"; when given, subfunctions newer than
 *                 the device are reported as errors
 * diagnostics     receives one finding per line; may be NULL
 *
 * Returns the number of errors found (0 means the telegram is usable), or a
 * negative GXNET_ERR_* code. Warnings do not affect the return value; read the
 * diagnostics buffer to see them.
 */
int gxnet_validate(const char* header_line, const char* data_line, const char* device_version, char* diagnostics,
                   size_t diagnostics_capacity);

/* Converts between the two transmission forms.
 *
 * gxnet_to_one_line joins a header and data line into the interleaved form used
 * by SendOne; gxnet_split_one_line does the reverse, writing the header into
 * header_out and the data into data_out.
 *
 * Returns GXNET_OK, or a negative GXNET_ERR_* code. On GXNET_ERR_PARSE the
 * output buffers receive the parser's message when capacity allows.
 */
int gxnet_to_one_line(const char* header_line, const char* data_line, char* out, size_t capacity);
int gxnet_split_one_line(const char* one_line, char* header_out, size_t header_capacity, char* data_out,
                         size_t data_capacity);

/* Escaping helpers for text payloads. Return GXNET_OK or GXNET_ERR_BUFFER. */
int gxnet_escape(const char* text, char* out, size_t capacity);
int gxnet_unescape(const char* text, char* out, size_t capacity);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GXNET_C_H */
