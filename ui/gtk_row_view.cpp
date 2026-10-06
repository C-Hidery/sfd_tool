/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "gtk_row_view.hpp"

struct _SfdRow
{
    GObject parent_instance;
    char* cells[SFD_ROW_MAX_CELLS];
    gboolean checked;
    int tag;
};

G_DEFINE_TYPE(SfdRow, sfd_row, G_TYPE_OBJECT)

static void sfd_row_finalize(GObject* obj)
{
    SfdRow* row = SFD_ROW(obj);
    for (int i = 0; i < SFD_ROW_MAX_CELLS; ++i)
    {
        g_free(row->cells[i]);
        row->cells[i] = nullptr;
    }
    G_OBJECT_CLASS(sfd_row_parent_class)->finalize(obj);
}

static void sfd_row_class_init(SfdRowClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = sfd_row_finalize;
}

static void sfd_row_init(SfdRow* row)
{
    for (int i = 0; i < SFD_ROW_MAX_CELLS; ++i) row->cells[i] = nullptr;
    row->checked = FALSE;
    row->tag = -1;
}

SfdRow* sfd_row_new(void)
{
    return SFD_ROW(g_object_new(SFD_ROW_TYPE, nullptr));
}

void sfd_row_set_cell(SfdRow* row, int idx, const char* text)
{
    if (!row || idx < 0 || idx >= SFD_ROW_MAX_CELLS) return;
    g_free(row->cells[idx]);
    row->cells[idx] = g_strdup(text ? text : "");
}

const char* sfd_row_get_cell(SfdRow* row, int idx)
{
    if (!row || idx < 0 || idx >= SFD_ROW_MAX_CELLS) return nullptr;
    return row->cells[idx];
}

void sfd_row_set_checked(SfdRow* row, gboolean checked)
{
    if (row) row->checked = checked ? TRUE : FALSE;
}

gboolean sfd_row_get_checked(SfdRow* row)
{
    return row ? row->checked : FALSE;
}

void sfd_row_set_tag(SfdRow* row, int tag)
{
    if (row) row->tag = tag;
}

int sfd_row_get_tag(SfdRow* row)
{
    return row ? row->tag : -1;
}

// ---------------------------------------------------------------------------
// 文本列
// ---------------------------------------------------------------------------

static void text_setup(GtkSignalListItemFactory*, GtkListItem* item, gpointer)
{
    GtkWidget* label = gtk_label_new(nullptr);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_list_item_set_child(item, label);
}

static void text_bind(GtkSignalListItemFactory*, GtkListItem* item, gpointer data)
{
    const int idx = GPOINTER_TO_INT(data);
    SfdRow* row = SFD_ROW(gtk_list_item_get_item(item));
    GtkWidget* label = gtk_list_item_get_child(item);
    const char* text =
        (row && idx >= 0 && idx < SFD_ROW_MAX_CELLS && row->cells[idx]) ? row->cells[idx] : "";
    gtk_label_set_text(GTK_LABEL(label), text);
}

static int text_compare(gconstpointer a, gconstpointer b, gpointer data)
{
    const int idx = GPOINTER_TO_INT(data);
    SfdRow* ra = SFD_ROW((gpointer)a);
    SfdRow* rb = SFD_ROW((gpointer)b);
    const char* sa = (ra && idx >= 0 && idx < SFD_ROW_MAX_CELLS && ra->cells[idx]) ? ra->cells[idx] : "";
    const char* sb = (rb && idx >= 0 && idx < SFD_ROW_MAX_CELLS && rb->cells[idx]) ? rb->cells[idx] : "";
    return g_strcmp0(sa, sb);
}

// ---------------------------------------------------------------------------
// 勾选列
// ---------------------------------------------------------------------------

static void toggle_toggled(GtkCheckButton* cb, gpointer)
{
    SfdRow* row = SFD_ROW(g_object_get_data(G_OBJECT(cb), "sfd-row"));
    if (row) row->checked = gtk_check_button_get_active(cb);
}

static void toggle_setup(GtkSignalListItemFactory*, GtkListItem* item, gpointer)
{
    GtkWidget* cb = gtk_check_button_new();
    gtk_widget_set_halign(cb, GTK_ALIGN_CENTER);
    g_signal_connect(cb, "toggled", G_CALLBACK(toggle_toggled), nullptr);
    gtk_list_item_set_child(item, cb);
}

static void toggle_bind(GtkSignalListItemFactory*, GtkListItem* item, gpointer)
{
    SfdRow* row = SFD_ROW(gtk_list_item_get_item(item));
    GtkWidget* cb = gtk_list_item_get_child(item);
    if (!cb) return;
    // 持有行对象的强引用，避免复选框在回收期间指向已释放的行
    g_object_set_data_full(G_OBJECT(cb), "sfd-row", row ? g_object_ref(row) : nullptr, g_object_unref);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb), (row && row->checked) ? TRUE : FALSE);
}

