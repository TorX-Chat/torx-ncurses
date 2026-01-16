/*
TorX: Metadata-safe Tor Chat Library
Copyright (C) 2024 TorX

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License version 3 as published by the Free
Software Foundation.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <https://www.gnu.org/licenses/>.

Appendix:

Section 7 Exceptions:

1) Modified versions of the material and resulting works must be clearly titled
in the following manner: "Unofficial TorX by Financier", where the word
Financier is replaced by the financier of the modifications. Where there is no
financier, the word Financier shall be replaced by the organization or
individual who is primarily responsible for causing the modifications. Example:
"Unofficial TorX by The United States Department of Defense". This amended
full-title must replace the word "TorX" in all source code files and all
resulting works. Where utilizing spaces is not possible, underscores may be
utilized. Example: "Unofficial_TorX_by_The_United_States_Department_of_Defense".
The title must not be replaced by an acronym or short title in any form of
distribution.

2) Modified versions of the material and resulting works must be distributed
with alternate logos and imagery that is substantially different from the
original TorX logo and imagery, especially the 7-headed snake logo. Modified
material and resulting works, where distributed with a logo or imagery, should
choose and distribute a logo or imagery that reflects the Financier,
organization, or individual primarily responsible for causing modifications and
must not cause any user to note similarities with any of the original TorX
imagery. Example: Modifications or works financed by The United States
Department of Defense should choose a logo and imagery similar to existing logos
and imagery utilized by The United States Department of Defense.

3) Those who modify, distribute, or finance the modification or distribution of
modified versions of the material or resulting works, shall not avail themselves
of any disclaimers of liability, such as those laid out by the original TorX
author in sections 15 and 16 of the License.

4) Those who modify, distribute, or finance the modification or distribution of
modified versions of the material or resulting works, shall jointly and
severally indemnify the original TorX author against any claims of damages
incurred and any costs arising from litigation related to any changes they are
have made, caused to be made, or financed. 

5) The original author of TorX may issue explicit exemptions from some or all of
the above requirements (1-4), but such exemptions should be interpreted in the
narrowest possible scope and to only grant limited rights within the narrowest
possible scope to those who explicitly receive the exemption and not those who
receive the material or resulting works from the exemptee.

6) The original author of TorX grants no exceptions from trademark protection in
any form.

7) Each aspect of these exemptions are to be considered independent and
severable if found in contradiction with the License or applicable law.
*/
#include <torx.h>

#define CLIENT_VERSION "TorX-Ncurses Alpha 2.0.38 2025/11/26 by TorX\n© Copyright 2025 TorX.\n"
#define DARK_THEME 0
#define LIGHT_THEME 1
#define THEME_DEFAULT DARK_THEME

static struct t_peer_list {
	char *unsent;
	size_t unsent_pos; // cursor position
	size_t unread; // unread message count
	int pm_n;
	int edit_n;
	int edit_i;
	int8_t mute; // 0 no, 1 yes
} *t_peer;

static uint8_t no_password = 0; // UI setting that is only relevant during first_start

static inline void initialize_library(void (*callback)(void))
{
	sql_error_suppression = 1; // Necessary otherwise our terminal breaks when entering a wrong password

	reduced_memory = 2; // TODO probably remove before release

	t_peer = torx_insecure_malloc(sizeof(struct t_peer_list) *11);
	intitialize_async_callbacks(callback);
	initial();

	if(no_password)
		login_start("");
}

#include <ncurses.h>
#include <locale.h>	// setlocale, for utilizing utf8
#include <unistd.h>	// read,write,pipe,close
#include <fcntl.h>	// related to pipe

#define minimum_size_horizontal 20 // Do not eliminate this or we have to go back to using int instead of size_t and checking for negative values
#define minimum_size_vertical 2

static void signal_resize(int sig);
static void draw_login(void);
static void draw_settings(void);
static void draw_tor_log(void);
static void draw_torx_log(void);
static void draw_torrc(void);
static void draw_change_password(void);
static void draw_generate(void);
static void draw_global_kill(void);
static void draw_home(void);
static void draw_chat_actions(void);
static void draw_chat_settings(void);
static void draw_group_invite(void);
static void draw_group_peerlist(void);
static void draw_contacts(void);
static void draw_chat(const int n);
static int await_key_or_signal(WINDOW *win);
void async_notifier(void);

enum {
	KEY_DELETE = 330,
	KEY_ESC = 27
};
enum widget_types {
	WIDGET_PASSWORD,
	WIDGET_INPUT_SINGLE_LINE,
	WIDGET_INPUT_MULTI_LINE,
	WIDGET_INPUT_NUMERICAL, // unused
	WIDGET_CHECKBOX
// TODO how will we handle scrolled windows? As a widget or not?
};

static struct widget {
	// Consider saving start_y and start_x so we can re-draw individual widgets rather than the while route (especially applicable to checkbox/toggle.
	int type;
	size_t max_width;
	int (*callback)(const int,const int); // typically holds the functionality to be executed upon ENTER press
	char **text;
	size_t *cursor;
} * widget = {0}; // REMEMBER to free this list whenever changing a page. Remember to initialize new widgets with zero_w

static int *current_focus = NULL; // XXX must be set otherwise we will dereference a NULL very quick! XXX
// XXX START One required for each route START XXX
static int focus_login = -1, focus_settings = -1, focus_contacts = -1, focus_chat = -1, focus_tor_log = -1, focus_torx_log = -1, focus_torrc = -1, focus_change_password = -1, focus_generate = -1, focus_global_kill = -1, focus_home = -1, focus_chat_actions = -1, focus_chat_settings = -1, focus_group_invite = -1, focus_group_peerlist = -1; // must initialize as -1 so that draw_* can set a default
static WINDOW *window_login = NULL, *window_settings = NULL, *window_contacts = NULL, *window_chat = NULL, *window_tor_log = NULL, *window_torx_log = NULL, *window_torrc = NULL, *window_change_password = NULL, *window_generate = NULL, *window_global_kill = NULL, *window_home = NULL, *window_chat_actions = NULL, *window_chat_settings = NULL, *window_group_invite = NULL, *window_group_peerlist = NULL;
// XXX END One required for each route END XXX

static int global_theme = THEME_DEFAULT;
static uint8_t highlight_active = 0; // must initialize as 0
static size_t cursor[2] = {0}; // y,x
static size_t inner_width;
static int selected_n = 0; // internal use only
static int global_n = -1;
static volatile sig_atomic_t resized = 0;
static volatile sig_atomic_t resize_seq = 0;

static bool running = true; // set to false to exit
static int sig_num = 0;

static size_t screen_rows, screen_cols; // this will be set on startup and resize

static int notify_fds[2] = { -1, -1 }; // triggered by library callbacks, indicating that a UI call to cb_buffer is requested

/* Chat state */
static size_t prior_print_start = 0; // TODO this should really be per peer, but this isn't just for message entry. Can't have it per widget because widgets get destroyed
static size_t chat_scroll_lines = 0; // Number of lines currently scrolled
static size_t chat_scroll_max;
static size_t chat_scroll_jump; // Number of lines to move upon PgUp PgDn (set by draw_chat)

/* Password window state */
static char *password = NULL;
static bool pw_show = false; // default false
static size_t pw_cursor = 0;

/* Contact list state */
static bool groups_mode = false;
static int list_first_peer_w = -1; // must initialize as -1 // This facilitates left-right navigation between peerlist and settings buttons

/* Settings state */
static size_t settings_scroll_offset = 0;
static char *tmp_snowflake_location = NULL;
static char *tmp_lyrebird_location = NULL;
static char *tmp_conjure_location = NULL;
static char *tmp_tor_location = NULL;
static char *tmp_threads_max = NULL;
static char *tmp_suffix_length = NULL;
static char *tmp_sing_expiration_days = NULL;
static char *tmp_mult_expiration_days = NULL;
static char *tmp_auto_accept_mult = NULL;
static char *tmp_tor_socks_port = NULL;
static char *tmp_tor_ctrl_port = NULL;
static char *tmp_control_password_clear = NULL;
static size_t tmp_snowflake_location_pos = 0;
static size_t tmp_lyrebird_location_pos = 0;
static size_t tmp_conjure_location_pos = 0;
static size_t tmp_tor_location_pos = 0;
static size_t tmp_threads_max_pos = 0;
static size_t tmp_suffix_length_pos = 0;
static size_t tmp_sing_expiration_days_pos = 0;
static size_t tmp_mult_expiration_days_pos = 0;
static size_t tmp_auto_accept_mult_pos = 0;
static size_t tmp_tor_socks_port_pos = 0;
static size_t tmp_tor_ctrl_port_pos = 0;
static size_t tmp_control_password_clear_pos = 0;

static void signal_resize(int sig)
{ // Do not call ncurses functions directly from here
	(void)sig;
	resized = 1;
	++resize_seq;
}

static char language[5+1] = {0};
const char* languages_available_name[] = {"English","中文",NULL};
const char* languages_available_code[] = {"en_US","zh_CN",NULL};

/* Global Text Declarations for ui_initialize_language() */
static const char *text_title = {0};
static const char *text_welcome = {0};
static const char *text_transfer_completed = {0};
static const char *text_online = {0};
static const char *text_new_friend = {0};
static const char *text_accepted_file = {0};
static const char *text_spoiled = {0};
static const char *text_placeholder_privkey = {0};
static const char *text_placeholder_identifier = {0};
static const char *text_placeholder_add_identifier = {0};
static const char *text_placeholder_add_onion = {0};
static const char *text_placeholder_add_group_identifier = {0};
static const char *text_placeholder_add_group_id = {0};
static const char *text_placeholder_search = {0};
static const char *text_dark = {0};
static const char *text_light = {0};
static const char *text_minimize_to_tray = {0};
static const char *text_generate_onionid = {0};
static const char *text_generate_torxid = {0};
static const char *text_disable = {0};
static const char *text_enable = {0};
static const char *text_leave_after = {0};
static const char *text_delete_after = {0};
static const char *text_select = {0};
static const char *text_save_sing = {0};
static const char *text_save_mult = {0};
static const char *text_emit_global_kill = {0};
static const char *text_vertical_emit_global_kill = {0};
static const char *text_peer = {0};
static const char *text_group = {0};
static const char *text_group_offer = {0};
static const char *text_audio_message = {0};
static const char *text_audio_call = {0};
static const char *text_sticker = {0};
static const char *text_current_members = {0};
static const char *text_group_public = {0};
static const char *text_group_private = {0};
static const char *text_block = {0};
static const char *text_unblock = {0};
static const char *text_ignore = {0};
static const char *text_unignore = {0};
static const char *text_edit = {0};
static const char *text_incoming = {0};
static const char *text_outgoing = {0};
static const char *text_active_mult = {0};
static const char *text_active_sing = {0};
static const char *text_you = {0};
static const char *text_queued = {0};
static const char *text_draft = {0};
static const char *text_accept = {0};
static const char *text_reject = {0};
static const char *text_copy = {0};
static const char *text_resend = {0};
static const char *text_start = {0};
static const char *text_pause = {0};
static const char *text_choose_file = {0};
static const char *text_choose_files = {0};
static const char *text_choose_folder = {0};
static const char *text_open_folder = {0}; // TODO implement after 4.10 drops
static const char *text_cancel = {0};
static const char *text_transfer_paused = {0};
static const char *text_transfer_rejected = {0};
static const char *text_transfer_cancelled = {0};
static const char *text_show_qr = {0};
static const char *text_save_qr = {0};
static const char *text_copy_qr = {0};
static const char *text_delete = {0};
static const char *text_delete_log = {0};
static const char *text_hold_to_talk = {0};
static const char *text_cancel_editing = {0};
static const char *text_private_messaging = {0};
static const char *text_rename = {0};
static const char *text_button_add = {0};
static const char *text_button_join = {0};
static const char *text_button_sing = {0};
static const char *text_button_mult = {0};
static const char *text_button_generate_invite = {0};
static const char *text_button_generate_public = {0};
static const char *text_wait = {0};
static const char *text_tooltip_image_header_0 = {0};
static const char *text_tooltip_image_header_1 = {0};
static const char *text_tooltip_image_header_2 = {0};
static const char *text_tooltip_image_header_3 = {0};
static const char *text_tooltip_image_header_4 = {0};
static const char *text_tooltip_image_header_5 = {0};
static const char *text_tooltip_image_header_6 = {0};
static const char *text_tooltip_image_header_7 = {0};
static const char *text_tooltip_image_header_8 = {0};
static const char *text_tooltip_image_header_9 = {0};
static const char *text_tooltip_logging_disabled = {0};
static const char *text_tooltip_logging_enabled = {0};
static const char *text_tooltip_logging_global_on = {0};
static const char *text_tooltip_logging_global_off = {0};
static const char *text_tooltip_notifications_off = {0};
static const char *text_tooltip_notifications_on = {0};
static const char *text_tooltip_blocked_on = {0};
static const char *text_tooltip_blocked_off = {0};
static const char *text_tooltip_button_select_custom = {0};
static const char *text_tooltip_button_custom_sing = {0};
static const char *text_tooltip_button_custom_mult = {0};
static const char *text_tooltip_group_or_user_name = {0};
static const char *text_tooltip_button_kill = {0};
static const char *text_tooltip_button_delete = {0};
static const char *text_tooltip_button_delete_log = {0};
static const char *text_status_online = {0};
static const char *text_of = {0};
static const char *text_status_last_seen = {0};
static const char *text_status_never = {0};
static const char *text_edit_torrc = {0};
static const char *text_saving_will_restart_tor = {0};
static const char *text_save = {0};
static const char *text_override = {0};
static const char *text_change_password = {0};
//static const char *text_resume_interupted = {0};
static const char *text_old_password = {0};
static const char *text_new_password = {0};
static const char *text_new_password_again = {0};
static const char *text_settings = {0};
static const char *text_set_select_theme = {0};
static const char *text_set_select_language = {0};
static const char *text_set_onionid_or_torxid = {0};
static const char *text_set_global_log = {0};
static const char *text_set_auto_resume_inbound = {0};
static const char *text_set_stickers_save_all = {0};
static const char *text_set_download_directory = {0};
static const char *text_tor = {0};
static const char *text_snowflake = {0};
static const char *text_lyrebird = {0};
static const char *text_conjure = {0};
static const char *text_binary_location = {0};
static const char *text_set_cpu = {0};
static const char *text_set_suffix = {0};
static const char *text_set_validity_sing = {0};
static const char *text_set_validity_mult = {0};
static const char *text_set_auto_mult = {0};
static const char *text_set_tor_port_socks = {0};
static const char *text_set_tor_port_ctrl = {0};
static const char *text_set_tor_password = {0};
static const char *text_set_externally_generated = {0};
static const char *text_tor_log = {0};
static const char *text_torx_log = {0};
static const char *text_global_kill = {0};
static const char *text_global_kill_warning = {0};
static const char *text_home = {0};
static const char *text_add_generate = {0};
static const char *text_add_peer_by = {0};
static const char *text_add_group_by = {0};
static const char *text_generate_for = {0};
static const char *text_generate_group_for = {0};
static const char *text_qr_code = {0};
static const char *text_enter_password = {0};
static const char *text_enter = {0};
static const char *text_incorrect = {0};
static const char *text_debug_level = {0};
static const char *text_debug_level_notice = {0};
static const char *text_fatal_error = {0};
static const char *text_active = {0};
static const char *text_identifier = {0};
static const char *text_onionid = {0};
static const char *text_torxid = {0};
static const char *text_invitor = {0};
static const char *text_groupid = {0};
static const char *text_successfully_created_group = {0};
static const char *text_error_creating_group = {0};
static const char *text_censored_region = {0};
static const char *text_invite_friend = {0};  // unused in GTK
static const char *text_group_peers = {0}; // unused in GTK
static const char *text_incoming_call = {0};

