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
	int (*callback)(const size_t,const int); // typically holds the functionality to be executed upon ENTER press
	char **text;
	size_t *cursor;
} * widget = {0}; // REMEMBER to free this list whenever changing a page. Remember to initialize new widgets with zero_w

// XXX START One required for each route START XXX
static WINDOW *window_contacts = NULL, *window_chat = NULL, *window_input = NULL, *window_login = NULL;

static size_t *current_focus = NULL; // XXX must be set otherwise we will dereference a NULL very quick! XXX
static size_t focus_login = 0;
static size_t focus_contacts = 0;
static size_t focus_chat = 0;
// XXX END One required for each route END XXX

static size_t cursor[2] = {0}; // y,x

static int selected_n = 0; // internal use only
static int global_n = -1;
static volatile sig_atomic_t resized = 0;
static volatile sig_atomic_t resize_seq = 0;

static bool running = true; // set to false to exit
static int sig_num = 0;

static size_t screen_rows = 24, screen_cols = 80; // this will be set on startup and resize

static int notify_fds[2] = { -1, -1 }; // triggered by library callbacks, indicating that a UI call to cb_buffer is requested

static size_t chat_scroll_lines = 0;

/* Password window state */
static char *password = NULL;
static bool pw_show = false; // default false
static size_t pw_cursor = 0;

/* Contact list state */
static bool groups_mode = false;
static size_t list_first_peer_w = 0; // This facilitates left-right navigation between peerlist and settings buttons

static void signal_resize(int sig)
{ // Do not call ncurses functions directly from here
	(void)sig;
	resized = 1;
	++resize_seq;
}

#define wmove_size(win, y, x) wmove(win, (int)(y), (int)(x))
#define newwin_size(nlines, ncols, begin_y, begin_x) newwin((int)(nlines), (int)(ncols), (int)(begin_y), (int)(begin_x))

#define mvwprintw_size(win, y, x, ...) mvwprintw(win, (int)(y), (int)(x), __VA_ARGS__)

static inline size_t index_to_visual_simple(size_t *y, size_t *x,const size_t inner_width,const char *str,const size_t len)
{ // Returns number of wraps/newlines in a string, and sets the final offset at y and x
	size_t offset_y = 0,offset_x = 0;
	if(str)
		for(size_t iter = 0; iter < len && str[iter] != '\0'; ++iter)
		{
			if(str[iter] == '\n' || offset_x + 1 >= inner_width)
			{
				offset_y++;
				offset_x = 0;
			}
			else
				offset_x++;
		}
	if(y)
		*y = offset_y; // NOTE: Unlike print_wrapped, we don't expect these to be initialized
	if(x)
		*x = offset_x;
	return offset_y; // Lines wrapped
}

