// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
extern "C" {
struct DocumentHandle;
DocumentHandle *miro_open(const char *, const char *);
void miro_close(DocumentHandle *);
const char *miro_last_result();
const char *miro_request(DocumentHandle *, const char *);
const char *miro_render(DocumentHandle *, const char *);
const unsigned char *miro_pixels(DocumentHandle *);
const char *miro_config(const char *);
const char *miro_state(const char *);
void miro_cancel_pending();
}