#define wmove_size(win, y, x) wmove(win, (int)(y), (int)(x))
#define newwin_size(nlines, ncols, begin_y, begin_x) newwin((int)(nlines), (int)(ncols), (int)(begin_y), (int)(begin_x))

#define mvwprintw_size(win, y, x, ...) mvwprintw(win, (int)(y), (int)(x), __VA_ARGS__)

static inline size_t print_wrapped(WINDOW *win,size_t *y,size_t *x,const size_t max_width,const char *str,const size_t len)
{ // NOTE: y and x must be initialized // TODO eliminate mvwprintw_size, except where wrapping is not desired
	if(max_width && str && len)
	{
		const uint8_t printing = (win && y && x) ? 1: 0;
		size_t offset_y = 0,offset_x = 0;
		for(size_t iter = 0; iter < len && str[iter] != '\0'; iter++)
		{
			if(str[iter] == '\n' || offset_x >= max_width)
			{
				offset_y++;
				offset_x = 0;
				if(str[iter] == '\n')
					continue; // do not print, skip
			}
			if(printing) // not else if
			{
				mvwaddch(win, (int)(*y + offset_y), (int)(*x + offset_x), (chtype)str[iter]); // TODO Incompatible with utf8/wide characters
			/*	char tmp[2];
				tmp[0] = str[iter];
				tmp[1] = '\0';
				mvwprintw_size(win,(*y + offset_y),(*x + offset_x),tmp); */
			}
			offset_x++;
		}
		if(offset_x == max_width)
		{ // Important for cursor position
			offset_y++;
			offset_x = 0;
		}
		if(y)
			*y = *y + offset_y;
		if(x)
			*x = *x + offset_x;
		return offset_y; // may be 0 if no wraps or newlines occurred during printing
	}
	return 0; // Do not throw error, probably just len is 0
}

static inline void getmaxyx_size(WINDOW *win,size_t *vertical,size_t *horizontal)
{ // TODO Test stripping out ability to values below minimum_size_*
	if(!win)
	{
		error_simple(0,"getmaxyx_size issue. UI Coding error. Report this.");
		return;
	}
	int y,x;
	getmaxyx(win,y,x); // macro
	if(y < 2)
		y = minimum_size_vertical;
	if(x < 2)
		x = minimum_size_horizontal;
	if(vertical)
		*vertical = (size_t)y;
	if(horizontal)
		*horizontal = (size_t)x;
}

static void redraw(void)
{ // Do not do anything other than calling draw_* here. Do it in individual draw_* functions so that they can be called independently of this function.
	if(window_login)
		draw_login();
	else if(window_settings)
		draw_settings();
	else if(window_tor_log)
		draw_tor_log();
	else if(window_torx_log)
		draw_torx_log();
	else if(window_torrc)
		draw_torrc();
	else if(window_change_password)
		draw_change_password();
	else if(window_generate)
		draw_generate();
	else if(window_global_kill)
		draw_global_kill();
	else if(window_home)
		draw_home();
	else if(window_chat_actions)
		draw_chat_actions();
	else if(window_chat_settings)
		draw_chat_settings();
	else if(window_group_invite)
		draw_group_invite();
	else if(window_group_peerlist)
		draw_group_peerlist();
	else if(window_contacts)
		draw_contacts();
	else if(window_chat)
		draw_chat(global_n);
	else
		error_simple(0,"Failing to redraw unknown window");
}

static void widget_set_cursor(const size_t y,const size_t x)
{ // Internal function only. All text widgets should call this if they need to show a cursor.
	cursor[0] = y;
	cursor[1] = x;
}

static void zero_w(const int w)
{
	if(widget && w > -1 && w < (int)(torx_allocation_len(widget) / sizeof(struct widget)))
	{ // sanity check
		widget[w].text = NULL; // Do not null or free underlying pointer
		widget[w].cursor = NULL; // Do not null or free underlying pointer
		widget[w].callback = NULL;
		widget[w].max_width = 0;
		widget[w].type = 0;
	}
}

static void widget_next_has_default_focus(void)
{ // Sets the next created widget as having default focus when drawing. Needs to be called before widget creation to facilitate cursor/highlighting.
	if(*current_focus < 0)
	{
		const int next_w = (int)(torx_allocation_len(widget) / sizeof(struct widget));
		*current_focus = next_w;
	}
}

static int widget_new(const int type,const size_t max_width)
{ // Internal function only. Will always return 0+.
	if(widget)
		widget = torx_realloc(widget,torx_allocation_len(widget)+sizeof(struct widget));
	else
		widget = torx_insecure_malloc(sizeof(struct widget));
	const int w = (int)(torx_allocation_len(widget) / sizeof(struct widget) - 1); // WILL ALWAYS BE 0+
	zero_w(w);
	widget[w].type = type;
	widget[w].max_width = max_width;
	return w;
}

static void widget_clear(int *new_focus)
{ // Must call first when drawing a new route, and on shutdown
	#define destroy_window(win) if(win) { delwin(win); win = NULL; }
	destroy_window(window_contacts)
	destroy_window(window_settings)
	destroy_window(window_tor_log)
	destroy_window(window_torx_log)
	destroy_window(window_torrc)
	destroy_window(window_change_password)
	destroy_window(window_generate)
	destroy_window(window_global_kill)
	destroy_window(window_home)
	destroy_window(window_chat_actions)
	destroy_window(window_chat_settings)
	destroy_window(window_group_invite)
	destroy_window(window_group_peerlist)
	destroy_window(window_chat)
	destroy_window(window_login)
	if(new_focus)
	{ // We're preparing to draw a new window
		getmaxyx_size(stdscr, &screen_rows, &screen_cols); // 2nd
		inner_width = screen_cols - 2; // 0 or 2 or 4 both acceptable,etc. Must be even.
		current_focus = new_focus;
		widget_set_cursor(0,0); // set to a safe place
		curs_set(0); // set invisible
	}
	const int active_widgets = (int)(torx_allocation_len(widget) / sizeof(struct widget));
	for(int w = 0; w < active_widgets; w++)
		zero_w(w);
	torx_free((void*)&widget);
}

static void widget_draw_cursor(WINDOW *win)
{ // Must call last when drawing a new route, after drawing the last widget, or when re-drawing a text widget alone.
	if(*current_focus < 0)
		*current_focus = 0; // must set to a valid w if unset or keypress will error out
	wmove_size(win, cursor[0], cursor[1]);
	wrefresh(win);
}

static void toggle_highlight(WINDOW *win)
{
	if(highlight_active)
		wattroff(win, A_REVERSE); // highlight off
	else
		wattron(win, A_REVERSE); // highlight on
	highlight_active = !highlight_active;
}

static int widget_button(WINDOW *win,size_t *y,size_t *x,const size_t max_width,int (*callback)(const int,const int),const char *text)
{ // Draw a button
	const int w = widget_new(WIDGET_CHECKBOX,max_width);
	widget[w].callback = callback;
	const size_t text_len = text ? strlen(text) : 0;
	if(*current_focus == w)
		toggle_highlight(win); // highlight on
	print_wrapped(win,y,x,max_width,text,text_len);
	if(*current_focus == w)
		toggle_highlight(win); // highlight off
	return w;
}

static int widget_checkbox(WINDOW *win,size_t *y,size_t *x,const size_t max_width,int (*callback)(const int,const int),const uint8_t reversed,const char *text,const uint8_t ticked)
{ // Draw a checkbox button
	const size_t text_len = text ? strlen(text) : 0;
	char array[text_len + 4 + 1];
	if(reversed)
		snprintf(array, sizeof(array), "[%c] %s",ticked ? 'x':' ',text);
	else
		snprintf(array, sizeof(array), "%s [%c]",text,ticked ? 'x':' ');
	const int w = widget_button(win,y,x,max_width,callback,array);
	sodium_memzero(array,sizeof(array));
	return w;
}

static int widget_text_entry(WINDOW *win,size_t *y,size_t *x,const size_t max_height,const size_t max_width,int (*callback)(const int,const int),const int type,char **text_p,size_t *cursor_pos)
{ // Draw a text entry. Single-line SHOULD highlight when selected. Multi-line should NOT highlight when selected.
	if(type != WIDGET_PASSWORD && type != WIDGET_INPUT_SINGLE_LINE && type != WIDGET_INPUT_MULTI_LINE && type != WIDGET_INPUT_NUMERICAL)
	{
		error_simple(-1,"widget_text_entry passed an inappropriate type. UI coding error. Report this.");
		return 0;
	}
	const size_t start_y = *y, start_x = *x;
	const size_t text_len = (text_p && *text_p) ? strlen(*text_p) : 0;
	if(cursor_pos && *cursor_pos > text_len)
		*cursor_pos = text_len; // Necesary to mitigate bugs
	const int w = widget_new(type,max_width);
	widget[w].callback = callback;
	widget[w].text = text_p;
	widget[w].cursor = cursor_pos;
	char array[text_len + 1]; // zero'd
	if(!pw_show && type == WIDGET_PASSWORD)
		memset(array,'*',sizeof(array)-1);
	else
		snprintf(array,sizeof(array),"%s",*text_p);
	array[text_len] = '\0';
	if(*current_focus == w && type != WIDGET_INPUT_MULTI_LINE)
		toggle_highlight(win); // highlight on
	const size_t cursor_line_of_whole = print_wrapped(NULL,NULL,NULL,max_width,array,cursor_pos ? *cursor_pos : 0);
	size_t print_start = 0; // number of bytes cut off from start
	size_t print_truncation = 0; // number of bytes cut off from end
	size_t print_lines = 1 + print_wrapped(NULL,NULL,NULL,max_width,array,sizeof(array)-1);
	if(print_lines > max_height)
	{ // Our message exceeds box size
		for(size_t lines_to_cut = print_lines - max_height, first_line = 0, last_line,reduction_in_lines; lines_to_cut; lines_to_cut -= reduction_in_lines,print_lines -= reduction_in_lines)
		{ // Must cut off another line
			last_line = first_line + max_height - 1;
			size_t new_print_lines;
			uint8_t shifting_forward;
			do {
				if(cursor_line_of_whole > last_line || (cursor_line_of_whole > first_line && print_start < prior_print_start))
				{ // 1st: Scroll down
					print_start++; // Can only safely go ++
					shifting_forward = 1;
				}
				else
				{ // 2nd: Cut off the excess
					print_truncation += lines_to_cut; // Can safely and efficienctly go this far
					shifting_forward = 0;
				}
			} while((new_print_lines = 1 + print_wrapped(NULL,NULL,NULL,max_width,&array[print_start],sizeof(array)-1-print_start-print_truncation)) == print_lines);
			reduction_in_lines = print_lines - new_print_lines;
			if(shifting_forward)
				first_line += reduction_in_lines;
		}
	}
	prior_print_start = print_start;
	print_wrapped(win,y,x,max_width,&array[print_start],sizeof(array)-1-print_start-print_truncation);
	if(*current_focus == w)
	{
		if(type != WIDGET_INPUT_MULTI_LINE)
			toggle_highlight(win); // highlight off
		size_t row = start_y, col = start_x;
		print_wrapped(NULL,&row, &col, max_width, &(*text_p)[print_start], cursor_pos ? *cursor_pos - print_start: 0);
		widget_set_cursor(row, col);
		curs_set(1);
	}
	sodium_memzero(array,sizeof(array));
	return w;
}

