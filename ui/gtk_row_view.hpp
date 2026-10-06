/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include <gtk/gtk.h>

// 通用的 GtkColumnView 行对象：最多 8 个字符串单元格 + 一个勾选状态 + 一个 int tag。
// 用于替代已废弃的 GtkTreeView / GtkListStore / GtkCellRenderer 组合。

#define SFD_ROW_MAX_CELLS 8

#define SFD_ROW_TYPE (sfd_row_get_type())
G_DECLARE_FINAL_TYPE(SfdRow, sfd_row, SFD, ROW, GObject)

SfdRow* sfd_row_new(void);
void sfd_row_set_cell(SfdRow* row, int idx, const char* text);
const char* sfd_row_get_cell(SfdRow* row, int idx);
void sfd_row_set_checked(SfdRow* row, gboolean checked);
gboolean sfd_row_get_checked(SfdRow* row);
void sfd_row_set_tag(SfdRow* row, int tag);
int sfd_row_get_tag(SfdRow* row);

// 创建一个 GtkColumnView（内部持有 GListStore）。
// single_selection=true 时使用 GtkSingleSelection，否则使用 GtkNoSelection。
GtkWidget* sfd_row_view_new(gboolean single_selection);
GListStore* sfd_row_view_get_store(GtkWidget* view);
void sfd_row_view_clear(GtkWidget* view);
// 先把 SfdRow（sfd_row_new 创建）填好单元格/勾选/tag，再调用本函数加入视图。
// 必须“先填后加”：加入 GListStore 会立刻触发列表项 bind，此时再去填就晚了。
// 本函数接管传入的引用。
void sfd_row_view_add(GtkWidget* view, SfdRow* row);
guint sfd_row_view_count(GtkWidget* view);
SfdRow* sfd_row_view_get(GtkWidget* view, guint pos);      // 返回引用，调用方需 g_object_unref
SfdRow* sfd_row_view_get_selected(GtkWidget* view);        // 借出，可能为 NULL
void sfd_row_view_add_text_column(GtkWidget* view, const char* title, int idx, gboolean sortable);
void sfd_row_view_add_toggle_column(GtkWidget* view, const char* title);
