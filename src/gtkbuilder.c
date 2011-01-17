#include "gtkbuilder.h"

static void
print_space (guint depth)
{
    guint i;

    for (i = 0; i < depth; i++)
        g_print ("  ");
}

#define P print_space(depth);

static gboolean
prop_is_ignored (GParamSpec *prop)
{
    guint i;
    static const char *ignored[] = {
        "name",
        "parent",
        "style",
        "window",
        "mnemonic-keyval",
        "screen",
        "resize-mode",
        "text-length",
        "invisible-char",
    };

    for (i = 0; i < G_N_ELEMENTS(ignored); i++) {
        if (g_str_equal (prop->name, ignored[i]))
            return TRUE;
    }

    return FALSE;
}

static char *
flags_to_string (GValue *val)
{
    GFlagsClass *klass;
    guint value, flag_num;
    GString *string = g_string_new (NULL);

    string = g_string_new (NULL);
    klass = g_type_class_ref (G_VALUE_TYPE (val));
    value = g_value_get_flags (val);

    /* Step through each of the flags in the class. */
    for (flag_num = 0; flag_num < klass->n_values; flag_num++) {
        guint mask;
        gboolean setting;
        const gchar *value_name;

        mask = klass->values[flag_num].value;
        setting = ((value & mask) == mask) ? TRUE : FALSE;

        //FIXME glade does this better than us
#if 0
        value_name = glade_get_displayable_value
            (eprop->klass->pspec->value_type, klass->values[flag_num].value_nick);

        if (value_name == NULL) value_name = klass->values[flag_num].value_name;
#else
        value_name = klass->values[flag_num].value_name;
#endif
        /* Setup string for property label */
        if (setting) {
            if (string->len > 0)
                g_string_append (string, " | ");
            g_string_append (string, value_name);
        }
    }
    g_type_class_unref(klass);

    return g_string_free (string, FALSE);
}

static char *
val_to_str (GParamSpec *prop, GValue *val)
{
    char *value;

    value = NULL;

    if (G_VALUE_HOLDS_ENUM(val)) {
        GEnumClass *enum_class = G_PARAM_SPEC_ENUM(prop)->enum_class;
        GEnumValue *enum_value = g_enum_get_value(enum_class,
                                                  g_value_get_enum(val));

        value = g_strdup(enum_value->value_name);
    } else if (G_VALUE_HOLDS_FLAGS(val)) {
        value = flags_to_string (val);
    } else if (G_VALUE_HOLDS_STRING(val)) {
        value = g_markup_escape_text (g_value_get_string (val), -1);
    } else if (G_VALUE_HOLDS_BOOLEAN(val)) {
        value = g_strdup (g_value_get_boolean (val) ? "True" : "False");
    } else if (G_VALUE_HOLDS_INT(val)) {
        value = g_strdup_printf("%d", g_value_get_int(val));
    } else if (G_VALUE_HOLDS_INT64(val)) {
        value = g_strdup_printf("%"G_GINT64_FORMAT, g_value_get_int64(val));
    } else {
        value = g_strdup_value_contents(val);
    }

    return value;
}