static int callback_password(const int w,const int ch)
{
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		const uint8_t lockout_local = threadsafe_read_uint8(&mutex_global_variable,&lockout);
		if(!lockout_local)
		{
			login_start(*widget[w].text);
			torx_free((void*)&*widget[w].text);
			*widget[w].cursor = 0; // must reset when freeing password
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int move_cursor_up(const int w)
{
	if(*widget[w].cursor == 0)
		return 0; // Can't go further
	size_t starting_row = 0, starting_col = 0;
	print_wrapped(NULL, &starting_row, &starting_col, widget[w].max_width, *widget[w].text, *widget[w].cursor);
	size_t new_cursor = *widget[w].cursor - 1;
	for(size_t end_of_first_line = 900000; new_cursor; new_cursor--)
	{ // Will run at least once
		size_t present_row = 0, present_col = 0;
		print_wrapped(NULL, &present_row, &present_col, widget[w].max_width, *widget[w].text, new_cursor);
		if(present_row < starting_row - 1 || (present_row == starting_row - 1 && present_col == starting_col))
		{
			if(present_row < starting_row - 1)
				new_cursor = end_of_first_line; // too far, go back to end of prior line
			break;
		}
		else if(end_of_first_line == 900000 && present_row == starting_row - 1)
			end_of_first_line = new_cursor;
	}
	*widget[w].cursor = new_cursor;
	return 1;
}

static int move_cursor_down(const int w)
{
	if(*widget[w].cursor + 1 == torx_allocation_len(*widget[w].text))
		return 0; // Can't go further
	size_t starting_row = 0, starting_col = 0;
	print_wrapped(NULL, &starting_row, &starting_col, widget[w].max_width, *widget[w].text, *widget[w].cursor);
	size_t new_cursor = *widget[w].cursor + 1;
	for(; new_cursor < torx_allocation_len(*widget[w].text); new_cursor++)
	{ // Will run at least once
		size_t present_row = 0, present_col = 0;
		print_wrapped(NULL, &present_row, &present_col, widget[w].max_width, *widget[w].text, new_cursor);
		if(present_row > starting_row + 1 || (present_row == starting_row + 1 && present_col == starting_col))
		{
			if(present_row > starting_row + 1)
				new_cursor--; // too far, go back to start of prior line
			break;
		}
	}
	*widget[w].cursor = new_cursor;
	return 1;
}

static int keypress(const int w,const int ch)
{
	if(w < 0 || w >= (int)(torx_allocation_len(widget) / sizeof(struct widget)))
	{ // XXX Due to zero indexing, it will likely show "10 of 10" which is indeed an error.
		error_printf(0,"Keypress called on possibly invalid widget: %lu of %lu",w,torx_allocation_len(widget) / sizeof(struct widget));
		return 0; // Sanity check
	}
	const size_t max_height = screen_rows - 3; // TODO this should be specific to each page
	if(ch == KEY_ESC || ch == KEY_HOME)
	{ // Go back or exit
		if(window_login || window_contacts)
			running = false;
		else if(window_chat || window_home || window_settings || window_generate)
		{
			global_n = -1;
			if(window_settings)
			{
				torx_free((void*)&tmp_snowflake_location);
				torx_free((void*)&tmp_lyrebird_location);
				torx_free((void*)&tmp_conjure_location);
				torx_free((void*)&tmp_tor_location);
			}
			draw_contacts();
		}
		else if(window_chat_actions || window_chat_settings || window_group_invite || window_group_peerlist)
			draw_chat(global_n);
		else if(window_tor_log || window_torx_log || window_global_kill)
			draw_home();
		else if(window_torrc || window_change_password)
			draw_settings();
		else
			error_printf(0,"No window to navigate to. Possible coding error.");
	}
	else if(ch == '\t' || ch == KEY_BTAB)
	{
		*current_focus = (*current_focus + 1) % (int)(torx_allocation_len(widget) / sizeof(struct widget));
		return 1; // Rebuild
	}
	else if((ch == KEY_PPAGE || ch == KEY_NPAGE) && widget[w].type == WIDGET_INPUT_MULTI_LINE && 1 + print_wrapped(NULL, NULL, NULL, widget[w].max_width, *widget[w].text, torx_allocation_len(*widget[w].text)-1) >= max_height)
	{ // PgUp / PgDwn (NOTE: Not just handled here)
		if(ch == KEY_PPAGE)
		{
			for(size_t count = 0; count < max_height; count++)
				if(!move_cursor_up(w))
					break;
		}
		else // if(ch == KEY_NPAGE)
		{
			for(size_t count = 0; count < max_height; count++)
				if(!move_cursor_down(w))
					break;
		}
		return 1;
	}
	else if(ch == KEY_UP)
	{
		if(widget[w].type == WIDGET_INPUT_MULTI_LINE)
			return move_cursor_up(w);
		else if(*current_focus > 0)
			*current_focus = *current_focus - 1;
		return 1; // Rebuild
	}
	else if(ch == KEY_DOWN)
	{
		if(widget[w].type == WIDGET_INPUT_MULTI_LINE)
			return move_cursor_down(w);
		else if(*current_focus < (int)(torx_allocation_len(widget) / sizeof(struct widget) - 1))
			*current_focus = *current_focus + 1;
		return 1; // Rebuild
	}
	else if(ch == KEY_LEFT)
	{
		if(window_contacts)
		{
			focus_contacts = list_first_peer_w;
			return 1; // Rebuild
		}
		else if(widget[w].cursor && *widget[w].cursor > 0)
		{
			*widget[w].cursor = *widget[w].cursor - 1;
			return 1; // Rebuild
		}
		beep();
	}
	else if(ch == KEY_RIGHT)
	{
		if(window_contacts && list_first_peer_w > -1)
		{
			focus_contacts = (focus_contacts + 1) % list_first_peer_w;
			return 1; // Rebuild
		}
		else if(widget[w].cursor && widget[w].text && *widget[w].cursor + 1 < torx_allocation_len(*widget[w].text))
		{
			*widget[w].cursor = *widget[w].cursor + 1;
			return 1; // Rebuild
		}
		beep();
	}
	else if(ch == KEY_DELETE)
	{
		if(widget[w].cursor && widget[w].text && *widget[w].cursor + 1 < torx_allocation_len(*widget[w].text))
		{
			const size_t prior_allocation_len = torx_allocation_len(*widget[w].text);
			memmove(&(*widget[w].text)[*widget[w].cursor], &(*widget[w].text)[*widget[w].cursor+1], prior_allocation_len - *widget[w].cursor - 1);
			*widget[w].text = torx_realloc(*widget[w].text,prior_allocation_len-1); // after memmove
			return 1; // Rebuild
		}
		beep();
	}
	else if(ch == KEY_BACKSPACE || ch == 127 || ch == 8)
	{
		if(widget[w].cursor && widget[w].text && *widget[w].cursor)
		{
			const size_t prior_allocation_len = torx_allocation_len(*widget[w].text);
			memmove(&(*widget[w].text)[*widget[w].cursor-1], &(*widget[w].text)[*widget[w].cursor], prior_allocation_len - *widget[w].cursor);
			*widget[w].text = torx_realloc(*widget[w].text,prior_allocation_len-1); // after memmove
			*widget[w].cursor = *widget[w].cursor - 1;
			return 1; // Rebuild
		}
		beep();
	}
	else if(widget[w].type == WIDGET_INPUT_NUMERICAL && !(ch >= '0' && ch <= '9'))
		beep(); // do nothing, ignore invalid entry
	else if(ch >= 32 && ch <= 126 && widget[w].cursor && widget[w].text) // TODO Incompatible with utf8/wide characters
	{ // Applicable to text widgets only. Captures space but NOT enter.
		if(!*widget[w].text) // first character
		{
			*widget[w].text = torx_secure_malloc(2);
			(*widget[w].text)[*widget[w].cursor + 1] = '\0';
		}
		else
		{ // Subsequent characters
			const size_t prior_allocation_len = torx_allocation_len(*widget[w].text);
			*widget[w].text = torx_realloc(*widget[w].text,prior_allocation_len+1); // before memmove
			memmove(&(*widget[w].text)[*widget[w].cursor+1], &(*widget[w].text)[*widget[w].cursor], prior_allocation_len - *widget[w].cursor);
		}
		(*widget[w].text)[*widget[w].cursor] = (char)ch; // TODO Incompatible with utf8/wide characters
		*widget[w].cursor = *widget[w].cursor + 1;
		return 1; // Rebuild
	}
	else if(widget[w].callback)
		return widget[w].callback(w,ch);
	return 0; // Do not rebuild
}

static int callback_censored_region(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		uint8_t censored_region_local = threadsafe_read_uint8(&mutex_global_variable,&censored_region);
		censored_region_local = !censored_region_local;
		threadsafe_write(&mutex_global_variable,&censored_region,&censored_region_local,sizeof(censored_region_local));
		if(censored_region_local == 1)
			sql_setting(1,-1,"censored_region","1",1);
		else
			sql_setting(1,-1,"censored_region","0",1);
	/*	char array[2]; // Alternative is to make an array and use snprintf, like this
		snprintf(array,sizeof(array),"%u",censored_region_local);
		sql_setting(1,-1,"censored_region",array,1);	*/
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_pw_show(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
		pw_show = !pw_show;
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static void window_prepare(WINDOW **win_p,int *new_focus)
{
	widget_clear(new_focus); // XXX Must do first
	*win_p = newwin_size(screen_rows, screen_cols, 0, 0);
	if(global_theme == LIGHT_THEME)
	{
		wattron(*win_p, A_REVERSE); // highlight on (do not use toggle_highlight here)
		highlight_active = 1;
		for(size_t y = 0; y < screen_rows; y++)
			for(size_t x = 0; x < screen_cols; x++)
				mvwaddch(*win_p, (int)y, (int)x, (chtype)' ');
	}
	box(*win_p,0,0); // Draw border
}

static void draw_login(void)
{ // Password Route
	window_prepare(&window_login,&focus_login); // XXX Must do first

	size_t fy = 0, fx = 2;
	print_wrapped(window_login,&fy,&fx,screen_cols-(fx*2),text_welcome,strlen(text_welcome));
	char text_password[] = "Password:";
	fy += 2, fx = 2; // fy must be += because there might be wrap
	print_wrapped(window_login,&fy,&fx,screen_cols-(fx*2),text_password,sizeof(text_password)-1);

	fy += 1, fx = 4; // fy must be += because there might be wrap
	widget_next_has_default_focus(); // XXX Set default widget focus
	widget_text_entry(window_login,&fy,&fx,screen_rows-fy,screen_cols-(fx*2),callback_password,WIDGET_PASSWORD,&password,&pw_cursor);

	fy += 2,fx = 4; // fy must be += because there might be wrap
	widget_checkbox(window_login,&fy,&fx,screen_cols-(fx*2),callback_pw_show,1,"Show Password",pw_show);

	fy += 1, fx = 4;
	widget_checkbox(window_login,&fy,&fx,screen_cols-(fx*2),callback_censored_region,1,"Censored Region",threadsafe_read_uint8(&mutex_global_variable,&censored_region));

	const char text_password_help[] = "Tab: cycle focus  Up/Down: move focus  Enter: proceed  Esc/Home: quit";
	fy = screen_rows-2, fx = 2;
	print_wrapped(window_login, &fy, &fx, screen_cols-(fx*2), text_password_help, sizeof(text_password_help)-1);

	widget_draw_cursor(window_login); // XXX Must do last
}

static void ui_initialize_language(void)
{
	if(language[0] == '\0' || !strncmp(language,"en_US",5))
	{
		text_title = "TorX";
		text_welcome = "Welcome to TorX";
		text_transfer_completed = "Transfer Completed";
		text_online = "Has come online";
		text_new_friend = "Has a new friend request";
		text_accepted_file = "Accepted a file";
		text_spoiled = "A single-use onion was spoiled";
		text_placeholder_privkey = "Onion Private Key (base64, 88 characters including trailing ==, or select from file)";
		text_placeholder_identifier = "Peer Nickname or Mult Identifier";
		text_placeholder_add_identifier = "Peer Nickname";
		text_placeholder_add_onion = "Peer TorX-ID or OnionID (provided by peer)";
		text_placeholder_add_group_identifier = "Group Nickname";
		text_placeholder_add_group_id = "Public Group ID (provided by peer)";
		text_placeholder_search = "Search";
		text_dark = "Dark";
		text_light = "Light";
		text_minimize_to_tray = "Minimize to tray";
		text_generate_onionid = "Generate OnionID";
		text_generate_torxid = "Generate TorX-ID";
		text_disable = "Disable";
		text_enable = "Enable";
		text_leave_after = "Leave After";
		text_delete_after = "Delete After";
		text_select = "Select";
		text_save_sing = "Save as SING";
		text_save_mult = "Save as MULT";
		text_emit_global_kill = "Emit Global Kill Code";
		text_vertical_emit_global_kill = "Emit Global\nKill Code";
		text_peer = "Peer";
		text_group = "Group";
		text_group_offer = "Group Offer";
		text_audio_message = "Audio Message";
		text_audio_call = "Audio Call";
		text_sticker = "Sticker";
		text_current_members = "Current Members";
		text_group_private = "Private Group";
		text_group_public = "Public Group";
		text_block = "Block";
		text_unblock = "Unblock";
		text_ignore = "Ignore";
		text_unignore = "Unignore";
		text_edit = "Edit";
		text_incoming = "Incoming Requests";
		text_outgoing = "Outgoing Requests";
		text_active_mult = "Active Multi-Use IDs";
		text_active_sing = "Active Single-Use IDs";
		text_you = "You";
		text_queued = "Queued";
		text_draft = "Draft";
		text_accept = "Accept";
		text_reject = "Reject";
		text_copy = "Copy";
		text_resend = "Resend";
		text_start = "Start";
		text_pause = "Pause";
		text_choose_file = "Choose File";
		text_choose_files = "Choose Files";
		text_choose_folder = "Choose Folder";
		text_open_folder = "Open Folder";
		text_cancel = "Cancel";
		text_transfer_paused = "Transfer Paused";
		text_transfer_cancelled = "Transfer Cancelled";
		text_transfer_rejected = "Transfer Rejected";
		text_show_qr = "Show QR";
		text_save_qr = "Save QR";
		text_copy_qr = "Copy QR";
		text_delete = "Delete";
		text_delete_log = "Clear Log";
		text_hold_to_talk = "Hold to Talk";
		text_cancel_editing = "Cancel editing";
		text_private_messaging = "Private Messaging";
		text_rename = "Rename";
		text_button_add = "Send\nFriend\nRequest";
		text_button_join = "Attempt\nTo\nJoin";
		text_button_sing = "Generate Single-Use ID";
		text_button_mult = "Generate Multi-Use ID";
		text_button_generate_invite = "Generate Invite-Only\nGroup";
		text_button_generate_public = "Generate Public\nGroup";
		text_wait = "Please Wait";
		text_tooltip_image_header_0 = "V3Auth Enabled. Peer Blocked.";
		text_tooltip_image_header_1 = "V3Auth Enabled. Peer Connected.";
		text_tooltip_image_header_2 = "V3Auth Enabled. Outbound Connected (50%).";
		text_tooltip_image_header_3 = "V3Auth Enabled. Inbound Connected (50%).";
		text_tooltip_image_header_4 = "V3Auth Enabled. Peer Disconnected.";
		text_tooltip_image_header_5 = "V3Auth Disabled. Peer Blocked.";
		text_tooltip_image_header_6 = "V3Auth Disabled. Peer Connected.";
		text_tooltip_image_header_7 = "V3Auth Disabled. Outbound Connected (50%).";
		text_tooltip_image_header_8 = "V3Auth Disabled. Inbound Connected (50%).";
		text_tooltip_image_header_9 = "V3Auth Disabled. Peer Disconnected.";
		text_tooltip_logging_disabled = "Logging Disabled";
		text_tooltip_logging_enabled = "Logging Enabled";
		text_tooltip_logging_global_on = "Using Global Logging Setting (On)";
		text_tooltip_logging_global_off = "Using Global Logging Setting (Off)";
		text_tooltip_notifications_off = "Notifications off";
		text_tooltip_notifications_on = "Notifications on";
		text_tooltip_blocked_on = "Peer Blocked";
		text_tooltip_blocked_off = "Peer Not Blocked";
		text_tooltip_button_select_custom = "Select hs_ed25519_secret_key file";
		text_tooltip_button_custom_sing = "Save as Single-Use OnionID";
		text_tooltip_button_custom_mult = "Save as Multiple-Use OnionID";
		text_tooltip_group_or_user_name = "Click to modify";
		text_tooltip_button_kill = "DANGER:\nInstruct Peer to Delete Keys and Message History\nThen Delete Keys and Message History";
		text_tooltip_button_delete = "DANGER:\nDelete Keys and Message History";
		text_tooltip_button_delete_log = "DANGER:\nDelete Message History";
		text_status_online = "Currently online";
		text_of = "of";
		text_status_last_seen = "Last seen";
		text_status_never = "Never";
		text_edit_torrc = "Edit Torrc";
		text_saving_will_restart_tor = "Saving will restart Tor";
		text_save = "Save";
		text_override = "Override / Ignore Error";
		text_change_password = "Change Password";
	//	text_resume_interupted = "Resume Interrupted\nPassword Change";
		text_old_password = "Old Password";
		text_new_password = "New Password";
		text_new_password_again = "New Password Again";
		text_settings = "Settings";
//		text_advanced_settings = "Advanced Settings";
		text_set_select_theme = "Select color scheme";
		text_set_select_language = "Select language";
		text_set_onionid_or_torxid = "TorX-ID (<=52 char) or OnionID (56 char with checksum)";
		text_set_global_log = "Message Logging (Global Default)";
		text_set_auto_resume_inbound = "Auto-Resume Inbound Transfers";
		text_set_stickers_save_all = "Save All Stickers";
		text_set_download_directory = "Select Download Directory";
		text_tor = "Tor"; // part B
		text_snowflake = "Snowflake"; // part B
		text_lyrebird = "Lyrebird"; // part B
		text_conjure = "Conjure"; // part B
		text_binary_location = "binary location (effective immediately)"; // part C
		text_set_cpu = "Maximum CPU threads for TorX-ID generation";
		text_set_suffix = "Minimum Suffix Length for TorX-ID generation";
		text_set_validity_sing = "Single-Use TorX-ID expiration time (days)";
		text_set_validity_mult = "Multiple-Use TorX-ID expiration time (days)";
		text_set_auto_mult = "Automatically Accept Incoming Mult Requests";
		text_set_tor_port_socks = "Tor SOCKS5 Port";
		text_set_tor_port_ctrl = "Tor Control Port";
		text_set_tor_password = "Tor Control Password";
		text_set_externally_generated = "Enter a externally generated vanity OnionID or TorX-ID (Advanced)";
		text_tor_log = "Tor Log";
		text_torx_log = "TorX Log";
		text_global_kill = "Global Kill Code";
		text_global_kill_warning = "WARNING:\n\nKill Code is intended to be \
used if your private keys are suspected to have been compromised and are being used to impersonate you. Emiting a Global Kill Code involves \
sending a Kill Code to each of your peers.\n\nEmitting a kill code is irrevocable for peers who are \
online and currently irrevocable also for peers who are offline. When a peer receives a kill code, they are instructed that your keys \
are compromised and their client is automatically requested to delete your contact and potentially all chat history also. After \
successfully sending the kill code, your client should also delete the peer and its associated chat history. \n\nDO NOT CLICK WITHOUT KNOWING WHAT YOU ARE DOING.\n\n\
After activating, you should keep your client active until all of your peers are automatically removed from your peer list, which occurs immediately \
after each comes online and receives the code.";
		text_home = "Home";
		text_add_generate = "Add Peer or Generate ID";
		text_add_peer_by = "Add a peer by their TorX-ID/OnionID";
		text_add_group_by = "Join a Public Group";
		text_generate_for = "Generate a TorX-ID/OnionID for sharing";
		text_generate_group_for = "Generate a Group for sharing";
		text_qr_code = "QR Code";
		text_enter_password = "Enter Password for Decryption";
		text_enter = "Enter";
		text_incorrect = "Incorrect Password";
		text_debug_level = "Current Debug Level:";
		text_debug_level_notice = "Debug level resets for safety on restart.";
		text_fatal_error = "Fatal Error";
		text_active =  "Active";
		text_identifier = "Identifier";
		text_onionid = "OnionID";
		text_torxid = "TorX-ID";
		text_invitor = "Invitor";
		text_groupid = "GroupID";
		text_successfully_created_group = "Successfully created group";
		text_error_creating_group = "Error creating group";
		text_censored_region = "Censored Region";
		text_invite_friend = "Invite Friend";  // unused in GTK
		text_group_peers = "Group Peers"; // unused in GTK
		text_incoming_call = "Incoming Call";
	}
	else if(!strncmp(language,"zh_CN",5))
	{
		//text_chats = "聊天";
		//text_add_generate_bottom = "添加 / 生成";
		//text_dismiss = "关闭";
		//text_enter_password_first_run = "输入密码";
		text_show_qr = "显示二维码";
		//text_scan_qr = "扫描二维码";
		//text_share_qr = "分享二维码";
		//text_log_always = "始终记录";
		//text_log_never = "从不记录";
		//text_log_global_on = "全域开启";
		//text_log_global_off = "全域关闭";
		//text_blocked = "已屏蔽";
		//text_unblocked = "解除屏蔽";
		//text_kill = "终止";
		//text_mute_on = "静音开启";
		//text_mute_off = "静音关闭";
		//text_keyboard_privacy = "键盘隐私";
		//text_placeholder_privkey_flutter = "Base64编码，88个字符（含末尾==）";
		//text_tap_return = "点击返回";
		//text_vertical_change_password = "修改密码";
		//text_quit = "退出";
		//text_reply = "回复";
		//text_warning = "警告";
		text_queued = "已排队";
		text_draft = "草稿";
		//text_select_sticker = "选择表情包";
		text_title = "TorX";
		text_welcome = "欢迎使用TorX";
		text_transfer_completed = "已发送";
		text_online = "在线";
		text_new_friend = "有新的好友请求";
		text_accepted_file = "已接收文件";
		text_spoiled = "一次性洋葱已失效";
		text_placeholder_privkey = "洋葱私钥（base64编码，88个字符含末尾==，或从文件选择）";
		text_placeholder_identifier = "好友昵称或多次标识符";
		text_placeholder_add_identifier = "好友昵称";
		text_placeholder_add_onion = "好友TorX-ID或洋葱ID（由好友提供）";
		text_placeholder_add_group_identifier = "群聊昵称";
		text_placeholder_add_group_id = "公共群聊ID（由好友提供）";
		text_placeholder_search = "搜索";
		text_dark = "深色";
		text_light = "浅色";
		text_minimize_to_tray = "最小化至托盘";
		text_generate_onionid = "生成洋葱ID";
		text_generate_torxid = "生成TorX-ID";
		text_disable = "禁用";
		text_enable = "启用";
		text_leave_after = "离开时间";
		text_delete_after = "删除时间";
		text_select = "选择";
		text_save_sing = "保存为一次性";
		text_save_mult = "保存为多次";
		text_emit_global_kill = "发送全域销毁代码";
		text_vertical_emit_global_kill = "发送全域\n销毁代码";
		text_peer = "好友";
		text_group = "群聊";
		text_group_offer = "群聊邀请";
		text_audio_message = "语音消息";
		text_audio_call = "语音通话";
		text_sticker = "表情包";
		text_current_members = "当前成员";
		text_group_private = "私密群聊";
		text_group_public = "公开群聊";
		text_block = "屏蔽";
		text_unblock = "取消屏蔽";
		text_ignore = "消息免打扰";
		text_unignore = "取消消息免打扰";
		text_edit = "编辑";
		text_incoming = " 收到的请求";
		text_outgoing = " 发出的请求";
		text_active_mult = "活跃多次IDs";
		text_active_sing = "活跃一次性IDs";
		text_you = "你";
		text_accept = "接受";
		text_reject = "拒绝";
		text_copy = "复制";
		//text_copy_all = "全选复制";
		text_start = "开始";
		text_pause = "暂停";
		text_cancel = "取消";
		text_save_qr = "保存二维码";
		text_copy_qr = "复制二维码";
		text_delete = "删除";
		text_delete_log = "清除日志";
		text_hold_to_talk = "按住说话";
		text_cancel_editing = "取消编辑";
		text_private_messaging = "私密消息";
		text_rename = "重命名";
		text_button_add = "发送好友请求";
		text_button_join = "尝试加入";
		text_button_sing = "生成一次性ID";
		text_button_mult = "生成多次ID";
		text_button_generate_invite = "生成邀请制群组";
		text_button_generate_public = "生成公开群组";
		text_wait = "请稍候";
		text_tooltip_image_header_0 = "V3Auth已启用，好友已阻止";
		text_tooltip_image_header_1 = "V3Auth已启用，好友已连接";
		text_tooltip_image_header_2 = "V3Auth已启用，出站连接（50%）";
		text_tooltip_image_header_3 = "V3Auth已启用，入站连接（50%）";
		text_tooltip_image_header_4 = "V3Auth已启用，好友已断开";
		text_tooltip_image_header_5 = "V3Auth已禁用，好友已阻止";
		text_tooltip_image_header_6 = "V3Auth已禁用，好友已连接";
		text_tooltip_image_header_7 = "V3Auth已禁用，出站连接（50%）";
		text_tooltip_image_header_8 = "V3Auth已禁用，入站连接（50%）";
		text_tooltip_image_header_9 = "V3Auth已禁用，好友已断开";
		text_tooltip_logging_disabled = "日志记录已禁用";
		text_tooltip_logging_enabled = "日志记录已启用";
		text_tooltip_logging_global_on = "使用全域日志设置（开启）";
		text_tooltip_logging_global_off = "使用全域日志设置（关闭）";
		text_tooltip_notifications_off = "通知已关闭";
		text_tooltip_notifications_on = "通知已开启";
		text_tooltip_blocked_on = "好友已阻止";
		text_tooltip_blocked_off = "好友未阻止";
		text_tooltip_button_select_custom = "选择hs_ed25519_secret_key文件";
		text_tooltip_button_custom_sing = "保存为一次性洋葱ID";
		text_tooltip_button_custom_mult = "保存为多次洋葱ID";
		text_tooltip_group_or_user_name = "点击修改";
		text_tooltip_button_kill = "危险：\n指示对等方删除密钥和消息历史\n然后删除本地密钥和历史消息记录";
		text_tooltip_button_delete = "危险：\n删除密钥和消息历史";
		text_status_online = "当前在线";
		text_of = "来自";
		text_status_last_seen = "上次在线";
		text_status_never = "从未";
		text_edit_torrc = "编辑Torrc";
		text_saving_will_restart_tor = "保存将重启Tor服务";
		//text_save_torrc = "保存Torrc";
		text_change_password = "修改密码";
		//text_resume_interupted = "恢复中断的\n密码修改";
		text_old_password = "旧密码";
		text_new_password = "新密码";
		text_new_password_again = "再次输入新密码";
		text_settings = "设置";
		text_set_select_theme = "选择主题色";
		text_set_select_language = "选择语言";
		text_set_onionid_or_torxid = "TorX-ID（≤52字符）或洋葱ID（含校验和56字符）";
		text_set_global_log = "消息日志（全域默认）";
		text_set_auto_resume_inbound = "自动恢复入站传输";
		text_set_stickers_save_all = "保存所有表情包";
		text_set_download_directory = "选择下载目录";
		//text_set_tor = "选择自定义Tor二进制文件路径（立即生效）";
		text_tor = "Tor"; // part B
		text_snowflake = "Snowflake"; // part B
		text_lyrebird = "Lyrebird"; // part B
		text_conjure = "Conjure"; // part B
		text_binary_location = "二进制地址(即刻生效)"; // part C
		text_set_cpu = "TorX-ID生成的最大CPU线程数";
		text_set_suffix = "TorX-ID生成的最小后缀长度";
		text_set_validity_sing = "一次性TorX-ID有效期（天）";
		text_set_validity_mult = "多次TorX-ID有效期（天）";
		text_set_auto_mult = "自动接受收到的多次请求";
		text_set_tor_port_socks = "Tor SOCKS5 端口";
		text_set_tor_port_ctrl = "Tor Control 端口";
		text_set_tor_password = "Tor Control 暗语";
		text_set_externally_generated = "输入外部生成的自定义洋葱ID或TorX-ID";
		text_tor_log = "Tor日志";
		text_torx_log = "TorX日志";
		text_global_kill = "全域销毁代码";
		text_global_kill_warning = "销毁代码用于怀疑私钥泄露并被用于冒充你时。发送全局销毁代码会向每个好友发送指令：\n\n在线好友会立即删除你的密钥和聊天记录，离线好友上线后也会执行相同操作。发送成功后，本地客户端也会删除该好友及其聊天记录。\n\n操作前请确保完全理解风险！\n\n激活后请保持客户端运行，直到所有好友从列表中自动移除（对方上线并接收代码后立即生效）。";
		text_home = "主页";
		text_add_generate = "添加对等方或生成ID";
		text_add_peer_by = "通过TorX-ID/洋葱ID添加好友";
		text_add_group_by = "加入公开群组";
		text_generate_for = "生成TorX-ID/洋葱ID或二维码用于分享";
		text_generate_group_for = "生成可分享的群组";
		text_qr_code = "二维码";
		text_enter_password = "输入解密密码";
		text_enter = "确认";
		text_incorrect = "密码错误";
		text_debug_level = "调试级别：";
		text_debug_level_notice = "调试级别重启后自动重置以确保安全。";
		text_fatal_error = "致命错误";
		text_active = "活动中";
		text_identifier = "标识符";
		text_onionid = "洋葱ID";
		text_torxid = "TorX-ID";
		text_invitor = "邀请者";
		text_groupid = "群组ID";
		text_successfully_created_group = "群组创建成功";
		text_error_creating_group = "创建群组失败";
		text_censored_region = "审查区域";
		text_invite_friend = "邀请好友";
		text_group_peers = "群组成员";
		text_save = "保存";
		text_override = "覆盖 / 忽略错误";
		text_incoming_call = "来电";
	}
}

static int callback_theme(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		global_theme = !global_theme;
		toggle_highlight(window_settings);
		char p1[21];
		snprintf(p1,sizeof(p1),"%d",global_theme);
		sql_setting(1,-1,"theme",p1,strlen(p1));
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_language(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		int iter = 0;
		while(language[0] != '\0' && languages_available_code[iter] != NULL && strncmp(language,languages_available_code[iter],5))
			iter++;
		if(languages_available_code[iter] == NULL || languages_available_code[iter + 1] == NULL)
			iter = 0; // reset to first
		else
			iter++;
		snprintf(language,sizeof(language),"%s",languages_available_code[iter]);
		sql_setting(1,-1,"language",language,strlen(language));
		ui_initialize_language();
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_onionid_or_torxid(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		const uint8_t toggled = !threadsafe_read_uint8(&mutex_global_variable,&shorten_torxids);
		threadsafe_write(&mutex_global_variable,&shorten_torxids,&toggled,sizeof(toggled));
		char p1[21];
		snprintf(p1,sizeof(p1),"%u",toggled);
		sql_setting(0,-1,"shorten_torxids",p1,strlen(p1));
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_global_log_messages(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		const uint8_t toggled = !threadsafe_read_uint8(&mutex_global_variable,&global_log_messages);
		threadsafe_write(&mutex_global_variable,&global_log_messages,&toggled,sizeof(toggled));
		char p1[21];
		snprintf(p1,sizeof(p1),"%u",toggled);
		sql_setting(0,-1,"global_log_messages",p1,strlen(p1));
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_tor_location(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_tor_location)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			torx_free((void*)&tor_location);
			tor_location = torx_copy(tmp_tor_location);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(1,-1,"tor_location",tmp_tor_location,torx_allocation_len(tmp_tor_location)-1);
			start_tor();
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_snowflake_location(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_snowflake_location)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			torx_free((void*)&snowflake_location);
			snowflake_location = torx_copy(tmp_snowflake_location);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(1,-1,"snowflake_location",tmp_snowflake_location,torx_allocation_len(tmp_snowflake_location)-1);
			start_tor();
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_lyrebird_location(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_lyrebird_location)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			torx_free((void*)&lyrebird_location);
			lyrebird_location = torx_copy(tmp_lyrebird_location);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(1,-1,"lyrebird_location",tmp_lyrebird_location,torx_allocation_len(tmp_lyrebird_location)-1);
			start_tor();
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_conjure_location(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_conjure_location)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			torx_free((void*)&conjure_location);
			conjure_location = torx_copy(tmp_conjure_location);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(1,-1,"conjure_location",tmp_conjure_location,torx_allocation_len(tmp_conjure_location)-1);
			start_tor();
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_threads(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_threads_max)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			threads_max = (uint32_t)atoll(tmp_threads_max);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(0,-1,"threads_max",tmp_threads_max,torx_allocation_len(tmp_threads_max)-1);
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_suffix(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_suffix_length)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			suffix_length = (uint8_t)atoll(tmp_suffix_length);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(0,-1,"suffix_length",tmp_suffix_length,torx_allocation_len(tmp_suffix_length)-1);
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_sing_days(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_sing_expiration_days)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			sing_expiration_days = (uint32_t)atoll(tmp_sing_expiration_days);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(0,-1,"sing_expiration_days",tmp_sing_expiration_days,torx_allocation_len(tmp_sing_expiration_days)-1);
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_mult_dats(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_mult_expiration_days)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			mult_expiration_days = (uint32_t)atoll(tmp_mult_expiration_days);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(0,-1,"mult_expiration_days",tmp_mult_expiration_days,torx_allocation_len(tmp_mult_expiration_days)-1);
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_auto_mult(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_auto_accept_mult)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			auto_accept_mult = (uint8_t)atoll(tmp_auto_accept_mult);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(0,-1,"auto_accept_mult",tmp_auto_accept_mult,torx_allocation_len(tmp_auto_accept_mult)-1);
		}


		const uint8_t toggled = !threadsafe_read_uint8(&mutex_global_variable,&auto_accept_mult);
		threadsafe_write(&mutex_global_variable,&auto_accept_mult,&toggled,sizeof(toggled));
		char p1[21];
		snprintf(p1,sizeof(p1),"%u",toggled);
		sql_setting(0,-1,"auto_accept_mult",p1,strlen(p1));
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_socks_port(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_tor_socks_port)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			tor_socks_port = (uint16_t)atoll(tmp_tor_socks_port);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(0,-1,"tor_socks_port",tmp_tor_socks_port,torx_allocation_len(tmp_tor_socks_port)-1);
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_ctrl_port(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		if(tmp_tor_ctrl_port)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			tor_ctrl_port = (uint16_t)atoll(tmp_tor_ctrl_port);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			sql_setting(0,-1,"tor_ctrl_port",tmp_tor_ctrl_port,torx_allocation_len(tmp_tor_ctrl_port)-1);
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_ctrl_pass(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		const size_t new_len = tmp_control_password_clear ? torx_allocation_len(tmp_control_password_clear) - 1 : 0;
		pthread_rwlock_rdlock(&mutex_global_variable); // 🟧
		const size_t current_len = control_password_clear ? torx_allocation_len(control_password_clear) - 1 : 0;
		uint8_t changed = 0;
		if((new_len || current_len) && (current_len != new_len || strcmp(tmp_control_password_clear,control_password_clear)))
			changed = 1;
		pthread_rwlock_unlock(&mutex_global_variable); // 🟩
		if(changed)
		{
			pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
			torx_free((void*)&control_password_clear);
			if(new_len)
				control_password_clear = torx_copy(tmp_control_password_clear);
			pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			if(new_len)
				sql_setting(0,-1,"control_password_clear",tmp_control_password_clear,new_len);
			else
				sql_delete_setting(0,-1,"control_password_clear");
			start_tor();
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static uint8_t scrollable(WINDOW *win,size_t *fyp,size_t *fxp,const size_t item_to_draw)
{
	if(win == window_settings)
	{
		const char *selected = "NULL"; // yes initialize as a string
		char label_text[512]; // size is arbitrary
		if(item_to_draw == 0)
		{ // Select language
			if(language[0] == '\0' || !strncmp(language,languages_available_code[0],5))
				selected = languages_available_name[0];
			else if(!strncmp(language,languages_available_code[1],5))
				selected = languages_available_name[1];
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(*fxp*2), text_set_select_language, strlen(text_set_select_language));
			*fyp += 1,*fxp = screen_cols - strlen(selected) - 2;
			widget_button(window_settings,fyp,fxp,screen_cols-(2*2),callback_language,selected);
		}
		else if(item_to_draw == 1)
		{ // Select color scheme
			if(global_theme == LIGHT_THEME)
				selected = text_light;
			else
				selected = text_dark;
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(*fxp*2), text_set_select_theme, strlen(text_set_select_theme));
			*fyp += 1,*fxp = screen_cols - strlen(selected) - 2;
			widget_button(window_settings,fyp,fxp,screen_cols-(2*2),callback_theme,selected);
		}
		else if(item_to_draw == 2)
		{ // TorX-ID (<=52 char) or OnionID (56 char with checksum)
			if(threadsafe_read_uint8(&mutex_global_variable,&shorten_torxids))
				selected = text_generate_torxid;
			else
				selected = text_generate_onionid;
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_onionid_or_torxid, strlen(text_set_onionid_or_torxid));
			*fyp += 1,*fxp = screen_cols - strlen(selected) - 2;
			widget_button(window_settings,fyp,fxp,screen_cols-(2*2),callback_onionid_or_torxid,selected);
		}
		else if(item_to_draw == 3)
		{ // Message Logging (Global Default)
			if(threadsafe_read_uint8(&mutex_global_variable,&global_log_messages))
				selected = text_enable;
			else
				selected = text_disable;
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_global_log, strlen(text_set_global_log));
			*fyp += 1,*fxp = screen_cols - strlen(selected) - 2;
			widget_button(window_settings,fyp,fxp,screen_cols-(2*2),callback_global_log_messages,selected);
		}
		else if(item_to_draw == 4)
		{ // Select Tor binary location (effective immediately)
			snprintf(label_text,sizeof(label_text),"%s %s %s",text_select,text_tor,text_binary_location);
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), label_text, strlen(label_text));
			*fyp += 1,*fxp = screen_cols - (tmp_tor_location ? strlen(tmp_tor_location) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_tor_location,WIDGET_INPUT_SINGLE_LINE,&tmp_tor_location,&tmp_tor_location_pos);
		}
		else if(item_to_draw == 5)
		{ // Select Snowflake binary location (effective immediately)
			snprintf(label_text,sizeof(label_text),"%s %s %s",text_select,text_snowflake,text_binary_location);
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), label_text, strlen(label_text));
			*fyp += 1,*fxp = screen_cols - (tmp_snowflake_location ? strlen(tmp_snowflake_location) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_snowflake_location,WIDGET_INPUT_SINGLE_LINE,&tmp_snowflake_location,&tmp_snowflake_location_pos);
		}
		else if(item_to_draw == 6)
		{ // Select Lyrebird binary location (effective immediately)
			snprintf(label_text,sizeof(label_text),"%s %s %s",text_select,text_lyrebird,text_binary_location);
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), label_text, strlen(label_text));
			*fyp += 1,*fxp = screen_cols - (tmp_lyrebird_location ? strlen(tmp_lyrebird_location) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_lyrebird_location,WIDGET_INPUT_SINGLE_LINE,&tmp_lyrebird_location,&tmp_lyrebird_location_pos);
		}
		else if(item_to_draw == 7)
		{ // Select Conjure binary location (effective immediately)
			snprintf(label_text,sizeof(label_text),"%s %s %s",text_select,text_conjure,text_binary_location);
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), label_text, strlen(label_text));
			*fyp += 1,*fxp = screen_cols - (tmp_conjure_location ? strlen(tmp_conjure_location) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_conjure_location,WIDGET_INPUT_SINGLE_LINE,&tmp_conjure_location,&tmp_conjure_location_pos);
		}
		else if(item_to_draw == 8)
		{ // Maximum CPU threads for TorX-ID generation
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_cpu, strlen(text_set_cpu));
			*fyp += 1,*fxp = screen_cols - (tmp_threads_max ? strlen(tmp_threads_max) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_threads,WIDGET_INPUT_NUMERICAL,&tmp_threads_max,&tmp_threads_max_pos);
		}
		else if(item_to_draw == 9)
		{ // Minimum Suffix Length for TorX-ID generation
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_suffix, strlen(text_set_suffix));
			*fyp += 1,*fxp = screen_cols - (tmp_suffix_length ? strlen(tmp_suffix_length) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_suffix,WIDGET_INPUT_NUMERICAL,&tmp_suffix_length,&tmp_suffix_length_pos);
		}
		else if(item_to_draw == 10)
		{ // Single-Use TorX-ID expiration time (days)
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_validity_sing, strlen(text_set_validity_sing));
			*fyp += 1,*fxp = screen_cols - (tmp_sing_expiration_days ? strlen(tmp_sing_expiration_days) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_sing_days,WIDGET_INPUT_NUMERICAL,&tmp_sing_expiration_days,&tmp_sing_expiration_days_pos);
		}
		else if(item_to_draw == 11)
		{ // Multiple-Use TorX-ID expiration time (days)
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_validity_mult, strlen(text_set_validity_mult));
			*fyp += 1,*fxp = screen_cols - (tmp_mult_expiration_days ? strlen(tmp_mult_expiration_days) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_mult_dats,WIDGET_INPUT_NUMERICAL,&tmp_mult_expiration_days,&tmp_mult_expiration_days_pos);
		}
		else if(item_to_draw == 12)
		{ // Automatically Accept Incoming Mult Requests
			if(threadsafe_read_uint8(&mutex_global_variable,&auto_accept_mult))
				selected = text_enable;
			else
				selected = text_disable;
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_auto_mult, strlen(text_set_auto_mult));
			*fyp += 1,*fxp = screen_cols - strlen(selected) - 2;
			widget_button(window_settings,fyp,fxp,screen_cols-(2*2),callback_auto_mult,selected);
		}
		else if(item_to_draw == 13)
		{ // Tor SOCKS5 Port
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_tor_port_socks, strlen(text_set_tor_port_socks));
			*fyp += 1,*fxp = screen_cols - (tmp_tor_socks_port ? strlen(tmp_tor_socks_port) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_socks_port,WIDGET_INPUT_NUMERICAL,&tmp_tor_socks_port,&tmp_tor_socks_port_pos);
		}
		else if(item_to_draw == 14)
		{ // Tor Control Port
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_tor_port_ctrl, strlen(text_set_tor_port_ctrl));
			*fyp += 1,*fxp = screen_cols - (tmp_tor_ctrl_port ? strlen(tmp_tor_ctrl_port) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_ctrl_port,WIDGET_INPUT_NUMERICAL,&tmp_tor_ctrl_port,&tmp_tor_ctrl_port_pos);
		}
		else if(item_to_draw == 15) // TODO keep the highest number up to date as `max`
		{ // Tor Control Password
			*fyp += 2, *fxp = 2;
			print_wrapped(window_settings, fyp, fxp, screen_cols-(2*2), text_set_tor_password, strlen(text_set_tor_password));
			*fyp += 1,*fxp = screen_cols - (tmp_control_password_clear ? strlen(tmp_control_password_clear) : 0) - 2;
			widget_text_entry(window_settings,fyp,fxp,1,screen_cols-(2*2),callback_ctrl_pass,WIDGET_INPUT_SINGLE_LINE,&tmp_control_password_clear,&tmp_control_password_clear_pos);
		}
		else
			return 0; // Printed nothing
		if(*fyp > screen_rows - 3) // don't modify without extensitve thought
			return 0; // Printed into border or beyond window size
	}
	else
		return 0; // Printed nothing
	return 1; // Printed something complete
}