static inline size_t print_wrapped(WINDOW *win,size_t *y,size_t *x,const size_t inner_width,const char *str,const size_t len)
{ // NOTE: y and x must be initialized // TODO eliminate mvwprintw_size
	if(win && y && x && str && len)
	{
		const size_t start_x = *x;
		const size_t start_y = *y;
		for(size_t iter = 0; str[iter] != '\0' && iter < len; iter++)
		{
			if(str[iter] == '\n' || *x > inner_width)
			{
				*y = *y + 1;
				*x = start_x;
				if(str[iter] == '\n')
					continue; // do not print, skip
			}
			mvwaddch(win, (int)*y, (int)*x, (chtype)str[iter]);
			*x = *x + 1;
		}
		error_printf(0,"Checkpoint wraps: %lu",*y - start_y);
		return *y - start_y; // may be 0 if no wraps or newlines occurred during printing
	}
	return 0;
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

static void zero_w(const size_t w)
{
	if(widget && w < torx_allocation_len(widget) / sizeof(struct widget))
	{ // sanity check
		widget[w].text = NULL; // Do not null or free underlying pointer
		widget[w].cursor = NULL; // Do not null or free underlying pointer
		widget[w].callback = NULL;
		widget[w].type = 0;
	}
}

static size_t widget_new(const int type)
{ // Internal function only
	if(widget)
		widget = torx_realloc(widget,torx_allocation_len(widget)+sizeof(struct widget));
	else
		widget = torx_insecure_malloc(sizeof(struct widget));
	const size_t w = torx_allocation_len(widget) / sizeof(struct widget) - 1;
	zero_w(w);
	widget[w].type = type;
	return w;
}

static void widget_clear(size_t *new_focus)
{ // Must call first when drawing a new route, and on shutdown
	#define destroy_window(win) if(win) { delwin(win); win = NULL; }
	destroy_window(window_contacts)
	destroy_window(window_chat)
	destroy_window(window_input)
	destroy_window(window_login)
	if(new_focus)
	{ // We're preparing to draw a new window
		getmaxyx_size(stdscr, &screen_rows, &screen_cols); // 2nd
		current_focus = new_focus;
		widget_set_cursor(0,0); // set to a safe place
		curs_set(0); // set invisible
	}
	const size_t active_widgets = torx_allocation_len(widget) / sizeof(struct widget);
	for(size_t w = 0; w < active_widgets; w++)
		zero_w(w);
	torx_free((void*)&widget);
}

static void widget_draw_cursor(WINDOW *win)
{ // Must call last when drawing a new route, after drawing the last widget, or when re-drawing a text widget alone.
	wmove_size(win, cursor[0], cursor[1]);
	wrefresh(win);
}

static size_t widget_button(WINDOW *win,size_t *y,size_t *x,const size_t inner_width,int (*callback)(const size_t,const int),const char *text)
{ // Draw a button
	const size_t w = widget_new(WIDGET_CHECKBOX);
	widget[w].callback = callback;
	const size_t text_len = text ? strlen(text) : 0;
	if(*current_focus == w)
		wattron(win, A_REVERSE); // highlight on
	print_wrapped(win,y,x,inner_width,text,text_len);
	if(*current_focus == w)
		wattroff(win, A_REVERSE); // highlight off
	return w;
}

static size_t widget_checkbox(WINDOW *win,size_t *y,size_t *x,const size_t inner_width,int (*callback)(const size_t,const int),const uint8_t reversed,const char *text,const uint8_t ticked)
{ // Draw a checkbox button
	const size_t text_len = text ? strlen(text) : 0;
	char array[text_len + 4 + 1];
	if(reversed)
		snprintf(array, sizeof(array), "[%c] %s",ticked ? 'x':' ',text);
	else
		snprintf(array, sizeof(array), "%s [%c]",text,ticked ? 'x':' ');
	const size_t w = widget_button(win,y,x,inner_width,callback,array);
	sodium_memzero(array,sizeof(array));
	return w;
}

static size_t widget_text_entry(WINDOW *win,size_t *y,size_t *x,const size_t inner_width,int (*callback)(const size_t,const int),const int type,char **text_p,size_t *cursor_pos)
{ // Draw a text entry. Single-line SHOULD highlight when selected. Multi-line should NOT highlight when selected.
	if(type != WIDGET_PASSWORD && type != WIDGET_INPUT_SINGLE_LINE && type != WIDGET_INPUT_MULTI_LINE && type != WIDGET_INPUT_NUMERICAL)
	{
		error_simple(-1,"widget_text_entry passed an inappropriate type. UI coding error. Report this.");
		return 0;
	}
	const size_t w = widget_new(type);
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
	print_wrapped(win,y,x,inner_width,array,sizeof(array)-1);
	if(*current_focus == w)
	{
		if(type != WIDGET_INPUT_MULTI_LINE)
			wattroff(win, A_REVERSE); // highlight off
		size_t vrow, vcol;
		index_to_visual_simple(&vrow, &vcol, inner_width - (start_x - 1), *text_p, cursor_pos ? *cursor_pos : 0); // XXX inner_width - (start_x - 1) is critical
		widget_set_cursor(start_y + vrow, start_x + vcol);
		curs_set(1);
	}
	else
		curs_set(0);
	sodium_memzero(array,sizeof(array));
	return w;
}

static int callback_password(const size_t w,const int ch)
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

static int keypress(const size_t w,const int ch)
{ // replace with callback_message_input ?
	if(w >= torx_allocation_len(widget) / sizeof(struct widget) /*|| !widget[w].cursor || !widget[w].text*/)
	{
		error_printf(0,"Keypress called on possibly invalid widget: %lu of %lu",w,torx_allocation_len(widget) / sizeof(struct widget));
		return 0; // Sanity check
	}
error_printf(0,"Checkpoint keypress: %d",ch);
	const size_t inner_w = screen_cols - 2;
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
		*current_focus = (*current_focus + 1) % (torx_allocation_len(widget) / sizeof(struct widget));
error_printf(0,"Checkpoint rebuild current_focus: %lu",*current_focus);
		return 1; // Rebuild
	}
	else if(ch == KEY_UP)
	{
		if(widget[w].type == WIDGET_INPUT_MULTI_LINE)
		{
			const size_t unsent_len = torx_allocation_len(*widget[w].text) ? torx_allocation_len(*widget[w].text) - 1 : 0;
			size_t r,c2;
			index_to_visual_simple(&r, &c2, inner_w, *widget[w].text, *widget[w].cursor);
			if(r == 0)
			{
				*widget[w].cursor = 0;
				return 1;
			}
			size_t best = 0;
			for(size_t iter = 0; iter <= unsent_len; ++iter)
			{
				size_t rr,cc;
				index_to_visual_simple(&rr, &cc, inner_w, *widget[w].text, iter);
				if(rr == r-1)
				{
					best = iter;
					break;
				}
			}
			size_t avail = 0;
			while(best + avail <= unsent_len)
			{
				size_t rr,cc;
				index_to_visual_simple(&rr, &cc, inner_w, *widget[w].text, best + avail);
				if(rr != r-1)
					break;
				avail++;
			}
			if(c2 > avail)
				c2 = avail;
			*widget[w].cursor = best + c2;
		}
		else if(*current_focus > 0)
			*current_focus = *current_focus - 1;
		return 1; // Rebuild
	}
	else if(ch == KEY_DOWN)
	{
		if(widget[w].type == WIDGET_INPUT_MULTI_LINE)
		{
			const size_t unsent_len = torx_allocation_len(*widget[w].text) ? torx_allocation_len(*widget[w].text) - 1 : 0;
			size_t r,c2;
			index_to_visual_simple(&r, &c2, inner_w, *widget[w].text, *widget[w].cursor);
			const size_t total_vis = index_to_visual_simple(NULL,NULL,inner_w,*widget[w].text,torx_allocation_len(*widget[w].text)-1);
			if(r >= total_vis - 1)
			{
				*widget[w].cursor = unsent_len;
				return 1;
			}
			int first = -1;
			for(size_t iter = 0; iter <= unsent_len; ++iter)
			{
				size_t rr,cc;
				index_to_visual_simple(&rr, &cc, inner_w, *widget[w].text, iter);
				if(rr == r+1)
				{
					first = (int)iter;
					break;
				}
			}
			if(first < 0)
			{
				*widget[w].cursor = unsent_len;
				return 1;
			}
			const size_t first_cast = (size_t)first;
			size_t avail = 0;
			while(first_cast + avail <= unsent_len)
			{
				size_t rr,cc;
				index_to_visual_simple(&rr, &cc, inner_w, *widget[w].text, first_cast + avail);
				if(rr != r+1)
					break;
				avail++;
			}
			if(c2 > avail)
				c2 = avail;
			*widget[w].cursor = first_cast + c2;
		}
		else if(*current_focus < torx_allocation_len(widget) / sizeof(struct widget) - 1)
			*current_focus = *current_focus + 1;
		return 1; // Rebuild
	}
	else if(ch == KEY_LEFT)
	{
error_simple(0,"Checkpoint left 1");
		if(window_contacts)
		{
			focus_contacts = list_first_peer_w;
			return 1; // Rebuild
		}
		else if(widget[w].cursor && *widget[w].cursor > 0)
		{
error_simple(0,"Checkpoint left 2");
			*widget[w].cursor = *widget[w].cursor - 1;
error_simple(0,"Checkpoint left 3");
			return 1; // Rebuild
		}
error_simple(0,"Checkpoint left beep");
		beep();
	}
	else if(ch == KEY_RIGHT)
	{
		if(window_contacts)
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

static int callback_censored_region(const size_t w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		uint8_t censored_region_local = threadsafe_read_uint8(&mutex_global_variable,&censored_region);
		if(censored_region_local == 0)
		{
			censored_region_local = 1;
			threadsafe_write(&mutex_global_variable,&censored_region,&censored_region_local,sizeof(censored_region_local));
			sql_setting(1,-1,"censored_region","1",1);
		}
		else if(censored_region_local == 1)
		{
			censored_region_local = 0;
			threadsafe_write(&mutex_global_variable,&censored_region,&censored_region_local,sizeof(censored_region_local));
			sql_setting(1,-1,"censored_region","0",1);
		}
	}
	else
		return 0; // Do not rebuild
	return 1; // Rebuild
}

static int callback_pw_show(const size_t w,const int ch)
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
	print_wrapped(window_login, &fy,&fx,screen_cols-4,text_enter_password,sizeof(text_enter_password)-1);
	char text_password[] = "Password:";
	fy += 2, fx = 2; // fy must be += because there might be wrap
	print_wrapped(window_login,&fy,&fx,screen_cols-fx,text_password,sizeof(text_password)-1);

	fy += 1, fx = 4; // fy must be += because there might be wrap
	widget_text_entry(window_login,&fy,&fx,screen_cols-fx,callback_password,WIDGET_PASSWORD,&password,&pw_cursor);

	fy += 2,fx = 4; // fy must be += because there might be wrap
	widget_checkbox(window_login,&fy,&fx,screen_cols-fx,callback_pw_show,1,"Show Password",pw_show);

	fy += 1, fx = 4;
	widget_checkbox(window_login,&fy,&fx,screen_cols-fx,callback_censored_region,1,"Censored Region",threadsafe_read_uint8(&mutex_global_variable,&censored_region));

	const char text_password_help[] = "Tab: cycle focus  Up/Down: move focus  Enter: proceed  Esc/Home: quit";
	fy = screen_rows-2, fx = 2;
	print_wrapped(window_login, &fy, &fx, screen_cols-fx, text_password_help, sizeof(text_password_help)-1);

	widget_draw_cursor(window_login); // XXX Must do last
}

