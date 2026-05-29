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
#include <ncurses.h>
#include <locale.h>	// setlocale, for utilizing utf8
#include <unistd.h>	// read,write,pipe,close
#include <fcntl.h>	// related to pipe
#include <beep.h>

#define CLIENT_VERSION "TorX-Ncurses Alpha 2.0.41 2026/04/30 by TorX\n© Copyright 2026 TorX.\n"
#define DARK_THEME 0
#define LIGHT_THEME 1
#define THEME_DEFAULT DARK_THEME
#define FILENAME_BEEP "beep.wav"

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

	char current_working_directory[PATH_MAX];
	if(getcwd(current_working_directory,sizeof(current_working_directory)))
	{ // Do not use starting_dir because cwd has been changed by initial
		char tdd_full_path[PATH_MAX];
		const int tdd_len = snprintf(tdd_full_path,sizeof(tdd_full_path),"%s%ctor_data_directory",current_working_directory,platform_slash) + 1;
		pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
		tor_data_directory = torx_insecure_malloc((size_t)tdd_len);
		memcpy(tor_data_directory,tdd_full_path,(size_t)tdd_len);
		pthread_rwlock_unlock(&mutex_global_variable); // 🟩
	}

	if(!get_file_size(FILENAME_BEEP))
		write_bytes(FILENAME_BEEP,beep_wav,beep_wav_len);

	if(no_password)
		login_start("");
}

#define minimum_size_horizontal 20 // Do not eliminate this or we have to go back to using int instead of size_t and checking for negative values
#define minimum_size_vertical 2

static inline size_t subtract_size(const size_t a,const size_t b)
{ // Safely subtract. Prevents size_t underflow.
	return a > b ? a - b : 0;
}

static void signal_resize(int sig);
static void draw_login(void);
static void draw_requests(void);
static void draw_ids(void);
static void draw_requests_popover(void);
static void draw_ids_popover(void);
static void draw_settings(void);
static void draw_logs(void);
static void draw_torrc(void);
static void draw_change_password(void);
static void draw_generate(void);
static void draw_global_kill(void);
static void draw_chat_actions(void);
static void draw_chat_settings(void);
static void draw_group_invite(void);
static void draw_group_peerlist(void);
static void draw_contacts(void);
static void draw_chat(void);
static int await_key_or_signal(WINDOW *win);
static void draw_scrollable(WINDOW *win,size_t *fyp,size_t *fxp,int *focus,size_t *scroll_offset);
void async_notifier(void);

enum {
	KEY_DELETE = 330,
	KEY_ESC = 27
};
enum widget_types {
	WIDGET_PASSWORD,
	WIDGET_INPUT_SINGLE_LINE,
	WIDGET_INPUT_MULTI_LINE,
	WIDGET_INPUT_NUMERICAL,
	WIDGET_OUTPUT_MULTI_LINE,
	WIDGET_CHECKBOX
};

static struct widget {
	// Consider saving start_y and start_x so we can re-draw individual widgets rather than the while route (especially applicable to checkbox/toggle.
	int type;
	size_t max_width;
	int (*callback)(const int); // typically holds the functionality to be executed upon ENTER press
	char **text;
	size_t *cursor;
} * widget = {0}; // REMEMBER to free this list whenever changing a page. Remember to initialize new widgets with zero_w

static WINDOW **window_current = NULL;
static int *current_focus = NULL; // XXX must be set otherwise we will dereference a NULL very quick! XXX
static size_t *current_scroll_offset = NULL; // WARNING: Is null if no scrollable in route
// XXX START One required for each route START XXX
static int focus_login = -1, focus_settings = -1, focus_requests = -1, focus_ids = -1, focus_popover = -1, focus_contacts = -1, focus_chat = -1, focus_logs = -1, focus_torrc = -1, focus_change_password = -1, focus_generate = -1, focus_global_kill = -1, focus_chat_actions = -1, focus_message_actions = -1, focus_chat_settings = -1, focus_group_invite = -1, focus_group_peerlist = -1; // must initialize as -1 so that draw_* can set a default
static WINDOW *window_login = NULL, *window_settings = NULL, *window_requests = NULL, *window_ids = NULL, *window_requests_popover = NULL, *window_ids_popover = NULL, *window_contacts = NULL, *window_chat = NULL, *window_logs = NULL, *window_torrc = NULL, *window_change_password = NULL, *window_generate = NULL, *window_global_kill = NULL, *window_chat_actions = NULL, *window_message_actions = NULL, *window_chat_settings = NULL, *window_group_invite = NULL, *window_group_peerlist = NULL;
// XXX END One required for each route END XXX

static void (*redraw)(void) = NULL;

static int global_theme = THEME_DEFAULT;
static uint8_t highlight_active = 0; // must initialize as 0
static size_t cursor[2] = {0}; // y,x
static int selected_n = 0; // internal use only, contact list
static int treeview_n = 0; // internal use only, IDs/Requests pages/popovers
static int selected_msg_n = 0; // internal use only, highlighted message_n. Do not rely upon outside of callback_message!
static int selected_msg_i = 0; // internal use only, highlighted message_i. Do not rely upon outside of callback_message!
static int global_n = -1;
static int global_group = -1;
static volatile sig_atomic_t resized = 0;
static volatile sig_atomic_t resize_seq = 0;
static char *search = NULL;

static int log_unread = 1;
static size_t totalUnreadPeer = 0;
static size_t totalUnreadGroup = 0;
static size_t totalIncoming = 0; // incoming requests

static bool running = true; // set to false to exit
static int sig_num = 0;

static size_t screen_rows, screen_cols, inner_width, printable_width; // this will be set on startup and resize

static int notify_fds[2] = { -1, -1 }; // triggered by library callbacks, indicating that a UI call to cb_buffer is requested

/* Login state */
static size_t login_scroll_offset = 0;

/* Chat state */
static bool message_entry_currently_selected = false;
static size_t prior_print_start = 0; // TODO this should really be per peer, but this isn't just for message entry. Can't have it per widget because widgets get destroyed
static size_t chat_scroll_lines = 0; // Number of lines currently scrolled. Similar concept with _scroll_offset
static size_t chat_scroll_max;
static size_t chat_scroll_jump; // Number of lines to move upon PgUp PgDn (set by draw_chat)

/* Password window state */
static bool pw_show = false; // default false
static char *password = NULL;
static char *password_old = NULL;
static char *password_new = NULL;
static char *password_verify = NULL;
static size_t pw_cursor = 0;
static size_t pw_old_cursor = 0;
static size_t pw_new_cursor = 0;
static size_t pw_verify_cursor = 0;
static size_t change_password_scroll_offset = 0;

/* Contact list state */
enum contact_list_values {
	ENUM_SHOW_PEER,
	ENUM_SHOW_GROUP,
	ENUM_SHOW_BLOCK
};
static uint8_t groups_mode = ENUM_SHOW_PEER;
static size_t contacts_scroll_offset = 0;

/* Generate state */
static bool generate_group_mode = 0;
static int generated_n = -1;
static char *generate_input = NULL;
static char *generate_output = NULL;
static char *add_identifier = NULL;
static char *add_id = NULL;
static size_t generate_input_cursor = 0;
static size_t add_identifier_cursor = 0;
static size_t add_id_cursor = 0;
static size_t generate_scroll_offset = 0;

/* Requests state */
static bool outgoing_mode = 0; // default to show incoming requests
static size_t requests_scroll_offset = 0;

/* IDs state */
static bool single_mode = 0; // default to showing mults
static size_t ids_scroll_offset = 0;

/* IDs / Requests Popover state */
static size_t popover_scroll_offset = 0;
static char *tmp_rename = NULL; // Note: also utilized elsewhere
static size_t tmp_rename_pos = 0; // Note: also utilized elsewhere

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

/* Torrc state */
static char *torrc_content_local = NULL;
static size_t torrc_pos = 0;

/* Logs state */
static char *tor_log_buffer = NULL; // Do not free in go_back
static char *torx_log_buffer = NULL; // Do not free in go_back
static size_t tor_log_buffer_pos = 0;
static size_t torx_log_buffer_pos = 0;
static uint8_t tor_log_mode = 0; // 0 == torx logs, 1 == tor logs
static char *tmp_debug_level = NULL;
static size_t tmp_debug_level_pos = 0;

/* Chat settings state */
static size_t chat_settings_scroll_offset = 0;

/* Actions State */
// Note: utilizes tmp_rename tmp_rename_cursor

/* Group Invite State */
static size_t group_invite_scroll_offset = 0;

/* Group Peerlist State */
static size_t group_peerlist_scroll_offset = 0;

/* Scrollable state */
static uint8_t more_to_print = 0;
static size_t widgets_existing_before_scrollable = 0;

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
static const char *text_actions = {0};
static const char *text_add_or_generate = {0};
static const char *text_password = {0};
static const char *text_show_password = {0};
static const char *text_navigation_chat = {0};
static const char *text_navigation_basic = {0};
static const char *text_requests = {0};
static const char *text_ids = {0};
static const char *text_logs = {0};

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
static const char *text_open_folder = {0};
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
static const char *text_invite_friend = {0}; // unused in GTK
static const char *text_group_peers = {0}; // unused in GTK
static const char *text_incoming_call = {0};

static inline size_t torx_utf8len(const char *str)
{ // Counts visible column usage, NOT newlines. BREAKS at newlines. XXX PROBABLY ONLY USEFUL FOR align_ FUNCTIONS
	if(!str)
		return 0;
	const size_t len = strlen(str); // cannot use torx_allocation_len because this is commonly used on other allocation types
	size_t total = 0;
	for(size_t iter = 0; iter < len && str[iter] != '\n'; ) // not necessary to check for \0 because we called strlen
	{
		wchar_t wc;
		const size_t num_bytes = mbrtowc(&wc, &str[iter], len - iter, NULL); // convert a multibyte sequence to a wide character
		int print_width;
		if(num_bytes == (size_t)-1 || num_bytes == (size_t)-2 || (print_width = wcwidth(wc)) < 0) // yes this is correct, according to man page
		{
			if(num_bytes > 0)
				iter += num_bytes;
			else
				iter++;
			continue; // invalid, won't print
		}
		total += (size_t)print_width;
		iter += num_bytes;
	}
	return total;
}

static inline size_t torx_utf8count(const char *str)
{ // Counts characters (wide or otherwise), NOT visible columns. BREAKS at newlines.
	if(!str)
		return 0;
	const size_t len = strlen(str); // cannot use torx_allocation_len because this is commonly used on other allocation types
	size_t total = 0;
	for(size_t iter = 0; iter < len && str[iter] != '\n'; ) // not necessary to check for \0 because we called strlen
	{
		wchar_t wc;
		const size_t num_bytes = mbrtowc(&wc, &str[iter], len - iter, NULL); // convert a multibyte sequence to a wide character
		if(num_bytes == (size_t)-1 || num_bytes == (size_t)-2)
		{
			iter++; // invalid, skip one byte
			continue;
		}
		total++; // count one character regardless of its visible width
		iter += num_bytes ? num_bytes : 1; // num_bytes == 0 means embedded \0, but len ensures we won't loop forever
	}
	return total;
}

static inline size_t align_right(const size_t length)
{ // WARNING: Use torx_utf8len not strlen
	if(length + 2 >= screen_cols)
		return 2; // Safety
	return screen_cols - length - 2;
}

static inline size_t align_center_uncapped(const size_t length)
{ // WARNING: Use torx_utf8len not strlen
	if(length >= screen_cols)
		return 2; // Safety
	return (screen_cols - length) / 2;
}

static inline size_t align_center(const size_t length)
{ // Suitable for WRAPPED printing ONLY. Prevents jumping when reaching the wrap
	return align_center_uncapped(length > printable_width ? printable_width : length);
}

static inline size_t cursor_forward(const char* str,const size_t cur)
{ // Returns how many bytes to move forward for one character
	if(!str || cur >= torx_allocation_len(str))
		return 0;
	size_t byte_movement = mbrtowc(NULL, &str[cur], MB_CUR_MAX, NULL);
	if(byte_movement == (size_t)-1 || byte_movement == (size_t)-2)
		byte_movement = 1;
	return byte_movement;
}

static inline size_t cursor_back(const char* str,const size_t cur)
{ // Returns how many bytes to move backward for one character
	if(!str || !cur)
		return 0;
	size_t byte_movement = 1;
	for(size_t ret,iter = 1; (ret = mbrtowc(NULL, &str[subtract_size(cur,iter)], iter, NULL)) == (size_t)-1 || ret == (size_t)-2; iter++)
		byte_movement += 1;
	return byte_movement;
}

static inline size_t print_internal(WINDOW *win,size_t *y,size_t *x,const size_t max_width,const uint8_t wrap,const char *str,const size_t len,const size_t cursor_pos,size_t *line_starts,size_t *cursor_line_out)
{ // WARNING: Pass bytes len (strlen) NOT utf8len // NOTE: y and x must be initialized
	if(!max_width || !str || !len)
		return 0; // Do not throw error, probably just len is 0
	const uint8_t printing = (win && y && x) ? 1 : 0;
	size_t offset_y = 0,offset_x = 0;
	uint8_t cursor_recorded = 0;
	wchar_t line_buf[max_width + 1];
	int buf_pos = 0; // not to be confused with offset_x
	if(line_starts)
		line_starts[0] = 0; // First line starts at 0
	for(size_t iter = 0; iter < len && str[iter] != '\0'; )
	{ // Go through message/string looking for wraps/newlines (if line_starts), or print, as appropriate
		if(line_starts && !cursor_recorded && iter >= cursor_pos)
		{ // Must set cursor line DO NOT MODIFY
			if(cursor_line_out)
				*cursor_line_out = (offset_x == max_width) ? offset_y + 1 : offset_y; // DO NOT MODIFY
			cursor_recorded = 1;
		}
		wchar_t wc;
		const size_t num_bytes = mbrtowc(&wc, &str[iter], len - iter, NULL);
		int print_width;
		if(str[iter] != '\n' && (num_bytes == (size_t)-1 || num_bytes == (size_t)-2 || (print_width = wcwidth(wc)) < 0))
		{ // Bad bytes or zero printable length, skip.
			if(num_bytes > 0)
				iter += num_bytes;
			else
				iter++;
			continue; // invalid
		}
		else if(str[iter] == '\n' || offset_x + (size_t)print_width > max_width)
		{ // Hit a newline or wrap
			if(!wrap)
				break; // Can't print more without wrapping
			if(printing && buf_pos > 0)
			{ // Print out this now complete line
				mvwaddnwstr(win,(int)(*y + offset_y),(int)(*x),line_buf,buf_pos);
				buf_pos = 0;
			}
			offset_y++;
			offset_x = 0;
			if(str[iter] == '\n')
			{ // Newline starts at next byte
				iter++;
				if(line_starts)
					line_starts[offset_y] = iter;
				continue;
			}
			else if(line_starts)
				line_starts[offset_y] = iter;
		}
		if(printing) // not else if
			line_buf[buf_pos++] = wc; // Append character to print buffer
		offset_x += (size_t)print_width; // Note: +=print_width is because not all wide characters take only one column
		iter += num_bytes;
	}
	if(printing && buf_pos > 0) // Print the final line
		mvwaddnwstr(win,(int)(*y + offset_y),(int)(*x),line_buf,buf_pos);
	if(line_starts && !cursor_recorded && cursor_line_out)
		*cursor_line_out = (offset_x == max_width) ? offset_y + 1 : offset_y;
	const size_t return_val = offset_y; // DO NOT ELIMINATE
	if(offset_x == max_width)
	{ // Important for cursor position (y/x output only, not return value). NOTE: This is so subsequent print_wrap/print_nowrap functions right. DO NOT ELIMINATE
		offset_y++;
		offset_x = 0;
	}
	if(y)
		*y += offset_y;
	if(x)
		*x += offset_x;
	return return_val; // may be 0 if no wraps or newlines occurred // DO NOT REPLACE WITH `return offset_y` because we may have modified offset_y, and callers expect pre-modified.
}

static inline size_t print_wrap(WINDOW *win,size_t *y,size_t *x,const size_t max_width,const char *str,const size_t len)
{ // WARNING: Pass bytes len (strlen) NOT utf8len
	return print_internal(win,y,x,max_width,1,str,len,0,NULL,NULL);
}