static void draw_settings(void)
{ // Settings Route TODO be sure all of these things being set can sunsequently be read using ENUM_CUSTOM_SETTING
	window_prepare(&window_settings,&focus_settings); // XXX Must do first

	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0, fx = 0;
	size_t iter = settings_scroll_offset;
	while(scrollable(window_settings,&fy,&fx,iter)) // Prints a "widget" for every iter. Returns 0 when there is no space left, or we run out of widgets to print.
		iter++; // Draw widgets until there is no space left on the screen
	if(!focus_settings && settings_scroll_offset) // XXX DO NOT MODIFY
		settings_scroll_offset--; // XXX DO NOT MODIFY
	else if(focus_settings == (int)(iter - settings_scroll_offset - 1) && fy > screen_rows - 3)
	{ // Scroll down
		settings_scroll_offset++;
		focus_settings--;

	}

	box(window_settings,0,0); // Draw border again (since we probably ran over it with scrollable)

	// TODO Enter an externally generated vanity OnionID or TorX-ID (Advanced)
	/*
	text_set_externally_generated
				[		]
	text_tooltip_button_select_custom
				[		]
	text_placeholder_privkey
				[		]
	text_placeholder_identifier
				[		]
	Output:
				[		]
	 [ text_save_sing ]  	[ text_save_mult ]
	*/
	widget_draw_cursor(window_settings); // XXX Must do last
}