static void toggle_unbind(GtkSignalListItemFactory*, GtkListItem* item, gpointer)
{
    GtkWidget* cb = gtk_list_item_get_child(item);
    if (cb) g_object_set_data(G_OBJECT(cb), "sfd-row", nullptr); // 触发 destroy notify 释放引用
}

// ---------------------------------------------------------------------------
// 视图
// ---------------------------------------------------------------------------

GtkWidget* sfd_row_view_new(gboolean single_selection)
{
    // 注意：gtk_single_selection_new / gtk_no_selection_new / gtk_column_view_new
    // 的 model 参数都是 (transfer full)，会接管传入引用，因此这里不能再 unref。
    GListStore* store = g_list_store_new(SFD_ROW_TYPE);
    GtkSelectionModel* sel = nullptr;
    if (single_selection)
    {
        GtkSingleSelection* single = gtk_single_selection_new(G_LIST_MODEL(store));
        // 与旧的 GtkTreeView 行为保持一致：初始不自动选中任何行
        gtk_single_selection_set_autoselect(single, FALSE);
        gtk_single_selection_set_can_unselect(single, TRUE);
        sel = GTK_SELECTION_MODEL(single);
    }
    else
    {
        sel = GTK_SELECTION_MODEL(gtk_no_selection_new(G_LIST_MODEL(store)));
    }
    GtkWidget* view = gtk_column_view_new(sel);
    // 视图自身再持有一个 store 引用，供 sfd_row_view_get_store() 查找
    g_object_set_data_full(G_OBJECT(view), "sfd-store", g_object_ref(store), g_object_unref);
    return view;
}

GListStore* sfd_row_view_get_store(GtkWidget* view)
{
    if (!view || !GTK_IS_COLUMN_VIEW(view)) return nullptr;
    return static_cast<GListStore*>(g_object_get_data(G_OBJECT(view), "sfd-store"));
}

void sfd_row_view_clear(GtkWidget* view)
{
    GListStore* store = sfd_row_view_get_store(view);
    if (store) g_list_store_remove_all(store);
}

void sfd_row_view_add(GtkWidget* view, SfdRow* row)
{
    if (!row) return;
    GListStore* store = sfd_row_view_get_store(view);
    if (!store)
    {
        g_object_unref(row);
        return;
    }
    // 必须先填好 row 再调用本函数：append 会立刻触发 bind。
    g_list_store_append(store, row);
    g_object_unref(row); // 消费调用方的引用（store 已持有）
}

guint sfd_row_view_count(GtkWidget* view)
{
    GListStore* store = sfd_row_view_get_store(view);
    return store ? g_list_model_get_n_items(G_LIST_MODEL(store)) : 0;
}

SfdRow* sfd_row_view_get(GtkWidget* view, guint pos)
{
    GListStore* store = sfd_row_view_get_store(view);
    if (!store) return nullptr;
    return SFD_ROW(g_list_model_get_item(G_LIST_MODEL(store), pos)); // 调用方负责 unref
}

SfdRow* sfd_row_view_get_selected(GtkWidget* view)
{
    if (!view || !GTK_IS_COLUMN_VIEW(view)) return nullptr;
    GtkSelectionModel* sel = gtk_column_view_get_model(GTK_COLUMN_VIEW(view));
    if (!sel || !GTK_IS_SINGLE_SELECTION(sel)) return nullptr;
    GObject* item = G_OBJECT(gtk_single_selection_get_selected_item(GTK_SINGLE_SELECTION(sel)));
    return item ? SFD_ROW(item) : nullptr;
}

void sfd_row_view_add_text_column(GtkWidget* view, const char* title, int idx, gboolean sortable)
{
    if (!view || !GTK_IS_COLUMN_VIEW(view)) return;
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(text_setup), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(text_bind), GINT_TO_POINTER(idx));
    GtkColumnViewColumn* col = gtk_column_view_column_new(title, factory);
    if (sortable)
    {
        GtkSorter* sorter = GTK_SORTER(gtk_custom_sorter_new(text_compare, GINT_TO_POINTER(idx), nullptr));
        gtk_column_view_column_set_sorter(col, sorter);
        g_object_unref(sorter);
    }
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), col);
    g_object_unref(col);
    // 注意：gtk_column_view_column_new() 接管 factory 的所有权，不能再 unref
}

void sfd_row_view_add_toggle_column(GtkWidget* view, const char* title)
{
    if (!view || !GTK_IS_COLUMN_VIEW(view)) return;
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(toggle_setup), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(toggle_bind), nullptr);
    g_signal_connect(factory, "unbind", G_CALLBACK(toggle_unbind), nullptr);
    GtkColumnViewColumn* col = gtk_column_view_column_new(title, factory);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), col);
    g_object_unref(col);
    // 注意：gtk_column_view_column_new() 接管 factory 的所有权，不能再 unref
}