static inline size_t print_nowrap(WINDOW *win,size_t *y,size_t *x,const size_t max_width,const char *str,const size_t len)
{ // WARNING: Pass bytes len (strlen) NOT utf8len
	return print_internal(win,y,x,max_width,0,str,len,0,NULL,NULL);
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
	if(window_current && *window_current)
	{
		delwin(*window_current);
		*window_current = NULL;
	}
	if(new_focus)
	{ // We're preparing to draw a new window
		getmaxyx_size(stdscr, &screen_rows, &screen_cols); // 2nd
		inner_width = subtract_size(screen_cols,2); // 0 or 2 or 4 both acceptable,etc. Must be even.
		printable_width = subtract_size(screen_cols,2*2);
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
	const int active_widgets = (int)(torx_allocation_len(widget) / sizeof(struct widget));
	if(*current_focus < 0)
	{ // There are widgets but widget_next_has_default_focus wasn't called, so default to 0 (NOTE: highlight won't work. Should always call widget_next_has_default_focus)
		error_simple(0,"Should always call widget_next_has_default_focus when drawing a window, if widgets exist");
		*current_focus = 0;
	}
	else if(*current_focus >= active_widgets)
	{
		error_simple(0,"Current focus was >= active widgets in widget_draw_cursor");
		*current_focus = active_widgets - 1;
	}
	if(cursor[0] >= screen_rows || cursor[1] >= screen_cols)
	{
		error_simple(0,"Cursor will be displayed in the wrong location, likely due to resizing");
		widget_set_cursor(0,0);
	}
	wmove(win, (int)cursor[0], (int)cursor[1]);
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

static int widget_button(WINDOW *win,size_t *y,size_t *x,const size_t max_width,int (*callback)(const int),const char *text)
{ // Draw a button
	const int w = widget_new(WIDGET_CHECKBOX,max_width);
	widget[w].callback = callback;
	const size_t text_len = text ? strlen(text) : 0;
	if(*current_focus == w)
		toggle_highlight(win); // highlight on
	print_wrap(win,y,x,max_width,text,text_len);
	if(*current_focus == w)
		toggle_highlight(win); // highlight off
	return w;
}

static int widget_checkbox(WINDOW *win,size_t *y,size_t *x,const size_t max_width,int (*callback)(const int),const uint8_t reversed,const char *text,const uint8_t ticked)
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

static int widget_text(WINDOW *win,size_t *y,size_t *x,const size_t max_height,const size_t max_width,int (*callback)(const int),const int type,char **text_p,size_t *cursor_pos)
{ // Draw a text entry. Single-line SHOULD highlight when selected. Multi-line should NOT highlight when selected.
	if(type != WIDGET_PASSWORD && type != WIDGET_INPUT_SINGLE_LINE && type != WIDGET_INPUT_MULTI_LINE && type != WIDGET_OUTPUT_MULTI_LINE && type != WIDGET_INPUT_NUMERICAL)
	{
		error_simple(-1,"widget_text passed an inappropriate type. UI coding error. Report this.");
		return 0;
	}
	if(!max_height || !max_width)
		return 0;
	const size_t start_y = *y, start_x = *x;
	size_t text_bytes = (text_p && *text_p) ? strlen(*text_p) : 0;
	if(type == WIDGET_OUTPUT_MULTI_LINE && text_bytes && (*text_p)[text_bytes-1] == '\n')
		text_bytes--; // strip trailing newline on outputs
	if(cursor_pos && *cursor_pos > text_bytes)
		*cursor_pos = text_bytes; // Necesary to mitigate bugs
	const int w = widget_new(type,max_width);
	widget[w].callback = callback;
	widget[w].text = text_p;
	widget[w].cursor = cursor_pos;
	const size_t array_len = (!pw_show && type == WIDGET_PASSWORD) ? text_bytes + 1 : 0; // only allocate a array when masking
	char mask_buf[array_len]; // zero'd // 0 byte (unused) for non-masked widgets
	const char *source; // mask_buf, or *text_p
	size_t cursor_local = cursor_pos ? *cursor_pos : 0; // For passwords, this is translated below into the masked array's coordinates
	if(!pw_show && type == WIDGET_PASSWORD)
	{ // Mask with one '*' per character (NOT per byte), so wide/multibyte chars render correctly and the cursor stays aligned
		const size_t real_cursor = cursor_local;
		size_t out = 0;
		for(size_t iter = 0; iter < text_bytes; out++)
		{
			mask_buf[out] = '*';
			iter += cursor_forward(*text_p,iter);
			if(iter <= real_cursor)
				cursor_local = out + 1; // masked cursor = count of characters fully before the real cursor
		}
		text_bytes = out; // masked length (<= byte length); subsequent wrapping/cursor logic operates on the mask
		mask_buf[text_bytes] = '\0';
		source = mask_buf;
	}
	else
		source = *text_p; // render directly, no copy (trailing newline excluded via text_bytes length, not truncation)
	if(*current_focus == w && type != WIDGET_INPUT_MULTI_LINE && type != WIDGET_OUTPUT_MULTI_LINE)
		toggle_highlight(win); // highlight on
	size_t line_starts[text_bytes + 1];
	size_t cursor_line_of_whole;
	const size_t print_lines = 1 + print_internal(NULL,NULL,NULL,max_width,1,source,text_bytes,cursor_local,line_starts,&cursor_line_of_whole);
	size_t print_start = 0; // number of bytes cut off from start
	size_t print_truncation = 0; // number of bytes cut off from end
	if(print_lines > max_height)
	{ // Our message exceeds box size -- determine visible window via line-start index
		size_t first_visible_line = 0;
		if(cursor_line_of_whole >= max_height)
			first_visible_line = cursor_line_of_whole - (max_height - 1);
		if(prior_print_start)
		{ // Try to maintain prior scroll position
			size_t prior_line = 0;
			for(size_t i = 1; i < print_lines; i++)
			{
				if(line_starts[i] > prior_print_start)
					break;
				prior_line = i;
			}
			if(prior_line > first_visible_line)
			{
				size_t target = prior_line;
				if(target > cursor_line_of_whole)
					target = cursor_line_of_whole;
				if(cursor_line_of_whole < target + max_height)
					first_visible_line = target;
			}
		}
		if(first_visible_line + max_height > print_lines)
			first_visible_line = print_lines - max_height;
		print_start = line_starts[first_visible_line];
		if(first_visible_line + max_height < print_lines)
			print_truncation = text_bytes - line_starts[first_visible_line + max_height];
	}
	prior_print_start = print_start;
	print_wrap(win,y,x,max_width,&source[print_start],text_bytes-print_start-print_truncation);
	if(*current_focus == w)
	{
		if(type != WIDGET_INPUT_MULTI_LINE && type != WIDGET_OUTPUT_MULTI_LINE)
			toggle_highlight(win); // highlight off
		size_t row = start_y, col = start_x;
		print_wrap(NULL,&row, &col, max_width, &source[print_start], cursor_pos ? cursor_local - print_start: 0);
		widget_set_cursor(row, col);
		curs_set(1);
	}
	sodium_memzero(mask_buf,sizeof(mask_buf));
	return w;
}

static void notify(const char *heading, const char *message)
{ // TODO placeholder notification function. Should consider whether a peer is muted before notifying. See usage of ui_notify in main_gtk4.c.
	error_printf(0,"Popover notification of some type!!! BEEP!!! Header: %s Message: %s",heading,message);
	#ifdef WIN32
	{ // call PlaySound asyncronously to prevent having to CreateProcess
		PlaySound(FILENAME_BEEP, NULL, SND_FILENAME | SND_ASYNC);
	}
	#else
	{
		pid_t pid;
		if((pid = fork()) == -1)
			error_simple(-1,"fork");
		if(pid == 0)
		{ // Alternatively, gresource can probably provide the audio to stdin on *nix but probably not on windows
			if(execlp("paplay","paplay",FILENAME_BEEP,NULL))
				if(execlp("aplay","aplay","-q",FILENAME_BEEP,NULL))
					if(execlp("afplay","afplay","-q",FILENAME_BEEP,NULL)) // OSX, untested
						beep(); // DO NOT MAKE error_printf, as its forked
			exit(0); // TODO wait() or waitpid() to clean up
		}
	}
	#endif
}

static int callback_password(const int w)
{
	const uint8_t lockout_local = threadsafe_read_uint8(&mutex_global_variable,&lockout);
	if(!lockout_local)
	{
		login_start(*widget[w].text);
		torx_free((void*)&*widget[w].text);
		*widget[w].cursor = 0; // must reset when freeing password
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int move_cursor_up(const int w)
{ // DO NOT MODIFY
	if(*widget[w].cursor == 0)
		return 0; // Can't go further
	size_t starting_row = 0, starting_col = 0;
	print_wrap(NULL, &starting_row, &starting_col, widget[w].max_width, *widget[w].text, *widget[w].cursor);
	size_t new_cursor = *widget[w].cursor - cursor_back(*widget[w].text,*widget[w].cursor);
	for(size_t end_of_prior_line = SIZE_MAX; new_cursor; new_cursor -= cursor_back(*widget[w].text,new_cursor))
	{ // Will run at least once
		size_t present_row = 0, present_col = 0;
		print_wrap(NULL, &present_row, &present_col, widget[w].max_width, *widget[w].text, new_cursor);
		if(present_row + 1 < starting_row || (present_row + 1 == starting_row && present_col <= starting_col))
		{
			if(present_row + 1 < starting_row)
				new_cursor = end_of_prior_line; // too far, go back to end of prior line
			break;
		}
		else if(end_of_prior_line == SIZE_MAX && present_row + 1 == starting_row)
		{
			if(starting_col > present_col)
				break;
			end_of_prior_line = new_cursor;
		}
	}
	*widget[w].cursor = new_cursor;
	return 1;
}

static int move_cursor_down(const int w)
{ // DO NOT MODIFY
	const size_t allocation = torx_allocation_len(*widget[w].text);
	if(*widget[w].cursor + 1 == allocation)
		return 0; // Can't go further
	size_t starting_row = 0, starting_col = 0;
	print_wrap(NULL, &starting_row, &starting_col, widget[w].max_width, *widget[w].text, *widget[w].cursor);
	size_t new_cursor = *widget[w].cursor + cursor_forward(*widget[w].text,*widget[w].cursor);
	for(size_t byte_movement = 12345; byte_movement && new_cursor < allocation; new_cursor += (byte_movement = cursor_forward(*widget[w].text,new_cursor)))
	{ // Will run at least once
		size_t present_row = 0, present_col = 0;
		print_wrap(NULL, &present_row, &present_col, widget[w].max_width, *widget[w].text, new_cursor);
		if(present_row > starting_row + 1 || (present_row == starting_row + 1 && present_col >= starting_col))
		{
			if(present_row > starting_row + 1)
				new_cursor -= cursor_back(*widget[w].text,new_cursor); // too far, go back to start of prior line
			break;
		}
	}
	*widget[w].cursor = new_cursor;
	return 1;
}

static inline void append_character_at_cursor(const int w, const wint_t ch)
{ // DO NOT MODIFY
	char buff[MB_CUR_MAX]; // NECESSARY to use buffer, if just to get length
	const size_t length_of_specific_char = wcrtomb(buff,(wchar_t)ch,NULL); // convert a wide character to a multibyte sequence
	if(!*widget[w].text)
	{ // Handle first character
		*widget[w].text = torx_secure_malloc(length_of_specific_char + 1);
		(*widget[w].text)[length_of_specific_char] = '\0';
	}
	else
	{ // Subsequent characters
		const size_t prior_allocation_len = torx_allocation_len(*widget[w].text);
		*widget[w].text = torx_realloc(*widget[w].text,prior_allocation_len+length_of_specific_char); // before memmove
		memmove(&(*widget[w].text)[*widget[w].cursor+length_of_specific_char], &(*widget[w].text)[*widget[w].cursor], prior_allocation_len - *widget[w].cursor); // move data forward from cursor, leaving a gap
	}
	memcpy(&(*widget[w].text)[*widget[w].cursor],buff,length_of_specific_char);
	*widget[w].cursor += length_of_specific_char;
	sodium_memzero(buff,sizeof(buff));
}

static WINDOW *window_prepare(void (*caller)(void),WINDOW **win_p,int *new_focus)
{
	widget_clear(new_focus); // XXX Must do first
	widgets_existing_before_scrollable = 0; // reset
	more_to_print = 0; // reset
	window_current = win_p;
	redraw = caller;
	*win_p = newwin((int)screen_rows,(int)screen_cols, 0, 0);
	if(global_theme == LIGHT_THEME)
	{
		wattron(*win_p, A_REVERSE); // highlight on (do not use toggle_highlight here)
		highlight_active = 1;
		for(size_t y = 0; y < screen_rows; y++)
			for(size_t x = 0; x < screen_cols; x++)
				mvwaddch(*win_p, (int)y, (int)x, (chtype)' '); // Cannot use erase() because that breaks light mode
	}
	box(*win_p,0,0); // Draw border
	return *win_p;
}

static void go_back(size_t motions)
{ // Go back or exit, after cleaning up any heap allocs
	if(current_scroll_offset)
	{ // There was a scrollable
		*current_scroll_offset = 0; // must reset neither or both of these
		*current_focus = -1; // must reset neither or both of these
		current_scroll_offset = NULL;
	}
	while(running && motions--)
	{
		if(window_login || window_contacts)
			running = false;
		else if(window_chat || window_requests || window_ids || window_settings || window_generate || window_logs || window_global_kill)
		{
			if(window_chat)
			{
				if(t_peer[global_n].pm_n > -1 || t_peer[global_n].edit_n > -1)
				{
					t_peer[global_n].pm_n = -1;
					t_peer[global_n].edit_n = -1;
					t_peer[global_n].edit_i = INT_MIN;
					torx_free((void*)&t_peer[global_n].unsent);
					t_peer[global_n].unsent_pos = 0;
					if(motions < 2)
						redraw();
					continue;
				}
				global_n = -1;
				global_group = -1;
			}
			else if(window_settings)
			{
				torx_free((void*)&tmp_snowflake_location);
				torx_free((void*)&tmp_lyrebird_location);
				torx_free((void*)&tmp_conjure_location);
				torx_free((void*)&tmp_tor_location);
				torx_free((void*)&tmp_threads_max);
				torx_free((void*)&tmp_suffix_length);
				torx_free((void*)&tmp_sing_expiration_days);
				torx_free((void*)&tmp_mult_expiration_days);
				torx_free((void*)&tmp_auto_accept_mult);
				torx_free((void*)&tmp_tor_socks_port);
				torx_free((void*)&tmp_tor_ctrl_port);
				torx_free((void*)&tmp_control_password_clear);
			}
			else if(window_generate)
			{
				torx_free((void*)&generate_input);
				torx_free((void*)&generate_output);
				torx_free((void*)&add_identifier);
				torx_free((void*)&add_id);
				generated_n = -1;
			}
			else if(window_logs)
				torx_free((void*)&tmp_debug_level);
			if(motions < 2)
				draw_contacts();
			else // Must prepare prior to destruction
				window_prepare(&draw_contacts,&window_contacts,&focus_contacts);
		}
		else if(window_chat_actions || window_message_actions || window_chat_settings)
		{
			if(window_chat_actions || window_message_actions)
				torx_free((void*)&tmp_rename);
			if(motions < 2)
				draw_chat();
			else // Must prepare prior to destruction
				window_prepare(&draw_chat,&window_chat,&focus_chat);
		}
		else if(window_group_invite || window_group_peerlist)
		{
			if(motions < 2)
				draw_chat_actions();
			else // Must prepare prior to destruction
				window_prepare(&draw_chat_actions,&window_chat_actions,&focus_chat_actions);
		}
		else if(window_torrc || window_change_password)
		{
			if(window_torrc)
				torx_free((void*)&torrc_content_local);
			else if(window_change_password)
			{
				torx_free((void*)&password);
				torx_free((void*)&password_old);
				torx_free((void*)&password_new);
				torx_free((void*)&password_verify);
			}
			if(motions < 2)
				draw_settings();
			else // Must prepare prior to destruction
				window_prepare(&draw_settings,&window_settings,&focus_settings);
		}
		else if(window_requests_popover)
		{
			torx_free((void*)&tmp_rename);
			if(motions < 2)
				draw_requests();
			else // Must prepare prior to destruction
				window_prepare(&draw_requests_popover,&window_requests_popover,&focus_popover);
		}
		else if(window_ids_popover)
		{
			torx_free((void*)&tmp_rename);
			if(motions < 2)
				draw_ids();
			else // Must prepare prior to destruction
				window_prepare(&draw_ids_popover,&window_ids_popover,&focus_popover);
		}
		else
			error_printf(0,"No window to navigate to. Possible coding error.");
	}
}

static int keypress(const int w, const wint_t ch)
{
	if(w >= (int)(torx_allocation_len(widget) / sizeof(struct widget)))
	{ // Due to zero indexing, it will likely show "10 of 10" or higher which is indeed an error.
		error_printf(0,"Keypress called on invalid widget: %d of %lu.",w,torx_allocation_len(widget) / sizeof(struct widget));
		go_back(1);
		return 0;
	}
	uint8_t goneto = 0;
	const size_t max_height = subtract_size(screen_rows,3); // TODO this should be specific to each page
	if(ch == KEY_ESC || ch == KEY_HOME)
		go_back(1);
	else if(w > - 1 && (ch == L'\t' || ch == KEY_BTAB))
	{
		if(window_chat && *current_focus == (int)widgets_existing_before_scrollable && (int)(torx_allocation_len(widget) / sizeof(struct widget)) > (int)widgets_existing_before_scrollable + 1)
			*current_focus = (int)(torx_allocation_len(widget) / sizeof(struct widget)) - 1; // skip to last widget (latest message -> unsent)
		else if(current_scroll_offset && *current_focus + 1 == (int)(torx_allocation_len(widget) / sizeof(struct widget)) && more_to_print)
			*current_scroll_offset += 1; // NOT the same as ++ // At end, need to move down, without current_focus
		else
		{ // DO NOT MODIFY
			if(current_scroll_offset && widgets_existing_before_scrollable && *current_focus + 1 == (int)widgets_existing_before_scrollable)
				*current_scroll_offset = 0; // NOTE: Scroll offset, NOT focus. Optional jump to start of scrollable.
			if(*current_focus + 1 == (int)(torx_allocation_len(widget) / sizeof(struct widget)))
			{ // NOT else if
				*current_focus = 0;
				if(current_scroll_offset && !widgets_existing_before_scrollable)
					*current_scroll_offset = 0;
			}
			else
				*current_focus = (*current_focus + 1) % (int)(torx_allocation_len(widget) / sizeof(struct widget));
		}
		return 1; // Rebuild
	}
	else if(w > - 1 && (ch == KEY_PPAGE || ch == KEY_NPAGE || ch == KEY_END) && (widget[w].type == WIDGET_INPUT_MULTI_LINE || widget[w].type == WIDGET_OUTPUT_MULTI_LINE) && 1 + print_wrap(NULL, NULL, NULL, widget[w].max_width, *widget[w].text, torx_allocation_len(*widget[w].text) - 1) >= max_height)
	{ // PgUp / PgDwn (NOTE: Not just handled here)
		if(ch == KEY_PPAGE)
		{
			for(size_t count = 0; count < max_height; count++)
				if(!move_cursor_up(w))
					break;
		}
		else if(ch == KEY_NPAGE)
		{
			for(size_t count = 0; count < max_height; count++)
				if(!move_cursor_down(w))
					break;
		}
		else // if(ch == KEY_END)
			*widget[w].cursor = torx_allocation_len(*widget[w].text) - 1;
		return 1;
	}
	else if(w > - 1 && window_chat && ch == KEY_PPAGE)
	{ // PgUp
		if(!chat_scroll_max)
		{ // Note: must not combine with parent `else if`
			if(message_entry_currently_selected)
				*current_focus = -1; // reset to default, which is message input (yes this is necessary)
			chat_scroll_lines += chat_scroll_jump;
			return 1;
		}
	}
	else if(w > - 1 && window_chat && ch == KEY_NPAGE)
	{ // PgDn
		if(chat_scroll_lines > 0)
		{ // Note: must not combine with parent `else if`
			if(message_entry_currently_selected)
				*current_focus = -1; // reset to default, which is message input (yes this is necessary)
			if(chat_scroll_lines > chat_scroll_jump)
				chat_scroll_lines -= chat_scroll_jump;
			else
				chat_scroll_lines = 0;
			return 1;
		}
	}
	else if(w > - 1 && window_chat && ch == KEY_END)
	{
		if(chat_scroll_lines)
		{
			if(message_entry_currently_selected)
				*current_focus = -1; // reset to default, which is message input (yes this is necessary)
			chat_scroll_lines = 0;
			return 1;
		}
	}
	else if(w > - 1 && ch == KEY_END)
	{ // Targets single lines entries. Must go AFTER prior usage in window_chat and MULTI_LINE.
		*widget[w].cursor = torx_allocation_len(*widget[w].text) - 1;
		return 1;
	}
	// TODO If not too difficult, scroll widgets HERE. Have some KEY_PPAGE KEY_NPAGE KEY_END usages here that act upon current_scroll and current_scroll_offset. Scrolling DOWN may be harder because we cannot predict how many widgets we can scroll.
	else if(w > - 1 && ch == KEY_UP)
	{ // DO NOT MODIFY
		up: {}
		if((widget[w].type == WIDGET_INPUT_MULTI_LINE || widget[w].type == WIDGET_OUTPUT_MULTI_LINE) && widget[w].text && torx_allocation_len(*widget[w].text) > 1)
			return move_cursor_up(w);
		else if(!goneto && window_chat && *current_focus + 1 == (int)(torx_allocation_len(widget) / sizeof(struct widget)))
		{
			*current_focus = (int)widgets_existing_before_scrollable;
			return 1;
		}
		else if(!goneto && window_chat && *current_focus + 1 < (int)(torx_allocation_len(widget) / sizeof(struct widget)) && *current_focus >= (int)widgets_existing_before_scrollable)
		{
			if(*current_focus == (int)(torx_allocation_len(widget) / sizeof(struct widget) - 2))
			{ // Go from oldest visible message to top bar
				*current_focus = (int)widgets_existing_before_scrollable - 1;
				return 1;
			}
			goneto = 1;
			goto down;
		}
		else if(*current_focus > 0 && (*current_focus != (int)widgets_existing_before_scrollable || !current_scroll_offset || !*current_scroll_offset))
			*current_focus -= 1; // NOT the same as --
		else if(current_scroll_offset && *current_scroll_offset)
			*current_scroll_offset -= 1; // NOT the same as --
		return 1; // Rebuild
	}
	else if(w > - 1 && ch == KEY_DOWN)
	{ // DO NOT MODIFY
		down: {}
		if((widget[w].type == WIDGET_INPUT_MULTI_LINE || widget[w].type == WIDGET_OUTPUT_MULTI_LINE) && widget[w].text && torx_allocation_len(*widget[w].text) > 1)
			return move_cursor_down(w);
		else if(!goneto && window_chat && *current_focus + 1 < (int)(torx_allocation_len(widget) / sizeof(struct widget)) && *current_focus >= (int)widgets_existing_before_scrollable)
		{
			if(*current_focus == (int)widgets_existing_before_scrollable)
			{ // Go from latest message to unsent
				*current_focus = (int)(torx_allocation_len(widget) / sizeof(struct widget) - 1);
				return 1;
			}
			goneto = 1;
			goto up;
		}
		else if(*current_focus + 1 < (int)(torx_allocation_len(widget) / sizeof(struct widget)))
			*current_focus += 1; // NOT the same as ++
		else if(more_to_print && current_scroll_offset)
			*current_scroll_offset += 1; // NOT the same as ++
		return 1; // Rebuild
	}
	else if(w > - 1 && ch == KEY_LEFT)
	{ // DO NOT MODIFY
		if(widget[w].cursor && *widget[w].cursor > 0)
		{
			*widget[w].cursor -= cursor_back(*widget[w].text,*widget[w].cursor);
			return 1; // Rebuild
		}
		else if(window_contacts && *current_focus < (int)widgets_existing_before_scrollable)
		{ // Specifically for contacts page
			*current_focus = (int)widgets_existing_before_scrollable;
			return 1;
		}
		else if(*current_focus < (int)widgets_existing_before_scrollable)
		{
			*current_focus += 1; // NOT the same as ++
			return 1;
		}
		beep();
	}
	else if(w > - 1 && ch == KEY_RIGHT)
	{ // DO NOT MODIFY
		if(widget[w].cursor && widget[w].text)
		{
			if(*widget[w].cursor + 1 < torx_allocation_len(*widget[w].text))
			{
				*widget[w].cursor += cursor_forward(*widget[w].text,*widget[w].cursor);
				return 1; // Rebuild
			}
		}
		else if(*current_focus && *current_focus < (int)widgets_existing_before_scrollable)
		{
			*current_focus -= 1;
			return 1;
		}
		else if(*current_focus >= (int)widgets_existing_before_scrollable && widgets_existing_before_scrollable)
		{
			*current_focus = (int)widgets_existing_before_scrollable - 1;
			return 1;
		}
		beep();
	}
	else if(w > - 1 && ch == KEY_DELETE && widget[w].cursor && widget[w].text && widget[w].type != WIDGET_OUTPUT_MULTI_LINE)
	{
		const size_t prior_allocation_len = torx_allocation_len(*widget[w].text);
		if(*widget[w].cursor + 1 < prior_allocation_len)
		{ // DO NOT MODIFY
			if(window_chat && message_entry_currently_selected)
				*current_focus = -1; // reset to default, which is message input (yes this is necessary)
			const size_t ret = cursor_forward(*widget[w].text,*widget[w].cursor);
			memmove(&(*widget[w].text)[*widget[w].cursor], &(*widget[w].text)[*widget[w].cursor+ret], prior_allocation_len - *widget[w].cursor - ret);
			*widget[w].text = torx_realloc(*widget[w].text,prior_allocation_len-ret); // after memmove
			return 1; // Rebuild
		}
		beep();
	}
	else if(w > - 1 && (ch == KEY_BACKSPACE || ch == 127 || ch == 8) && widget[w].cursor && widget[w].text && widget[w].type != WIDGET_OUTPUT_MULTI_LINE)
	{
		if(*widget[w].cursor)
		{
			if(window_chat && message_entry_currently_selected)
				*current_focus = -1; // reset to default, which is message input (yes this is necessary)
			const size_t removal = cursor_back(*widget[w].text,*widget[w].cursor);
			const size_t prior_allocation_len = torx_allocation_len(*widget[w].text);
			memmove(&(*widget[w].text)[*widget[w].cursor-removal], &(*widget[w].text)[*widget[w].cursor], prior_allocation_len - *widget[w].cursor);
			*widget[w].text = torx_realloc(*widget[w].text,prior_allocation_len-removal); // after memmove
			*widget[w].cursor -= removal;
			return 1; // Rebuild
		}
		beep();
	}
	else if(w > - 1 && widget[w].callback && (ch == L'\n' || ch == KEY_ENTER || ch == L'\r' || (ch == L' ' && (!widget[w].cursor || !widget[w].text || widget[w].type == WIDGET_OUTPUT_MULTI_LINE))))
		return widget[w].callback(w); // XXX MUST BE LAST because if this contains a draw_, it will free widget
	else if(w > - 1 && widget[w].cursor && widget[w].text && widget[w].type != WIDGET_OUTPUT_MULTI_LINE)
	{ // Applicable to text widgets only. Captures space but NOT enter.
		if(widget[w].type == WIDGET_INPUT_NUMERICAL && (ch < '0' || ch > '9'))
			beep(); // do nothing, ignore invalid entry
		else
		{
			if(window_chat && message_entry_currently_selected)
				*current_focus = -1; // reset to default, which is message input (yes this is necessary)
			append_character_at_cursor(w,ch);
			return 1; // Rebuild
		}
	}
	else if(window_contacts || window_ids || window_requests || window_group_invite || window_group_peerlist)
	{ // Handle search entry XXX WARNING: Do not do operations on a widget[w]. There may be no widget. XXX
		size_t allocation = torx_allocation_len(search);
		if(ch == KEY_BACKSPACE || ch == 127 || ch == 8)
		{
			if(allocation > 1)
			{ // DO NOT COMBINE
				allocation -= cursor_back(search,allocation - 1);
				search = torx_realloc(search,allocation);
				search[allocation-1] = '\0';
				return 1;
			}
		}
		else
		{
			char buff[MB_CUR_MAX]; // NECESSARY to use buffer, if just to get length
			const size_t length_of_specific_char = wcrtomb(buff,(wchar_t)ch,NULL); // convert a wide character to a multibyte sequence
			if(allocation)
			{
				allocation += length_of_specific_char;
				search = torx_realloc(search,allocation);
			}
			else
			{
				allocation = length_of_specific_char + 1;
				search = torx_secure_malloc(allocation);
			}
			memcpy(&search[allocation - length_of_specific_char - 1],buff,length_of_specific_char);
			search[allocation - 1] = '\0'; // redundant
			sodium_memzero(buff,sizeof(buff));
			return 1;
		}
		beep();
	}
	error_printf(0,"Keypress not rebuilding. Resized=%d",resized);
	return 0; // Do not rebuild
}

static int callback_censored_region(const int w)
{
	(void)w;
	uint8_t censored_region_local = threadsafe_read_uint8(&mutex_global_variable,&censored_region);
	censored_region_local = !censored_region_local;
	threadsafe_write(&mutex_global_variable,&censored_region,&censored_region_local,sizeof(censored_region_local));
	if(censored_region_local == 1)
		sql_setting(1,-1,"censored_region","1",1);
	else
		sql_setting(1,-1,"censored_region","0",1);
	return 1; // Rebuild
}

static int callback_pw_show(const int w)
{
	(void)w;
	pw_show = !pw_show;
	return 1; // Rebuild
}

static void draw_login(void)
{ // Password Route
	WINDOW *win = window_prepare(&draw_login,&window_login,&focus_login); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0, fx = 2;
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx*2),text_welcome,strlen(text_welcome));
	wattroff(win,A_BOLD); // bold off

//	fy = screen_rows-2, fx = 2; // Drawing bottom first because it is less important
//	print_wrap(win, &fy, &fx, subtract_size(screen_cols,fx*2), text_navigation_basic, strlen(text_navigation_basic));

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_login,&login_scroll_offset);

	widget_draw_cursor(win); // XXX Must do last
}

static void ui_initialize_language(void)
{
	if(language[0] == '\0' || !strncmp(language,"en_US",5))
	{
		text_actions = "Actions";
		text_add_or_generate = "Add/Generate";
		text_password = "Password";
		text_show_password = "Show Password";
		text_navigation_chat = "Type message (Enter to send at end, Esc/Home: back, PgUp/PgDn: scroll)";
		text_navigation_basic = "Tab: cycle focus  Up/Down: move focus  Enter: proceed  Esc/Home: quit";
		text_requests = "Requests";
		text_ids = "IDs";
		text_logs = "Logs";

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
		text_active_mult = "Multi-Use IDs";
		text_active_sing = "Single-Use IDs";
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
		text_button_add = "Send Friend Request";
		text_button_join = "Attempt To Join";
		text_button_sing = "Generate Single-Use ID";
		text_button_mult = "Generate Multi-Use ID";
		text_button_generate_invite = "Generate Invite-Only Group";
		text_button_generate_public = "Generate Public Group";
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
		text_active = "Active";
		text_identifier = "Identifier";
		text_onionid = "OnionID";
		text_torxid = "TorX-ID";
		text_invitor = "Invitor";
		text_groupid = "GroupID";
		text_successfully_created_group = "Successfully created group";
		text_error_creating_group = "Error creating group";
		text_censored_region = "Censored Region";
		text_invite_friend = "Invite Friend"; // unused in GTK
		text_group_peers = "Group Peers"; // unused in GTK
		text_incoming_call = "Incoming Call";
	}
	else if(!strncmp(language,"zh_CN",5))
	{
		text_actions = "操作";
		text_add_or_generate = "添加 / 生成";
		text_password = "密码";
		text_show_password = "显示密码";
		text_navigation_chat = "输入消息（Enter 在末尾发送，Esc/Home：返回，PgUp/PgDn：滚动）";
		text_navigation_basic = "Tab：循环切换焦点  Up/Down：移动焦点  Enter：继续  Esc/Home：退出";
		text_requests = "请求";
		text_ids = "IDs";
		text_logs = "日志";

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
		text_active_mult = "多次IDs";
		text_active_sing = "一次性IDs";
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

static int callback_theme(const int w)
{
	(void)w;
	global_theme = !global_theme;
	toggle_highlight(window_settings);
	char p1[21];
	const size_t len = (size_t)snprintf(p1,sizeof(p1),"%d",global_theme);
	sql_setting(1,-1,"theme",p1,len);
	return 1; // Rebuild
}

static int callback_language(const int w)
{
	(void)w;
	int iter = 0;
	while(language[0] != '\0' && languages_available_code[iter] != NULL && strncmp(language,languages_available_code[iter],5))
		iter++;
	if(languages_available_code[iter] == NULL || languages_available_code[iter + 1] == NULL)
		iter = 0; // reset to first
	else
		iter++;
	const size_t len = (size_t)snprintf(language,sizeof(language),"%s",languages_available_code[iter]);
	sql_setting(1,-1,"language",language,len);
	ui_initialize_language();
	return 1; // Rebuild
}

static int callback_onionid_or_torxid(const int w)
{
	(void)w;
	const uint8_t toggled = !threadsafe_read_uint8(&mutex_global_variable,&shorten_torxids);
	threadsafe_write(&mutex_global_variable,&shorten_torxids,&toggled,sizeof(toggled));
	char p1[21];
	const size_t len = (size_t)snprintf(p1,sizeof(p1),"%u",toggled);
	sql_setting(0,-1,"shorten_torxids",p1,len);
	return 1; // Rebuild
}

static int callback_global_log_messages(const int w)
{
	(void)w;
	const uint8_t toggled = !threadsafe_read_uint8(&mutex_global_variable,&global_log_messages);
	threadsafe_write(&mutex_global_variable,&global_log_messages,&toggled,sizeof(toggled));
	char p1[21];
	const size_t len = (size_t)snprintf(p1,sizeof(p1),"%u",toggled);
	sql_setting(0,-1,"global_log_messages",p1,len);
	return 1; // Rebuild
}

static int callback_tor_location(const int w)
{
	(void)w;
	if(tmp_tor_location)
	{
		pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
		torx_free((void*)&tor_location);
		tor_location = torx_copy(tmp_tor_location);
		pthread_rwlock_unlock(&mutex_global_variable); // 🟩
		sql_setting(1,-1,"tor_location",tmp_tor_location,torx_allocation_len(tmp_tor_location)-1);
		start_tor();
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_snowflake_location(const int w)
{
	(void)w;
	if(tmp_snowflake_location)
	{
		pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
		torx_free((void*)&snowflake_location);
		snowflake_location = torx_copy(tmp_snowflake_location);
		pthread_rwlock_unlock(&mutex_global_variable); // 🟩
		sql_setting(1,-1,"snowflake_location",tmp_snowflake_location,torx_allocation_len(tmp_snowflake_location)-1);
		start_tor();
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_lyrebird_location(const int w)
{
	(void)w;
	if(tmp_lyrebird_location)
	{
		pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
		torx_free((void*)&lyrebird_location);
		lyrebird_location = torx_copy(tmp_lyrebird_location);
		pthread_rwlock_unlock(&mutex_global_variable); // 🟩
		sql_setting(1,-1,"lyrebird_location",tmp_lyrebird_location,torx_allocation_len(tmp_lyrebird_location)-1);
		start_tor();
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_conjure_location(const int w)
{
	(void)w;
	if(tmp_conjure_location)
	{
		pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
		torx_free((void*)&conjure_location);
		conjure_location = torx_copy(tmp_conjure_location);
		pthread_rwlock_unlock(&mutex_global_variable); // 🟩
		sql_setting(1,-1,"conjure_location",tmp_conjure_location,torx_allocation_len(tmp_conjure_location)-1);
		start_tor();
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_threads(const int w)
{
	(void)w;
	pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
	threads_max = (uint32_t)atoll(tmp_threads_max);
	pthread_rwlock_unlock(&mutex_global_variable); // 🟩
	sql_setting(0,-1,"threads_max",tmp_threads_max,torx_allocation_len(tmp_threads_max)-1);
	return 1; // Rebuild
}

static int callback_suffix(const int w)
{
	(void)w;
	pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
	suffix_length = (uint8_t)atoll(tmp_suffix_length);
	pthread_rwlock_unlock(&mutex_global_variable); // 🟩
	sql_setting(0,-1,"suffix_length",tmp_suffix_length,torx_allocation_len(tmp_suffix_length)-1);
	return 1; // Rebuild
}

static int callback_sing_days(const int w)
{
	(void)w;
	pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
	sing_expiration_days = (uint32_t)atoll(tmp_sing_expiration_days);
	pthread_rwlock_unlock(&mutex_global_variable); // 🟩
	sql_setting(0,-1,"sing_expiration_days",tmp_sing_expiration_days,torx_allocation_len(tmp_sing_expiration_days)-1);
	return 1; // Rebuild
}

static int callback_mult_dats(const int w)
{
	(void)w;
	pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
	mult_expiration_days = (uint32_t)atoll(tmp_mult_expiration_days);
	pthread_rwlock_unlock(&mutex_global_variable); // 🟩
	sql_setting(0,-1,"mult_expiration_days",tmp_mult_expiration_days,torx_allocation_len(tmp_mult_expiration_days)-1);
	return 1; // Rebuild
}

static int callback_auto_mult(const int w)
{
	(void)w;
	const uint8_t toggled = !threadsafe_read_uint8(&mutex_global_variable,&auto_accept_mult);
	threadsafe_write(&mutex_global_variable,&auto_accept_mult,&toggled,sizeof(toggled));
	char p1[21];
	const size_t len = (size_t)snprintf(p1,sizeof(p1),"%u",toggled);
	sql_setting(0,-1,"auto_accept_mult",p1,len);
	return 1; // Rebuild
}

static int callback_socks_port(const int w)
{
	(void)w;
	pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
	tor_socks_port = (uint16_t)atoll(tmp_tor_socks_port);
	pthread_rwlock_unlock(&mutex_global_variable); // 🟩
	sql_setting(0,-1,"tor_socks_port",tmp_tor_socks_port,torx_allocation_len(tmp_tor_socks_port)-1);
	return 1; // Rebuild
}

static int callback_ctrl_port(const int w)
{
	(void)w;
	pthread_rwlock_wrlock(&mutex_global_variable); // 🟥
	tor_ctrl_port = (uint16_t)atoll(tmp_tor_ctrl_port);
	pthread_rwlock_unlock(&mutex_global_variable); // 🟩
	sql_setting(0,-1,"tor_ctrl_port",tmp_tor_ctrl_port,torx_allocation_len(tmp_tor_ctrl_port)-1);
	return 1; // Rebuild
}

static int callback_ctrl_pass(const int w)
{
	(void)w;
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
		sql_setting(0,-1,"control_password_clear",tmp_control_password_clear,new_len); // Will delete if NULL/0
		start_tor();
	}
	return 1; // Rebuild
}

static int callback_rename(const int w)
{
	if(window_message_actions) // must be first because global_n is also set
		change_nick(selected_msg_n,*widget[w].text);
	else if(global_n > -1)
		change_nick(global_n,*widget[w].text);
	else if(treeview_n > -1)
		change_nick(treeview_n,*widget[w].text);
	return 1; // Rebuild
}

static int callback_toggle_block(const int w)
{
	(void)w;
	if(global_n > -1)
		block_peer(global_n);
	else if(treeview_n > -1)
		block_peer(treeview_n);
	return 1; // Rebuild
}

static int callback_peer_accept(const int w)
{
	(void)w;
	if(window_requests_popover && !outgoing_mode)
		totalIncoming--;
	peer_accept(treeview_n);
	go_back(1);
	return 0; // Do not rebuild
}

static int callback_peer_delete(const int w)
{ // This is not a general deletion function, it is only for incoming requests
	(void)w;
	if(window_requests_popover && !outgoing_mode)
		totalIncoming--;
	const int peer_index = getter_int(treeview_n,INT_MIN,-1,offsetof(struct peer_list,peer_index));
	takedown_onion(peer_index,1);
	go_back(1);
	return 0; // Do not rebuild
}

static int callback_chat_logging(const int w)
{
	(void)w;
	const int peer_index = getter_int(global_n,INT_MIN,-1,offsetof(struct peer_list,peer_index));
	int8_t log_messages = getter_int8(global_n,INT_MIN,-1,offsetof(struct peer_list,log_messages));
	// Update Setting
	if(log_messages == -1 || log_messages == 0)
	{
		log_messages++;
		setter(global_n,INT_MIN,-1,offsetof(struct peer_list,log_messages),&log_messages,sizeof(log_messages));
	}
	else if(log_messages == 1)
	{
		log_messages = -1;
		setter(global_n,INT_MIN,-1,offsetof(struct peer_list,log_messages),&log_messages,sizeof(log_messages));
	}
	// Save Setting
	char p1[21];
	const size_t len = (size_t)snprintf(p1,sizeof(p1),"%d",log_messages);
	sql_setting(0,peer_index,"logging",p1,len);
	return 1; // Rebuild
}

static int callback_chat_notifications(const int w)
{
	(void)w;
	t_peer[global_n].mute = !t_peer[global_n].mute;
	const int peer_index = getter_int(global_n,INT_MIN,-1,offsetof(struct peer_list,peer_index));
	char p1[21];
	const size_t len = (size_t)snprintf(p1,sizeof(p1),"%d",t_peer[global_n].mute);
	sql_setting(0,peer_index,"mute",p1,len);
	return 1; // Rebuild
}

static int callback_chat_block(const int w)
{
	(void)w;
	block_peer(global_n);
	return 1; // Rebuild
}

static int callback_chat_kill(const int w)
{
	(void)w;
	kill_code(global_n,NULL);
	go_back(2);
	return 0; // Do not rebuild
}

static int callback_chat_delete(const int w)
{
	(void)w;
	const int peer_index = getter_int(global_n,INT_MIN,-1,offsetof(struct peer_list,peer_index));
	takedown_onion(peer_index,1);
	go_back(2);
	return 0; // Do not rebuild
}

static int callback_chat_clear(const int w)
{
	(void)w;
	delete_log(global_n);
	return 1; // Rebuild
}

static int callback_generate_one(const int w)
{
	(void)w;
	int g = -1;
	if(generate_group_mode) // generate invite-only
		g = group_generate(1,generate_input);
	else // generate sing
		generate_onion(ENUM_OWNER_SING,NULL,generate_input);
	torx_free((void*)&generate_input);
	generate_input_cursor = 0;
	if(g > -1)
	{ // Immediate result available
		const size_t len = strlen(text_successfully_created_group);
		generate_output = torx_insecure_malloc(len+1);
		snprintf(generate_output,len+1,"%s",text_successfully_created_group);
		return 1; // Rebuild
	}
	return 0; // Do not rebuild
}

static int callback_generate_two(const int w)
{
	(void)w;
	int g = -1;
	if(generate_group_mode) // generate public group
		g = group_generate(0,generate_input);
	else // generate mult
		generate_onion(ENUM_OWNER_MULT,NULL,generate_input);
	torx_free((void*)&generate_input);
	generate_input_cursor = 0;
	if(g > -1)
	{ // Immediate result available
		unsigned char id[GROUP_ID_SIZE]; // zero'd
		pthread_rwlock_rdlock(&mutex_expand_group); // 🟧
		memcpy(id,group[g].id,sizeof(id));
		pthread_rwlock_unlock(&mutex_expand_group); // 🟩
		generate_output = b64_encode(id,GROUP_ID_SIZE);
		sodium_memzero(id,sizeof(id));
		return 1; // Rebuild
	}
	return 0; // Do not rebuild
}

static int callback_attempt_connect(const int w)
{
	(void)w;
	int ret = -1;
	if(generate_group_mode)
	{
		unsigned char id[GROUP_ID_SIZE]; // zero'd
		if(b64_decode(id,sizeof(id),add_id) == GROUP_ID_SIZE)
			ret = group_join(-1,id,add_identifier,NULL,NULL);
		sodium_memzero(id,sizeof(id));
	}
	else
		ret = peer_save(add_id,add_identifier);
	if(ret > -1)
	{ // Successfully saved a group or peer
		torx_free((void*)&add_identifier);
		torx_free((void*)&add_id);
		add_identifier_cursor = 0;
		add_id_cursor = 0;
		return 1; // Rebuild
	}
	return 0; // Do not rebuild
}

static int callback_change_password_attempt(const int w)
{
	(void)w;
	change_password_start(password_old ? password_old : "",password_new ? password_new : "",password_verify ? password_verify : "");
	return 0; // Do not rebuild
}

static int scrollable(WINDOW *win,size_t *fyp,size_t *fxp,const size_t item_to_draw)
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
			print_wrap(win, fyp, fxp, subtract_size(screen_cols,*fxp*2), text_set_select_language, strlen(text_set_select_language));
			*fyp += 1,*fxp = align_right(torx_utf8len(selected));
			widget_button(win,fyp,fxp,printable_width,callback_language,selected);
		}
		else if(item_to_draw == 1)
		{ // Select color scheme
			if(global_theme == LIGHT_THEME)
				selected = text_light;
			else
				selected = text_dark;
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, subtract_size(screen_cols,*fxp*2), text_set_select_theme, strlen(text_set_select_theme));
			*fyp += 1,*fxp = align_right(torx_utf8len(selected));
			widget_button(win,fyp,fxp,printable_width,callback_theme,selected);
		}
		else if(item_to_draw == 2)
		{ // TorX-ID (<=52 char) or OnionID (56 char with checksum)
			if(threadsafe_read_uint8(&mutex_global_variable,&shorten_torxids))
				selected = text_generate_torxid;
			else
				selected = text_generate_onionid;
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_onionid_or_torxid, strlen(text_set_onionid_or_torxid));
			*fyp += 1,*fxp = align_right(torx_utf8len(selected));
			widget_button(win,fyp,fxp,printable_width,callback_onionid_or_torxid,selected);
		}
		else if(item_to_draw == 3)
		{ // Message Logging (Global Default)
			if(threadsafe_read_uint8(&mutex_global_variable,&global_log_messages))
				selected = text_enable;
			else
				selected = text_disable;
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_global_log, strlen(text_set_global_log));
			*fyp += 1,*fxp = align_right(torx_utf8len(selected));
			widget_button(win,fyp,fxp,printable_width,callback_global_log_messages,selected);
		}
		else if(item_to_draw == 4)
		{ // Select Tor binary location (effective immediately)
			const size_t len = (size_t)snprintf(label_text,sizeof(label_text),"%s %s %s",text_select,text_tor,text_binary_location);
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, label_text, len);
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_tor_location));
			widget_text(win,fyp,fxp,1,printable_width,callback_tor_location,WIDGET_INPUT_SINGLE_LINE,&tmp_tor_location,&tmp_tor_location_pos);
			sodium_memzero(label_text,len);
		}
		else if(item_to_draw == 5)
		{ // Select Snowflake binary location (effective immediately)
			const size_t len = (size_t)snprintf(label_text,sizeof(label_text),"%s %s %s",text_select,text_snowflake,text_binary_location);
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, label_text, len);
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_snowflake_location));
			widget_text(win,fyp,fxp,1,printable_width,callback_snowflake_location,WIDGET_INPUT_SINGLE_LINE,&tmp_snowflake_location,&tmp_snowflake_location_pos);
			sodium_memzero(label_text,len);
		}
		else if(item_to_draw == 6)
		{ // Select Lyrebird binary location (effective immediately)
			const size_t len = (size_t)snprintf(label_text,sizeof(label_text),"%s %s %s",text_select,text_lyrebird,text_binary_location);
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, label_text, len);
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_lyrebird_location));
			widget_text(win,fyp,fxp,1,printable_width,callback_lyrebird_location,WIDGET_INPUT_SINGLE_LINE,&tmp_lyrebird_location,&tmp_lyrebird_location_pos);
			sodium_memzero(label_text,len);
		}
		else if(item_to_draw == 7)
		{ // Select Conjure binary location (effective immediately)
			const size_t len = (size_t)snprintf(label_text,sizeof(label_text),"%s %s %s",text_select,text_conjure,text_binary_location);
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, label_text, len);
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_conjure_location));
			widget_text(win,fyp,fxp,1,printable_width,callback_conjure_location,WIDGET_INPUT_SINGLE_LINE,&tmp_conjure_location,&tmp_conjure_location_pos);
			sodium_memzero(label_text,len);
		}
		else if(item_to_draw == 8)
		{ // Maximum CPU threads for TorX-ID generation
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_cpu, strlen(text_set_cpu));
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_threads_max));
			widget_text(win,fyp,fxp,1,printable_width,callback_threads,WIDGET_INPUT_NUMERICAL,&tmp_threads_max,&tmp_threads_max_pos);
		}
		else if(item_to_draw == 9)
		{ // Minimum Suffix Length for TorX-ID generation
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_suffix, strlen(text_set_suffix));
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_suffix_length));
			widget_text(win,fyp,fxp,1,printable_width,callback_suffix,WIDGET_INPUT_NUMERICAL,&tmp_suffix_length,&tmp_suffix_length_pos);
		}
		else if(item_to_draw == 10)
		{ // Single-Use TorX-ID expiration time (days)
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_validity_sing, strlen(text_set_validity_sing));
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_sing_expiration_days));
			widget_text(win,fyp,fxp,1,printable_width,callback_sing_days,WIDGET_INPUT_NUMERICAL,&tmp_sing_expiration_days,&tmp_sing_expiration_days_pos);
		}
		else if(item_to_draw == 11)
		{ // Multiple-Use TorX-ID expiration time (days)
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_validity_mult, strlen(text_set_validity_mult));
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_mult_expiration_days));
			widget_text(win,fyp,fxp,1,printable_width,callback_mult_dats,WIDGET_INPUT_NUMERICAL,&tmp_mult_expiration_days,&tmp_mult_expiration_days_pos);
		}
		else if(item_to_draw == 12)
		{ // Automatically Accept Incoming Mult Requests
			if(threadsafe_read_uint8(&mutex_global_variable,&auto_accept_mult))
				selected = text_enable;
			else
				selected = text_disable;
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_auto_mult, strlen(text_set_auto_mult));
			*fyp += 1,*fxp = align_right(torx_utf8len(selected));
			widget_button(win,fyp,fxp,printable_width,callback_auto_mult,selected);
		}
		else if(item_to_draw == 13)
		{ // Tor SOCKS5 Port
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_tor_port_socks, strlen(text_set_tor_port_socks));
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_tor_socks_port));
			widget_text(win,fyp,fxp,1,printable_width,callback_socks_port,WIDGET_INPUT_NUMERICAL,&tmp_tor_socks_port,&tmp_tor_socks_port_pos);
		}
		else if(item_to_draw == 14)
		{ // Tor Control Port
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_tor_port_ctrl, strlen(text_set_tor_port_ctrl));
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_tor_ctrl_port));
			widget_text(win,fyp,fxp,1,printable_width,callback_ctrl_port,WIDGET_INPUT_NUMERICAL,&tmp_tor_ctrl_port,&tmp_tor_ctrl_port_pos);
		}
		else if(item_to_draw == 15)
		{ // Tor Control Password
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_set_tor_password, strlen(text_set_tor_password));
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_control_password_clear));
			widget_text(win,fyp,fxp,1,printable_width,callback_ctrl_pass,WIDGET_INPUT_SINGLE_LINE,&tmp_control_password_clear,&tmp_control_password_clear_pos);
			return 0; // Printed last (XXX IMPORTANT: sets more_to_print XXX)
		}
		else
			return 0; // Printed nothing
	}
	else if(win == window_requests_popover || win == window_ids_popover)
	{ // utilizes treeview_n
		if(item_to_draw == 0)
		{ // Rename text_entry
			*fyp += 2, *fxp = 2;
			print_wrap(win, fyp, fxp, printable_width, text_identifier, strlen(text_identifier));
			*fyp += 1,*fxp = align_right(torx_utf8len(tmp_rename));
			widget_text(win,fyp,fxp,1,printable_width,callback_rename,WIDGET_INPUT_SINGLE_LINE,&tmp_rename,&tmp_rename_pos);
		}
		else if(item_to_draw == 1)
		{
			if(window_ids_popover)
			{ // Active checkbox
				const uint8_t status = getter_uint8(treeview_n,INT_MIN,-1,offsetof(struct peer_list,status));
				*fyp += 2, *fxp = align_right(torx_utf8len(text_active) + 4);
				widget_checkbox(win,fyp,fxp,printable_width,callback_toggle_block,1,text_active,status == ENUM_STATUS_FRIEND ? 1 : 0);
			}
			else if(window_requests_popover && !outgoing_mode)
			{ // Accept Incoming request button
				*fyp += 1, *fxp = align_right(torx_utf8len(text_accept));
				widget_button(win,fyp,fxp,printable_width,callback_peer_accept,text_accept);
			}
		}
		else if(item_to_draw == 2)
		{
			char label[256];
			snprintf(label,sizeof(label),"[ %s ]",window_requests_popover && !outgoing_mode ? text_reject : text_delete);
			*fyp += 2, *fxp = align_right(torx_utf8len(label));
			widget_button(win,fyp,fxp,printable_width,callback_peer_delete,label);
			sodium_memzero(label,sizeof(label));
			return 0; // Printed last (XXX IMPORTANT: sets more_to_print XXX)
		}
		else
			return 0; // Printed nothing
	}
	else if(win == window_login)
	{
		if(item_to_draw == 0)
		{
			*fyp += 2, *fxp = 2;
			print_wrap(win,fyp,fxp,subtract_size(screen_cols,*fxp*2),text_password,strlen(text_password));
			*fyp += 1, *fxp = 4;
			widget_text(win,fyp,fxp,subtract_size(screen_rows,*fyp),subtract_size(screen_cols,*fxp*2),callback_password,WIDGET_PASSWORD,&password,&pw_cursor);
		}
		else if(item_to_draw == 1)
		{
			*fyp += 2,*fxp = 4;
			widget_checkbox(win,fyp,fxp,subtract_size(screen_cols,*fxp*2),callback_pw_show,1,text_show_password,pw_show);
		}
		else if(item_to_draw == 2)
		{
			*fyp += 1, *fxp = 4;
			widget_checkbox(win,fyp,fxp,subtract_size(screen_cols,*fxp*2),callback_censored_region,1,text_censored_region,threadsafe_read_uint8(&mutex_global_variable,&censored_region));
			return 0; // Printed last (XXX IMPORTANT: sets more_to_print XXX)
		}
		else
			return 0; // Printed nothing
	}
	else if(win == window_change_password)
	{
		if(item_to_draw == 0)
		{
			*fyp += 2, *fxp = align_center(torx_utf8len(text_old_password));
			print_wrap(win,fyp,fxp,printable_width,text_old_password,strlen(text_old_password));
			*fyp += 1, *fxp = align_center(pw_show ? torx_utf8len(password_old) : torx_utf8count(password_old));
			widget_text(win,fyp,fxp,subtract_size(screen_rows,*fyp),printable_width,NULL,WIDGET_PASSWORD,&password_old,&pw_old_cursor);
		}
		else if(item_to_draw == 1)
		{
			*fyp += 2, *fxp = align_center(torx_utf8len(text_new_password));
			print_wrap(win,fyp,fxp,printable_width,text_new_password,strlen(text_new_password));
			*fyp += 1, *fxp = align_center(pw_show ? torx_utf8len(password_new) : torx_utf8count(password_new));
			widget_text(win,fyp,fxp,subtract_size(screen_rows,*fyp),printable_width,NULL,WIDGET_PASSWORD,&password_new,&pw_new_cursor);
		}
		else if(item_to_draw == 2)
		{
			*fyp += 2, *fxp = align_center(torx_utf8len(text_new_password_again));
			print_wrap(win,fyp,fxp,printable_width,text_new_password_again,strlen(text_new_password_again));
			*fyp += 1, *fxp = align_center(pw_show ? torx_utf8len(password_verify) : torx_utf8count(password_verify));
			widget_text(win,fyp,fxp,subtract_size(screen_rows,*fyp),printable_width,NULL,WIDGET_PASSWORD,&password_verify,&pw_verify_cursor);
		}
		else if(item_to_draw == 3)
		{
			char label[256];
			snprintf(label,sizeof(label),"[ %s ]",text_change_password);
			const size_t utf8len = torx_utf8len(label);
			*fyp += 2,*fxp = align_center(utf8len);
			widget_button(win,fyp,fxp,utf8len,callback_change_password_attempt,label);
			sodium_memzero(label,sizeof(label));
		}
		else if(item_to_draw == 4)
		{
			*fyp += 1,*fxp = align_center(4 + torx_utf8len(text_show_password)); // 4 + is for [ ]
			widget_checkbox(win,fyp,fxp,printable_width,callback_pw_show,1,text_show_password,pw_show);
			return 0; // Printed last (XXX IMPORTANT: sets more_to_print XXX)
		}
		else
			return 0; // Printed nothing
	}
	else if(win == window_generate)
	{
		if(item_to_draw == 0)
		{
			wattron(win,A_BOLD); // bold on
			*fyp += 2, *fxp = align_center(torx_utf8len(generate_group_mode ? text_add_group_by : text_add_peer_by));
			print_wrap(win,fyp,fxp,printable_width,generate_group_mode ? text_add_group_by : text_add_peer_by,strlen(generate_group_mode ? text_add_group_by : text_add_peer_by));
			wattroff(win,A_BOLD); // bold off
			*fyp += 2, *fxp = align_center(torx_utf8len(generate_group_mode ? text_placeholder_add_group_identifier : text_placeholder_add_identifier));
			print_wrap(win,fyp,fxp,printable_width,generate_group_mode ? text_placeholder_add_group_identifier : text_placeholder_add_identifier,strlen(generate_group_mode ? text_placeholder_add_group_identifier : text_placeholder_add_identifier));
			*fyp += 1, *fxp = align_center(torx_utf8len(add_identifier));
			widget_text(win,fyp,fxp,subtract_size(screen_rows,*fyp),printable_width,NULL,WIDGET_INPUT_SINGLE_LINE,&add_identifier,&add_identifier_cursor);
		}
		else if(item_to_draw == 1)
		{
			*fyp += 2, *fxp = align_center(torx_utf8len(generate_group_mode ? text_placeholder_add_group_id : text_placeholder_add_onion));
			print_wrap(win,fyp,fxp,printable_width,generate_group_mode ? text_placeholder_add_group_id : text_placeholder_add_onion,strlen(generate_group_mode ? text_placeholder_add_group_id : text_placeholder_add_onion));
			*fyp += 1, *fxp = align_center(torx_utf8len(add_id));
			widget_text(win,fyp,fxp,subtract_size(screen_rows,*fyp),printable_width,NULL,WIDGET_INPUT_SINGLE_LINE,&add_id,&add_id_cursor);
		}
		else if(item_to_draw == 2)
		{
			char label[256];
			snprintf(label,sizeof(label),"[ %s ]",generate_group_mode ? text_button_join : text_button_add);
			const size_t utf8len = torx_utf8len(label);
			*fyp += 2,*fxp = align_center(utf8len);
			widget_button(win,fyp,fxp,utf8len,callback_attempt_connect,label);
			sodium_memzero(label,sizeof(label));
		}
		else if(item_to_draw == 3)
		{
			wattron(win,A_BOLD); // bold on
			*fyp += 2, *fxp = align_center(torx_utf8len(generate_group_mode ? text_generate_group_for : text_generate_for));
			print_wrap(win,fyp,fxp,printable_width,generate_group_mode ? text_generate_group_for : text_generate_for,strlen(generate_group_mode ? text_generate_group_for : text_generate_for));
			wattroff(win,A_BOLD); // bold off
			*fyp += 2, *fxp = align_center(torx_utf8len(generate_group_mode ? text_placeholder_add_group_identifier : text_placeholder_add_identifier));
			print_wrap(win,fyp,fxp,printable_width,generate_group_mode ? text_placeholder_add_group_identifier : text_placeholder_add_identifier,strlen(generate_group_mode ? text_placeholder_add_group_identifier : text_placeholder_add_identifier));
			*fyp += 1, *fxp = align_center(torx_utf8len(generate_input));
			widget_text(win,fyp,fxp,subtract_size(screen_rows,*fyp),printable_width,NULL,WIDGET_INPUT_SINGLE_LINE,&generate_input,&generate_input_cursor);
		}
		else if(item_to_draw == 4)
		{
			char label[256];
			snprintf(label,sizeof(label),"[ %s ]",generate_group_mode ? text_button_generate_invite : text_button_sing);
			const size_t utf8len = torx_utf8len(label);
			*fyp += 2,*fxp = align_center(utf8len);
			widget_button(win,fyp,fxp,utf8len,callback_generate_one,label);
			sodium_memzero(label,sizeof(label));
		}
		else if(item_to_draw == 5)
		{
			char label[256];
			snprintf(label,sizeof(label),"[ %s ]",generate_group_mode ? text_button_generate_public : text_button_mult);
			const size_t utf8len = torx_utf8len(label);
			*fyp += 1,*fxp = align_center(utf8len);
			widget_button(win,fyp,fxp,utf8len,callback_generate_two,label);
			if(generate_output)
			{
				*fyp += 2, *fxp = align_center(torx_utf8len(generate_output));
				print_wrap(win,fyp,fxp,printable_width,generate_output,torx_allocation_len(generate_output) - 1);
			}
			sodium_memzero(label,sizeof(label));
			return 0; // Printed last (XXX IMPORTANT: sets more_to_print XXX)
		}
		else
			return 0; // Printed nothing
	}
	else if(win == window_chat_settings)
	{
		if(item_to_draw == 0)
		{
			const char *selected;
			const int8_t log_messages = getter_int8(global_n,INT_MIN,-1,offsetof(struct peer_list,log_messages));
			if(log_messages == -1)
				selected = text_tooltip_logging_disabled;
			else if(log_messages == 0)
			{
				if(threadsafe_read_uint8(&mutex_global_variable,&global_log_messages) == 1)
					selected = text_tooltip_logging_global_on;
				else
					selected = text_tooltip_logging_global_off;
			}
			else if(log_messages == 1)
				selected = text_tooltip_logging_enabled;
			*fyp += 2, *fxp = 2;
			widget_button(win,fyp,fxp,printable_width,callback_chat_logging,selected);
		}
		else if(item_to_draw == 1)
		{
			const char *selected;
			if(t_peer[global_n].mute == 1)
				selected = text_tooltip_notifications_off;
			else
				selected = text_tooltip_notifications_on;
			*fyp += 2, *fxp = 2;
			widget_button(win,fyp,fxp,printable_width,callback_chat_notifications,selected);
		}
		else if(item_to_draw == 2)
		{
			const char *selected;
			const uint8_t status = getter_uint8(global_n,INT_MIN,-1,offsetof(struct peer_list,status));
			if(status == ENUM_STATUS_BLOCKED)
				selected = text_tooltip_blocked_on;
			else
				selected = text_tooltip_blocked_off;
			*fyp += 2, *fxp = 2;
			widget_button(win,fyp,fxp,printable_width,callback_chat_block,selected);
		}
		else if(item_to_draw == 3)
		{
			*fyp += 2, *fxp = 2;
			widget_button(win,fyp,fxp,printable_width,callback_chat_kill,text_tooltip_button_kill);
		}
		else if(item_to_draw == 4)
		{
			*fyp += 2, *fxp = 2;
			widget_button(win,fyp,fxp,printable_width,callback_chat_delete,text_tooltip_button_delete);
		}
		else if(item_to_draw == 5)
		{
			*fyp += 2, *fxp = 2;
			widget_button(win,fyp,fxp,printable_width,callback_chat_clear,text_tooltip_button_delete_log);
			return 0; // Printed last (XXX IMPORTANT: sets more_to_print XXX)
		}
		else
			return 0; // Printed nothing
	}
	else
		return 0; // Printed nothing
	return 1; // Printed something complete
}