static void draw_tor_log(void)
{ // Tor Log Route
	window_prepare(&window_tor_log,&focus_tor_log); // XXX Must do first






	widget_draw_cursor(window_tor_log); // XXX Must do last
}

static void draw_torx_log(void)
{ // TorX Log Route
	window_prepare(&window_torx_log,&focus_torx_log); // XXX Must do first






	widget_draw_cursor(window_torx_log); // XXX Must do last
}

static void draw_torrc(void)
{ // Torrc Route
	window_prepare(&window_torrc,&focus_torrc); // XXX Must do first






	widget_draw_cursor(window_torrc); // XXX Must do last
}

static void draw_change_password(void)
{ // Change Password Route
	window_prepare(&window_change_password,&focus_change_password); // XXX Must do first







	widget_draw_cursor(window_change_password); // XXX Must do last
}

static void draw_generate(void)
{ // Generate Route
	window_prepare(&window_generate,&focus_generate); // XXX Must do first







	widget_draw_cursor(window_generate); // XXX Must do last
}

static void draw_global_kill(void)
{ // Global Kill Route
	window_prepare(&window_global_kill,&focus_global_kill); // XXX Must do first






	widget_draw_cursor(window_global_kill); // XXX Must do last
}

static void draw_home(void)
{ // Lists Route
	window_prepare(&window_home,&focus_home); // XXX Must do first





	widget_draw_cursor(window_home); // XXX Must do last
}

