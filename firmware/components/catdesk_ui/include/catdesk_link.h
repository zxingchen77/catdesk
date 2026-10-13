#pragma once

#include "catdesk_model.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Includes the terminating NUL; the transport should reject longer lines. */
#define CD_LINK_MAX_LINE 128

/* Input may have no line ending, LF, or CRLF. Replies exclude the line ending.
 * Invalid input or insufficient reply space leaves the model unchanged and
 * clears reply[0] when a writable reply buffer is supplied. */
bool cd_link_parse(cd_model_t *model, const char *line, char *reply, size_t cap);
void cd_link_disconnect(cd_model_t *model);

#ifdef __cplusplus
}
#endif