static void draw_requests_popover(void)
{ // Requests Route Popover, utilizes treeview_n
	WINDOW *win = window_prepare(&draw_requests_popover,&window_requests_popover,&focus_popover); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_edit,strlen(text_edit));
	wattroff(win,A_BOLD); // bold off

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_popover,&popover_scroll_offset);

	widget_draw_cursor(win); // XXX Must do last
}

static void draw_ids_popover(void)
{ // ID Route Popover, utilizes treeview_n
	WINDOW *win = window_prepare(&draw_ids_popover,&window_ids_popover,&focus_popover); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_edit,strlen(text_edit));
	wattroff(win,A_BOLD); // bold off

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_popover,&popover_scroll_offset);

	widget_draw_cursor(win); // XXX Must do last
}

static int callback_popover(const int w)
{ // utilizes treeview_n
	(void)w;
	tmp_rename = getter_string(treeview_n,INT_MIN,-1,offsetof(struct peer_list,peernick));
	tmp_rename_pos = tmp_rename ? torx_allocation_len(tmp_rename) - 1 : 0;
	if(window_ids)
		draw_ids_popover();
	else if(window_requests)
		draw_requests_popover();
	return 0; // Do not rebuild
}

static int callback_pm(const int w)
{ // utilizes treeview_n
	(void)w;
	if(t_peer[global_n].edit_n > -1 && t_peer[global_n].edit_i > INT_MIN)
	{
		t_peer[global_n].edit_n = -1;
		t_peer[global_n].edit_i = INT_MIN;
	}
	t_peer[global_n].pm_n = treeview_n;
	go_back(2);
	return 0; // Do not rebuild
}