static int callback_contacts_groups(const size_t w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		groups_mode = !groups_mode;
		list_first_peer_w = 0;
	}
	else
		return 0;
	return 1;
}

static int callback_peer(const size_t w,const int ch)
{
	(void)w;
	if(ch == '\n' || ch == KEY_ENTER || ch =='\r' || ch == ' ')
	{
		global_n = selected_n;
		t_peer[global_n].unread = 0;
		chat_scroll_lines = 0;
		draw_chat(global_n);
	}
	return 0;
}

static void draw_contacts(void)
{ // Contact List Route
	widget_clear(&focus_contacts); // XXX Must do first
	window_contacts = newwin_size(screen_rows, screen_cols, 0, 0);
	box(window_contacts,0,0); // Draw border
	if(groups_mode)
		mvwprintw_size(window_contacts,0,2," Groups ");
	else
		mvwprintw_size(window_contacts,0,2," Contacts ");

	const char *groups_label = groups_mode ? "[ Contacts ]" : "[ Groups ]";
	size_t fy = 0,fx = screen_cols - strlen(groups_label) - 3;
	widget_button(window_contacts,&fy,&fx,screen_cols-2,callback_contacts_groups,groups_label);

	const char settings_label[] = "[ Settings ]";
	fy += 1,fx = screen_cols - (sizeof(settings_label) - 1) - 3;
	widget_button(window_contacts,&fy,&fx,screen_cols-2,NULL,settings_label); // TODO set a callback

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
			const size_t w = widget_button(window_contacts,&fy,&fx,screen_cols-2,callback_peer,label);
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
	print_wrapped(window_contacts, &fy, &fx, screen_cols-fx, text_list_help, sizeof(text_list_help)-1);

	widget_draw_cursor(window_contacts); // XXX Must do last
}

