#pragma once

/* appends a timestamped line to wheelie_assist.log next to the module. For rare events only */
void wa_diag(const char *fmt, ...);
