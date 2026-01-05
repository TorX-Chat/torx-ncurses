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
static int focus_login = -1, focus_contacts = -1, focus_chat = -1; // must initialize as -1 so that draw_* can set a default
static WINDOW *window_login = NULL, *window_contacts = NULL, *window_chat = NULL;
// XXX END One required for each route END XXX

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

static void signal_resize(int sig)
{ // Do not call ncurses functions directly from here
	(void)sig;
	resized = 1;
	++resize_seq;
}

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
				mvwaddch(win, (int)(*y + offset_y), (int)(*x + offset_x), (chtype)str[iter]);
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

static int widget_button(WINDOW *win,size_t *y,size_t *x,const size_t max_width,int (*callback)(const int,const int),const char *text)
{ // Draw a button
	const int w = widget_new(WIDGET_CHECKBOX,max_width);
	widget[w].callback = callback;
	const size_t text_len = text ? strlen(text) : 0;
	if(*current_focus == w)
		wattron(win, A_REVERSE); // highlight on
	print_wrapped(win,y,x,max_width,text,text_len);
	if(*current_focus == w)
		wattroff(win, A_REVERSE); // highlight off
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

static int widget_text_entry(WINDOW *win,size_t *y,size_t *x,const size_t max_width,int (*callback)(const int,const int),const int type,char **text_p,size_t *cursor_pos)
{ // Draw a text entry. Single-line SHOULD highlight when selected. Multi-line should NOT highlight when selected.
	if(type != WIDGET_PASSWORD && type != WIDGET_INPUT_SINGLE_LINE && type != WIDGET_INPUT_MULTI_LINE && type != WIDGET_INPUT_NUMERICAL)
	{
		error_simple(-1,"widget_text_entry passed an inappropriate type. UI coding error. Report this.");
		return 0;
	}
	const int w = widget_new(type,max_width);
	widget[w].callback = callback;
	widget[w].text = text_p;
	widget[w].cursor = cursor_pos;
	const size_t start_y = *y, start_x = *x;
	const size_t text_len = (text_p && *text_p) ? strlen(*text_p) : 0;
	char array[text_len + 1]; // zero'd
	if(!pw_show && type == WIDGET_PASSWORD)
		memset(array,'*',sizeof(array)-1);
	else
		snprintf(array,sizeof(array),"%s",*text_p);
	array[text_len] = '\0';
	if(*current_focus == w && type != WIDGET_INPUT_MULTI_LINE)
		wattron(win, A_REVERSE); // highlight on
	print_wrapped(win,y,x,max_width,array,sizeof(array)-1);
	if(*current_focus == w)
	{
		if(type != WIDGET_INPUT_MULTI_LINE)
			wattroff(win, A_REVERSE); // highlight off
		size_t row = start_y, col = start_x;
		print_wrapped(NULL,&row, &col, max_width, *text_p, cursor_pos ? *cursor_pos : 0);
		widget_set_cursor(row, col);
		curs_set(1);
	}
	else
		curs_set(0);
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

static int keypress(const int w,const int ch)
{ // replace with callback_message_input ?
	if(w < 0 || w >= (int)(torx_allocation_len(widget) / sizeof(struct widget)))
	{
		error_printf(0,"Keypress called on possibly invalid widget: %lu of %lu",w,torx_allocation_len(widget) / sizeof(struct widget));
		return 0; // Sanity check
	}
	if(ch == KEY_ESC || ch == KEY_HOME)
	{ // Go back or exit
		if(window_login || window_contacts)
			running = false;
		else if(window_chat)
		{
			global_n = -1;
			draw_contacts();
		}
		else
			error_printf(0,"No window to navigate to. Possible coding error.");
	}
	else if(ch == '\t' || ch == KEY_BTAB)
	{
		*current_focus = (*current_focus + 1) % (int)(torx_allocation_len(widget) / sizeof(struct widget));
		return 1; // Rebuild
	}
	else if(ch == KEY_UP)
	{
		if(widget[w].type == WIDGET_INPUT_MULTI_LINE)
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
		else if(*current_focus > 0)
			*current_focus = *current_focus - 1;
		return 1; // Rebuild
	}
	else if(ch == KEY_DOWN)
	{
		if(widget[w].type == WIDGET_INPUT_MULTI_LINE)
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
	else if(ch >= 32 && ch <= 126 && widget[w].cursor && widget[w].text)
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
		(*widget[w].text)[*widget[w].cursor] = (char)ch;
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

static void draw_login(void)
{ // Password Route
	widget_clear(&focus_login); // XXX Must do first
	window_login = newwin_size(screen_rows, screen_cols, 0, 0);
	box(window_login,0,0); // Draw border

	size_t fy = 0, fx = 2;
	char text_enter_password[] = " Welcome to TorX ";
	print_wrapped(window_login,&fy,&fx,screen_cols-(fx*2),text_enter_password,sizeof(text_enter_password)-1);
	char text_password[] = "Password:";
	fy += 2, fx = 2; // fy must be += because there might be wrap
	print_wrapped(window_login,&fy,&fx,screen_cols-(fx*2),text_password,sizeof(text_password)-1);

	fy += 1, fx = 4; // fy must be += because there might be wrap
	widget_next_has_default_focus(); // XXX Set default widget focus
	widget_text_entry(window_login,&fy,&fx,screen_cols-(fx*2),callback_password,WIDGET_PASSWORD,&password,&pw_cursor);

	fy += 2,fx = 4; // fy must be += because there might be wrap
	widget_checkbox(window_login,&fy,&fx,screen_cols-(fx*2),callback_pw_show,1,"Show Password",pw_show);

	fy += 1, fx = 4;
	widget_checkbox(window_login,&fy,&fx,screen_cols-(fx*2),callback_censored_region,1,"Censored Region",threadsafe_read_uint8(&mutex_global_variable,&censored_region));

	const char text_password_help[] = "Tab: cycle focus  Up/Down: move focus  Enter: proceed  Esc/Home: quit";
	fy = screen_rows-2, fx = 2;
	print_wrapped(window_login, &fy, &fx, screen_cols-(fx*2), text_password_help, sizeof(text_password_help)-1);

	widget_draw_cursor(window_login); // XXX Must do last
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
	widget_clear(&focus_contacts); // XXX Must do first
	window_contacts = newwin_size(screen_rows, screen_cols, 0, 0);
	box(window_contacts,0,0); // Draw border

	if(groups_mode)
		mvwprintw_size(window_contacts,0,2," Groups "); // do not wrap
	else
		mvwprintw_size(window_contacts,0,2," Contacts "); // do not wrap

	const char *groups_label = groups_mode ? "[ Contacts ]" : "[ Groups ]";
	size_t fy = 0,fx = screen_cols - strlen(groups_label) - 3;
	widget_next_has_default_focus(); // XXX Set default widget focus
	widget_button(window_contacts,&fy,&fx,strlen(groups_label),callback_contacts_groups,groups_label);

	const char settings_label[] = "[ Settings ]";
	fx = screen_cols - (sizeof(settings_label) - 1) - 3;
	widget_button(window_contacts,&fy,&fx,(sizeof(settings_label) - 1),NULL,settings_label); // TODO set a callback

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
	widget_clear(&focus_chat); // XXX Must do first
	window_chat = newwin_size(screen_rows, screen_cols, 0, 0);
	box(window_chat,0,0); // Draw border
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
	const size_t mid = screen_rows - visual_lines - 2;
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
	widget_button(window_chat,&fy,&fx,(sizeof(settings_label) - 1),NULL,settings_label); // TODO set a callback

	const char actions_label[] = "[ Actions ]";
	fy = 0,fx = screen_cols - (sizeof(actions_label) - 1) - 1 - (sizeof(settings_label) - 1) - 3;
	widget_button(window_chat,&fy,&fx,(sizeof(actions_label) - 1),NULL,actions_label); // TODO set a callback

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
	widget_text_entry(window_chat,&fy,&fx,inner_width,callback_message_input,WIDGET_INPUT_MULTI_LINE,&t_peer[n].unsent,&t_peer[n].unsent_pos);

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
					error_simple(0,"Checkpoint ENUM_CUSTOM_SETTING"); // TODO
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