static inline size_t print_message(int *visual_idx,size_t *draw_y,const uint8_t owner,const int n,const int i,const int first_line_index,const int bottom_line_index)
{
	size_t lines_printed = 0;
	const int p_iter = getter_int(n,i,-1,offsetof(struct message_list,p_iter));
	if(p_iter < 0)
		return lines_printed;
	pthread_rwlock_rdlock(&mutex_protocols); // 🟧
	const uint8_t utf8 = protocols[p_iter].utf8;
	const uint8_t group_pm = protocols[p_iter].group_pm;
	const uint32_t null_terminated_len = protocols[p_iter].null_terminated_len;
	const uint32_t date_len = protocols[p_iter].date_len;
	const uint32_t signature_len = protocols[p_iter].signature_len;
	pthread_rwlock_unlock(&mutex_protocols); // 🟩
	if(owner == ENUM_OWNER_GROUP_PEER)
	{
		const uint8_t stat = getter_uint8(n,i,-1,offsetof(struct message_list,stat));
		if(!group_pm && stat != ENUM_MESSAGE_RECV)
			return lines_printed; // Do not print OUTBOUND messages on GROUP_PEER unless they are private
		const uint8_t status = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,status));
		if(stat == ENUM_MESSAGE_RECV && (t_peer[n].mute || status == ENUM_STATUS_BLOCKED))
			return lines_printed; // Do not print inbound messages from muted (ignored) or blocked group peers
	}
	const size_t inner_w = screen_cols - 2;
	if(utf8 && null_terminated_len)
	{
		char *message = getter_string(n,i,-1,offsetof(struct message_list,message));
		if(!message) // this would be a bug?
			return lines_printed;
		if(*visual_idx >= first_line_index && *visual_idx <= bottom_line_index)
		{ // we only ACTUALLY print it if it is within the visual zone
			size_t start_x = 1;
			lines_printed += 1 + print_wrapped(window_chat,draw_y,&start_x,inner_w,message,torx_allocation_len(message) - null_terminated_len - date_len - signature_len);
			*draw_y = *draw_y + 1;
		}
		*visual_idx = *visual_idx + 1;
		torx_free((void*)&message);
	}
	return lines_printed;
}

