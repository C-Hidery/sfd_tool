/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ryan Crepa
 */
#ifndef SFD_TOOL_I18N_H
#define SFD_TOOL_I18N_H

#include <libintl.h>
#include <locale.h>

#define _(String) gettext (String)

#endif // SFD_TOOL_I18N_H