static void draw_chat_actions(void)
{ // Chat Actions Route (NOTE: global_n is still set)
	window_prepare(&window_chat_actions,&focus_chat_actions); // XXX Must do first






	widget_draw_cursor(window_chat_actions); // XXX Must do last
}

static void draw_chat_settings(void)
{ // Chat Settings Route (NOTE: global_n is still set) TODO be sure all of these things being set can sunsequently be read using ENUM_CUSTOM_SETTING
	window_prepare(&window_chat_settings,&focus_chat_settings); // XXX Must do first






	widget_draw_cursor(window_chat_settings); // XXX Must do last
}

static void draw_group_invite(void)
{ // Group Invite Route (NOTE: global_n is still set)
	window_prepare(&window_group_invite,&focus_group_invite); // XXX Must do first






	widget_draw_cursor(window_group_invite); // XXX Must do last
}

static void draw_group_peerlist(void)
{ // Group Peerlist Route (NOTE: global_n is still set)
	window_prepare(&window_group_peerlist,&focus_group_peerlist); // XXX Must do first






	widget_draw_cursor(window_group_peerlist); // XXX Must do last
}

static int callback_contacts_groups(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		groups_mode = !groups_mode;
		list_first_peer_w = -1;
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static char *torx_itoa(const size_t value)
{
	const size_t length = (size_t)snprintf(NULL, 0, "%lu", value);
	char *allocation = torx_insecure_malloc(length + 1);
	snprintf(allocation,length + 1,"%lu",value);
	return allocation;
}

static int callback_settings(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_settings = -1; // unset, important

		pthread_rwlock_rdlock(&mutex_global_variable); // 🟧
		tmp_snowflake_location = torx_copy(snowflake_location);
		tmp_lyrebird_location = torx_copy(lyrebird_location);
		tmp_conjure_location = torx_copy(conjure_location);
		tmp_tor_location = torx_copy(tor_location);
		tmp_threads_max = torx_itoa(threads_max);
		tmp_suffix_length = torx_itoa(suffix_length);
		tmp_sing_expiration_days = torx_itoa(sing_expiration_days);
		tmp_mult_expiration_days = torx_itoa(mult_expiration_days);
		tmp_auto_accept_mult = torx_itoa(auto_accept_mult);
		tmp_tor_socks_port = torx_itoa(tor_socks_port);
		tmp_tor_ctrl_port = torx_itoa(tor_ctrl_port);
		tmp_control_password_clear = torx_copy(control_password_clear);
		pthread_rwlock_unlock(&mutex_global_variable); // 🟩

		tmp_snowflake_location_pos = tmp_snowflake_location ? torx_allocation_len(tmp_snowflake_location) - 1 : 0;
		tmp_lyrebird_location_pos = tmp_lyrebird_location ? torx_allocation_len(tmp_lyrebird_location) - 1 : 0;
		tmp_conjure_location_pos = tmp_conjure_location ? torx_allocation_len(tmp_conjure_location) - 1 : 0;
		tmp_tor_location_pos = tmp_tor_location ? torx_allocation_len(tmp_tor_location) - 1 : 0;
		tmp_threads_max_pos = tmp_threads_max ? torx_allocation_len(tmp_threads_max) - 1 : 0;
		tmp_suffix_length_pos = tmp_suffix_length ? torx_allocation_len(tmp_suffix_length) - 1 : 0;
		tmp_sing_expiration_days_pos = tmp_sing_expiration_days ? torx_allocation_len(tmp_sing_expiration_days) - 1 : 0;
		tmp_mult_expiration_days_pos = tmp_mult_expiration_days ? torx_allocation_len(tmp_mult_expiration_days) - 1 : 0;
		tmp_auto_accept_mult_pos = tmp_auto_accept_mult ? torx_allocation_len(tmp_auto_accept_mult) - 1 : 0;
		tmp_tor_socks_port_pos = tmp_tor_socks_port ? torx_allocation_len(tmp_tor_socks_port) - 1 : 0;
		tmp_tor_ctrl_port_pos = tmp_tor_ctrl_port ? torx_allocation_len(tmp_tor_ctrl_port) - 1 : 0;
		tmp_control_password_clear_pos = tmp_control_password_clear ? torx_allocation_len(tmp_control_password_clear) - 1 : 0;

		draw_settings();
	}
	return 0; // Do not rebuild
}

static int callback_tor_log(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_tor_log = -1; // unset, important
		draw_tor_log();
	}
	return 0; // Do not rebuild
}

static int callback_torx_log(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_torx_log = -1; // unset, important
		draw_torx_log();
	}
	return 0; // Do not rebuild
}

static int callback_torrc(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_torrc = -1; // unset, important
		draw_torrc();
	}
	return 0; // Do not rebuild
}

static int callback_change_password(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_change_password = -1; // unset, important
		draw_change_password();
	}
	return 0; // Do not rebuild
}

static int callback_generate(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_generate = -1; // unset, important
		draw_generate();
	}
	return 0; // Do not rebuild
}

static int callback_global_kill(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_global_kill = -1; // unset, important
		draw_global_kill();
	}
	return 0; // Do not rebuild
}

static int callback_home(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_home = -1; // unset, important
		draw_home();
	}
	return 0; // Do not rebuild
}

static int callback_chat_actions(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_chat_actions = -1; // unset, important
		draw_chat_actions();
	}
	return 0; // Do not rebuild
}

static int callback_chat_settings(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_chat_settings = -1; // unset, important
		draw_chat_settings();
	}
	return 0; // Do not rebuild
}

static int callback_group_invite(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_group_invite = -1; // unset, important
		draw_group_invite();
	}
	return 0; // Do not rebuild
}

static int callback_group_peerlist(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		focus_group_peerlist = -1; // unset, important
		draw_group_peerlist();
	}
	return 0; // Do not rebuild
}

static int callback_peer(const int w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		global_n = selected_n;
		t_peer[global_n].unread = 0;
		chat_scroll_lines = 0;
		focus_chat = -1; // unset, important
		draw_chat(global_n);
	}
	return 0; // Do not rebuild
}

static void draw_contacts(void)
{ // Contact List Route
	window_prepare(&window_contacts,&focus_contacts); // XXX Must do first

	if(groups_mode)
		mvwprintw_size(window_contacts,0,2," Groups "); // do not wrap
	else
		mvwprintw_size(window_contacts,0,2," Contacts "); // do not wrap

	const char *groups_label = groups_mode ? "[ Contacts ]" : "[ Groups ]";
	size_t fy = 0,fx = screen_cols - strlen(groups_label) - 3;
	widget_next_has_default_focus(); // XXX Set default widget focus
	widget_button(window_contacts,&fy,&fx,strlen(groups_label),callback_contacts_groups,groups_label);

	const char home_label[] = "[ Home ]";
	fx = screen_cols - (sizeof(home_label) - 1) - 3;
	widget_button(window_contacts,&fy,&fx,(sizeof(home_label) - 1),callback_home,home_label);

	const char generate_label[] = "[ Generate ]";
	fx = screen_cols - (sizeof(generate_label) - 1) - 3;
	widget_button(window_contacts,&fy,&fx,(sizeof(generate_label) - 1),callback_generate,generate_label);

	const char settings_label[] = "[ Settings ]";
	fx = screen_cols - (sizeof(settings_label) - 1) - 3;
	widget_button(window_contacts,&fy,&fx,(sizeof(settings_label) - 1),callback_settings,settings_label);

	int len = 0;
	int *array;
	if(groups_mode)
		array = refined_list(&len,ENUM_OWNER_GROUP_CTRL,ENUM_STATUS_FRIEND,NULL);
	else
		array = refined_list(&len,ENUM_OWNER_CTRL,ENUM_STATUS_FRIEND,NULL);
	if(len)
	{
		for(size_t pos = 0; pos < (size_t)len; ++pos)
		{
			const int n = array[pos];
			fy = 2 + pos;
			fx = 2;
			if(fy >= screen_rows - 1) // TODO we don't have scrolling? It just cuts off?
				break;
			const uint8_t sendfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,sendfd_connected));
			const uint8_t recvfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,recvfd_connected));
			char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
			char label[256]; // zero'd
			if(t_peer[n].unread > 0)
				snprintf(label, sizeof(label), "%s(%lu) %s", (sendfd_connected || recvfd_connected) ? "* ":"", t_peer[n].unread, peernick);
			else
				snprintf(label, sizeof(label), "%s%s", (sendfd_connected || recvfd_connected) ? "* ":"", peernick);
			if(sendfd_connected || recvfd_connected)
				wattron(window_contacts,A_BOLD); // bold on
			const int w = widget_button(window_contacts,&fy,&fx,screen_cols-(fx*2),callback_peer,label);
			if(!pos)
				list_first_peer_w = w;
			if(focus_contacts == w)
				selected_n = array[pos];
			if(sendfd_connected || recvfd_connected)
				wattroff(window_contacts,A_BOLD); // bold off
			sodium_memzero(label,sizeof(label));
			torx_free((void*)&peernick);
		}
		torx_free((void*)&array);
	}

	const char text_list_help[] = "Up/Down: select  Enter/Space: open  Esc/Home: quit  Tab: cycle focus";
	fy = screen_rows-2, fx = 2;
	print_wrapped(window_contacts, &fy, &fx, screen_cols-(fx*2), text_list_help, sizeof(text_list_help)-1);

	widget_draw_cursor(window_contacts); // XXX Must do last
}

