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

#define CLIENT_VERSION "TorX-Ncurses Alpha 2.0.37 2025/11/26 by TorX\n© Copyright 2025 TorX.\n"

static struct t_peer_list {
	char *unsent;
	size_t unsent_pos; // cursor position
	size_t unread;
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

enum {
	KEY_DELETE = 330,
	KEY_ESC = 27
};

enum {
	PAGE_PASSWORD,
	PAGE_CONTACTS,
	PAGE_CHAT,
	PAGE_SETTINGS
};

static void signal_resize(int sig);
static void create_windows(void);
static void draw_password(void);
static void draw_list(void);
static void draw_chat(const int n);
static int await_key_or_signal(WINDOW *win);
void async_notifier(void);
static int chat_input_loop(const int n);
static void draw_settings(void);
static int settings_loop(void);
static int settings_chat_loop(const int n);
static int actions_chat_loop(const int n);

static WINDOW *main_win = NULL, *list_win = NULL, *chat_msgs_win = NULL, *chat_input_win = NULL, *pw_win = NULL, *settings_win = NULL;

static int selected_iter = 0; // internal use only
static int selected_n = 0; // internal use only
static int global_n = -1;
static volatile sig_atomic_t resized = 0;
static volatile sig_atomic_t resize_seq = 0;

static bool running = true; // set to false to exit
static int sig_num = 0;

static size_t screen_rows = 24, screen_cols = 80;

static int notify_fds[2] = { -1, -1 };

static int window = PAGE_PASSWORD; // must start at PAGE_PASSWORD
static size_t chat_scroll_lines = 0;

/* Password window state */
static char *password = NULL;
static bool pw_show = false; // default false
static bool pw_like_cats = false; // default false
static int pw_focus = 0; // 0=password field,1=show,2=cats
static size_t pw_cursor = 0;

/* Contact list state */
static bool groups_mode = false;
static int list_focus = 0; // 0=list,1=groups button,2=settings
static int chat_btn_focus = 0; // 0=input,1=settings,2=actions

/* Settings window state */
#define SETTINGS_COUNT 4
static bool settings_toggle[SETTINGS_COUNT] = { false, true, false, false };
static char settings_text[128] = {0};
static size_t settings_text_len = 0;
static size_t settings_focus_idx = 0;

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
{ // Strip out ability to return -1 or values below our minimum_size_*
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

static void destroy_windows(void)
{
	#define destroy_window(window) if(window) { delwin(window); window = NULL; }
	destroy_window(main_win)
	destroy_window(list_win)
	destroy_window(chat_msgs_win)
	destroy_window(chat_input_win)
	destroy_window(pw_win)
	destroy_window(settings_win)
}

static void create_windows(void)
{
	destroy_windows();
	getmaxyx_size(stdscr, &screen_rows, &screen_cols);
	main_win = newwin_size(screen_rows, screen_cols, 0, 0);

	if(window == PAGE_PASSWORD)
	{
		size_t h = 11, w = screen_cols - 4, y = (screen_rows - h) / 2, x = 2;
		if(h >= screen_rows)
		{
			h = screen_rows;
			y = 0;
		}
		pw_win = newwin_size(h, w, y, x);
		keypad(pw_win, TRUE);
	}
	else if(window == PAGE_CONTACTS)
	{
		list_win = newwin_size(screen_rows, screen_cols, 0, 0);
		keypad(list_win, TRUE);
	}
	else if(window == PAGE_SETTINGS)
	{
		const size_t h = screen_rows - 4, w = screen_cols - 8, y = 2, x = 4;
		settings_win = newwin_size(h, w, y, x);
		keypad(settings_win, TRUE);
	}
	else
	{
		size_t input_h = 3;
		size_t msgs_h = screen_rows - input_h;
		if(msgs_h < 3)
		{
			msgs_h = 3;
			input_h = screen_rows - msgs_h;
		}
		chat_msgs_win = newwin_size(msgs_h, screen_cols, 0, 0);
		chat_input_win = newwin_size(input_h, screen_cols, msgs_h, 0);
		keypad(chat_input_win, TRUE);
		keypad(chat_msgs_win, TRUE);
	}
	if(window == PAGE_CHAT)
		curs_set(1);
	else
		curs_set(0);
	// draw active window
	if(window == PAGE_PASSWORD)
		draw_password();
	else if(window == PAGE_CONTACTS)
		draw_list();
	else if(window == PAGE_SETTINGS)
		draw_settings();
//	else if(window == PAGE_CHAT)
//		draw_chat(global_n); // XXX Currently must be called only by chat_input_loop.
}

/* static void widget_checkbox(text,toggle_function,arg)
{ // Should have a global_index, and each new widget should be registered globally with its type, callback function, and arg?

} */

static void draw_password(void)
{ // Password Route
	if(!pw_win)
	{
		error_simple(0,"pw_win does not exist. UI Coding error. Report this.");
		return;
	}
	werase(pw_win);
	box(pw_win,0,0);
	getmaxyx_size(pw_win, &screen_rows, &screen_cols);
	size_t fy = 0, fx = 2;
	char text_enter_password[] = " Enter password ";
	print_wrapped(pw_win, &fy,&fx,screen_cols-4,text_enter_password,sizeof(text_enter_password)-1);
	char text_password[] = "Password:";
	fy += 2, fx = 2; // fy must be += because there might be wrap
	print_wrapped(pw_win,&fy,&fx,screen_cols-2,text_password,sizeof(text_password)-1);
	size_t maxfw = screen_cols - fx - 3;
	if(maxfw < 1)
		maxfw = 1;
	char shown[256]; // zero'd
	if(pw_show && password)
		snprintf(shown, sizeof(shown), "%s", password);
	else
	{
		size_t L = password ? torx_allocation_len(password) - 1 : 0;
		if(L > sizeof(shown)-1)
			L = sizeof(shown)-1;
		for(size_t iter = 0; iter < L; ++iter)
			shown[iter] = '*';
		shown[L] = '\0';
	}
	fy += 1, fx = 4; // fy must be += because there might be wrap
	print_wrapped(pw_win,&fy,&fx,screen_cols-2,shown,maxfw);
	sodium_memzero(shown,sizeof(shown));
	size_t lab_w = screen_cols - 10;
	if(lab_w < 10)
		lab_w = 10;
	char cb1[64];
	snprintf(cb1, sizeof(cb1), "[%c] Show/Hide password", pw_show ? 'x':' ');
	char cb2[64];
	snprintf(cb2, sizeof(cb2), "[%c] I like cats", pw_like_cats ? 'x':' ');
	if(pw_focus == 1)
		wattron(pw_win, A_REVERSE);
	fy += 2,fx = 4; // fy must be += because there might be wrap
	print_wrapped(pw_win, &fy, &fx, lab_w, cb1,strlen(cb1));
	if(pw_focus == 1)
		wattroff(pw_win, A_REVERSE);
	if(pw_focus == 2)
		wattron(pw_win, A_REVERSE);
	fy += 1, fx = 4;
	print_wrapped(pw_win, &fy, &fx, lab_w, cb2, strlen(cb2));
	if(pw_focus == 2)
		wattroff(pw_win, A_REVERSE);
	char text_password_help[] = "Tab: cycle focus  Up/Down: move focus  Enter: proceed  Esc/Home: quit";
	fy = screen_rows-2, fx = 2;
	print_wrapped(pw_win, &fy, &fx, screen_cols-2, text_password_help, sizeof(text_password_help)-1);
	if(password && pw_cursor + 1 > torx_allocation_len(password))
		pw_cursor = torx_allocation_len(password) - 1;
	size_t disp = pw_cursor;
	if(disp > maxfw - 1)
		disp = maxfw - 1;
	if(pw_focus == 0)
	{
		wmove_size(pw_win, 3, 4 + disp);
		curs_set(1);
	}
	else
		curs_set(0);
	wrefresh(pw_win);
}

static void draw_list(void)
{ // Contact List Route
	if(!list_win)
	{
		error_simple(0,"list_win does not exist. UI Coding error. Report this.");
		return;
	}
	werase(list_win);
	box(list_win,0,0);
	if(!groups_mode)
		mvwprintw_size(list_win,0,2," Contacts ");
	else
		mvwprintw_size(list_win,0,2," Groups ");
	const char *groups_label = groups_mode ? "[ Contacts ]" : "[ Groups ]";
	const char settings_label[] = "[ Settings ]";
	const size_t gx = screen_cols - strlen(groups_label) - 3;
	const size_t sx = screen_cols - sizeof(settings_label) - 1 - 3;
	if(list_focus == 1)
		wattron(list_win,A_REVERSE);
	mvwprintw_size(list_win,0,gx,"%s",groups_label);
	if(list_focus == 1)
		wattroff(list_win,A_REVERSE);
	if(list_focus == 2)
		wattron(list_win,A_REVERSE);
	mvwprintw_size(list_win,1,sx,"%s",settings_label);
	if(list_focus == 2)
		wattroff(list_win,A_REVERSE);

	int len = 0;
	int *array;
	if(groups_mode)
		array = refined_list(&len,ENUM_OWNER_GROUP_CTRL,ENUM_STATUS_FRIEND,NULL);
	else
		array = refined_list(&len,ENUM_OWNER_CTRL,ENUM_STATUS_FRIEND,NULL);
	if(len)
	{
		if(selected_iter < 0)
			selected_iter = 0;
		if(selected_iter >= len)
			selected_iter = len-1;
		for(size_t pos = 0; pos < (size_t)len; ++pos)
		{
			const int n = array[pos];
			const size_t y = 2 + pos;
			if(y >= screen_rows - 1) // TODO we don't have scrolling? It just cuts off?
				break;
			const uint8_t sendfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,sendfd_connected));
			const uint8_t recvfd_connected = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,recvfd_connected));
			char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
			char label[256]; // zero'd
			if(t_peer[n].unread > 0)
				snprintf(label, sizeof(label), "(%lu) %s", t_peer[n].unread, peernick);
			else
				snprintf(label, sizeof(label), "%s", peernick);
			if(list_focus == 0 && pos == (size_t)selected_iter)
			{
				wattron(list_win,A_REVERSE);
				selected_n = array[pos];
			}
			if(sendfd_connected || recvfd_connected)
			{
				wattron(list_win,A_BOLD);
				mvwprintw_size(list_win,y,2,"* %s", label);
				wattroff(list_win,A_BOLD);
			}
			else
				mvwprintw_size(list_win,y,2,"%s", label);
			if(list_focus == 0 && pos == (size_t)selected_iter)
				wattroff(list_win,A_REVERSE);
			sodium_memzero(label,sizeof(label));
			torx_free((void*)&peernick);
		}
		torx_free((void*)&array);
	}
	mvwprintw_size(list_win, screen_rows-1, 2, "Up/Down: select  Enter/Space: open  Esc/Home: quit  Tab: cycle focus");
	wrefresh(list_win);
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
			lines_printed += 1 + print_wrapped(chat_msgs_win,draw_y,&start_x,inner_w,message,torx_allocation_len(message) - null_terminated_len - date_len - signature_len);
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

