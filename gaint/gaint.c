/**
 * GAim Is Not Telnet: GAINT
 *
 * @authors: Sabit Anjum Sayeed <sabitas@gmail.com>
 *           Sadrul Habib Chowdhury <imadil@gmail.com>
 */

/* TODO:
 *      - send large files in small-sized (~1MB?) files
 */

#define PLUGIN_ID   "GAINT"
#define TRIGGER     "!gaint"

#define PREF_ROOT       "/plugins/gaint"
#define PREF_TRIGGER    "/plugins/gaint/trigger"
#define PREF_PERMITLIST "/plugins/gaint/permitlist"
#define PREF_ECHOSEND   "/plugins/gaint/echosend"
#define PREF_TEST       "/plugins/gaint/test"
#define PREF_HOME       "/plugins/gaint/home"

#define GAINT_START "<font face=\"courier new\">"
#define DIR_START   "<font color=\"#800000\">"
#define FILE_START  "<font color=\"#008000\">"
#define END         "</font>\n"

#include <sys/types.h>
#include <glob.h>

#include <gtk/gtkclist.h>
#include <gtk/gtk.h>

#include "internal.h"
#include "connection.h"
#include "conversation.h"
#include "core.h"
#include "debug.h"
#include "signals.h"
#include "version.h"
#include "ft.h"
#include "gtkprefs.h"
#include "gtkutils.h"
#include "gtkplugin.h"

static gboolean verifySharePerm(char *sender);
/****** UI Stuff [start] ********/
static GList * addBuddyToList(GaimBlistNode * nd, GList * list)
{
    if(nd == NULL)
        return list;
    while(nd)
    {
        if(GAIM_BLIST_NODE_IS_GROUP(nd))
        {
            list = addBuddyToList(nd->child, list);
        }
        else if(GAIM_BLIST_NODE_IS_CONTACT(nd))
        {
            char * name = (char*)gaim_contact_get_alias((GaimContact*)nd);
            if(name && *name)
            {
                list = g_list_append(list, name); 
            }
        }
        nd = nd->next;
    }
    return list;
}

static GList * createList()
{
    GList * list = NULL;
    GaimBuddyList * gaimlist = gaim_get_blist();
    if(gaimlist)
    {
        GaimBlistNode *nd = gaimlist->root;
        list = addBuddyToList(nd, list);
    }
    return list;
}

static GtkTreeModel * createModel (void)
{
    gint i = 0;
    GtkListStore *store;
    GtkTreeIter iter;
    GList * list = createList();
    int len;

    /* create list store */
    store = gtk_list_store_new (2,
			        G_TYPE_BOOLEAN,
			        G_TYPE_STRING);

    if(!list)
        goto end;
    list = g_list_sort(list, (GCompareFunc)g_ascii_strcasecmp);
    len = g_list_length(list);
    
    /* add data to the list store */
    for (i = 0; i < len; i++)
    {
        char * name = g_list_nth_data(list, i);
        gboolean perm = verifySharePerm(name);
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
			    0, perm,
                1, name,
			    -1);
    }
    g_list_free(list);
end:
    return GTK_TREE_MODEL (store);
}

static void toggleAllow(GtkCellRendererToggle * cell, char * path_str, gpointer data)
{
    GtkTreeModel *model = (GtkTreeModel *)data;
    GtkTreeIter  iter;
    GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
    gboolean allow;
    char * name;

    /* get toggled iter */
    gtk_tree_model_get_iter (model, &iter, path);
    gtk_tree_model_get (model, &iter, 0, &allow, -1);
    gtk_tree_model_get (model, &iter, 1, &name, -1);

    /* do something with the value */
    allow ^= 1;

    GString * list = g_string_new(gaim_prefs_get_string(PREF_PERMITLIST));
    GString * nw = g_string_new("#");
    g_string_append(nw, name);
    g_string_append_c(nw, '#');
    
    if(allow)
    {
        if(strstr(list->str, nw->str))
            goto skip;

        if(list->str && *(list->str) != '#')
        {
            g_string_prepend_c(list, '#');
        }
        g_string_prepend(list, name);
        g_string_prepend_c(list, '#');
    }
    else
    {
        char * s;
        if(!(s = strstr(list->str, nw->str)))
            goto skip;
        list = g_string_erase(list, s - list->str + 1, strlen(nw->str)-1);
    }
skip:
    gaim_prefs_set_string(PREF_PERMITLIST, list->str);
    g_string_free(nw, TRUE);
    g_string_free(list, TRUE);

    /* set new value */
    gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, allow, -1);

    /* clean up */
    gtk_tree_path_free (path);
}