static int callback_invite(const int w)
{ // utilizes treeview_n
	(void)w;
	const int g = global_group;
	const uint32_t g_peercount = getter_group_uint32(g,offsetof(struct group_list,peercount));
	const uint8_t g_invite_required = getter_group_uint8(g,offsetof(struct group_list,invite_required));
	if(g_invite_required == 1 && g_peercount == 0)
		message_send(treeview_n,ENUM_PROTOCOL_GROUP_OFFER_FIRST,itovp(global_group),GROUP_OFFER_FIRST_LEN);
	else
		message_send(treeview_n,ENUM_PROTOCOL_GROUP_OFFER,itovp(global_group),GROUP_OFFER_LEN);
	go_back(2);
	return 0; // Do not rebuild
}

static int callback_peer(const int w)
{
	(void)w;
	global_n = selected_n;
	const uint8_t owner = getter_uint8(global_n,INT_MIN,-1,offsetof(struct peer_list,owner));
	if(owner == ENUM_OWNER_GROUP_CTRL)
		totalUnreadGroup -= t_peer[global_n].unread;
	else
		totalUnreadPeer -= t_peer[global_n].unread;
	t_peer[global_n].unread = 0;
	chat_scroll_lines = 0;
	draw_chat();
	return 0; // Do not rebuild
}