static void draw_chat(const int n)
{ // Chat Route. XXX Currently must be called only by chat_input_loop.
	#define TOP_LINE_HEIGHT 1
	if(!chat_msgs_win)
	{
		error_simple(0,"chat_msgs_win does not exist. UI Coding error. Report this.");
		return;
	}
	werase(chat_msgs_win);
	box(chat_msgs_win,0,0);
	if(!t_peer[n].unsent)
	{ // Necessary
		t_peer[n].unsent = torx_secure_malloc(1);
		t_peer[n].unsent[0] = '\0';
		t_peer[n].unsent_pos = 0;
	}
	else if(t_peer[n].unsent_pos >= torx_allocation_len(t_peer[n].unsent))
		t_peer[n].unsent_pos = torx_allocation_len(t_peer[n].unsent) - 1;
	char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
	mvwprintw_size(chat_msgs_win,0,2,"%s",peernick);
	torx_free((void*)&peernick);
	getmaxyx_size(chat_msgs_win, &screen_rows, &screen_cols); // necessary
	const size_t inner_h = screen_rows - minimum_size_vertical;
	const size_t inner_w = screen_cols - 2;
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
	if(chat_btn_focus == 1)
		wattron(chat_msgs_win, A_REVERSE);
	mvwprintw_size(chat_msgs_win, 0, b1x, "%s", btn1);
	if(chat_btn_focus == 1)
		wattroff(chat_msgs_win, A_REVERSE);
	if(chat_btn_focus == 2)
		wattron(chat_msgs_win, A_REVERSE);
	mvwprintw_size(chat_msgs_win, 0, b2x, "%s", btn2);
	if(chat_btn_focus == 2)
		wattroff(chat_msgs_win, A_REVERSE);
	wrefresh(chat_msgs_win);

	werase(chat_input_win);
	box(chat_input_win, 0, 0);
	const char hint[] = " Type message (Enter to send at end, Esc/Home: back, PgUp/PgDn: scroll) ";
	mvwprintw_size(chat_input_win, 0, 2, "%.*s",(int)screen_cols - 4, hint); // do not wrap
}