static void addColumns(GtkTreeView * view)
{
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeModel *model = gtk_tree_view_get_model (view);

    /* column for fixed toggles */
    renderer = gtk_cell_renderer_toggle_new ();

    g_signal_connect (renderer, "toggled",
		    G_CALLBACK (toggleAllow), model);

    column = gtk_tree_view_column_new_with_attributes ("Allow?",
						     renderer,
						     "active", 0,
						     NULL);
    /* set this column to a fixed sizing (of 50 pixels) */
    gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (column),
				     GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width (GTK_TREE_VIEW_COLUMN (column), 50);
/*    gtk_tree_view_column_set_sort_column_id (column, 0);*/
    gtk_tree_view_append_column (view, column);

    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("Buddy",
						       renderer,
						       "text",
						       1,
						       NULL);
/*    gtk_tree_view_column_set_sort_column_id (column, 1);*/
    gtk_tree_view_append_column (view, column);
}

GtkWidget *get_config_frame(GaimPlugin *plugin)
{
    GtkWidget * ret;
    GtkWidget * prefs, * buddies;
    GtkWidget * scroll;
    GtkTreeModel * model;

#define SPACING 18
    
    ret = gtk_vbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(ret), 12);

    prefs = gaim_gtk_make_frame(ret, "GAINT Preferences");

    /* Trigger */
    gaim_gtk_prefs_labeled_entry(prefs, "_Trigger Phrase:\t\t\t", PREF_TRIGGER, GTK_SIZE_GROUP(prefs));
                                                        /* tabs for alignment :) */

    /* Default Working Directory */
    gaim_gtk_prefs_labeled_entry(prefs, "Default _Working Directory:\t", PREF_HOME, GTK_SIZE_GROUP(prefs));

    /* Echo */
    gaim_gtk_prefs_checkbox("_Echo GAINT Response", PREF_ECHOSEND, prefs);

    /* List of Buddies */
    prefs = gaim_gtk_make_frame(ret, "List of Buddies");
    
    /* from gtkprefs.c */
	/* The following is an ugly hack to make the frame expand so the
	 * sound events list is big enough to be usable */
    gtk_box_set_child_packing(GTK_BOX(prefs->parent), prefs, TRUE, TRUE, 0,
			GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(prefs->parent->parent), prefs->parent, TRUE,
			TRUE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(prefs->parent->parent->parent),
			prefs->parent->parent, TRUE, TRUE, 0, GTK_PACK_START);

    model = createModel();
    buddies = gtk_tree_view_new_with_model(model);
    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (buddies), TRUE);
    gtk_tree_view_set_search_column (GTK_TREE_VIEW (buddies), 1);
    g_object_unref (model);
    addColumns(GTK_TREE_VIEW(buddies));

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(scroll), 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), buddies);
    gtk_box_pack_start(GTK_BOX(prefs), scroll, TRUE, TRUE, 0);

    gtk_widget_show_all(ret);
    
    return ret;
}

static GaimGtkPluginUiInfo ui_info =
{
	get_config_frame
};

/*
 * Prefs-info not necessary anymore, since we have UI-info
 */
/****** UI Stuff [end] ********/

/* these defines ripped from gtkimhtl.c :-) */
#define VALID_TAG(x)    if (!g_ascii_strncasecmp (string, x ">", strlen (x ">"))) {    \
                *len = strlen (x) + 1;            	\
                return TRUE;            		\
            }