static inline wint_t get_online_char(const uint8_t sendfd_connected,const uint8_t recvfd_connected)
{ // we use wint_t instead of wchar_t so we can use %lc
	if(sendfd_connected && recvfd_connected)
		return L'●';
	else if(sendfd_connected)
		return L'◓';
	else if(recvfd_connected)
		return L'◒';
	else
		return L'○';
}

static void draw_scrollable(WINDOW *win,size_t *fyp,size_t *fxp,int *focus,size_t *scroll_offset)
{
	if(!win || !fyp || !fxp || !focus || !scroll_offset)
		return;
	size_t iter = *scroll_offset;
	current_scroll_offset = scroll_offset;
	widgets_existing_before_scrollable = torx_allocation_len(widget) / sizeof(struct widget);
	if(win == window_group_invite || win == window_group_peerlist)
	{ // ui_populate_peer_popover / ui_populate_group_peerlist_popover
		int len;
		int *array = refined_list(&len,window_group_invite ? ENUM_OWNER_CTRL : ENUM_OWNER_GROUP_PEER,window_group_invite ? ENUM_STATUS_FRIEND : global_group,search);
		char label[printable_width + 1];
		while((int)iter < len && *fyp < subtract_size(screen_rows,2))
		{
			*fyp += 1,*fxp = 2;
			const int n = array[iter++];
			const uint8_t sendfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,sendfd_connected));
			const uint8_t recvfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,recvfd_connected));
			const wint_t online_char = get_online_char(sendfd_connected,recvfd_connected);
			char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
			snprintf(label,sizeof(label),"%lc %s",online_char,peernick);
			torx_free((void*)&peernick);
			if(widget_button(win,fyp,fxp,printable_width+1,window_group_invite ? callback_invite : callback_pm,label) == *focus) // the +1 is to prevent wrapping
				treeview_n = n;
		}
		if((int)iter < len)
			more_to_print = 1;
		torx_free((void*)&array);
		sodium_memzero(label,sizeof(label));
	}
	else if(win == window_contacts)
	{
		int len;
		int *array;
		if(groups_mode == ENUM_SHOW_PEER)
			array = refined_list(&len,ENUM_OWNER_CTRL,ENUM_STATUS_FRIEND,search);
		else if(groups_mode == ENUM_SHOW_GROUP)
			array = refined_list(&len,ENUM_OWNER_GROUP_CTRL,ENUM_STATUS_FRIEND,search);
		else if(groups_mode == ENUM_SHOW_BLOCK)
			array = refined_list(&len,ENUM_OWNER_CTRL,ENUM_STATUS_BLOCKED,search);
		if(len)
		{
			char label[printable_width + 1];
			while((int)iter < len && *fyp < subtract_size(screen_rows,2))
			{
				*fyp += 1,*fxp = 2;
				const int n = array[iter++];
				uint8_t sendfd_connected;
				uint8_t recvfd_connected;
				if(groups_mode == ENUM_SHOW_GROUP)
					sendfd_connected = recvfd_connected = 1; // Always show groups as green
				else
				{
					sendfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,sendfd_connected));
					recvfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,recvfd_connected));
				}
				const wint_t online_char = get_online_char(sendfd_connected,recvfd_connected);
				char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
				if(t_peer[n].unread > 0)
					snprintf(label,sizeof(label),"%lc(%lu) %s",online_char,t_peer[n].unread,peernick);
				else
					snprintf(label,sizeof(label),"%lc %s",online_char,peernick);
				torx_free((void*)&peernick);
				if(sendfd_connected || recvfd_connected)
					wattron(win,A_BOLD); // bold on
				const int w = widget_button(win,fyp,fxp,printable_width,callback_peer,label);
				if(*focus == w)
					selected_n = n;
				if(sendfd_connected || recvfd_connected)
					wattroff(win,A_BOLD); // bold off
			}
			if((int)iter < len)
				more_to_print = 1;
			torx_free((void*)&array);
			sodium_memzero(label,sizeof(label));
		}
	}
	else if(win == window_ids || win == window_requests)
	{
		int len;
		int *array;
		if(win == window_ids)
			array = refined_list(&len,single_mode ? ENUM_OWNER_SING : ENUM_OWNER_MULT,ENUM_STATUS_PENDING,search);
		else // if(win == window_requests)
			array = refined_list(&len,outgoing_mode ? ENUM_OWNER_PEER : ENUM_OWNER_CTRL,ENUM_STATUS_PENDING,search);
		if(len)
		{
			const uint8_t shorten_torxids_local = threadsafe_read_uint8(&mutex_global_variable,&shorten_torxids);
			char label[printable_width + 1];
			char onion[56+1]; // zero'd
			while((int)iter < len && *fyp < subtract_size(screen_rows,2))
			{
				*fyp += 1,*fxp = 2;
				const int n = array[iter++];
				if(shorten_torxids_local)
					getter_array(&onion,52+1,n,INT_MIN,-1,offsetof(struct peer_list,torxid));
				else
					getter_array(&onion,sizeof(onion),n,INT_MIN,-1,offsetof(struct peer_list,onion));
				char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
				if(win == window_ids)
				{
					const uint8_t status = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,status));
					snprintf(label,sizeof(label),"%s %s %s",status == ENUM_STATUS_FRIEND ? "[x]" : "[ ]",peernick,onion);
				}
				else // if(win == window_requests)
					snprintf(label,sizeof(label),"%s %s",peernick,onion);
				if(widget_button(win,fyp,fxp,printable_width+1,callback_popover,label) == *focus) // the +1 is to prevent wrapping
					treeview_n = n;
				torx_free((void*)&peernick);
			}
			if((int)iter < len)
				more_to_print = 1;
			sodium_memzero(onion,sizeof(onion));
			sodium_memzero(label,sizeof(label));
			torx_free((void*)&array);
		}
	}
	else
	{
		int ret;
		while((ret = scrollable(win,fyp,fxp,iter)) && *fyp < subtract_size(screen_rows,2))
			iter++; // Draw widgets until there is no space left on the screen
		if(ret) // cut off or didn't print all
			more_to_print = 1;
	}
	if(*fyp > subtract_size(screen_rows,2)) // Draw border again (if we ran over it with scrollable)
	{ // DO NOT MODIFY
		if(torx_allocation_len(widget) / sizeof(struct widget) - widgets_existing_before_scrollable > 1 && *focus + 1 == (int)(torx_allocation_len(widget) / sizeof(struct widget)))
		{ // VERY IMPORTANT. Do not modify! When (scrollable widgets > 1 && current focus is on a partially printed widget)
			*focus -= 1; // NOT the same as --
			*scroll_offset += 1; // NOT the same as ++
			redraw();
			return; // safety
		}
		else
			for(size_t x = 1; x + 1 < screen_cols; x++)
				mvwaddch(win, (int)subtract_size(screen_rows,1), (int)x, ACS_HLINE);
	}
/*	else if(!more_to_print && *scroll_offset > 0 *fyp + 2 < subtract_size(screen_rows, 2))
	{ // Reverse of above: window grew; reveal a previously hidden widget at top. TODO this only works on single line widgets otherwise it segfaults, but the thought is nice. To make it work on multiline widgets would probably require efforts elsewhere and a lot of redrawing?
		if(*focus >= (int)widgets_existing_before_scrollable)
			*focus += 1;
		*scroll_offset -= 1;
		redraw();
		return;
	}*/
}

static int callback_torrc(const int w)
{
	(void)w;
	torx_free((void*)&torrc_content_local);
	torrc_pos = 0;
	pthread_rwlock_rdlock(&mutex_global_variable); // 🟧
	torrc_content_local = torx_copy(torrc_content);
	pthread_rwlock_unlock(&mutex_global_variable); // 🟩
	draw_torrc();
	return 0; // Do not rebuild
}

static int callback_change_password(const int w)
{
	(void)w;
	draw_change_password();
	return 0; // Do not rebuild
}