static int await_key_or_signal(WINDOW *win)
{ // Blocks on select(), awaiting keypress or callback.
	fd_set rfds;
	int stdin_fd = fileno(stdin);
	int notify_rd = notify_fds[0];
	if(stdin_fd < 0)
		stdin_fd = -1;
	if(notify_rd < 0)
		notify_rd = -1;
	while(1)
	{
		if(resized)
			return -1;
		FD_ZERO(&rfds);
		if(stdin_fd >= 0)
			FD_SET(stdin_fd, &rfds);
		if(notify_rd >= 0)
			FD_SET(notify_rd, &rfds);
		const int maxfd = (stdin_fd > notify_rd) ? stdin_fd : notify_rd;
		if(select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0)
		{
			if(errno == EINTR)
				continue;
			error_printf(0, "select() failed: %s", strerror(errno));
			return -1;
		}
		else if(notify_rd >= 0 && FD_ISSET(notify_rd, &rfds))
		{ // One or more callbacks are ready, must drain the pipe then return -2
			char buf[128];
			ssize_t r;
			do {
				r = read(notify_fds[0], buf, sizeof(buf));
			} while(r > 0 || (r < 0 && errno == EINTR));
			uint8_t must_redraw_ui = 0; // use this sparingly, only when necessary to do a full re-draw
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
					if(window == PAGE_CONTACTS && ((owner == ENUM_OWNER_GROUP_CTRL && groups_mode) || (owner == ENUM_OWNER_CTRL && !groups_mode)))
						must_redraw_ui = 1; // alt: draw_list();
				}
				else if(cb_page->cb_type == ENUM_PEER_ONLINE)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
					if(window == PAGE_CONTACTS && owner == ENUM_OWNER_CTRL && !groups_mode)
						must_redraw_ui = 1; // alt: draw_list();
				}
				else if(cb_page->cb_type == ENUM_PEER_OFFLINE)
				{
					const int n = cb_page->cb_args->mem_int_a;
					const uint8_t owner = getter_uint8(n,INT_MIN,-1,offsetof(struct peer_list,owner));
					if(window == PAGE_CONTACTS && owner == ENUM_OWNER_CTRL && !groups_mode)
						must_redraw_ui = 1; // alt: draw_list();
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
						if(global_n == n || global_n == group_n)
							must_redraw_ui = 1; // better than draw_chat(global_n); // NOT n or this could draw a PM chat
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
					if(value == 0)
					{ // Correct password
						window = PAGE_CONTACTS;
						create_windows();
					}
					else
					{ // Wrong password
						beep();
					//	draw_password();
					}
					must_redraw_ui = 1; // necessary when inputting correct password, must leave this loop
				}
				else if(cb_page->cb_type == ENUM_PEER_LOADED)
				{
					error_simple(0,"Checkpoint ENUM_PEER_LOADED"); // TODO
				}
				else if(cb_page->cb_type == ENUM_CLEANUP)
				{
					running = false;
					sig_num = cb_page->cb_args->mem_int_a;
					must_redraw_ui = 1; // necessary
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
				return -2;
		}
		else if(stdin_fd >= 0 && FD_ISSET(stdin_fd, &rfds))
		{ // Keyboard input is ready
			int ch = wgetch(win);
			if(ch == ERR)
			{
				int attempts = 3;
				while(attempts-- > 0 && ch == ERR)
					ch = wgetch(win);
			}
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

static int chat_input_loop(const int n)
{
	chat_btn_focus = 0; // reset
	for(;;)
	{ // TODO there is no way to scroll input window, and it gets weird when it grows too big
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
		const size_t msgs_h = screen_rows - desired_input_h;
		size_t cur_msgs_h = 0, cur_msgs_w = 0;
		if(chat_msgs_win)
			getmaxyx_size(chat_msgs_win, &cur_msgs_h, &cur_msgs_w);
		if(!chat_msgs_win || cur_msgs_h != msgs_h || cur_msgs_w != screen_cols)
		{ // Grow message input box
			if(chat_msgs_win)
				delwin(chat_msgs_win);
			if(chat_input_win)
				delwin(chat_input_win);
			chat_msgs_win = newwin_size(msgs_h, screen_cols, 0, 0);
			chat_input_win = newwin_size(desired_input_h, screen_cols, msgs_h, 0);
			keypad(chat_input_win, TRUE);
			keypad(chat_msgs_win, TRUE);
		}
// error_printf(0,"Checkpoint content_len=%lu inner_w=%lu visual_lines=%lu cur_row=%lu cur_col=%lu max_input_lines=%lu desired_input_h=%lu msgs_h=%lu cur_msgs_h=%lu cur_msgs_w=%lu",torx_allocation_len(t_peer[n].unsent) - 1 ,inner_w,visual_lines,cur_row,cur_col,max_input_lines,desired_input_h,msgs_h,cur_msgs_h,cur_msgs_w);
		draw_chat(n);

		const size_t unsent_len = torx_allocation_len(t_peer[n].unsent) ? torx_allocation_len(t_peer[n].unsent) - 1 : 0;
		size_t start_x = 1,row = 1;
		print_wrapped(chat_input_win,&row,&start_x,inner_w,t_peer[n].unsent,unsent_len);

		// cursor visibility
		if(chat_btn_focus != 0)
		{
			curs_set(0);
			wrefresh(chat_input_win);
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
			getmaxyx_size(chat_input_win, &input_h_cur, &screen_cols);
			if(cursy >= input_h_cur)
				cursy = input_h_cur - 1;
			wmove_size(chat_input_win, cursy, cursx);
			curs_set(1);
		}
		wrefresh(chat_input_win);

		const int ch = await_key_or_signal(chat_input_win);
		if(ch == -1 && resized)
			return 0; // handle at main loop
		else if(ch == -2)
			continue; // redraw
		else if(ch == ERR || ch < 0)
			continue;
		else if(ch == '\t')
		{
			chat_btn_focus = (chat_btn_focus + 1) % 3;
			continue;
		}
		else if(chat_btn_focus == 1 || chat_btn_focus == 2)
		{
			if(ch == KEY_LEFT || ch == KEY_RIGHT)
				chat_btn_focus = 3 - chat_btn_focus;
			else if(ch == KEY_UP || ch == KEY_DOWN)
				chat_btn_focus = 0;
			else if(ch == KEY_ESC || ch == KEY_HOME)
				return 1; /* return to contacts */
			else if(ch == '\n' || ch == KEY_ENTER || ch == ' ')
			{
				if(chat_btn_focus == 1)
				{
					if(settings_chat_loop(n) == -2)
						return 0; /* closed due to resize */
				}
				else if(actions_chat_loop(n) == -2)
					return 0;
			}
			continue;
		}
		else if(ch == '\n' || ch == '\r')
		{
			if(!unsent_len)
				continue; // ignore it
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
		else if(ch == KEY_DELETE)
		{
			if(t_peer[n].unsent_pos < unsent_len)
			{
				const size_t rem = unsent_len - t_peer[n].unsent_pos;
				memmove(&t_peer[n].unsent[t_peer[n].unsent_pos], &t_peer[n].unsent[t_peer[n].unsent_pos+1], rem);
				t_peer[n].unsent = torx_realloc(t_peer[n].unsent, torx_allocation_len(t_peer[n].unsent) - 1);
			}
			else
				beep();
		}
		else if(ch == KEY_BACKSPACE || ch == 127 || ch == 8)
		{
			if(t_peer[n].unsent_pos > 0)
			{
				const size_t rem = unsent_len - t_peer[n].unsent_pos;
				memmove(&t_peer[n].unsent[t_peer[n].unsent_pos-1], &t_peer[n].unsent[t_peer[n].unsent_pos], rem + 1);
				t_peer[n].unsent = torx_realloc(t_peer[n].unsent, torx_allocation_len(t_peer[n].unsent) - 1);
				t_peer[n].unsent_pos--;
			}
			else
				beep();
		}
		else if(ch == KEY_ESC || ch == KEY_HOME)
			return 1;
		else if(ch == KEY_PPAGE)
		{ // PgUp
			size_t mh;
			getmaxyx_size(chat_msgs_win, &mh, &screen_cols);
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
			getmaxyx_size(chat_msgs_win, &mh, &screen_cols);
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
		else if(ch == KEY_LEFT)
		{
			if(t_peer[n].unsent_pos > 0)
				t_peer[n].unsent_pos--;
			else
				beep();
		}
		else if(ch == KEY_RIGHT)
		{
			if(t_peer[n].unsent_pos < unsent_len)
				t_peer[n].unsent_pos++;
		}
		else if(ch == KEY_UP)
		{
			size_t r,c2;
			index_to_visual_simple(&r, &c2, inner_w, t_peer[n].unsent, t_peer[n].unsent_pos);
			if(r == 0)
			{
				t_peer[n].unsent_pos = 0;
				continue;
			}
			size_t best = 0;
			for(size_t iter = 0; iter <= unsent_len; ++iter)
			{
				size_t rr,cc;
				index_to_visual_simple(&rr, &cc, inner_w, t_peer[n].unsent, iter);
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
				index_to_visual_simple(&rr, &cc, inner_w, t_peer[n].unsent, best + avail);
				if(rr != r-1)
					break;
				avail++;
			}
			if(c2 > avail)
				c2 = avail;
			t_peer[n].unsent_pos = best + c2;
		}
		else if(ch == KEY_DOWN)
		{
			size_t r,c2;
			index_to_visual_simple(&r, &c2, inner_w, t_peer[n].unsent, t_peer[n].unsent_pos);
			const size_t total_vis = index_to_visual_simple(NULL,NULL,inner_w,t_peer[n].unsent,torx_allocation_len(t_peer[n].unsent)-1);
			if(r >= total_vis - 1)
			{
				t_peer[n].unsent_pos = unsent_len;
				continue;
			}
			int first = -1;
			for(size_t iter = 0; iter <= unsent_len; ++iter)
			{
				size_t rr,cc;
				index_to_visual_simple(&rr, &cc, inner_w, t_peer[n].unsent, iter);
				if(rr == r+1)
				{
					first = (int)iter;
					break;
				}
			}
			if(first < 0)
			{
				t_peer[n].unsent_pos = unsent_len;
				continue;
			}
			const size_t first_cast = (size_t)first;
			size_t avail = 0;
			while(first_cast + avail <= unsent_len)
			{
				size_t rr,cc;
				index_to_visual_simple(&rr, &cc, inner_w, t_peer[n].unsent, first_cast + avail);
				if(rr != r+1)
					break;
				avail++;
			}
			if(c2 > avail)
				c2 = avail;
			t_peer[n].unsent_pos = first_cast + c2;
		}
		else if(ch >= 32 && ch <= 126)
		{
			t_peer[n].unsent = torx_realloc(t_peer[n].unsent, torx_allocation_len(t_peer[n].unsent) + 1);
			const size_t rem = unsent_len - t_peer[n].unsent_pos;
			memmove(&t_peer[n].unsent[t_peer[n].unsent_pos + 1], &t_peer[n].unsent[t_peer[n].unsent_pos], rem + 1);
			t_peer[n].unsent[t_peer[n].unsent_pos] = (char)ch;
			t_peer[n].unsent_pos++;
		}
	}
	return 0;
}

static int settings_chat_loop(const int n)
{
	static bool depreciated_online = false; // TODO remove
	const size_t h = 10;
	size_t w = screen_cols - minimum_size_horizontal;
	if(w < 40)
		w = 40;
	const size_t y = (screen_rows - h) / 2;
	const size_t x = (screen_cols - w) / 2;
	WINDOW *dlg = newwin_size(h, w, y, x);
	keypad(dlg, TRUE);
	int focus = 0;
	char *peernick = getter_string(n,INT_MIN,-1,offsetof(struct peer_list,peernick));
	const sig_atomic_t seen = resize_seq;
	while(1)
	{
		if(seen != resize_seq)
		{ // close popover on resize so main UI can rebuild cleanly
			delwin(dlg);
			torx_free((void*)&peernick);
			return -2;
		}
		werase(dlg);
		box(dlg,0,0);
		mvwprintw_size(dlg,0,2," Contact Settings ");
		if(!focus)
			wattron(dlg,A_REVERSE);

		const size_t maxnick = w - 15;
		mvwprintw_size(dlg,2,2,"Nickname: ");
		mvwprintw_size(dlg,2,12,"%.*s", (int)maxnick, peernick);
		if(focus == 0)
			wattroff(dlg,A_REVERSE);
		if(focus == 1)
			wattron(dlg,A_REVERSE);
		mvwprintw_size(dlg,4,2,"[ %c ] Online", depreciated_online ? 'x':' ');
		if(focus == 1)
			wattroff(dlg,A_REVERSE);
		if(focus == 2)
			wattron(dlg,A_REVERSE);
		mvwprintw_size(dlg,5,2,"Unread count: %lu", t_peer[n].unread);
		if(focus == 2)
			wattroff(dlg,A_REVERSE);
		if(focus == 3)
			wattron(dlg,A_REVERSE);
		mvwprintw_size(dlg,7,2,"[ Close ]");
		if(focus == 3)
			wattroff(dlg,A_REVERSE);
		mvwprintw_size(dlg,h-2,2,"Tab: cycle  Enter/Space: edit/toggle  Esc: close");
		wrefresh(dlg);

		const int ch = wgetch(dlg);
		if(ch == KEY_ESC)
			break;
		else if(ch == KEY_UP)
			focus = (focus + 3) % 4;
		else if(ch == KEY_DOWN)
			focus = (focus + 1) % 4;
		else if(ch == '\t')
			focus = (focus + 1) % 4;
		else if(ch == '\n' || ch == KEY_ENTER || ch == ' ')
		{
			if(focus == 0)
			{
				echo();
				curs_set(1);
				size_t idx = torx_allocation_len(peernick) - 1;
				if(idx > w - 15 - 1)
					idx = w - 15 - 1;
				mvwprintw_size(dlg,2,12,"%-*s", (int)w - 15, peernick);
				wmove_size(dlg,2,12 + idx);
				wrefresh(dlg);
				while(1)
				{
					const int cch = wgetch(dlg);
					if(cch == '\n' || cch == '\r' || cch == KEY_ESC)
						break;
					else if(cch == KEY_LEFT)
					{
						if(idx > 0)
						{
							idx--;
							wmove_size(dlg,2,12+idx);
							wrefresh(dlg);
						}
					}
					else if(cch == KEY_RIGHT)
					{
						if(idx < torx_allocation_len(peernick) - 1)
						{
							idx++;
							wmove_size(dlg,2,12+idx);
							wrefresh(dlg);
						}
					}
					else if(cch == KEY_DELETE)
					{
						if(idx < torx_allocation_len(peernick) - 1)
						{
							memmove(&peernick[idx], &peernick[idx+1], torx_allocation_len(peernick) - 1 - idx);
							peernick = torx_realloc(peernick,torx_allocation_len(peernick)-1);
							mvwprintw_size(dlg,2,12,"%-*s", (int)w - 15, peernick);
							wmove_size(dlg,2,12+idx);
							wrefresh(dlg);
						}
						else
							beep();
					}
					else if(cch == KEY_BACKSPACE || cch == 127 || cch == 8)
					{
						if(idx > 0)
						{
							memmove(&peernick[idx-1], &peernick[idx], torx_allocation_len(peernick) - 1 - idx + 1);
							peernick = torx_realloc(peernick,torx_allocation_len(peernick)-1);
							idx--;
							mvwprintw_size(dlg,2,12,"%-*s", (int)w - 15, peernick);
							wmove_size(dlg,2,12+idx);
							wrefresh(dlg);
						}
						else
							beep();
					}
					else if(cch >= 32 && cch <= 126)
					{
						if(torx_allocation_len(peernick) - 1 < w - 15 - 1)
						{ // TODO permit unlimited lengths
							peernick = torx_realloc(peernick,torx_allocation_len(peernick)+1);
							memmove(&peernick[idx+1], &peernick[idx], torx_allocation_len(peernick) - 1 - idx + 1);
							peernick[idx] = (char)cch;
							idx++;
							mvwprintw_size(dlg,2,12,"%-*s", (int)w - 15, peernick);
							wmove_size(dlg,2,12+idx);
							wrefresh(dlg);
						}
						else
							beep();
					}
				}
				noecho();
				curs_set(0);
				change_nick(n,peernick);
			}
			else if(focus == 1)
				depreciated_online = !depreciated_online;
			else if(focus == 2)
			{
				echo();
				curs_set(1);
				char nb[20];
				size_t nb_len = (size_t)snprintf(nb,sizeof(nb), "%lu", t_peer[n].unread);
				size_t idx = nb_len;
				mvwprintw_size(dlg,5,18,"%6s", nb);
				wmove_size(dlg,5,18+nb_len);
				wrefresh(dlg);
				while(1)
				{
					const int cch = wgetch(dlg);
					if(cch == '\n' || cch == '\r' || cch == KEY_ESC)
						break;
					else if(cch == KEY_DELETE)
					{
						if(idx < nb_len)
						{
							memmove(&nb[idx], &nb[idx+1], nb_len - idx);
							nb_len--;
							mvwprintw_size(dlg,5,18,"%6s", nb);
							wmove_size(dlg,5,18+idx);
							wrefresh(dlg);
						}
						else
							beep();
					}
					else if(cch == KEY_BACKSPACE || cch == 127 || cch == 8)
					{
						if(idx > 0)
						{
							memmove(&nb[idx-1], &nb[idx], nb_len - idx + 1);
							nb_len--;
							idx--;
							mvwprintw_size(dlg,5,18,"%6s", nb);
							wmove_size(dlg,5,18+idx);
							wrefresh(dlg);
						}
						else
							beep();
					}
					else if(cch >= '0' && cch <= '9')
					{
						if(idx < sizeof(nb)-2)
						{
							memmove(&nb[idx+1], &nb[idx], nb_len - idx + 1);
							nb[idx] = (char)cch;
							nb_len++;
							idx++;
							mvwprintw_size(dlg,5,18,"%6s", nb);
							wmove_size(dlg,5,18+idx);
							wrefresh(dlg);
						}
						else
							beep();
					}
				}
				noecho();
				curs_set(0);
				t_peer[n].unread = (size_t)atoll(nb);
			}
			else
				break;
		}
	}
	delwin(dlg);
	torx_free((void*)&peernick);
	return 0;
}

static int actions_chat_loop(const int n)
{
	const size_t h = 7, w = 40; // TODO why is this hardcoded?
	const size_t y = (screen_rows - h) / 2, x = (screen_cols - w) / 2;
	WINDOW *dlg = newwin_size(h,w,y,x);
	keypad(dlg, TRUE);
	int focus = 0;
	const sig_atomic_t seen = resize_seq;
	while(1)
	{
		if(seen != resize_seq)
		{
			delwin(dlg);
			return -2;
		}
		werase(dlg);
		box(dlg,0,0);
		mvwprintw_size(dlg,0,2," Actions ");
		if(focus == 0)
			wattron(dlg,A_REVERSE);
		mvwprintw_size(dlg,2,2,"Clear message history");
		if(focus == 0)
			wattroff(dlg,A_REVERSE);
		if(focus == 1)
			wattron(dlg,A_REVERSE);
		mvwprintw_size(dlg,3,2,"Clear unread count");
		if(focus == 1)
			wattroff(dlg,A_REVERSE);
		mvwprintw_size(dlg,h-2,2,"Up/Down: move  Enter/Space: action  Esc: close");
		wrefresh(dlg);
		const int ch = wgetch(dlg);
		if(ch == KEY_ESC)
			break;
		else if(ch == KEY_UP)
			focus = (focus + 1) % 2;
		else if(ch == KEY_DOWN)
			focus = (focus + 1) % 2;
		else if(ch == '\t')
			focus = (focus + 1) % 2;
		else if(ch == '\n' || ch == KEY_ENTER || ch == ' ')
		{
			if(focus == 0)
				delete_log(n);
			else
				t_peer[n].unread = 0;
		}
	}
	delwin(dlg);
	return 0;
}

static void draw_settings(void)
{ // Settings Route
	if(!settings_win)
	{
		error_simple(0,"settings_win does not exist. UI Coding error. Report this.");
		return;
	}
	werase(settings_win);
	box(settings_win,0,0);
	mvwprintw_size(settings_win, 0, 2, " Settings ");
	const char *labels[SETTINGS_COUNT] = {
		"Enable Flibbertigibbet Mode",
		"Auto-Snort on New Message",
		"Enable Ridonkulous Echo",
		"Use Quantum Sock Drawer"
	};
	for(size_t iter = 0; iter < SETTINGS_COUNT; ++iter)
	{
		size_t label_w = screen_cols - 12;
		if(label_w < 10)
			label_w = 10;
		if(settings_focus_idx == iter)
			wattron(settings_win, A_REVERSE);
		size_t y = 2  + iter * 3, x = 4;
		mvwprintw_size(settings_win, y, x, "[%c] %.*s", settings_toggle[iter] ? 'x':' ', (int)label_w - 6, labels[iter]);
		if(settings_focus_idx == iter)
			wattroff(settings_win, A_REVERSE);
	}
	const size_t text_y = 2 + SETTINGS_COUNT*3;
	if(settings_focus_idx == SETTINGS_COUNT)
		wattron(settings_win, A_REVERSE);
	mvwprintw_size(settings_win, text_y, 4, "Custom Silliness: %s", settings_text);
	if(settings_focus_idx == SETTINGS_COUNT)
		wattroff(settings_win, A_REVERSE);
	mvwprintw_size(settings_win, screen_rows-2, 2, "Up/Down: move  Enter/Space: toggle/edit  Esc/Home: Back");
	wrefresh(settings_win);
}

static int settings_loop(void)
{
	keypad(settings_win, TRUE);
	while(1)
	{
		draw_settings();
		const int ch = await_key_or_signal(settings_win);
		if(ch == -1 && resized)
			return 0; // handle at main loop
		else if(ch == -2)
			continue; // redraw
		else if(ch == ERR || ch < 0)
			continue;
		else if(ch == KEY_UP)
		{
			if(settings_focus_idx > 0)
				settings_focus_idx--;
			else
				settings_focus_idx = SETTINGS_COUNT;
		}
		else if(ch == KEY_DOWN)
		{
			if(settings_focus_idx < SETTINGS_COUNT)
				settings_focus_idx++;
			else
				settings_focus_idx = 0;
		}
		else if(ch == '\t')
			settings_focus_idx = (settings_focus_idx + 1) % (SETTINGS_COUNT + 1);
		else if(ch == '\n' || ch == KEY_ENTER || ch == ' ')
		{
			if(settings_focus_idx < SETTINGS_COUNT)
				settings_toggle[settings_focus_idx] = !settings_toggle[settings_focus_idx];
			else
			{
				echo();
				curs_set(1);
				const size_t ty = 2 + SETTINGS_COUNT*3;
				size_t idx = settings_text_len;
				const size_t maxlen = sizeof(settings_text) - 2;
				mvwprintw_size(settings_win, ty, 4 + 18, "%-*s", 40, settings_text);
				wmove_size(settings_win, ty, 4 + 18 + idx);
				wrefresh(settings_win);
				while(1)
				{
					const int cch = wgetch(settings_win);
					if(cch == '\n' || cch == '\r')
						break;
					else if(cch == KEY_ESC || cch == KEY_HOME)
						break;
					else if(cch == KEY_LEFT)
					{
						if(idx > 0)
						{
							idx--;
							wmove_size(settings_win, ty, 4 + 18 + idx);
							wrefresh(settings_win);
						}
					}
					else if(cch == KEY_RIGHT)
					{
						if(idx < settings_text_len)
						{
							idx++;
							wmove_size(settings_win, ty, 4 + 18 + idx);
							wrefresh(settings_win);
						}
					}
					else if(cch == KEY_DELETE)
					{
						if(idx < settings_text_len)
						{
							memmove(&settings_text[idx], &settings_text[idx+1], settings_text_len - idx);
							settings_text_len--;
							mvwprintw_size(settings_win, ty, 4 + 18, "%-40s", settings_text);
							wmove_size(settings_win, ty, 4 + 18 + idx);
							wrefresh(settings_win);
						}
						else
							beep();
					}
					else if(cch == KEY_BACKSPACE || cch == 127 || cch == 8)
					{
						if(idx > 0)
						{
							memmove(&settings_text[idx-1], &settings_text[idx], settings_text_len - idx + 1);
							idx--;
							settings_text_len--;
							mvwprintw_size(settings_win, ty, 4 + 18, "%-40s", settings_text);
							wmove_size(settings_win, ty, 4 + 18 + idx);
							wrefresh(settings_win);
						}
						else
							beep();
					}
					else if(cch >= 32 && cch <= 126)
					{
						if(settings_text_len < maxlen)
						{ // TODO permit unlimited lengths
							memmove(&settings_text[idx+1], &settings_text[idx], settings_text_len - idx + 1);
							settings_text[idx] = (char)cch;
							idx++;
							settings_text_len++;
							mvwprintw_size(settings_win, ty, 4 + 18, "%-40s", settings_text);
							wmove_size(settings_win, ty, 4 + 18 + idx);
							wrefresh(settings_win);
						}
						else
							beep();
					}
				}
				noecho();
				curs_set(0);
			}
		}
		else if(ch == KEY_ESC || ch == KEY_HOME)
			return 0;
	}
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
	// Set up pipe for async callbacks
	if(pipe(notify_fds) < 0)
	{
		perror("pipe");
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
	initscr(); cbreak(); noecho(); noqiflush(); keypad(stdscr, TRUE);
	set_escdelay(50); // Reduce delay upon pressing Esc

	// Set window resize function
	struct sigaction sa = {0};
	sa.sa_handler = signal_resize;
	sigaction(SIGWINCH, &sa, NULL);

	create_windows();

	while(running)
	{
		if(resized)
		{
			resized = 0;
			endwin(); refresh(); clear(); // all necessary when resizing
			create_windows();
		}
		else if(window == PAGE_PASSWORD)
		{
			const int ch = await_key_or_signal(pw_win);
			if(ch == -1 && resized)
				continue;
			else if(ch == -2)
				draw_password(); // redraw
			else if(ch == ERR || ch < 0)
				continue;
			else if(ch == '\t' || ch == KEY_BTAB)
			{
				pw_focus = (pw_focus + 1) % 3;
				draw_password();
			}
			else if(ch == KEY_ESC || ch == KEY_HOME)
				break;
			else if(ch == KEY_UP)
			{
				if(pw_focus > 0)
					pw_focus--;
				draw_password();
			}
			else if(ch == KEY_DOWN)
			{
				if(pw_focus < 2)
					pw_focus++;
				draw_password();
			}
			else if(pw_focus == 0)
			{ // Password entry field
				if(ch == KEY_LEFT)
				{
					if(pw_cursor > 0)
					{
						pw_cursor--;
						draw_password();
					}
					else
						beep();
				}
				else if(ch == KEY_RIGHT)
				{
					if(pw_cursor + 1 < torx_allocation_len(password))
					{
						pw_cursor++;
						draw_password();
					}
					else
						beep();
				}
				else if(ch == KEY_DELETE)
				{
					if(pw_cursor + 1 < torx_allocation_len(password))
					{
						const size_t prior_allocation_len = torx_allocation_len(password);
						memmove(&password[pw_cursor], &password[pw_cursor+1], prior_allocation_len - pw_cursor - 1);
						password = torx_realloc(password,prior_allocation_len-1); // after memmove
						draw_password();
					}
					else
						beep();
				}
				else if(ch == KEY_BACKSPACE || ch == 127 || ch == 8)
				{
					if(pw_cursor)
					{ // TODO permit unlimited lengths
						const size_t prior_allocation_len = torx_allocation_len(password);
						memmove(&password[pw_cursor-1], &password[pw_cursor], prior_allocation_len - pw_cursor);
						password = torx_realloc(password,prior_allocation_len-1); // after memmove
						pw_cursor--;
						draw_password();
					}
					else
						beep();
				}
				else if(ch == '\n' || ch == KEY_ENTER)
				{
					const uint8_t lockout_local = threadsafe_read_uint8(&mutex_global_variable,&lockout);
					if(!lockout_local)
					{
						login_start(password);
						torx_free((void*)&password);
						pw_cursor = 0; // must reset when freeing password
					}
				}
				else if(ch >= 32 && ch <= 126)
				{
					if(!password) // first character
					{
						password = torx_secure_malloc(2);
						password[pw_cursor + 1] = '\0';
					}
					else
					{ // Subsequent characters
						const size_t prior_allocation_len = torx_allocation_len(password);
						password = torx_realloc(password,prior_allocation_len+1); // before memmove
						memmove(&password[pw_cursor+1], &password[pw_cursor], prior_allocation_len - pw_cursor);
					}
					password[pw_cursor] = (char)ch;
					pw_cursor++;
					draw_password();
				}
			}
			else
			{
				if(ch == '\n' || ch == KEY_ENTER || ch == ' ')
				{
					if(pw_focus == 1)
						pw_show = !pw_show;
					else
						pw_like_cats = !pw_like_cats;
					draw_password();
				}
			}
		}
		else if(window == PAGE_CONTACTS)
		{
			const int ch = await_key_or_signal(list_win);
			if(ch == -1 && resized)
				continue;
			else if(ch == -2)
				draw_list(); // redraw
			else if(ch == ERR || ch < 0)
				continue;
			else if(ch == KEY_UP)
			{
				if(list_focus == 0)
				{
					if(selected_iter > 0)
						selected_iter--;
					else
						selected_iter = 0;
				}
				else
				{
					if(list_focus == 2)
						list_focus = 1;
					else
						list_focus = 0;
				}
				draw_list();
			}
			else if(ch == KEY_DOWN)
			{
				if(list_focus == 0)
				{
					if(selected_iter < 2)
						selected_iter++;
					else
						selected_iter = 2;
				}
				else
					list_focus = (list_focus + 1) % 3;
				draw_list();
			}
			else if(ch == '\n' || ch == KEY_ENTER || ch == ' ')
			{
				if(list_focus == 2)
				{
					window = PAGE_SETTINGS;
					create_windows();
				}
				else if(list_focus == 1)
				{
					groups_mode = !groups_mode;
					selected_iter = 0;
					draw_list();
				}
				else
				{
					window = PAGE_CHAT;
					global_n = selected_n;
					t_peer[global_n].unread = 0;
					chat_scroll_lines = 0;
				//	create_windows(); // currently redundant because chat_input_loop is called on the next loop, which calls draw_chat
				}
			}
			else if(ch == '\t' || ch == KEY_RIGHT)
			{
				list_focus = (list_focus + 1) % 3;
				draw_list();
			}
			else if(ch == KEY_LEFT)
			{
				list_focus = (list_focus + 2) % 3;
				draw_list();
			}
			else if(ch == KEY_ESC || ch == KEY_HOME)
				running = false;
		}
		else if(window == PAGE_CHAT)
		{
			const int res = chat_input_loop(global_n);
			if(res == 1)
			{
				window = PAGE_CONTACTS;
				global_n = -1;
				create_windows();
			}
			else if(res == -1)
				running = false;
		}
		else if(window == PAGE_SETTINGS)
		{
			settings_loop();
			window = PAGE_CONTACTS;
			create_windows();
		}
	}
	// Clean-up
	cleanup_lib(sig_num);
	destroy_windows();
	endwin();
	if(notify_fds[0] >= 0)
		close(notify_fds[0]);
	if(notify_fds[1] >= 0)
		close(notify_fds[1]);
	return 0;
}