#define VALID_OPT_TAG(x)    if (!g_ascii_strncasecmp (string, x " ", strlen (x " "))) {    \
                    const gchar *c = string + strlen (x " ");    \
                    gchar e = '"';        			\
                    gboolean quote = FALSE;        		\
                    while (*c) {        			\
                        if (*c == '"' || *c == '\'') {    	\
                            if (quote && (*c == e))		\
                            	quote = !quote;		\
                            else if (!quote) {		\
                            	quote = !quote;		\
                            	e = *c;			\
                            }				\
                        } else if (!quote && (*c == '>'))    \
                            break;				\
                        c++;    				\
                    }        				\
                    if (*c) {        			\
                        *len = c - string + 1;    		\
                        return TRUE;    			\
                    }        				\
                }
/* this functions has also been ripped from gtkimhtml.c.
 * changed slightly (last couple of lines)
 */
static gboolean isHtmlTag(const gchar *string, gint *len)
{
    char *close;

    if (!(close = strchr (string, '>')))
        return FALSE;

    VALID_TAG ("B");
    VALID_TAG ("BOLD");
    VALID_TAG ("/B");
    VALID_TAG ("/BOLD");
    VALID_TAG ("I");
    VALID_TAG ("ITALIC");
    VALID_TAG ("/I");
    VALID_TAG ("/ITALIC");
    VALID_TAG ("U");
    VALID_TAG ("UNDERLINE");
    VALID_TAG ("/U");
    VALID_TAG ("/UNDERLINE");
    VALID_TAG ("S");
    VALID_TAG ("STRIKE");
    VALID_TAG ("/S");
    VALID_TAG ("/STRIKE");
    VALID_TAG ("SUB");
    VALID_TAG ("/SUB");
    VALID_TAG ("SUP");
    VALID_TAG ("/SUP");
    VALID_TAG ("PRE");
    VALID_TAG ("/PRE");
    VALID_TAG ("TITLE");
    VALID_TAG ("/TITLE");
    VALID_TAG ("BR");
    VALID_TAG ("HR");
    VALID_TAG ("/FONT");
    VALID_TAG ("/A");
    VALID_TAG ("P");
    VALID_TAG ("/P");
    VALID_TAG ("H3");
    VALID_TAG ("/H3");
    VALID_TAG ("HTML");
    VALID_TAG ("/HTML");
    VALID_TAG ("BODY");
    VALID_TAG ("/BODY");
    VALID_TAG ("FONT");
    VALID_TAG ("HEAD");
    VALID_TAG ("/HEAD");
    VALID_TAG ("BINARY");
    VALID_TAG ("/BINARY");

    VALID_OPT_TAG ("HR");
    VALID_OPT_TAG ("FONT");
    VALID_OPT_TAG ("BODY");
    VALID_OPT_TAG ("A");
    VALID_OPT_TAG ("IMG");
    VALID_OPT_TAG ("P");
    VALID_OPT_TAG ("H3");
    VALID_OPT_TAG ("HTML");

    VALID_TAG ("CITE");
    VALID_TAG ("/CITE");
    VALID_TAG ("EM");
    VALID_TAG ("/EM");
    VALID_TAG ("STRONG");
    VALID_TAG ("/STRONG");

    VALID_OPT_TAG ("SPAN");
    VALID_TAG ("/SPAN");
    VALID_TAG ("BR/"); /* hack until gtkimhtml handles things better */
    VALID_TAG ("IMG");
    VALID_TAG("SPAN");
    VALID_OPT_TAG("BR");

    if (!g_ascii_strncasecmp(string, "!--", strlen ("!--"))) {
        gchar *e = strstr (string + strlen("!--"), "-->");
        if (e) {
            /*
             * If we uncomment the following line then HTML comments will be
             * hidden.  This is good because it means when a WinAIM users pastes
             * part of a conversation to you, the screen names won't be
             * duplicated (because WinAIM pastes an HTML comment containing the
             * screen name, for some reason).
             *
             * However, uncommenting this is bad because we use HTML comment
             * tags to print timestamps to conversations (at least, I think...)
             *
             * KingAnt thinks it would be best to display timestamps using
             * something other than comment tags.
             */
            /* *type = -1; */
            *len = e - string + strlen ("-->");
            return TRUE;
        }
    }
    return FALSE;
}