static void draw_settings(void)
{ // Settings Route. Be sure all of these things being set can sunsequently be read using ENUM_CUSTOM_SETTING.
	WINDOW *win = window_prepare(&draw_settings,&window_settings,&focus_settings); // XXX Must do first

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_settings,strlen(text_settings));
	wattroff(win,A_BOLD); // bold off

	char label[256];
	snprintf(label,sizeof(label),"[ %s ]",text_change_password);
	const size_t utf8len1 = torx_utf8len(label);
	fy = 0,fx = align_right(utf8len1);
	widget_button(win,&fy,&fx,utf8len1,callback_change_password,label);

	snprintf(label,sizeof(label),"[ %s ]",text_edit_torrc);
	const size_t utf8len2 = torx_utf8len(label);
	fy = 0,fx = align_right(utf8len1 + 1 + utf8len2);
	widget_button(win,&fy,&fx,utf8len2,callback_torrc,label);

	widget_next_has_default_focus(); // XXX Set default widget focus
	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_settings,&settings_scroll_offset);

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
	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static int callback_toggle_logs(const int w)
{
	(void)w;
	tor_log_mode = !tor_log_mode;
	return 1; // Rebuild
}

static int callback_debug_level(const int w)
{
	(void)w;
	if(tmp_debug_level)
		torx_debug_level((int8_t)atoi(tmp_debug_level));
	return 1; // Rebuild
}

static void draw_logs(void)
{ // Tor Log Route
	WINDOW *win = window_prepare(&draw_logs,&window_logs,&focus_logs); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,tor_log_mode ? text_tor_log : text_torx_log,strlen(tor_log_mode ? text_tor_log : text_torx_log));
	wattroff(win,A_BOLD); // bold off

	char label[256];
	snprintf(label,sizeof(label),"[ %s ]",tor_log_mode ? text_torx_log : text_tor_log);
	const size_t utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_toggle_logs,label);

	if(!tor_log_mode)
	{ // TorX logging level
		fy -= 1, fx = align_right(utf8len) - 1 - torx_utf8len(text_debug_level) - 2;
		print_nowrap(win,&fy,&fx,printable_width,text_debug_level,strlen(text_debug_level));
		fx = align_right(utf8len) - 2;
		widget_text(win,&fy,&fx,subtract_size(screen_rows,2),inner_width,callback_debug_level,WIDGET_INPUT_NUMERICAL,&tmp_debug_level,&tmp_debug_level_pos);
		fy += 1;
	}
	fx = align_center(inner_width);
	widget_text(win,&fy,&fx,subtract_size(screen_rows,2),inner_width,NULL,WIDGET_OUTPUT_MULTI_LINE,tor_log_mode ? &tor_log_buffer : &torx_log_buffer,tor_log_mode ? &tor_log_buffer_pos : &torx_log_buffer_pos);
	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static int callback_save_torrc(const int w)
{
	(void)w;
	char *torrc_errors = torrc_verify(torrc_content_local);
	if(torrc_errors == NULL)
		torrc_save(torrc_content_local);
	else
	{
		notify(text_override,torrc_errors); // TODO should give an option to override errors
		torx_free((void*)&torrc_errors);
	}
	return 0; // Do not rebuild
}

static void draw_torrc(void)
{ // Torrc Route
	WINDOW *win = window_prepare(&draw_torrc,&window_torrc,&focus_torrc); // XXX Must do first

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_edit_torrc,strlen(text_edit_torrc));
	wattroff(win,A_BOLD); // bold off

	char label[256];
	snprintf(label,sizeof(label),"[ %s ]",text_save);
	const size_t utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_save_torrc,label);

	fx = align_center(inner_width);
	widget_next_has_default_focus(); // XXX Set default widget focus
	widget_text(win,&fy,&fx,subtract_size(screen_rows,2),inner_width,NULL,WIDGET_INPUT_MULTI_LINE,&torrc_content_local,&torrc_pos);

	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static void draw_change_password(void)
{ // Change Password Route
	WINDOW *win = window_prepare(&draw_change_password,&window_change_password,&focus_change_password); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_change_password,strlen(text_change_password));
	wattroff(win,A_BOLD); // bold off

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_change_password,&change_password_scroll_offset);

	widget_draw_cursor(win); // XXX Must do last
}

static int callback_toggle_group(const int w)
{
	(void)w;
	torx_free((void*)&generate_input);
	torx_free((void*)&generate_output);
	torx_free((void*)&add_identifier);
	torx_free((void*)&add_id);
	generated_n = -1;
	generate_group_mode = !generate_group_mode;
	return 1; // Rebuild
}

static void draw_generate(void)
{ // Generate Route
	WINDOW *win = window_prepare(&draw_generate,&window_generate,&focus_generate); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,generate_group_mode ? text_group : text_add_generate,strlen(generate_group_mode ? text_group : text_add_generate));
	wattroff(win,A_BOLD); // bold off

	char label[256];
	snprintf(label,sizeof(label),"[ %s ]",generate_group_mode ? text_peer : text_group);
	const size_t utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_toggle_group,label);

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_generate,&generate_scroll_offset);

	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static int callback_emit_global_kill(const int w)
{
	(void)w;
	kill_code(-1,NULL);
	go_back(1);
	return 0; // Do not rebuild
}

static void draw_global_kill(void)
{ // Global Kill Route
	WINDOW *win = window_prepare(&draw_global_kill,&window_global_kill,&focus_global_kill); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_global_kill,strlen(text_global_kill));
	wattroff(win,A_BOLD); // bold off

	size_t label_len = strlen(text_global_kill_warning);
	fy += 2, fx = 2;
	print_wrap(win,&fy,&fx,printable_width,text_global_kill_warning,label_len);

	char label[256];
	snprintf(label,sizeof(label),"[ %s ]",text_emit_global_kill);
	const size_t utf8len = torx_utf8len(label);
	fy += 2,fx = align_center(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_emit_global_kill,label);

	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static int callback_group_invite(const int w)
{
	(void)w;
	draw_group_invite();
	return 0; // Do not rebuild
}

static int callback_group_peerlist(const int w)
{
	(void)w;
	draw_group_peerlist();
	return 0; // Do not rebuild
}

static void draw_chat_actions(void)
{ // Chat Actions Route (NOTE: global_n is still set). This should be nearly blank, as we have no gifs, file transfers, etc.
	WINDOW *win = window_prepare(&draw_chat_actions,&window_chat_actions,&focus_chat_actions); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_actions,strlen(text_actions));
	wattroff(win,A_BOLD); // bold off

	fy += 2, fx = 2;
	print_wrap(win, &fy, &fx, printable_width, text_identifier, strlen(text_identifier));
	fy += 1,fx = align_right(torx_utf8len(tmp_rename));
	widget_text(win,&fy,&fx,1,printable_width,callback_rename,WIDGET_INPUT_SINGLE_LINE,&tmp_rename,&tmp_rename_pos);

	const uint8_t owner = getter_uint8(global_n,INT_MIN,-1,offsetof(struct peer_list,owner));
	if(owner == ENUM_OWNER_GROUP_CTRL)
	{
		const int g = set_g(global_n,NULL);
		const uint8_t g_invite_required = getter_group_uint8(g,offsetof(struct group_list,invite_required));
		fy += 2, fx = align_right(torx_utf8len(text_current_members));
		widget_button(win,&fy,&fx,printable_width,callback_group_peerlist,text_current_members);
		if(g_invite_required)
		{
			fy += 2, fx = align_right(torx_utf8len(text_invite_friend));
			widget_button(win,&fy,&fx,printable_width,callback_group_invite,text_invite_friend);
		}
		else
		{ // Shows GroupID
			unsigned char id[GROUP_ID_SIZE]; // zero'd
			pthread_rwlock_rdlock(&mutex_expand_group); // 🟧
			memcpy(id,group[g].id,sizeof(id));
			pthread_rwlock_unlock(&mutex_expand_group); // 🟩
			char *group_id_encoded = b64_encode(id,GROUP_ID_SIZE);
			fy += 2, fx = 2;
			print_wrap(win, &fy, &fx, printable_width, text_groupid, strlen(text_groupid));
			fy += 2, fx = align_right(torx_utf8len(group_id_encoded));
			print_wrap(win, &fy, &fx, printable_width, group_id_encoded, torx_allocation_len(group_id_encoded)-1);
			sodium_memzero(id,sizeof(id));
			torx_free((void*)&group_id_encoded);
		}
	}

// TODO EMOJI MENU ???? NO. Could be complicated, require settings to save frequently saved emojis. Future feature, perhaps.

	widget_draw_cursor(win); // XXX Must do last
}

static void draw_chat_settings(void)
{ // Chat Settings Route (NOTE: global_n is still set). Be sure all of these things being set can sunsequently be read using ENUM_CUSTOM_SETTING.
	WINDOW *win = window_prepare(&draw_chat_settings,&window_chat_settings,&focus_chat_settings); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_settings,strlen(text_settings));
	wattroff(win,A_BOLD); // bold off

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_chat_settings,&chat_settings_scroll_offset);

	widget_draw_cursor(win); // XXX Must do last
}

static void draw_group_invite(void)
{ // Group Invite Route (NOTE: global_n is still set)
	WINDOW *win = window_prepare(&draw_group_invite,&window_group_invite,&focus_group_invite); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_invite_friend,strlen(text_invite_friend));
	wattroff(win,A_BOLD); // bold off

	if(search)
	{
		fy = 0,fx = align_right(torx_utf8len(search));
		print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx+2),search,strlen(search));
	}

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_group_invite,&group_invite_scroll_offset);

	widget_draw_cursor(win); // XXX Must do last
}

static void draw_group_peerlist(void)
{ // Group Peerlist Route (NOTE: global_n is still set)
	WINDOW *win = window_prepare(&draw_group_peerlist,&window_group_peerlist,&focus_group_peerlist); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_group_peers,strlen(text_group_peers));
	wattroff(win,A_BOLD); // bold off

	if(search)
	{
		fx = align_right(torx_utf8len(search));
		print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx+2),search,strlen(search));
	}

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_group_peerlist,&group_peerlist_scroll_offset);

	widget_draw_cursor(win); // XXX Must do last
}

static int callback_contacts_groups(const int w)
{
	(void)w;
	if(groups_mode == ENUM_SHOW_BLOCK) // last
		groups_mode = ENUM_SHOW_PEER; // first
	else
		groups_mode++;
	contacts_scroll_offset = 0; // Necessary
	return 1; // Rebuild
}

static char *torx_itoa(const size_t value)
{
	const size_t length = (size_t)snprintf(NULL, 0, "%lu", value);
	char *allocation = torx_insecure_malloc(length + 1);
	snprintf(allocation,length + 1,"%lu",value);
	return allocation;
}

static int callback_settings(const int w)
{
	(void)w;
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
	tmp_threads_max_pos = torx_allocation_len(tmp_threads_max) - 1;
	tmp_suffix_length_pos = torx_allocation_len(tmp_suffix_length) - 1;
	tmp_sing_expiration_days_pos = torx_allocation_len(tmp_sing_expiration_days) - 1;
	tmp_mult_expiration_days_pos = torx_allocation_len(tmp_mult_expiration_days) - 1;
	tmp_auto_accept_mult_pos = torx_allocation_len(tmp_auto_accept_mult) - 1;
	tmp_tor_socks_port_pos = torx_allocation_len(tmp_tor_socks_port) - 1;
	tmp_tor_ctrl_port_pos = torx_allocation_len(tmp_tor_ctrl_port) - 1;
	tmp_control_password_clear_pos = tmp_control_password_clear ? torx_allocation_len(tmp_control_password_clear) - 1 : 0;

	draw_settings();
	return 0; // Do not rebuild
}

static int callback_toggle_requests(const int w)
{
	(void)w;
	outgoing_mode = !outgoing_mode;
	return 1; // Rebuild
}

static void draw_requests(void)
{ // Requests Route
	WINDOW *win = window_prepare(&draw_requests,&window_requests,&focus_requests); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,outgoing_mode ? text_outgoing : text_incoming,strlen(outgoing_mode ? text_outgoing : text_incoming));
	wattroff(win,A_BOLD); // bold off

	char label[256];
	snprintf(label,sizeof(label),"[ %s ]",outgoing_mode ? text_incoming : text_outgoing);
	size_t utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_toggle_requests,label);

	if(search)
	{
		fy = 0,fx = align_right(torx_utf8len(search) + 1 + utf8len);
		print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx+2),search,strlen(search));
	}

	const size_t label_len = (size_t)snprintf(label,sizeof(label),"%s %s",text_identifier, threadsafe_read_uint8(&mutex_global_variable,&shorten_torxids) ? text_torxid : text_onionid);
	fx = 2;
	print_wrap(win,&fy,&fx,printable_width,label,label_len);

	draw_scrollable(win,&fy,&fx,&focus_requests,&requests_scroll_offset);

	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static int callback_toggle_ids(const int w)
{
	(void)w;
	single_mode = !single_mode;
	return 1; // Rebuild
}

static void draw_ids(void)
{ // IDs Route
	WINDOW *win = window_prepare(&draw_ids,&window_ids,&focus_ids); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,single_mode ? text_active_sing : text_active_mult,strlen(single_mode ? text_active_sing : text_active_mult));
	wattroff(win,A_BOLD); // bold off

	char label[256];
	snprintf(label,sizeof(label),"[ %s ]",single_mode ? text_active_mult : text_active_sing);
	size_t utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_toggle_ids,label);

	if(search)
	{
		fy = 0,fx = align_right(torx_utf8len(search) + 1 + utf8len);
		print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx+2),search,strlen(search));
	}

	const size_t label_len = (size_t)snprintf(label,sizeof(label),"%s %s %s",text_active,text_identifier, threadsafe_read_uint8(&mutex_global_variable,&shorten_torxids) ? text_torxid : text_onionid);
	fx = 2;
	print_wrap(win,&fy,&fx,printable_width,label,label_len);

	draw_scrollable(win,&fy,&fx,&focus_ids,&ids_scroll_offset);

	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static int callback_logs(const int w)
{
	(void)w;
	tmp_debug_level = torx_itoa((size_t)torx_debug_level(-1));
	tmp_debug_level_pos = tmp_debug_level ? torx_allocation_len(tmp_debug_level) - 1 : 0;
	draw_logs();
	return 0; // Do not rebuild
}

static int callback_generate(const int w)
{
	(void)w;
	draw_generate();
	return 0; // Do not rebuild
}

static int callback_global_kill(const int w)
{
	(void)w;
	draw_global_kill();
	return 0; // Do not rebuild
}

static int callback_chat_actions(const int w)
{
	(void)w;
	tmp_rename = getter_string(global_n,INT_MIN,-1,offsetof(struct peer_list,peernick));
	tmp_rename_pos = tmp_rename ? torx_allocation_len(tmp_rename) - 1 : 0;
	draw_chat_actions();
	return 0; // Do not rebuild
}

static int callback_chat_settings(const int w)
{
	(void)w;
	draw_chat_settings();
	return 0; // Do not rebuild
}

static int callback_requests(const int w)
{
	(void)w;
	draw_requests();
	return 0; // Do not rebuild
}

static int callback_ids(const int w)
{
	(void)w;
	draw_ids();
	return 0; // Do not rebuild
}

static void draw_contacts(void)
{ // Contact List Route
	WINDOW *win = window_prepare(&draw_contacts,&window_contacts,&focus_contacts); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	size_t fy = 0,fx = 2;
	wattron(win,A_BOLD); // bold on
	if(groups_mode == ENUM_SHOW_PEER)
		print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx*2),text_peer,strlen(text_peer));
	else if(groups_mode == ENUM_SHOW_GROUP)
		print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx*2),text_group,strlen(text_group));
	else if(groups_mode == ENUM_SHOW_BLOCK)
		print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx*2),text_block,strlen(text_block));
	wattroff(win,A_BOLD); // bold off

	char label[256];
	if(groups_mode == ENUM_SHOW_PEER && totalUnreadGroup)
		snprintf(label,sizeof(label),"[ (%lu) %s ]",totalUnreadGroup,text_group);
	else if(groups_mode == ENUM_SHOW_PEER)
		snprintf(label,sizeof(label),"[ %s ]",text_group);
	else if(groups_mode == ENUM_SHOW_GROUP)
		snprintf(label,sizeof(label),"[ %s ]",text_block);
	else if(groups_mode == ENUM_SHOW_BLOCK && totalUnreadPeer)
		snprintf(label,sizeof(label),"[ (%lu) %s ]",totalUnreadPeer,text_peer);
	else if(groups_mode == ENUM_SHOW_BLOCK)
		snprintf(label,sizeof(label),"[ %s ]",text_peer);
	size_t utf8len = torx_utf8len(label);
	fy = 0,fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_contacts_groups,label);

	if(search)
	{
		fy = 0,fx = align_right(torx_utf8len(search) + 1 + utf8len);
		print_nowrap(win,&fy,&fx,subtract_size(screen_cols,fx+2),search,strlen(search));
	}

	fy = 1;
	snprintf(label,sizeof(label),"[ %s ]",text_add_or_generate);
	utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_generate,label);
	if(totalIncoming)
		snprintf(label,sizeof(label),"[ (%lu) %s ]",totalIncoming,text_requests);
	else
		snprintf(label,sizeof(label),"[ %s ]",text_requests);
	utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_requests,label);

	snprintf(label,sizeof(label),"[ %s ]",text_ids);
	utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_ids,label);

	snprintf(label,sizeof(label),"[ %s ]",text_logs);
	utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_logs,label);

	snprintf(label,sizeof(label),"[ %s ]",text_settings);
	utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_settings,label);

	snprintf(label,sizeof(label),"[ %s ]",text_global_kill);
	utf8len = torx_utf8len(label);
	fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_global_kill,label);

//	fy = screen_rows-2, fx = 2; // Drawing bottom first because it is less important
//	print_wrap(win, &fy, &fx, subtract_size(screen_cols,fx*2), text_navigation_basic, strlen(text_navigation_basic));

	fy = 0; // back to top
	draw_scrollable(win,&fy,&fx,&focus_contacts,&contacts_scroll_offset);

	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static int callback_message_delete(const int w)
{
	(void)w;
	message_edit(selected_msg_n,selected_msg_i,NULL);
	go_back(1);
	return 0; // Do not rebuild
}