static inline size_t print_message(WINDOW *win,const size_t top_line,const size_t height_of_scrollable,const size_t must_be_processed_lines,const size_t processed_lines,const int n,const int i)
{
	size_t lines = 0;
	const int p_iter = getter_int(n,i,-1,offsetof(struct message_list,p_iter));
	if(p_iter < 0)
		return lines;
	pthread_rwlock_rdlock(&mutex_protocols); // 🟧
	const uint8_t utf8 = protocols[p_iter].utf8;
	const uint8_t group_pm = protocols[p_iter].group_pm;
	const uint32_t null_terminated_len = protocols[p_iter].null_terminated_len;
	const uint32_t date_len = protocols[p_iter].date_len;
	const uint32_t signature_len = protocols[p_iter].signature_len;
	pthread_rwlock_unlock(&mutex_protocols); // 🟩
	const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
	if(owner == ENUM_OWNER_GROUP_PEER)
	{
		const uint8_t stat = getter_uint8(n,i,-1,offsetof(struct message_list,stat));
		if(!group_pm && stat != ENUM_MESSAGE_RECV)
			return lines; // Do not print OUTBOUND messages on GROUP_PEER unless they are private
		const uint8_t status = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,status));
		if(stat == ENUM_MESSAGE_RECV && (t_peer[n].mute || status == ENUM_STATUS_BLOCKED))
			return lines; // Do not print inbound messages from muted (ignored) or blocked group peers
	}
	if(utf8 && null_terminated_len)
	{
		char *message = getter_string(n,i,-1,offsetof(struct message_list,message));
		if(!message) // this would be a bug?
			return lines;
		const size_t printable_len = torx_allocation_len(message) - null_terminated_len - date_len - signature_len;
		size_t anticipated_lines = 1 + print_wrapped(NULL,NULL,NULL,inner_width,message,printable_len);
		if(win && ((anticipated_lines + processed_lines > must_be_processed_lines - height_of_scrollable) || must_be_processed_lines < height_of_scrollable))
		{ // we are ACTUALLY printing
			const size_t required_offset = chat_scroll_lines > processed_lines ? chat_scroll_lines - processed_lines : 0;
			size_t truncation = 0; // number of characters truncated from the end
			if(required_offset)
			{ // Some of the message was already processed. Truncation required XXX Must do BEFORE calculating offset.
				anticipated_lines -= required_offset; // XXX reducing anticipated lines from the end
				size_t tmp_iter = 0;
				for(size_t found_lines = 0; found_lines < anticipated_lines; found_lines++)
				{ // Need to find point of necessary truncation, if applicable (printing only first part of message)
					size_t iter = 0;
					while(iter < inner_width && message[tmp_iter + iter] != '\n')
						iter++;
					tmp_iter += iter;
				}
				truncation = printable_len - tmp_iter;
			}
			const size_t available_lines = (must_be_processed_lines - processed_lines > height_of_scrollable) ? height_of_scrollable : must_be_processed_lines - processed_lines;
			size_t offset = 0; // number of characters stripped from the start
			if(anticipated_lines > available_lines)
			{ // Can only print latter part of message. Offset required. XXX Must do AFTER calculating truncation.
				for(size_t reduction_required = anticipated_lines - available_lines; reduction_required; reduction_required--)
				{
					size_t iter = 0;
					while(iter < inner_width && message[offset + iter] != '\n')
						iter++;
					offset += iter;
				}
				anticipated_lines = available_lines; // XXX reducing anticipated lines from the start
			}
			size_t fx = (screen_cols - inner_width)/2;
			size_t fy = top_line + available_lines - anticipated_lines;
			lines = 1 + required_offset + print_wrapped(win,&fy,&fx,inner_width,&message[offset],printable_len - offset - truncation);
		//	error_printf(0,"Checkpoint printed-lines: %lu out of anticipated: %lu into available: %lu in scrollable height: %lu chat_scroll_lines: %lu processed: %lu must-be: %lu msg: %s",lines,anticipated_lines,available_lines,height_of_scrollable,chat_scroll_lines,processed_lines,must_be_processed_lines,&message[offset]);
		}
		else // not actually printing
			lines = anticipated_lines;
		torx_free((void*)&message);
	}
	return lines;
}