/* strip the html-tags from `html` */
static GString* stripHtml(char *html)
{
    GString * str = g_string_new("");

    gchar *end, *start = html;
    gint len;

    while((end = strstr(start, "<")))
    {
        g_string_append_len(str, start, end-start);    /* add everything before < */
        end++;  /* skip < */
        start = end;

        if(isHtmlTag(start, &len))
        {
            /* html tag found */
            start += len;   /* skip the whole tag */
        }
        else
        {
            /* not a tag */
            g_string_append_c(str, '<');    /* add < to the message */
        }
    }
    if(*start)
        g_string_append(str, start);
    return str;
}


/* does str start with needle? */
static gboolean startsWith(const char *str, const char *needle)
{
    while(*needle && *needle == *str)
    {
        needle++;
        str++;
    }
    if(*needle)
        return FALSE;
    if(isalnum(*str))
        return FALSE;
    return TRUE;
}

/* is the sender permitted to share? */
static gboolean verifySharePerm(char *sender)
{
    gboolean ret = FALSE;

    GString *list = g_string_new("#");
    g_string_append(list, sender);
    g_string_append_c(list, '#');

    if(strstr(gaim_prefs_get_string(PREF_PERMITLIST), list->str))
        ret = TRUE;
    g_string_free(list, TRUE);
    return ret;
}

/* send message.
 * chop the messages in chunks of size ~1KB if the message is too large.
 * currently, MSN supports 15xx bytes (including header)
 * and Yahoo supports 2xxx bytes (apparently)
 */
static void sendMessage(char *sender, GaimAccount *account, GString *output)
{
    GaimConversation *conv = gaim_find_conversation_with_account(sender, account);
    GaimConvIm *im = gaim_conversation_get_im_data(conv);
    GString *echo;
    char *start, *end;
    int len;

    g_string_prepend(output, "GAINT RESPONSE: ");
    g_string_prepend(output, GAINT_START);
    g_string_append(output, END);
    start = output->str;

    switch(fork())
    {
        case -1:    /* couldn't fork! heh? */
            gaim_conversation_write(conv, NULL, "Couldn't fork. Better check everything for any mess.", GAIM_MESSAGE_ERROR, time(NULL));
            return;
        case 0:     /* the child is going to send the message */
            break;
        default:    /* the parent does nothing */
            echo = g_string_new("GAINT: response sent.");
            if(gaim_prefs_get_bool(PREF_ECHOSEND))  /* show the response */
            {
                if(output->str[0] != '\n')
                    g_string_append_c(echo, '\n');
                g_string_append(echo, output->str);
            }
            gaim_conv_im_write(im, sender, echo->str, GAIM_MESSAGE_NICK, time(NULL));
            g_string_free(echo, TRUE);
            return;
                
    }
    /***** Only child reaches here, which must exit after doing the work *****/
#define SLEEP   4       /* min time between successive messages */
#define FRAG_LIMIT 1024
    while((len = strlen(start)) > FRAG_LIMIT)   /* we can get rid of this strlen call, btw */
    {
        end = start;

        /* add one line at a time until the message-size reaches the limit */
        while(end-start < FRAG_LIMIT)
        {
            end = strchr(end, '\n') + 1;
            if(!end)
            {
                goto end;
            }
        }
        *(end-1) = 0;
        gaim_conv_im_send(im, start);
        sleep(SLEEP);       /* wait before we send another message. throttling?! dunno :) */
        start = end - 1;
        *start = '\n';      /* start the new chunk with a blank line */
    }

end:
    gaim_conv_im_send(im, start);
    exit(0);                /* child has done it's job. exit. */
}

/* show the list of files in the current directory.
 * may be send a recursive list of the subdirectories as well, if requested?
 */