static inline size_t get_lines(const int n,const int i,const size_t inner_w)
{ // highly inefficient, should use getter_length (NO, does not account for newlines) or cache length (ALSO NO, doesn't account for window size changes)
	size_t lines = 0;
	const int p_iter = getter_int(n,i,-1,offsetof(struct message_list,p_iter));
	if(p_iter < 0)
		return lines;
	pthread_rwlock_rdlock(&mutex_protocols); // 🟧
	const uint8_t utf8 = protocols[p_iter].utf8;
	const uint32_t null_terminated_len = protocols[p_iter].null_terminated_len;
	const uint32_t date_len = protocols[p_iter].date_len;
	const uint32_t signature_len = protocols[p_iter].signature_len;
	pthread_rwlock_unlock(&mutex_protocols); // 🟩
	if(utf8 && null_terminated_len)
	{
		char *message = getter_string(n,i,-1,offsetof(struct message_list,message));
		lines = 1 + index_to_visual_simple(NULL,NULL,inner_w,message,torx_allocation_len(message) - null_terminated_len - date_len - signature_len);
		torx_free((void*)&message);
	}
	return lines;
}

static int callback_message_input(const size_t w,const int ch)
{
	(void)w;
	const int n = global_n;
	const size_t inner_w = screen_cols - 2;
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
	else if(ch == KEY_PPAGE)
	{ // PgUp
		size_t mh;
		getmaxyx_size(window_chat, &mh, &screen_cols);
		size_t inner_h = mh - minimum_size_vertical;
		if(inner_h < 1)
			inner_h = 1;
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
		size_t total_visual_lines = 0;
		if(msg_count)
		{
			if(owner == ENUM_OWNER_GROUP_CTRL)
				while(page)
				{
					total_visual_lines += get_lines(page->n,page->i,inner_w);
					page = page->message_next;
				}
			else
				for(int i = min_i; i <= max_i; i++)
					total_visual_lines += get_lines(n,i,inner_w);
		}
		else
			total_visual_lines = 1;
		const size_t max_scroll = (total_visual_lines > inner_h) ? (total_visual_lines - inner_h) : 0;
		if(chat_scroll_lines < max_scroll)
		{
			chat_scroll_lines = chat_scroll_lines + inner_h;
			if(chat_scroll_lines > max_scroll)
				chat_scroll_lines = max_scroll;
		}
	}
	else if(ch == KEY_NPAGE)
	{ // PgDn
		size_t mh;
		getmaxyx_size(window_chat, &mh, &screen_cols);
		size_t inner_h = mh - minimum_size_vertical;
		if(inner_h < 1)
			inner_h = 1;
		if(chat_scroll_lines >= inner_h)
			chat_scroll_lines -= inner_h;
		else
			chat_scroll_lines = 0;
	}
	else if(ch == KEY_END)
		chat_scroll_lines = 0;
	else
		return 0;
	return 1;
}