static int callback_message_edit(const int w)
{
	(void)w;
	const int p_iter = getter_int(selected_msg_n,selected_msg_i,-1,offsetof(struct message_list,p_iter));
	if(p_iter < 0)
		return 0;
	pthread_rwlock_rdlock(&mutex_protocols); // 🟧
	const uint32_t null_terminated_len = protocols[p_iter].null_terminated_len;
	const uint32_t date_len = protocols[p_iter].date_len;
	const uint32_t signature_len = protocols[p_iter].signature_len;
	pthread_rwlock_unlock(&mutex_protocols); // 🟩

	t_peer[global_n].edit_n = selected_msg_n;
	t_peer[global_n].edit_i = selected_msg_i;

	torx_free((void*)&t_peer[global_n].unsent);
	t_peer[global_n].unsent_pos = 0;

	t_peer[global_n].unsent = getter_string(selected_msg_n,selected_msg_i,-1,offsetof(struct message_list,message));
	t_peer[global_n].unsent_pos = torx_allocation_len(t_peer[global_n].unsent) - (null_terminated_len + date_len + signature_len);
	if(date_len + signature_len)
		t_peer[global_n].unsent = torx_realloc(t_peer[global_n].unsent,t_peer[global_n].unsent_pos + null_terminated_len);

	go_back(1);
	return 0; // Do not rebuild
}

static int callback_message_resend(const int w)
{
	(void)w;
	message_resend(selected_msg_n,selected_msg_i);
	go_back(1);
	return 0; // Do not rebuild
}

static int callback_message_group_accept(const int w)
{
	(void)w;
	group_join_from_i(selected_msg_n,selected_msg_i);
	go_back(1);
	return 0; // Do not rebuild
}

static void draw_message_actions(void)
{ // Message Actions Route (NOTE: global_n is still set).
	const int n = selected_msg_n;
	const int i = selected_msg_i;
	const int p_iter = getter_int(n,i,-1,offsetof(struct message_list,p_iter));
	if(p_iter < 0)
		return;
	WINDOW *win = window_prepare(&draw_message_actions,&window_message_actions,&focus_message_actions); // XXX Must do first
	widget_next_has_default_focus(); // XXX Set default widget focus

	pthread_rwlock_rdlock(&mutex_protocols); // 🟧
	const uint8_t utf8 = protocols[p_iter].utf8;
	const uint16_t protocol = protocols[p_iter].protocol;
	const uint32_t null_terminated_len = protocols[p_iter].null_terminated_len;
	pthread_rwlock_unlock(&mutex_protocols); // 🟩
	const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
	const uint8_t stat = getter_uint8(n,i,-1,offsetof(struct message_list,stat));

	size_t fy = 0,fx = 2;
	// Draw top line widgets
	wattron(win,A_BOLD); // bold on
	print_nowrap(win,&fy,&fx,printable_width,text_actions,strlen(text_actions));
	wattroff(win,A_BOLD); // bold off

	char label[256];
	snprintf(label,sizeof(label),"[ %s ]",text_delete);
	size_t utf8len = torx_utf8len(label);
	fy += 2, fx = align_right(utf8len);
	widget_button(win,&fy,&fx,utf8len,callback_message_delete,label);

	if(utf8 && null_terminated_len)
	{
		snprintf(label,sizeof(label),"[ %s ]",text_edit);
		utf8len = torx_utf8len(label);
		fy += 1, fx = align_right(utf8len);
		widget_button(win,&fy,&fx,utf8len,callback_message_edit,label);
	}

	if(stat != ENUM_MESSAGE_RECV)
	{
		snprintf(label,sizeof(label),"[ %s ]",text_resend);
		utf8len = torx_utf8len(label);
		fy += 1, fx = align_right(utf8len);
		widget_button(win,&fy,&fx,utf8len,callback_message_resend,label);
	}

	if(stat == ENUM_MESSAGE_RECV && (protocol == ENUM_PROTOCOL_GROUP_OFFER || protocol == ENUM_PROTOCOL_GROUP_OFFER_FIRST))
	{
		snprintf(label,sizeof(label),"[ %s ]",text_accept);
		utf8len = torx_utf8len(label);
		fy += 1, fx = align_right(utf8len);
		widget_button(win,&fy,&fx,utf8len,callback_message_group_accept,label);
	}

	if(owner == ENUM_OWNER_GROUP_PEER)
	{ // allow renaming the peer
		fy += 1, fx = 2;
		print_wrap(win, &fy, &fx, printable_width, text_identifier, strlen(text_identifier));
		fy += 1,fx = align_right(torx_utf8len(tmp_rename));
		widget_text(win,&fy,&fx,1,printable_width,callback_rename,WIDGET_INPUT_SINGLE_LINE,&tmp_rename,&tmp_rename_pos);
	}

	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static int callback_message(const int w)
{
	(void)w;
	tmp_rename = getter_string(global_n,INT_MIN,-1,offsetof(struct peer_list,peernick));
	tmp_rename_pos = tmp_rename ? torx_allocation_len(tmp_rename) - 1 : 0;
	draw_message_actions();
	return 0; // Do not rebuild
//	return 1; // Rebuild
}

static inline void calculate_truncation(size_t *offset,const size_t printable_len,const char *message)
{ // Only for use in print_message
	for(size_t visual_col = 0; *offset < printable_len && message[*offset] != '\n'; )
	{
		wchar_t wc;
		const size_t num_bytes = mbrtowc(&wc, &message[*offset], printable_len - *offset, NULL);
		int print_width;
		if(num_bytes == (size_t)-1 || num_bytes == (size_t)-2 || (print_width = wcwidth(wc)) < 0)
		{
			if(num_bytes > 0)
				*offset += num_bytes;
			else
				*offset += 1; // NOT the same as ++
			continue;
		}
		if(visual_col + (size_t)print_width > inner_width)
			break; // too much
		visual_col += (size_t)print_width;
		*offset += num_bytes;
	}
	if(*offset < printable_len && message[*offset] == '\n')
		*offset += 1; // skip past the newline character // NOT the same as ++
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
	const uint16_t protocol = protocols[p_iter].protocol;
	const uint32_t null_terminated_len = protocols[p_iter].null_terminated_len;
	const uint32_t date_len = protocols[p_iter].date_len;
	const uint32_t signature_len = protocols[p_iter].signature_len;
	pthread_rwlock_unlock(&mutex_protocols); // 🟩
	const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
	const uint8_t stat = getter_uint8(n,i,-1,offsetof(struct message_list,stat));
	const uint8_t show_date = 1;
	uint8_t show_nick = 0;
	if(owner != ENUM_OWNER_CTRL || stat != ENUM_MESSAGE_RECV)
		show_nick = 1; // show nick if group or if we sent it ("You")
	if(owner == ENUM_OWNER_GROUP_PEER)
	{
		if(!group_pm && stat != ENUM_MESSAGE_RECV)
			return lines; // Do not print OUTBOUND messages on GROUP_PEER unless they are private
		const uint8_t status = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,status));
		if(stat == ENUM_MESSAGE_RECV && (t_peer[n].mute || status == ENUM_STATUS_BLOCKED))
			return lines; // Do not print inbound messages from muted (ignored) or blocked group peers
	}
	if((utf8 && null_terminated_len) || protocol == ENUM_PROTOCOL_GROUP_OFFER || protocol == ENUM_PROTOCOL_GROUP_OFFER_FIRST)
	{
		char *timebuffer = NULL;
		char *peernick = NULL;
		char *message = utf8 && null_terminated_len ? getter_string(n,i,-1,offsetof(struct message_list,message)) : NULL;
		size_t peernick_len = 0; // including null byte
		size_t timebuffer_len = 0; // including null byte
		size_t message_len = torx_allocation_len(message); // including null byte
		if(!message && utf8 && null_terminated_len) // this would be a bug?
			return lines;
		if(show_date)
		{
			timebuffer = message_time_string(n,i);
			timebuffer = torx_realloc(timebuffer,torx_allocation_len(timebuffer)-3); // Slice off seconds
			timebuffer[torx_allocation_len(timebuffer)-1] = '\0';
			timebuffer_len = torx_allocation_len(timebuffer);
			if(stat == ENUM_MESSAGE_FAIL && owner != ENUM_OWNER_GROUP_CTRL)
				for(int iter = (int)timebuffer_len - 2; iter > -1; iter--)
					if(timebuffer[iter] >= '0' && timebuffer[iter] <= '9')
						timebuffer[iter] = '-'; // Replace digits with - if the message is unsent
		}
		if(show_nick)
		{
			if(stat == ENUM_MESSAGE_RECV)
			{
				peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
				peernick_len = torx_allocation_len(peernick);
			}
			else
			{ // "You" sent it
				peernick_len = strlen(text_you) + 1;
				peernick = torx_insecure_malloc(peernick_len);
				snprintf(peernick,peernick_len,"%s",text_you);
			}
		}
		size_t printable_len;
		if(protocol == ENUM_PROTOCOL_GROUP_OFFER || protocol == ENUM_PROTOCOL_GROUP_OFFER_FIRST)
		{ // TODO consider highlighting message to enhance visibility and prevent pointless spoofing
			uint32_t untrusted_peercount;
			const int g = set_g_from_i(&untrusted_peercount,n,i);
			const int group_n = getter_group_int(g,offsetof(struct group_list,n));
			const uint32_t g_peercount = getter_group_uint32(g,offsetof(struct group_list,peercount));
			const uint8_t g_invite_required = getter_group_uint8(g,offsetof(struct group_list,invite_required));
			const uint32_t peercount = untrusted_peercount > g_peercount ? untrusted_peercount : g_peercount; // use whatever is higher
			char *g_peernick = group_n > -1 ? getter_string(group_n,INT_MIN,-1,offsetof(struct peer_list,peernick)) : NULL;
			if(!g_peernick)
			{ // We have not joined yet, so no name. Use encoded GroupID instead
				unsigned char id[GROUP_ID_SIZE]; // zero'd
				pthread_rwlock_rdlock(&mutex_expand_group); // 🟧
				memcpy(id,group[g].id,sizeof(id));
				pthread_rwlock_unlock(&mutex_expand_group); // 🟩
				g_peernick = b64_encode(id,GROUP_ID_SIZE);
				sodium_memzero(id,sizeof(id));
			}
			printable_len = (size_t)snprintf(NULL,0,"%s %s: %u %s",g_invite_required ? text_group_private : text_group_public,text_current_members,peercount,g_peernick); // must be same as below
			message_len = printable_len + 1;
			message = torx_secure_malloc(message_len);
			snprintf(message,message_len,"%s %s: %u %s",g_invite_required ? text_group_private : text_group_public,text_current_members,peercount,g_peernick); // must be same as above
			torx_free((void*)&g_peernick);
		}
		else
			printable_len = message_len - null_terminated_len - date_len - signature_len;
		printable_len += timebuffer_len + peernick_len;
		if(show_date && show_nick)
		{
			message = torx_realloc_shift(message,message_len + timebuffer_len + peernick_len,1);
			memcpy(message,timebuffer,timebuffer_len - 1);
			message[timebuffer_len - 1] = ' '; // space after timebuffer
			memcpy(&message[timebuffer_len],peernick,peernick_len - 1);
			message[timebuffer_len + peernick_len - 1] = ' '; // space after peernick
		}
		else if(show_date)
		{
			message = torx_realloc_shift(message,message_len + timebuffer_len,1);
			memcpy(message,timebuffer,timebuffer_len - 1);
			message[timebuffer_len - 1] = ' '; // space after timebuffer
		}
		else if(show_nick)
		{
			message = torx_realloc_shift(message,message_len + peernick_len,1);
			memcpy(message,peernick,peernick_len - 1);
			message[peernick_len - 1] = ' '; // space after peernick
		}
		size_t anticipated_lines = 1 + print_wrap(NULL,NULL,NULL,inner_width,message,printable_len);
		if(win && anticipated_lines + processed_lines > subtract_size(must_be_processed_lines,height_of_scrollable))
		{ // we are ACTUALLY printing
			const size_t required_offset = subtract_size(chat_scroll_lines,processed_lines);
			size_t truncation = 0; // number of characters truncated from the end
			if(required_offset)
			{ // Some of the message was already processed. Truncation required XXX Must do BEFORE calculating offset.
				anticipated_lines = subtract_size(anticipated_lines,required_offset); // XXX reducing anticipated lines from the end
				size_t tmp_iter = 0;
				for(size_t found_lines = 0; found_lines < anticipated_lines; found_lines++)
					calculate_truncation(&tmp_iter,printable_len,message); // Need to find point of necessary truncation, if applicable (printing only first part of message)
				truncation = printable_len - tmp_iter;
			}
			const size_t available_lines = (must_be_processed_lines - processed_lines > height_of_scrollable) ? height_of_scrollable : must_be_processed_lines - processed_lines;
			size_t offset = 0; // number of characters stripped from the start
			if(anticipated_lines > available_lines)
			{ // Can only print latter part of message. Offset required. XXX Must do AFTER calculating truncation.
				for(size_t reduction_required = anticipated_lines - available_lines; reduction_required; reduction_required--)
					calculate_truncation(&offset,printable_len,message);
				anticipated_lines = available_lines; // XXX reducing anticipated lines from the start
			}
			size_t fx = align_center_uncapped(inner_width);
			size_t fy = top_line + available_lines - anticipated_lines;
			size_t seperate_fy = fy, seperate_fx = fx;
			const int w = widget_new(WIDGET_CHECKBOX,inner_width);
			widget[w].callback = callback_message;
			if(*current_focus == w)
			{
				toggle_highlight(win); // highlight on
				selected_msg_n = n;
				selected_msg_i = i;
			}
			if(group_pm) // Make PMs bold
				wattron(win,A_BOLD); // bold on
			lines = 1 + required_offset + print_wrap(win,&fy,&fx,inner_width,&message[/*peernick_len + timebuffer_len + */offset],printable_len - offset - truncation/* - peernick_len - timebuffer_len*/);
			if(group_pm) // Make PMs bold
				wattroff(win,A_BOLD); // bold off
			else if((show_date || show_nick) && offset < peernick_len + timebuffer_len)
			{ // Re-printing over the date / peernick, in bold.
				wattron(win,A_BOLD); // bold on
				print_wrap(win,&seperate_fy,&seperate_fx,inner_width,&message[offset],peernick_len + timebuffer_len);
				wattroff(win,A_BOLD); // bold off
			}
			if(*current_focus == w)
				toggle_highlight(win); // highlight off
		//	error_printf(0,"Checkpoint printed-lines: %lu out of anticipated: %lu into available: %lu in scrollable height: %lu chat_scroll_lines: %lu processed: %lu must-be: %lu msg: %s",lines,anticipated_lines,available_lines,height_of_scrollable,chat_scroll_lines,processed_lines,must_be_processed_lines,&message[offset]);
		}
		else // not actually printing
			lines = anticipated_lines;
		torx_free((void*)&message);
		torx_free((void*)&timebuffer);
		torx_free((void*)&peernick);
	}
	return lines;
}

static int callback_message_input(const int w)
{
	(void)w;
	const int n = global_n;
	const size_t unsent_len = torx_allocation_len(t_peer[n].unsent) ? torx_allocation_len(t_peer[n].unsent) - 1 : 0;
	if(message_entry_currently_selected) // XXX DO THIS CHECK BEFORE message_send, in case of race condition
		*current_focus = -1; // reset to default, which is message input (yes this is necessary)
	if(!unsent_len)
		return 1; // Ignore it, but trigger rebuild to prevent it from being appended.
	else if(t_peer[n].unsent_pos == unsent_len)
	{ // send message
		if(t_peer[n].edit_n > -1 && t_peer[n].edit_i > INT_MIN)
		{
			message_edit(t_peer[n].edit_n,t_peer[n].edit_i,t_peer[n].unsent);
			t_peer[n].edit_n = -1;
			t_peer[n].edit_i = INT_MIN;
		}
	/*	else if(t_peer[n].edit_n > -1)
		{ // NOT in use, but we could. This is how we do it in GTK
			change_nick(t_peer[n].edit_n,t_peer[n].unsent);
			t_peer[n].edit_n = -1
		}	*/
		else if(t_peer[n].pm_n > -1) // send to GROUP_PEER instead of GROUP_CTRL
			message_send(t_peer[n].pm_n,ENUM_PROTOCOL_UTF8_TEXT_PRIVATE,t_peer[n].unsent,(uint32_t)torx_allocation_len(t_peer[n].unsent) - 1);
		else
		{
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
		}
		torx_free((void*)&t_peer[n].unsent);
		t_peer[n].unsent_pos = 0;
		chat_scroll_lines = 0;
	}
	else // Insert newline at cursor
		append_character_at_cursor(w,L'\n'); // L makes it wide
	return 1; // Rebuild
}