static GString * showList(GString *pwd, const char *pattern)
{
    glob_t files;
    int count;

    while(isspace(*pattern))
        pattern++;

    GString *search;
    if(*pattern != '/')
    {
        /* relative path. so add pwd */
        search = g_string_new(pwd->str);
        g_string_append_c(search, '/');
    }
    else
        search = g_string_new("");
    g_string_append(search, *pattern ? pattern : "*");
    
    GString * output = g_string_new("\nDirectory Listing of: ");
    g_string_append(output, search->str);
    g_string_append_c(output, '\n');

    glob(search->str, /*GLOB_MARK | */GLOB_NOESCAPE, NULL, &files);
    g_string_free(search, TRUE);

    /* prepare the list for output */
    for(count=0; count < files.gl_pathc; count++)
    {
        GString *fname = g_string_new(files.gl_pathv[count]);
        struct stat st;
        if(stat(fname->str, &st) == 0)
        {
            char *base = strrchr(fname->str, '/');
            if(base)
                base++;
            else
                base = fname->str;

            if(S_ISDIR(st.st_mode))
            {   /* directory */
                g_string_append(output, DIR_START);
                g_string_append(output, base);
            }
            else
            {
                /* file */
                g_string_append(output, FILE_START);
                g_string_append(output, base);
                g_string_sprintfa(output, "  [size: %ld bytes]", st.st_size);
            }
            g_string_append(output, END);
        }
        g_string_free(fname, TRUE);
    }
    g_string_append(output, "----- END OF LIST -----");
    return output;
}

/* well... */
static GString *showHelp()
{
    GString * help = g_string_new("GAIM Is Not Telnet Commands: \n\
list [<pattern>]  -- shows the list of files matching <pattern>. If <pattern>\n\
                     is not given, then shows the files in working directory.\n\
pwd               -- shows the path of the working directory.\n\
cd <path>         -- change pwd to <path>. <path> can be either a relative or\n\
                     absolute path.\n\
get <file>        -- get the file named <file>. <file> can be a relative or\n\
                     absolute path to the file.\n\
help              -- shows this help.");

    return help;
}

static gboolean receiving_im_msg_cb(GaimAccount *account, char **sender, char **buffer,
                   int *flags, void *data)
{
    char * cmd;
    GString *message = stripHtml(*buffer);
    if(!(cmd = strstr(message->str, gaim_prefs_get_string(PREF_TRIGGER))))
    {
        goto skip;
    }

    if(!GAIM_BUDDY_IS_ONLINE(gaim_find_buddy(account, *sender)))
    {
        /* the buddy is not online. no point sending the respone. */
        goto skip;
    }

    if(!verifySharePerm(*sender))
    {
        GString * output = g_string_new(":error: authorization failed");
        sendMessage(*sender, account, output);
        g_string_free(output, TRUE);
        goto skip;
    }

    static GHashTable * pwdList = NULL;
    
    if(pwdList == NULL)
        pwdList = g_hash_table_new_full(g_str_hash, g_str_equal, free, g_free);

    char * cwd = (char *) g_hash_table_lookup(pwdList, (char*)*sender);
    GString * pwd;
    if(cwd == NULL)
    {
        /* get the default working directory from the preference */
        cwd = strdup(gaim_prefs_get_string(PREF_HOME));
        pwd = g_string_new(cwd);
        g_hash_table_insert(pwdList, strdup(*sender), cwd);
    }
    else
        pwd = g_string_new(cwd);

    cmd += strlen(gaim_prefs_get_string(PREF_TRIGGER)); /* skip the trigger */
    if(isalnum(*cmd))
        goto skip;
    
    while(isspace(*cmd))    /* skip whitespaces */
        cmd++;
    if(startsWith(cmd, "list"))
    {
        /* show list */
        GString *output = showList(pwd, cmd+4);
        sendMessage(*sender, account, output);
        g_string_free (output, TRUE);
    }
    else if(startsWith(cmd, "pwd"))
    {
        /* present working directory */
        GString *output = g_string_new("current working directory: ");
        g_string_append(output, pwd->str);
        sendMessage(*sender, account, output);
        g_string_free(output, TRUE);
    }
    else if(startsWith(cmd, "cd"))
    {
        /* change working directory */
        char *ch = cmd + 3;

        /* look for backticks. if we find any, we will ignore this command */
        while(*ch)
        {
            if(*ch == '`')
            {
                GString *output = g_string_new("possible malicious command");
                sendMessage(*sender, account, output);
                g_string_free(output, TRUE);
                goto skip;
            }
            ch++;
        }
        GString *path = g_string_new(""), *output;
        g_string_sprintfa(path, "cd \"%s\"; cd \"%s\"; pwd", pwd->str, cmd+3);

        /* this whole hassle to keep track of `pwd` */
        FILE *pipe = popen(path->str, "r");
        if(pipe)
        {
            char *cwd = (char*) g_malloc(1024);
            while((fgets(cwd, 1024, pipe)))
            {
                if(cwd[0])
                {
                    cwd[strlen(cwd)-1] = 0;     /* getting rid of the trailing new-line */
                    g_string_assign(pwd, cwd);
                }
            }
            output = g_string_new("working directory after cd: ");
            g_string_append(output, pwd->str);
            g_hash_table_insert(pwdList, strdup(*sender), cwd);
        }
        else
        {
            output = g_string_new(":error: popen()");
        }
        sendMessage(*sender, account, output);
        g_string_free(output, TRUE);
        g_string_free(path, TRUE);
    }
    else if(startsWith(cmd, "get"))
    {
        /* send the file */

        char *name = cmd + 3;
        while(*name == ' ' || *name == '\t')
            name++;
        if(!*name)          /* no filename given, show error? */
            goto skip;
        
        
        GString *output = g_string_new("The file being sent: ");
        GString *filename = g_string_new("");
                
        if(*name != '/')
        {
            /* relative path, so add the pwd */
            g_string_append(filename, pwd->str);
            g_string_append_c(filename, '/');
        }
        g_string_append(filename, name);

        GaimConnection *gc = gaim_conversation_get_gc(gaim_find_conversation_with_account(*sender, account));
        serv_send_file(gc, *sender, filename->str);

        g_string_append(output, filename->str);
        sendMessage(*sender, account, output);    /* show the complete path of the file */

        g_string_free(filename, TRUE);
        g_string_free(output, TRUE);
    }
    else if(startsWith(cmd, "test"))
    {
        /* test out new-commands here. */
    }
    else if(startsWith(cmd, "help"))
    {
        GString * output = showHelp();
        sendMessage(*sender, account, output);
        g_string_free(output, TRUE);
    }
    else
    {
        GString *output = g_string_new("Unknown Command");
        sendMessage(*sender, account, output);
        g_string_free(output, TRUE);
    }
skip:
    g_string_free(message, TRUE);
    return FALSE;
}