static void draw_chat(const int n)
{ // Chat Route
	if(n < 0)
	{
		error_simple(0,"draw_chat called on invalid n. UI coding error. Report this.");
		return; // Bug
	}
	widget_clear(&focus_chat); // XXX Must do first

	size_t input_h = 3;
	size_t msgs_h = screen_rows - input_h;
	if(msgs_h < 3)
	{
		msgs_h = 3;
		input_h = screen_rows - msgs_h;
	}
	window_chat = newwin_size(msgs_h, screen_cols, 0, 0);
	window_input = newwin_size(input_h, screen_cols, msgs_h, 0);

	box(window_chat,0,0); // Draw border

	getmaxyx_size(stdscr, &screen_rows, &screen_cols); // necessary
	const size_t inner_w = screen_cols - 2;
	size_t cur_row = 0, cur_col = 0;
	size_t visual_lines = 1 + index_to_visual_simple(&cur_row, &cur_col, inner_w, t_peer[n].unsent, torx_allocation_len(t_peer[n].unsent)); // alt: t_peer[n].unsent_pos
	size_t max_input_lines = screen_rows - 5;
	if(screen_rows < 6)
		max_input_lines = 1;
	if(visual_lines > max_input_lines)
		visual_lines = max_input_lines;
	size_t desired_input_h = visual_lines + 2;
	if(desired_input_h < 3)
		desired_input_h = 3;
	msgs_h = screen_rows - desired_input_h;
	size_t cur_msgs_h = 0, cur_msgs_w = 0;
	if(window_chat)
		getmaxyx_size(window_chat, &cur_msgs_h, &cur_msgs_w);
	if(!window_chat || cur_msgs_h != msgs_h || cur_msgs_w != screen_cols)
	{ // Grow message input box
		if(window_chat)
			delwin(window_chat);
		if(window_input)
			delwin(window_input);
		window_chat = newwin_size(msgs_h, screen_cols, 0, 0);
		window_input = newwin_size(desired_input_h, screen_cols, msgs_h, 0);
	}

	#define TOP_LINE_HEIGHT 1

	if(!t_peer[n].unsent)
	{ // Necessary
		t_peer[n].unsent = torx_secure_malloc(1);
		t_peer[n].unsent[0] = '\0';
		t_peer[n].unsent_pos = 0;
	}
	else if(t_peer[n].unsent_pos >= torx_allocation_len(t_peer[n].unsent))
		t_peer[n].unsent_pos = torx_allocation_len(t_peer[n].unsent) - 1;
	char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
	mvwprintw_size(window_chat,0,2,"%s",peernick);
	torx_free((void*)&peernick);
	const size_t inner_h = screen_rows - minimum_size_vertical;
	int total_visual_lines = 0;
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
	if(msg_count)
	{ // one or more messages exist
		if(owner == ENUM_OWNER_GROUP_CTRL)
			while(page)
			{
				total_visual_lines += (int)get_lines(page->n,page->i,inner_w);
				page = page->message_next;
			}
		else
			for(int i = min_i; i <= max_i; i++)
				total_visual_lines += (int)get_lines(n,i,inner_w);
	}
	else
		total_visual_lines = 1;
	const size_t max_scroll = ((size_t)total_visual_lines > inner_h) ? ((size_t)total_visual_lines - inner_h) : 0;
	if(chat_scroll_lines > max_scroll)
	{
error_printf(0,"Checkpoint triggered: %lu > %lu",chat_scroll_lines,max_scroll);
		chat_scroll_lines = max_scroll;
	}
size_t actual = 0;
	if(msg_count)
	{
		const int bottom_line_index = total_visual_lines - TOP_LINE_HEIGHT - (int)chat_scroll_lines; // MUST BE INT
		const int first_line_index = bottom_line_index - ((int)inner_h - 1); // MUST BE INT
error_printf(0,"Checkpoint csl=%lu tvl=%lu ms=%lu bli=%d fli=%d",chat_scroll_lines,total_visual_lines,max_scroll,bottom_line_index,first_line_index);
		int visual_idx = 0;
		size_t draw_y = TOP_LINE_HEIGHT;
		if(owner == ENUM_OWNER_GROUP_CTRL)
		{
			pthread_rwlock_rdlock(&mutex_expand_group); // 🟧
			page = group[g].msg_first;
			pthread_rwlock_unlock(&mutex_expand_group); // 🟩
			for(; page; page = page->message_next)
				actual += print_message(&visual_idx,&draw_y,owner,page->n,page->i,first_line_index,bottom_line_index);
		}
		else
			for(int i = min_i; i <= max_i; i++)
				actual += print_message(&visual_idx,&draw_y,owner,n,i,first_line_index,bottom_line_index);
	}
error_printf(0,"Checkpoint anticipated=%lu actual=%lu",total_visual_lines,actual);
	const char btn1[] = "[ Settings ]";
	const char btn2[] = "[ Actions ]";
	const size_t b1len = sizeof(btn1) - 1;
	const size_t b2len = sizeof(btn2) - 1;
	const size_t b2x = screen_cols - b2len - 3;
	const size_t b1x = b2x - b1len - 1;
	if(focus_chat == 1)
		wattron(window_chat, A_REVERSE);
	mvwprintw_size(window_chat, 0, b1x, "%s", btn1);
	if(focus_chat == 1)
		wattroff(window_chat, A_REVERSE);
	if(focus_chat == 2)
		wattron(window_chat, A_REVERSE);
	mvwprintw_size(window_chat, 0, b2x, "%s", btn2);
	if(focus_chat == 2)
		wattroff(window_chat, A_REVERSE);
	wrefresh(window_chat);

	box(window_input, 0, 0); // Draw border
	const char hint[] = " Type message (Enter to send at end, Esc/Home: back, PgUp/PgDn: scroll) ";
	mvwprintw_size(window_input, 0, 2, "%.*s",(int)screen_cols - 4, hint); // do not wrap


		const size_t unsent_len = torx_allocation_len(t_peer[n].unsent) ? torx_allocation_len(t_peer[n].unsent) - 1 : 0;
		size_t start_x = 1,row = 1;
		print_wrapped(window_input,&row,&start_x,inner_w,t_peer[n].unsent,unsent_len);

		// cursor visibility
		if(focus_chat != 0)
		{
			curs_set(0);
			wrefresh(window_input);
		}
		else
		{
			size_t vrow, vcol;
			index_to_visual_simple(&vrow, &vcol, inner_w,t_peer[n].unsent, t_peer[n].unsent_pos);
			if(vrow >= visual_lines)
				vrow = visual_lines - 1;
			size_t cursy = 1 + vrow;
			size_t cursx = 1 + vcol;
			size_t input_h_cur;
			getmaxyx_size(window_input, &input_h_cur, &screen_cols);
			if(cursy >= input_h_cur)
				cursy = input_h_cur - 1;
			wmove_size(window_input, cursy, cursx);
			curs_set(1);
		}
		wrefresh(window_input);


	widget_draw_cursor(window_input); // XXX Must do last
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