static void draw_chat(void)
{ // Chat Route
	const int n = global_n;
	if(n < 0)
	{
		error_simple(0,"draw_chat called on invalid n. UI coding error. Report this.");
		return; // Bug
	}
	WINDOW *win = window_prepare(&draw_chat,&window_chat,&focus_chat); // XXX Must do first

	#define TOP_LINE_HEIGHT 1
	if(!t_peer[n].unsent)
	{ // Necessary
		t_peer[n].unsent = torx_secure_malloc(1);
		t_peer[n].unsent[0] = '\0';
		t_peer[n].unsent_pos = 0;
	}
	else if(t_peer[n].unsent_pos >= torx_allocation_len(t_peer[n].unsent))
		t_peer[n].unsent_pos = torx_allocation_len(t_peer[n].unsent) - 1;

	size_t unsent_required_lines = 1; // XXX Do not use return from print_wrap, we need the offset_y after modification for cursor
	print_wrap(NULL,&unsent_required_lines, NULL, inner_width, t_peer[n].unsent, torx_allocation_len(t_peer[n].unsent) - 1); // alt: t_peer[n].unsent_pos

	// Draw horizontal divider
	const size_t mid = unsent_required_lines + 2 + TOP_LINE_HEIGHT >= screen_rows ? TOP_LINE_HEIGHT : subtract_size(screen_rows,unsent_required_lines + 2);
	for(size_t x = 1; x + 1 < screen_cols; x++)
		mvwaddch(win, (int)mid, (int)x, ACS_HLINE);
	// Draw intersection characters
	mvwaddch(win, (int)mid, 0, ACS_LTEE);
	mvwaddch(win, (int)mid, (int)subtract_size(screen_cols,1), ACS_RTEE);

	size_t fy = mid,fx = 2;
	if(t_peer[n].pm_n > -1)
	{
		char *peernick = getter_string(t_peer[n].pm_n,INT_MIN,-1,offsetof(struct peer_list,peernick));
		char cancel_message[256]; // zero'd
		snprintf(cancel_message,sizeof(cancel_message),"%s %s",text_private_messaging,peernick);
		torx_free((void*)&peernick);
		print_nowrap(win,&fy,&fx,printable_width,cancel_message,strlen(cancel_message));
		sodium_memzero(cancel_message,sizeof(cancel_message));
	}
	else if(t_peer[n].edit_n > -1)
		print_nowrap(win,&fy,&fx,printable_width,text_cancel_editing,strlen(text_cancel_editing));
	else
		print_nowrap(win,&fy,&fx,printable_width,text_navigation_chat,strlen(text_navigation_chat));

	const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
	// Draw top line widgets
	char label[printable_width + 1];
	uint8_t sendfd_connected;
	uint8_t recvfd_connected;
	if(owner == ENUM_OWNER_GROUP_CTRL)
		sendfd_connected = recvfd_connected = 1; // Always show groups as green
	else
	{
		sendfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,sendfd_connected));
		recvfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,recvfd_connected));
	}
	const wint_t online_char = get_online_char(sendfd_connected,recvfd_connected);
	char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
	snprintf(label,sizeof(label),"%lc %s",online_char,peernick);
	torx_free((void*)&peernick);
	wattron(win,A_BOLD); // bold on
	fy = 0,fx = 2;
	print_nowrap(win,&fy,&fx,printable_width,label,strlen(label));
	wattroff(win,A_BOLD); // bold off

	snprintf(label,sizeof(label),"[ %s ]",text_settings);
	const size_t utf8len1 = torx_utf8len(label);
	fy = 0,fx = align_right(utf8len1);
	widget_button(win,&fy,&fx,utf8len1,callback_chat_settings,label);

	snprintf(label,sizeof(label),"[ %s ]",text_actions);
	const size_t utf8len2 = torx_utf8len(label);
	fy = 0,fx = align_right(utf8len1 + 1 + utf8len2);
	widget_button(win,&fy,&fx,utf8len2,callback_chat_actions,label);

	// Get chat history height
	int min_i,max_i,msg_count,g;
	struct msg_list *page;
	if(owner == ENUM_OWNER_GROUP_CTRL)
	{
		global_group = g = set_g(n,NULL);
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
	chat_scroll_jump = subtract_size(mid,TOP_LINE_HEIGHT);
	chat_scroll_max = 0; // must reset
	widgets_existing_before_scrollable = torx_allocation_len(widget) / sizeof(struct widget);
	// Print message history
	uint8_t reprinted = 0;
	reprint: {}
	if(reprinted)
	{ // Clear what we already printed. This is ONLY triggered when we scroll ALL the way to the top.
		// First, if we created any scrollable widgets, get rid of them.
		const int active_widgets = (int)(torx_allocation_len(widget) / sizeof(struct widget));
		for(int w = (int)widgets_existing_before_scrollable; w < active_widgets; w++)
			zero_w(w);
		widget = torx_realloc(widget,sizeof(struct widget) * widgets_existing_before_scrollable);
		// Then clear the print area
		chat_scroll_lines = subtract_size(chat_scroll_max,chat_scroll_jump);
		const size_t edge = subtract_size(screen_cols,inner_width)/2;
		for(size_t y = 0; y < chat_scroll_jump; y++)
			for(size_t x = 0; x < inner_width; x++)
				mvwaddch(win, (int)y + TOP_LINE_HEIGHT, (int)(x + edge), (chtype)' ');
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
				processed_lines += print_message(win,TOP_LINE_HEIGHT,chat_scroll_jump,must_be_processed_lines,processed_lines,page->n,page->i);
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
				processed_lines += print_message(win,TOP_LINE_HEIGHT,chat_scroll_jump,must_be_processed_lines,processed_lines,n,i);
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
	fy = mid + 1,fx = align_center_uncapped(inner_width);
	if(*current_focus < 0 || *current_focus == (int)(torx_allocation_len(widget) / sizeof(struct widget))) // must be before widget_next_has_default_focus
		message_entry_currently_selected = true;
	else
		message_entry_currently_selected = false;
	widget_next_has_default_focus(); // XXX Set default widget focus
	widget_text(win,&fy,&fx,subtract_size(screen_rows,mid + 2),inner_width,callback_message_input,WIDGET_INPUT_MULTI_LINE,&t_peer[n].unsent,&t_peer[n].unsent_pos);

	sodium_memzero(label,sizeof(label));
	widget_draw_cursor(win); // XXX Must do last
}

static inline void shift_or_append(char **destination,char **source,size_t *cursor_p)
{ // NO SAFETY CHECKS: be careful not to de-reference a null. THIS IS ONLY FOR USE IN await_key_or_signal for LOG OUTPUT ONLY
	if(!*destination)
	{ // Just shift it over
		*destination = *source;
		*source = NULL; // necessary in await_key_or_signal
		*cursor_p = torx_allocation_len(*destination) - 2; // Set at end // -2 is necessary because we "strip trailing newline on outputs" in widget_text
	}
	else
	{ // Append
		const size_t former_len = torx_allocation_len(*destination);
		*destination = torx_realloc(*destination,former_len + torx_allocation_len(*source) - 1); // cut off one null pointer
		memcpy(&(*destination)[former_len-1],*source,torx_allocation_len(*source));
		if(*cursor_p + 2 == former_len) // -2 is necessary because we "strip trailing newline on outputs" in widget_text
			*cursor_p = torx_allocation_len(*destination) - 2; // Set at end // -2 is necessary because we "strip trailing newline on outputs" in widget_text
	}
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
					const int val = cb_page->cb_args->mem_int_a;
					if(val == 0 || val == -1)
					{ // Success
						torx_free((void*)&password_old);
						torx_free((void*)&password_new);
						torx_free((void*)&password_verify);
						pw_old_cursor = 0;
						pw_new_cursor = 0;
						pw_verify_cursor = 0;
					}
					else if(val == 1)
					{ // Incorrect old
						torx_free((void*)&password_old);
						pw_old_cursor = 0;
						beep();
					}
					else if(val == 2)
					{ // Inconsistent new
						torx_free((void*)&password_new);
						torx_free((void*)&password_verify);
						pw_new_cursor = 0;
						pw_verify_cursor = 0;
						beep();
					}
					must_redraw_ui = -2;
				}
				else if(cb_page->cb_type == ENUM_INCOMING_FRIEND_REQUEST)
				{
					if(window_contacts || (window_requests && !outgoing_mode))
						must_redraw_ui = -2;
					totalIncoming++;
					notify("TODO","ENUM_INCOMING_FRIEND_REQUEST");
				}
				else if(cb_page->cb_type == ENUM_ONION_DELETED)
				{ // TODO should go_back in the case of global_n (ie receiving kill code), or we could have draw_chat and popovers trigger a go_back if the peer is deleted.
					const int n = cb_page->cb_args->mem_int_a;
					if(n == generated_n)
					{
						torx_free((void*)&generate_output);
						generated_n = -1;
						must_redraw_ui = -2;
					}
					else
					{
						const uint8_t owner = cb_page->cb_args->mem_uint8;
						if((window_contacts && ((owner == ENUM_OWNER_GROUP_CTRL && groups_mode == ENUM_SHOW_GROUP) || (owner == ENUM_OWNER_CTRL && groups_mode != ENUM_SHOW_GROUP)))
						|| (window_ids && ((single_mode && owner == ENUM_OWNER_SING) || (!single_mode && owner == ENUM_OWNER_MULT)))
						|| (window_requests && outgoing_mode && owner == ENUM_OWNER_PEER)
						|| (window_chat && global_n == n)
						|| (window_group_invite && owner == ENUM_OWNER_CTRL)
						|| (window_group_peerlist && owner == ENUM_OWNER_GROUP_PEER && global_n == getter_group_int(set_g(n,NULL),offsetof(struct group_list,n))))
							must_redraw_ui = -2; // alt: draw_contacts();
					}
				}
				else if(cb_page->cb_type == ENUM_PEER_ONLINE)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
					if((window_contacts && owner == ENUM_OWNER_CTRL && groups_mode == ENUM_SHOW_PEER)
					|| (window_chat && global_n == n)
					|| (window_group_invite && owner == ENUM_OWNER_CTRL)
					|| (window_group_peerlist && owner == ENUM_OWNER_GROUP_PEER && global_n == getter_group_int(set_g(n,NULL),offsetof(struct group_list,n))))
						must_redraw_ui = -2; // alt: draw_contacts();
				}
				else if(cb_page->cb_type == ENUM_PEER_OFFLINE)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
					if((window_contacts && owner == ENUM_OWNER_CTRL && groups_mode == ENUM_SHOW_PEER)
					|| (window_chat && global_n == n)
					|| (window_group_invite && owner == ENUM_OWNER_CTRL)
					|| (window_group_peerlist && owner == ENUM_OWNER_GROUP_PEER && global_n == getter_group_int(set_g(n,NULL),offsetof(struct group_list,n))))
						must_redraw_ui = -2; // alt: draw_contacts();
				}
				else if(cb_page->cb_type == ENUM_PEER_NEW)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
					if((window_contacts && groups_mode == ENUM_SHOW_PEER && owner == ENUM_OWNER_CTRL) || (window_group_peerlist && owner == ENUM_OWNER_GROUP_PEER && global_n == getter_group_int(set_g(n,NULL),offsetof(struct group_list,n))))
						must_redraw_ui = -2;
				}
				else if(cb_page->cb_type == ENUM_ONION_READY)
				{
					generated_n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(generated_n,INT_MIN,-1,offsetof(struct peer_list,owner));
					if(threadsafe_read_uint8(&mutex_global_variable,&shorten_torxids))
						generate_output = getter_string(generated_n, INT_MIN, -1, offsetof(struct peer_list, torxid));
					else
						generate_output = getter_string(generated_n, INT_MIN, -1, offsetof(struct peer_list, onion));
					if(window_generate || (window_ids && ((single_mode && owner == ENUM_OWNER_SING) || (!single_mode && owner == ENUM_OWNER_MULT))))
						must_redraw_ui = -2;
				}
				else if(cb_page->cb_type == ENUM_ERROR)
				{
					shift_or_append(&torx_log_buffer,&cb_page->cb_args->mem_charp_a,&torx_log_buffer_pos);
					if(window_logs && !tor_log_mode)
						must_redraw_ui = -2;
				}
				else if(cb_page->cb_type == ENUM_FATAL)
				{
					shift_or_append(&torx_log_buffer,&cb_page->cb_args->mem_charp_a,&torx_log_buffer_pos);
					if(window_logs && !tor_log_mode)
						must_redraw_ui = -2;
				}
				else if(cb_page->cb_type == ENUM_TOR_LOG)
				{
					shift_or_append(&tor_log_buffer,&cb_page->cb_args->mem_charp_a,&tor_log_buffer_pos);
					if(window_logs && tor_log_mode)
						must_redraw_ui = -2;
				}
				else if(cb_page->cb_type == ENUM_CUSTOM_SETTING)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const char *setting_name = cb_page->cb_args->mem_charp_a;
					const char *setting_value = cb_page->cb_args->mem_charp_b;
					size_t setting_value_len = cb_page->cb_args->mem_size;
					int plaintext = cb_page->cb_args->mem_int_b;
					if(!strncmp(setting_name,"theme",5))
					{
						const int proposed_theme = (int)strtoll(setting_value, NULL, 10);
						if(proposed_theme != global_theme && global_theme > -1 && proposed_theme != THEME_DEFAULT)
						{ // Checking that it is (a) a change and (b) that we have already initialized, or that we haven't but we are different than default
							global_theme = proposed_theme;
							must_redraw_ui = -2;
						}
					}
					else if(!strncmp(setting_name,"language",8) && sizeof(language) == setting_value_len+1)
					{ // We are requiring the language to be exactly 5 characters long to be considered valid (ex: en_US)
						if(memcmp(language,setting_value,sizeof(language)))
						{ // Loading a different language setting.
							memcpy(language,setting_value,setting_value_len);
							language[setting_value_len] = '\0';
							ui_initialize_language();
							must_redraw_ui = -2;
						}
					}
					else if(plaintext == 0 && !strncmp(setting_name,"mute",4))
						t_peer[n].mute = (int8_t)strtoll(setting_value, NULL, 10);
					else if(plaintext == 0 && !strncmp(setting_name,"unread",6))
					{
						if(log_unread == 1)
						{ // Ignoring if not logging, since they may be potentially old
							const int8_t log_messages = getter_int8(n,INT_MIN,-1,offsetof(struct peer_list,log_messages));
							if(log_messages == 1 || (!log_messages && threadsafe_read_uint8(&mutex_global_variable,&global_log_messages)))
							{
								t_peer[n].unread = strtoull(setting_value, NULL, 10);
								if(t_peer[n].unread > 0)
								{
									const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
									if(owner == ENUM_OWNER_GROUP_CTRL)
										totalUnreadGroup += t_peer[n].unread;
									else
										totalUnreadPeer += t_peer[n].unread;
									if(window_contacts)
										must_redraw_ui = -2;
								}
							}
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
						if(global_n < 0 || (global_n != n && global_n != group_n))
						{
							if(group_n > -1)
							{
								t_peer[group_n].unread++;
								totalUnreadGroup++;
							}
							else
							{
								t_peer[n].unread++;
								totalUnreadPeer++;
							}
						}
						if(window_chat && message_entry_currently_selected)
							*current_focus = -1; // reset to default, which is message input (yes this is necessary)
						if(window_contacts || (global_n > -1 && (global_n == n || global_n == group_n)))
							must_redraw_ui = -2; // better than draw_chat(global_n); // NOT n or this could draw a PM chat
						if((window_contacts || must_redraw_ui != -2) && (owner != ENUM_OWNER_GROUP_PEER || t_peer[group_n].mute == 0) && t_peer[n].mute == 0) // NOT else if
							notify("TODO","ENUM_MESSAGE_NEW"); // Notify if on contact list, or if we're on a chat that isn't the relevant one
					}
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_MODIFIED)
				{
					const int n = cb_page->cb_args->mem_int_a;
				//	const int i = cb_page->cb_args->mem_int_b;
					if(n == global_n || (getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner)) == ENUM_OWNER_GROUP_PEER && global_n == getter_group_int(set_g(n,NULL),offsetof(struct group_list,n)))) // TODO could also check if this message is visible, if that is trivial, and verify that we didn't just manually edit this message (which also triggers MESSAGE_MODIFIED).
						must_redraw_ui = -2;
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_DELETED)
				{
					// Unnecessary to do anything since this will only be manually triggered.
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
					const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
					const uint8_t status = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,status));
					if(owner == ENUM_OWNER_CTRL && status == ENUM_STATUS_PENDING)
					{
						totalIncoming++;
						if(window_contacts)
							must_redraw_ui = -2;
					}
					else if(window_contacts && ((groups_mode == ENUM_SHOW_PEER && owner == ENUM_OWNER_CTRL) || (groups_mode == ENUM_SHOW_GROUP && owner == ENUM_OWNER_GROUP_CTRL)))
						must_redraw_ui = -2;
				}
				else if(cb_page->cb_type == ENUM_CLEANUP)
				{
					running = false;
					sig_num = cb_page->cb_args->mem_int_a;
					must_redraw_ui = -1; // necessary
				}
				else if(cb_page->cb_type == ENUM_STREAM)
				{
					error_simple(0,"Checkpoint ENUM_STREAM");
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_EXTRA)
				{ // This is used in other clients for loading from disk whether an audio message has been heard/unheard
					error_simple(0,"Checkpoint ENUM_MESSAGE_EXTRA");
				}
				else if(cb_page->cb_type == ENUM_MESSAGE_MORE)
				{ // Triggers after we call message_load_more. Should be not need to be handled, but untested.
					error_simple(0,"Checkpoint ENUM_MESSAGE_MORE");
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
			int ret;
			wint_t wch;
			do {
				ret = wget_wch(win,&wch);
			} while(ret != OK && ret != KEY_CODE_YES);
			return (int)wch;
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
			sodium_memzero(array,sizeof(array));
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
	refresh(); // Prevents window blanking if the first keypress is KEY_BACKSPACE or KEY_LEFT
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
			if(window_chat && message_entry_currently_selected)
				*current_focus = -1; // Prevent jumping from message entry when widget counts change
			else if(*current_focus > -1 && widget[*current_focus].text && widget[*current_focus].type != WIDGET_OUTPUT_MULTI_LINE)
				*current_focus = -1; // TODO this is undesirable but working around a hang on wrefresh(win)
			endwin(); refresh(); clear(); keypad(stdscr,TRUE); // all necessary when resizing
			redraw();
		}
		const int ch = await_key_or_signal(stdscr);
		if(ch == ERR || (ch < 0 && ch != -2) || (wint_t)ch == KEY_RESIZE)
			continue;
		else if(ch == -2 || keypress(*current_focus,(wint_t)ch))
			redraw();
	}
	// Clean-up
	char p1[21];
	if(log_unread == 1)
	{ // Log Unread Message Count in the same manner that we store last_seen
		const uint8_t global_log_messages_local = threadsafe_read_uint8(&mutex_global_variable,&global_log_messages);
		for(int peer_index,n = 0 ; (peer_index = getter_int(n,INT_MIN,-1,offsetof(struct peer_list,peer_index))) > -1 || getter_byte(n,INT_MIN,-1,offsetof(struct peer_list,onion)) != 0 ; n++)
		{
			const int8_t log_messages = getter_int8(n,INT_MIN,-1,offsetof(struct peer_list,log_messages));
			if(peer_index > -1 && (log_messages == 1 || (!log_messages && global_log_messages_local)))
			{
				const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
				if(owner == ENUM_OWNER_CTRL || owner == ENUM_OWNER_GROUP_CTRL)
				{
					if(t_peer[n].unread)
					{
						const size_t len = (size_t)snprintf(p1,sizeof(p1),"%zu",t_peer[n].unread);
						sql_setting(0,peer_index,"unread",p1,len);
					}
					else
						sql_delete_setting(0,peer_index,"unread");
				}
			}
		}
	}
	cleanup_lib(sig_num);
	torx_free((void*)&search);
	widget_clear(NULL);
	endwin();
	if(notify_fds[0] >= 0)
		close(notify_fds[0]);
	if(notify_fds[1] >= 0)
		close(notify_fds[1]);
	return 0;
}