/**************************************************************************
 * Plugin stuff
 **************************************************************************/
static gboolean
plugin_load(GaimPlugin *plugin)
{
    void *conv_handle = gaim_conversations_get_handle();

    gaim_signal_connect(conv_handle, "receiving-im-msg",
                        plugin, GAIM_CALLBACK(receiving_im_msg_cb), NULL);
    return TRUE;
}

static GaimPluginInfo info =
{
    GAIM_PLUGIN_MAGIC,
    GAIM_MAJOR_VERSION,
    GAIM_MINOR_VERSION,
    GAIM_PLUGIN_STANDARD,                             /**< type           */
    GAIM_GTK_PLUGIN_TYPE,                             /**< ui_requirement */
    0,                                                /**< flags          */
    NULL,                                             /**< dependencies   */
    GAIM_PRIORITY_DEFAULT,                            /**< priority       */

    PLUGIN_ID,                                        /**< id             */
    N_("GAim Is Not Telnet"),                         /**< name           */
    VERSION,                                          /**< version        */
                                                      /**  summary        */
    N_("Remotely share files."),
                                                      /**  description    */
    N_("Buddies can copy/send files even when you are away."),
    "Sabit Anjum Sayeed <sabitas@gmail.com>\n\t\t\tSadrul Habib Chowdhury <imadil@gmail.com>",
                                                      /**< author         */
    "http://gaint.berlios.de",                        /**< homepage       */

    plugin_load,                                      /**< load           */
    NULL,                                             /**< unload         */
    NULL,                                             /**< destroy        */

    &ui_info,                                         /**< ui_info        */
    NULL,                                             /**< extra_info     */
    NULL,                                             /**< preferece      */
    NULL
};

static void
init_plugin(GaimPlugin *plugin)
{
    gaim_prefs_add_none(PREF_ROOT);
    gaim_prefs_add_string(PREF_TRIGGER, "!gaint");  /* default trigger */
    gaim_prefs_add_string(PREF_PERMITLIST, "#");
    gaim_prefs_add_string(PREF_HOME, "/home");  /* default home */
    gaim_prefs_add_bool(PREF_ECHOSEND, FALSE);
}

GAIM_INIT_PLUGIN(gaint, init_plugin, info)

