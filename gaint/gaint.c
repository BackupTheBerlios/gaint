/**
 * GAim Is Not Telnet: GAINT
 *
 * @authors: Sabit Anjum Sayeed (sabit_a_s@yahoo.com)
 *           Sadrul Habib Chowdhury (imadil@gmail.com)
 */

/* TODO:
 *      - send large files in small-sized (~1MB?) files
 *      - keep a hashtable of `pwd`s for each user, rather than sharing the same one
 *      - new feature `glob`    : DONE: 13jan0202
 */

#define PLUGIN_ID   "GAINT"
#define TRIGGER     "!gaint"

#define PREF_ROOT       "/plugins/gaint"
#define PREF_TRIGGER    "/plugins/gaint/trigger"
#define PREF_PERMITLIST "/plugins/gaint/permitlist"
#define PREF_ECHOSEND   "/plugins/gaint/echosend"
#define PREF_TEST       "/plugins/gaint/test"

#define GAINT_START "<font face=\"courier new\">"
#define DIR_START   "<font color=\"#800000\">"
#define FILE_START  "<font color=\"#008000\">"
#define END         "</font>\n"

#include <sys/types.h>
//#include <dirent.h>
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

/****** UI Stuff [start] ********/
static GaimPluginPrefFrame *
get_plugin_pref_frame(GaimPlugin *plugin)
{
	GaimPluginPrefFrame *frame;
	GaimPluginPref *pref;

	frame = gaim_plugin_pref_frame_new();

	pref = gaim_plugin_pref_new_with_label(_("GAINT Preferences"));
	gaim_plugin_pref_frame_add(frame, pref);

	pref = gaim_plugin_pref_new_with_name_and_label(PREF_TRIGGER, _("Trigger Phrase"));
	gaim_plugin_pref_frame_add(frame, pref);

	pref = gaim_plugin_pref_new_with_name_and_label(PREF_PERMITLIST,
                    _("Add the names of the buddies between \na pair of #'s (eg. #abc#def#)"));
	gaim_plugin_pref_frame_add(frame, pref);

	pref = gaim_plugin_pref_new_with_name_and_label(PREF_ECHOSEND, _("Do you want to see what response GAINT is sending?"));
	gaim_plugin_pref_frame_add(frame, pref);

	/* TODO: add a check-list of buddies here. the list will initially be empty.
     * we'll use the connect/disconnet callback functions to populate/reset the list.
     * may need to go through the GTK-doc for this one :(
     */

	return frame;
}
/****** UI Stuff [end] ********/

/* these defines ripped from gtkimhtl.c :-) */
#define VALID_TAG(x)	if (!g_ascii_strncasecmp (string, x ">", strlen (x ">"))) {	\
				*len = strlen (x) + 1;				\
				return TRUE;					\
            }

#define VALID_OPT_TAG(x)	if (!g_ascii_strncasecmp (string, x " ", strlen (x " "))) {	\
					const gchar *c = string + strlen (x " ");	\
					gchar e = '"';					\
					gboolean quote = FALSE;				\
					while (*c) {					\
						if (*c == '"' || *c == '\'') {		\
							if (quote && (*c == e))		\
								quote = !quote;		\
							else if (!quote) {		\
								quote = !quote;		\
								e = *c;			\
							}				\
						} else if (!quote && (*c == '>'))	\
							break;				\
						c++;					\
					}						\
					if (*c) {					\
						*len = c - string + 1;			\
						return TRUE;				\
					}						\
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
    g_string_append(list, "#");

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
        g_string_append(search, "/");
    }
    else
        search = g_string_new("");
    g_string_append(search, *pattern ? pattern : "*");
    
    GString * output = g_string_new("\nDirectory Listing of: ");
    g_string_append(output, search->str);
    g_string_append(output, "\n");

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
    static GString * pwd = NULL;
    if(pwd == NULL)
    {   /* get the current working directory */
        char *cwd = (char*)g_malloc(1024);  /* hoping an absolute path won't exceed this size */
        pwd = g_string_new(getcwd(cwd, 1024));
        g_free(cwd);
    }

    char *cmd;
    GString *message = stripHtml(*buffer);

    if((cmd = strstr(message->str, gaim_prefs_get_string(PREF_TRIGGER))))
    {
        if(!verifySharePerm(*sender))
        {
            GString * output = g_string_new(":error: authorization failed");
            sendMessage(*sender, account, output);
            g_string_free(output, TRUE);
            return FALSE;
        }

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
            GString *path = g_string_new("");
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
            g_string_sprintfa(path, "cd \"%s\"; cd \"%s\"; pwd", pwd->str, cmd+3);

            /* this whole hassle to keep track of `pwd` */
            FILE *pipe = popen(path->str, "r");
            if(pipe)
            {
                char *cwd = (char*) g_malloc(1024);
                while((fgets(cwd, 1024, pipe)))
                {
                    cwd[strlen(cwd)-1] = 0;     /* getting rid of the trailing new-line */
                    if(cwd[0])
                        g_string_assign(pwd, cwd);
                }

                GString *output = g_string_new("working directory after cd: ");
                g_string_append(output, pwd->str);
                sendMessage(*sender, account, output);
                g_string_free(output, TRUE);
            }
            else
            {
                GString *output = g_string_new(":error: popen()");
                sendMessage(*sender, account, output);
                g_string_free(output, TRUE);
            }
        }
        else if(startsWith(cmd, "get"))
        {
            /* send the file */

            char *name = cmd + 3;
            while(*name == ' ' || *name == '\t')
                name++;
            if(!*name)          /* no filename given */
                goto skip;
            
            
            GString *output = g_string_new("The file being sent: ");
            GString *filename = g_string_new("");
                    
            if(*name != '/')
            {
                /* relative path, so add the pwd */
                g_string_append(filename, pwd->str);
                g_string_append(filename, "/");
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
            /* test out new commands here. */
            GString *output = g_string_new(account->protocol_id);
            sendMessage(*sender, account, output);
            g_string_free(output, TRUE);
            g_string_free(message, TRUE);
            return TRUE;
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


static GaimPluginUiInfo prefs_info =
{
	get_plugin_pref_frame
};

static GaimPluginInfo info =
{
	GAIM_PLUGIN_MAGIC,
	GAIM_MAJOR_VERSION,
	GAIM_MINOR_VERSION,
	GAIM_PLUGIN_STANDARD,                             /**< type           */
	NULL,                                             /**< ui_requirement */
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
	"Sabit Anjum Sayeed <sabit_a_s@yahoo.com>, Sadrul Habib Chowdhury (imadil@gmail.com)",
                                                      /**< author         */
	GAIM_WEBSITE,                                     /**< homepage       */

	plugin_load,                                      /**< load           */
	NULL,                                             /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	&prefs_info,                                      /**< preferece      */
	NULL
};

static void
init_plugin(GaimPlugin *plugin)
{
	gaim_prefs_add_none(PREF_ROOT);
	gaim_prefs_add_string(PREF_TRIGGER, "!gaint");
    gaim_prefs_add_string(PREF_PERMITLIST, "");
    gaim_prefs_add_bool(PREF_ECHOSEND, FALSE);
}

GAIM_INIT_PLUGIN(gaint, init_plugin, info)