static int callback_message_input(const int w,const int ch)
{
	(void)w;
	const int n = global_n;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r')
	{
		const size_t unsent_len = torx_allocation_len(t_peer[n].unsent) ? torx_allocation_len(t_peer[n].unsent) - 1 : 0;
		if(!unsent_len)
			return 0; // ignore it
		else if(t_peer[n].unsent_pos == unsent_len)
		{ // send message
			int g = -1;
			uint8_t g_invite_required = 0;
			const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
			if(owner == ENUM_OWNER_GROUP_CTRL)
			{
				g = set_g(n,NULL);
				g_invite_required = getter_group_uint8(g,offsetof(struct group_list,invite_required));
			}
			if(owner == ENUM_OWNER_GROUP_CTRL && g_invite_required) // date && sign private group messages
				message_send(n,ENUM_PROTOCOL_UTF8_TEXT_DATE_SIGNED,t_peer[n].unsent,torx_allocation_len(t_peer[n].unsent)-1);
			else // regular messages, private messages (in authenticated pipes), public messages in public groups (in authenticated pipes)
				message_send(n,ENUM_PROTOCOL_UTF8_TEXT,t_peer[n].unsent,torx_allocation_len(t_peer[n].unsent)-1);
			torx_free((void*)&t_peer[n].unsent);
			t_peer[n].unsent_pos = 0;
			chat_scroll_lines = 0;
		}
		else
		{ // insert newline at cursor
			t_peer[n].unsent = torx_realloc(t_peer[n].unsent, torx_allocation_len(t_peer[n].unsent) + 1);
			const size_t rem = unsent_len - t_peer[n].unsent_pos;
			memmove(&t_peer[n].unsent[t_peer[n].unsent_pos+1], &t_peer[n].unsent[t_peer[n].unsent_pos], rem + 1);
			t_peer[n].unsent[t_peer[n].unsent_pos] = '\n';
			t_peer[n].unsent_pos++;
		}
	}
	else if(ch == KEY_PPAGE && !chat_scroll_max) // PgUp
		chat_scroll_lines += chat_scroll_jump;
	else if(ch == KEY_NPAGE && chat_scroll_lines > 0)
	{ // PgDn
		if(chat_scroll_lines > chat_scroll_jump)
			chat_scroll_lines -= chat_scroll_jump;
		else
			chat_scroll_lines = 0;
	}
	else if(ch == KEY_END)
		chat_scroll_lines = 0;
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static void draw_chat(const int n)
{ // Chat Route
	if(n < 0)
	{
		error_simple(0,"draw_chat called on invalid n. UI coding error. Report this.");
		return; // Bug
	}
	window_prepare(&window_chat,&focus_chat); // XXX Must do first

	#define TOP_LINE_HEIGHT 1
	if(!t_peer[n].unsent)
	{ // Necessary
		t_peer[n].unsent = torx_secure_malloc(1);
		t_peer[n].unsent[0] = '\0';
		t_peer[n].unsent_pos = 0;
	}
	else if(t_peer[n].unsent_pos >= torx_allocation_len(t_peer[n].unsent))
		t_peer[n].unsent_pos = torx_allocation_len(t_peer[n].unsent) - 1;

	const size_t visual_lines = 1 + print_wrapped(NULL,NULL, NULL, inner_width, t_peer[n].unsent, torx_allocation_len(t_peer[n].unsent)); // alt: t_peer[n].unsent_pos

	// Draw horizontal divider
	const size_t mid = visual_lines + 2 + TOP_LINE_HEIGHT >= screen_rows ? TOP_LINE_HEIGHT : screen_rows - visual_lines - 2;
	for(size_t x = 1; x < screen_cols - 1; x++)
		mvwaddch(window_chat, (int)mid, (int)x, ACS_HLINE);
	// Draw intersection characters
	mvwaddch(window_chat, (int)mid, 0, ACS_LTEE);
	mvwaddch(window_chat, (int)mid, (int)screen_cols-1, ACS_RTEE);

	size_t fy = 0,fx = 2;
	const char hint[] = " Type message (Enter to send at end, Esc/Home: back, PgUp/PgDn: scroll) ";
	mvwprintw_size(window_chat, mid, fx, "%.*s",(int)(screen_cols-(fx*2)), hint); // do not wrap

	// Draw top line widgets
	char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
	mvwprintw_size(window_chat,fy,fx,"%s",peernick); // do not wrap
	torx_free((void*)&peernick);

	const char settings_label[] = "[ Settings ]";
	fy = 0,fx = screen_cols - (sizeof(settings_label) - 1) - 3;
	widget_button(window_chat,&fy,&fx,(sizeof(settings_label) - 1),callback_chat_settings,settings_label);

	const char actions_label[] = "[ Actions ]";
	fy = 0,fx = screen_cols - (sizeof(actions_label) - 1) - 1 - (sizeof(settings_label) - 1) - 3;
	widget_button(window_chat,&fy,&fx,(sizeof(actions_label) - 1),callback_chat_actions,actions_label);

	// Get chat history height
	const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
	int min_i,max_i,msg_count,g;
	struct msg_list *page;
	if(owner == ENUM_OWNER_GROUP_CTRL)
	{
		g = set_g(n,NULL);
		pthread_rwlock_rdlock(&mutex_expand_group); // 🟧
		page = group[g].msg_first;
		msg_count = (int)group[g].msg_count;
		pthread_rwlock_unlock(&mutex_expand_group); // 🟩
	}
	else
	{
		max_i = getter_int(n,INT_MIN,-1,offsetof(struct peer_list,max_i));
		min_i = getter_int(n,INT_MIN,-1,offsetof(struct peer_list,min_i));
		msg_count = max_i + 1 - min_i;
	}
	chat_scroll_jump = mid - TOP_LINE_HEIGHT;
	chat_scroll_max = 0; // must reset
	// Print message history
	uint8_t reprinted = 0;
	reprint: {}
	if(reprinted)
	{ // Clear what we already printed. This is ONLY triggered when we scroll ALL the way to the top.
		chat_scroll_lines = chat_scroll_max - chat_scroll_jump;
		const size_t edge = (screen_cols - inner_width)/2;
		for(size_t y = 0; y < chat_scroll_jump; y++)
			for(size_t x = 0; x < inner_width; x++)
				mvwaddch(window_chat, (int)y + TOP_LINE_HEIGHT, (int)(x + edge), (chtype)' ');
	}
	if(msg_count)
	{
		const size_t must_be_processed_lines = chat_scroll_lines + chat_scroll_jump;
		size_t processed_lines = 0;
		if(owner == ENUM_OWNER_GROUP_CTRL)
		{
			pthread_rwlock_rdlock(&mutex_expand_group); // 🟧
			page = group[g].msg_last;
			pthread_rwlock_unlock(&mutex_expand_group); // 🟩
			for(; page && must_be_processed_lines > processed_lines; page = page->message_prior)
			{ // Do not modify logic without extensive testing!
				processed_lines += print_message(window_chat,TOP_LINE_HEIGHT,chat_scroll_jump,must_be_processed_lines,processed_lines,page->n,page->i);
				if(page->message_prior == NULL && message_load_more(n) == 0)
				{ // no more to load. This is rarely triggered!
					chat_scroll_max = processed_lines;
					if(!reprinted++)
						goto reprint;
					break;
				}
			}
		}
		else
			for(int i = max_i; i >= min_i && must_be_processed_lines > processed_lines; i--)
			{ // Do not modify logic without extensive testing!
				processed_lines += print_message(window_chat,TOP_LINE_HEIGHT,chat_scroll_jump,must_be_processed_lines,processed_lines,n,i);
				if(i == min_i)
				{ // no older messages loaded
					if(message_load_more(n) == 0)
					{ // no more to load. This is rarely triggered!
						chat_scroll_max = processed_lines;
						if(!reprinted++)
							goto reprint;
						break;
					}
					min_i = getter_int(n,INT_MIN,-1,offsetof(struct peer_list,min_i)); // alt: min_i += return of message_load_more(n)
				}
			}
	}
	// Print unsent
	fy = mid + 1,fx = (screen_cols - inner_width)/2;
	widget_next_has_default_focus(); // XXX Set default widget focus
	widget_text_entry(window_chat,&fy,&fx,screen_rows - mid - 2,inner_width,callback_message_input,WIDGET_INPUT_MULTI_LINE,&t_peer[n].unsent,&t_peer[n].unsent_pos);

	widget_draw_cursor(window_chat); // XXX Must do last
}

static void notify(void)
{ // TODO placeholder notification function. Should consider whether a peer is muted before notifying. See usage of ui_notify in main_gtk4.c.
	error_simple(0,"Notification of some type!!! BEEP!!!");
	beep();
}

static int await_key_or_signal(WINDOW *win)
{ // Blocks on select(), awaiting keypress or callback.
	fd_set rfds;
	const int stdin_fd = fileno(stdin);
	const int notify_rd = notify_fds[0];
	if(stdin_fd < 0 || notify_rd < 0)
	{ // Fail
		error_simple(0,"stdin_fd or notify_rd failed. Coding error. Report this.");
		running = false;
		return -1;
	}
	while(1)
	{
		if(resized)
			return -1; // Not a bug
		FD_ZERO(&rfds);
		FD_SET(stdin_fd, &rfds);
		FD_SET(notify_rd, &rfds);
		const int maxfd = (stdin_fd > notify_rd) ? stdin_fd : notify_rd;
		if(select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0)
		{
			if(errno == EINTR)
				continue;
			error_printf(0, "select() failed: %s", strerror(errno));
			return -1;
		}
		else if(FD_ISSET(notify_rd, &rfds))
		{ // One or more callbacks are ready, must drain the pipe then return -2
			char buf[128];
			ssize_t r;
			do {
				r = read(notify_fds[0], buf, sizeof(buf));
			} while(r > 0 || (r < 0 && errno == EINTR));
			int must_redraw_ui = 0; // use this sparingly, only when necessary to do a full re-draw
			for(struct cb_info *cb_page; (cb_page = cb_buffer()) ; )
			{
				if(cb_page->cb_type == ENUM_INITIALIZE_N)
				{
					const int n = cb_page->cb_args->mem_int_a;
					t_peer[n].unsent = NULL;
					t_peer[n].unsent_pos = 0;
					t_peer[n].unread = 0;
					t_peer[n].pm_n = -1;
					t_peer[n].edit_n = -1;
					t_peer[n].edit_i = INT_MIN;
					t_peer[n].mute = 0; // 0 no, 1 yes
				}
				else if(cb_page->cb_type == ENUM_INITIALIZE_I)
				{
					// currently N/A
				}
				else if(cb_page->cb_type == ENUM_INITIALIZE_G)
				{
					// currently N/A
				}
				else if(cb_page->cb_type == ENUM_SHRINKAGE)
				{
					// currently N/A
				}
				else if(cb_page->cb_type == ENUM_EXPAND_MESSAGE_STRUC)
				{
					// currently N/A
				}
				else if(cb_page->cb_type == ENUM_EXPAND_PEER_STRUC)
				{
					const uint32_t current_allocation_size = torx_allocation_len(t_peer);
					t_peer = torx_realloc(t_peer,current_allocation_size + sizeof(struct t_peer_list) *10);
				}
				else if(cb_page->cb_type == ENUM_EXPAND_GROUP_STRUC)
				{
					// currently N/A
				}
				else if(cb_page->cb_type == ENUM_CHANGE_PASSWORD)
				{
					error_simple(0,"Checkpoint ENUM_CHANGE_PASSWORD"); // TODO
				}
				else if(cb_page->cb_type == ENUM_INCOMING_FRIEND_REQUEST)
				{
					error_simple(0,"Checkpoint ENUM_INCOMING_FRIEND_REQUEST"); // TODO
				}
				else if(cb_page->cb_type == ENUM_ONION_DELETED)
				{
				//	const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = cb_page->cb_args->mem_uint8;
					if(window_contacts && ((owner == ENUM_OWNER_GROUP_CTRL && groups_mode) || (owner == ENUM_OWNER_CTRL && !groups_mode)))
						must_redraw_ui = -2; // alt: draw_contacts();
				}
				else if(cb_page->cb_type == ENUM_PEER_ONLINE)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
					if(window_contacts && owner == ENUM_OWNER_CTRL && !groups_mode)
						must_redraw_ui = -2; // alt: draw_contacts();
				}
				else if(cb_page->cb_type == ENUM_PEER_OFFLINE)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
					if(window_contacts && owner == ENUM_OWNER_CTRL && !groups_mode)
						must_redraw_ui = -2; // alt: draw_contacts();
				}
				else if(cb_page->cb_type == ENUM_PEER_NEW)
				{
					error_simple(0,"Checkpoint ENUM_PEER_NEW"); // TODO
					notify();
				}
				else if(cb_page->cb_type == ENUM_ONION_READY)
				{
					error_simple(0,"Checkpoint ENUM_ONION_READY"); // TODO
				}
				else if(cb_page->cb_type == ENUM_ERROR)
				{
					// currently N/A, rely on debug_file until we put it in a route
				}
				else if(cb_page->cb_type == ENUM_FATAL)
				{
					// currently N/A, rely on debug_file until we put it in a route
				}
				else if(cb_page->cb_type == ENUM_TOR_LOG)
				{
					// currently N/A
				}
				else if(cb_page->cb_type == ENUM_CUSTOM_SETTING)
				{
				//	const int n = cb_page->cb_args->mem_int_a;
					const char *setting_name = cb_page->cb_args->mem_charp_a;
					const char *setting_value = cb_page->cb_args->mem_charp_b;
					size_t setting_value_len = cb_page->cb_args->mem_size;
					int plaintext = cb_page->cb_args->mem_int_b;
					if(!strncmp(setting_name,"language",8) && sizeof(language) == setting_value_len+1)
					{ // We are requiring the language to be exactly 5 characters long to be considered valid (ex: en_US)
						if(memcmp(language,setting_value,sizeof(language)))
						{ // Loading a different language setting.
							memcpy(language,setting_value,setting_value_len);
							language[setting_value_len] = '\0';
							ui_initialize_language();
							must_redraw_ui = -2;
						}
					}
					else if(plaintext == 0)
						error_printf(3,"Unrecognized encrypted config option: %s",setting_name);
					else if(plaintext == 1)
						error_printf(0,"Unrecognized unencrypted config option: %s",setting_name);
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_NEW)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const int i = cb_page->cb_args->mem_int_b;
					const uint8_t stat = getter_uint8(n,i,-1,offsetof(struct message_list,stat));
					if(stat == ENUM_MESSAGE_RECV)
					{ // Currently we re-draw on every keypress, so we only need to redraw here if it is received
						int group_n = -1;
						const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
						if(owner == ENUM_OWNER_GROUP_PEER)
						{
							const int g = set_g(n,NULL);
							group_n = getter_group_int(g,offsetof(struct group_list,n));
						}
						if(window_contacts || (global_n > -1 && (global_n == n || global_n == group_n)))
							must_redraw_ui = -2; // better than draw_chat(global_n); // NOT n or this could draw a PM chat
						if(window_contacts || must_redraw_ui != -2) // NOT else if
							notify(); // Notify if on contact list, or if we're on a chat that isn't the relevant one
					}
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_MODIFIED)
				{
					error_simple(0,"Checkpoint ENUM_MESSAGE_MODIFIED"); // TODO
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_DELETED)
				{
					error_simple(0,"Checkpoint ENUM_MESSAGE_DELETED"); // TODO
				}
				else if(cb_page->cb_type == ENUM_LOGIN)
				{
					const int value = cb_page->cb_args->mem_int_a;
					if(value == 0) // Correct password
						draw_contacts();
					else // Wrong password
						beep();
				}
				else if(cb_page->cb_type == ENUM_PEER_LOADED)
				{
					error_simple(0,"Checkpoint ENUM_PEER_LOADED"); // TODO
				}
				else if(cb_page->cb_type == ENUM_CLEANUP)
				{
					running = false;
					sig_num = cb_page->cb_args->mem_int_a;
					must_redraw_ui = -1; // necessary
				}
				else if(cb_page->cb_type == ENUM_STREAM)
				{
					error_simple(0,"Checkpoint ENUM_STREAM"); // TODO
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_EXTRA)
				{
					error_simple(0,"Checkpoint ENUM_MESSAGE_EXTRA"); // TODO
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_MORE)
				{
					error_simple(0,"Checkpoint ENUM_MESSAGE_MORE"); // TODO
				}
				else if(cb_page->cb_type == ENUM_UNKNOWN)
				{
					// currently N/A
				}
				torx_free((void*)&cb_page->cb_args->mem_charp_a);
				torx_free((void*)&cb_page->cb_args->mem_charp_b);
				torx_free((void*)&cb_page->cb_args->mem_ucharp);
				torx_free((void*)&cb_page->cb_args->mem_intp_a);
				torx_free((void*)&cb_page->cb_args->mem_intp_b);
				torx_free((void*)&cb_page->cb_args);
				torx_free((void*)&cb_page);
			}
			if(must_redraw_ui)
				return must_redraw_ui;
		}
		else if(FD_ISSET(stdin_fd, &rfds))
		{ // Keyboard input is ready
			int attempts = 3,ch;
			do {
				ch = wgetch(win);
			} while(ch == ERR && attempts-- > 0);
			return ch;
		}
	}
}

void async_notifier(void)
{ // This is passed to the library and will notify us when a callback is ready. A single byte written here will safely trigger await_key_or_signal in the UI thread.
	if(notify_fds[1] < 0)
		return;
	const uint8_t b = 1;
	write(notify_fds[1], &b, 1); // Do not write more than one byte. Only single byte writes are threadsafe here.
}

static inline void option_handler(int argc, char **argv)
{ // XXX Use printf here, not error_printf
	for(int i = 1; i < argc; i++)
	{
		const size_t len = strlen(argv[i]);
		if(!strncmp(argv[i],"-V",len) || !strncmp(argv[i],"--version",len))
		{
			char array[2048]; // arbitrary size
			snprintf(array,sizeof(array),"%sTorX Library Version: %u.%u.%u.%u\n",CLIENT_VERSION,torx_library_version[0],torx_library_version[1],torx_library_version[2],torx_library_version[3]);
			printf("%s",array);
			exit(0);
		}
		else if(!strncmp(argv[i],"-n",len) || !strncmp(argv[i],"--no-password",len))
			no_password = 1;
		else if(!strncmp(argv[i],"-v",len) || !strncmp(argv[i],"--verbose",len))
		{
			if(i + 1 < argc)
				torx_debug_level((int8_t)strtoll(argv[++i], NULL, 10)); // note the ++i
			else
				torx_debug_level(1);
		}
		else if(!strncmp(argv[i], "--directory",len))
		{
			if(i + 1 < argc)
			{
				const size_t allocation_len = strlen(argv[++i]) + 1; // note the ++i
				pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
				working_dir = torx_insecure_malloc(allocation_len);
				snprintf(working_dir,allocation_len,"%s",argv[i]);
				pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			}
			else
				printf("Error: %s requires a path\n",argv[i]);
		}
		else if(!strncmp(argv[i], "--debug-file",len))
		{
			if(i + 1 < argc)
			{
				const size_t allocation_len = strlen(argv[++i]) + 1; // note the ++i
				pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
				debug_file = torx_insecure_malloc(allocation_len);
				snprintf(debug_file,allocation_len,"%s",argv[i]);
				pthread_rwlock_unlock(&mutex_global_variable); // 🟩
			}
			else
				printf("Error: %s requires a path\n",argv[i]);
		}
		else if(!strncmp(argv[i], "-h",len) || !strncmp(argv[i], "--help",len))
		{ // Use printf here, not error_printf. Keep relatively consistent with GTK4.
			const char array[] = "\
Usage:\n\
  torx-ncurses [OPTION…]\n\
\n\
Application Options:\n\
  -V, --version             Print library version\n\
  -n, --no-password         No password\n\
  -v, --verbose=level       Set debug level\n\
  --directory <path>        Set working directory\n\
  --debug-file <path>       Set debug file\n\
";
			printf("%s",array);
			exit(0);
		}
		else
			printf("Unknown option %s\n", argv[i]);
	}
}

int main(int argc, char **argv)
{
	if(pipe(notify_fds) < 0) // Set up pipe for async callbacks
	{ // XXX Use printf here, not error_printf
		printf("Pipe appears not supported. Cannot run on this system.\n");
		return 1;
	}
	int flags = fcntl(notify_fds[0], F_GETFL, 0);
	if(flags != -1)
		fcntl(notify_fds[0], F_SETFL, flags | O_NONBLOCK);
	flags = fcntl(notify_fds[1], F_GETFL, 0);
	if(flags != -1)
		fcntl(notify_fds[1], F_SETFL, flags | O_NONBLOCK);

	option_handler(argc,argv); // must be before initialize_library
	initialize_library(async_notifier);

	setlocale(LC_ALL, "");

	// Initialize ncurses
	initscr(); cbreak(); noecho(); noqiflush();
	keypad(stdscr,TRUE); // necessary to use arrow keys
	set_escdelay(50); // Reduce delay upon pressing Esc. Only relevant if we use keypad() anywhere

	// Set window resize function
	struct sigaction sa = {0};
	sa.sa_handler = signal_resize;
	sigaction(SIGWINCH, &sa, NULL);

	ui_initialize_language();
	draw_login();

	while(running)
	{
		if(resized)
		{
			resized = 0;
			endwin(); refresh(); clear(); // all necessary when resizing
			redraw();
		}
		const int ch = await_key_or_signal(stdscr);
		if(ch == ERR || (ch < 0 && ch != -2))
			continue;
		else if(ch == -2 || keypress(*current_focus,ch))
			redraw();
	}
	// Clean-up
	cleanup_lib(sig_num);
	widget_clear(NULL);
	endwin();
	if(notify_fds[0] >= 0)
		close(notify_fds[0]);
	if(notify_fds[1] >= 0)
		close(notify_fds[1]);
	return 0;
}