static void
dump_widget(GtkWidget *widget, guint depth, GHashTable *hash, guint *num_unnamed)
{
    const char *class_name = G_OBJECT_CLASS_NAME(GTK_WIDGET_GET_CLASS(widget));
    GParamSpec **props;
    GtkWidget *parent;
    guint num_properties;
    guint i;
    char *name;
    gboolean print_packing;

    depth++;

    /* Dump the widget itself */
    name = g_strdup (gtk_widget_get_name(widget));
    if (name == NULL || g_str_equal(name, class_name)) {
        g_free (name);
        (*num_unnamed)++;
        name = g_strdup_printf ("widget%d", *num_unnamed);
    }
    g_hash_table_insert (hash, widget, name);

    P;
    g_print ("<object class=\"%s\" id=\"%s\">\n", class_name, name);
    /* The name is now owned by the hash table */

    /* And now its properties */
    depth++;

    props = g_object_class_list_properties (G_OBJECT_GET_CLASS(widget), &num_properties);
    for (i = 0; i < num_properties; i++) {
        GParamSpec *prop = props[i];
        GValue val = { 0, };
        char *value;

        if (!(prop->flags & G_PARAM_READABLE))
            continue;

        value = NULL;

        /* Is it the default value? */
        g_value_init(&val, prop->value_type);
        g_object_get_property (G_OBJECT (widget), prop->name, &val);
        if (g_param_value_defaults (prop, &val) != FALSE)
            continue;
        if (prop_is_ignored (prop))
            continue;

        if (g_str_equal (prop->name, "events") && g_value_get_flags (&val) == 0)
            continue;
        if (g_str_equal (prop->name, "image"))
            continue;
        /* Ignore the label in buttons if we have an image as well */
        if (GTK_IS_BUTTON (widget) && g_str_equal (prop->name, "label")) {
            gpointer image;
            gboolean use_stock;
            g_object_get (G_OBJECT (widget),
                          "image", &image,
                          "use-stock", &use_stock, NULL);
            if (image != NULL && use_stock == FALSE)
                continue;
        }

        if (g_str_equal (prop->name, "mnemonic-widget")) {
            gpointer widptr;
            const char *target_widget;

            widptr = g_value_get_object (&val);
            if (widptr == NULL)
                continue;
            target_widget = g_hash_table_lookup (hash, widptr);
            if (target_widget != NULL)
                value = g_strdup (target_widget);
        }

        if (value == NULL)
            value = val_to_str (prop, &val);

        g_value_unset (&val);

        P;
        g_print ("<property name=\"%s\">%s</property>\n", prop->name, value);

        g_free (value);
    }
    g_free (props);

    /* Onto the children */
    if (GTK_IS_CONTAINER(widget) &&
        GTK_IS_CHECK_BUTTON (widget) == FALSE)
    {
        GList *l, *list;

        if (GTK_IS_BUTTON (widget)) {
            gpointer image;
            gboolean use_stock;
            g_object_get (G_OBJECT (widget),
                          "image", &image,
                          "use-stock", &use_stock, NULL);
            if (use_stock || image == NULL)
                goto skip_children;
        }

        depth++;

        list = gtk_container_get_children(GTK_CONTAINER(widget));
        for (l = list; l != NULL; l = l->next) {
            /* Ignore composite children */
            if (GTK_IS_WIDGET (l->data) && GTK_WIDGET_COMPOSITE_CHILD(l->data))
                continue;

            P;
            g_print ("<child>\n");
            dump_widget(GTK_WIDGET(l->data), depth, hash, num_unnamed);
            P;
            g_print("</child>\n");
        }

        depth--;
    }

skip_children:

    /* And close the object up */
    depth--;
    P;
    g_print ("</object>\n");

    /* And now the packing */
    num_properties = 0;
    parent = gtk_widget_get_parent (widget);
    print_packing = FALSE;
    if (parent != NULL && GTK_IS_CONTAINER_CLASS (G_OBJECT_GET_CLASS(parent)))
        props = gtk_container_class_list_child_properties (G_OBJECT_GET_CLASS(parent), &num_properties);
    else
        props = NULL;

    for (i = 0; i < num_properties; i++) {
        GParamSpec *prop = props[i];
        GValue val = { 0, };
        char *value;

        if (!(prop->flags & G_PARAM_READABLE))
            continue;
        if (!(prop->flags & G_PARAM_WRITABLE))
            continue;

        /* Is it the default value? */
        g_value_init(&val, prop->value_type);
        gtk_container_child_get_property (GTK_CONTAINER (parent), widget, prop->name, &val);
        if (g_param_value_defaults (prop, &val) != FALSE)
            continue;

        if (print_packing == FALSE) {
            P;
            g_print ("<packing>\n");
            depth++;
            print_packing = TRUE;
        }

        value = val_to_str (prop, &val);
        g_value_unset (&val);

        P;
        g_print ("<property name=\"%s\">%s</property>\n", prop->name, value);

        g_free (value);
    }
    if (print_packing != FALSE) {
        depth--;
        P;
        g_print ("</packing>\n");
    }
}

void
dump_gtkbuilder_tree(GtkWidget *widget)
{
    GHashTable *hash;
    guint num_unnamed = 0;

    hash = g_hash_table_new_full (g_direct_hash,
                                  g_direct_equal,
                                  NULL,
                                  g_free);

    g_print ("<?xml version=\"1.0\"?>\n");
    g_print ("<interface>\n");
    dump_widget(widget, 0, hash, &num_unnamed);
    g_print ("</interface>\n");

    g_hash_table_destroy (hash);
}

// vim: set et sw=4 ts=4:
