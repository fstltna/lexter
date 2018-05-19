/* Copyright 1998,2001  Mark Pulford
* This file is subject to the terms and conditions of the GNU General Public
* License. Read the file COPYING found in this archive for details, or
* visit http://www.gnu.org/copyleft/gpl.html
*/

/* Interface to display game on a 80x24 terminal with the curses library */

/* Be charful with int/char unsigned/signed conversions */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#ifdef HAVE_CURSES_H
#include <curses.h>
#endif
#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#endif

#include "lexter.h"
#include "iface.h"
#include "words.h"
#include "lang.h"
#include "dict.h"
#include "scorestuff.h"


#define INFOX	40

static int input_fd;

static void show_help();
static void show_scores();
static char *repeatstr(char ch, int count);

int main(int argc,char **argv)
{
char *optstr = "hvd:s";
char *dict = NULL;
int o;

#ifdef ENABLE_NLS
setlocale(LC_ALL, "");
bindtextdomain(PACKAGE, LOCALEDIR);
textdomain(PACKAGE);
#endif

o = getopt(argc, argv, optstr);
while(-1 != o) {
	switch(o) {
	case 'h':
		show_help();
		exit(0);
	case 'v':
		printf(_("Lexter v%s\n"), VERSION);
		exit(0);
	case 'd':
		if(strchr(optarg, '/') || strchr(optarg, '.')) {
			printf(_("Error: dictionary name must not contain '/' or '.'\n"));
			exit(-1);
		}
		dict = optarg;
		break;
	case ':':
		fprintf(stderr, _("lexter: missing parameter\n"));
		exit(-1);
	case 's':
		show_scores();
	case '?':
		exit(-1);	/* getopt has already warned user */
	default:
		abort();
	}
	o = getopt(argc, argv, optstr);
}

if_init();
if_showhelp();
if_putmsg(_("Loading dictionary..."));
game_load_dict(dict);
if_putmsg(_("Press any key to start the game"));
if_getaction(NULL);	/* wait key */
if_putmsg("");

game_run();

return 0;
}

/* Print high score table on stdout and exit */
static void show_scores()
{
struct hiscore *hs;
const char *err;
int i;

hs = score_update(NULL, NULL, &err);
if(!hs) {
	printf(_("ERROR: %s\n"), err);
	exit(-1);
}

printf(_("#   Name       Total   Words   Blocks   Dictionary   Date\n"));
printf("%s\n",repeatstr('-', 79));

/* While there is an entry (name[0]==0 terminates) */
/* Note: ctime does \n */
for(i=0; hs[i].name[0]; i++)
	printf("%-2d  %-8s   %-6d  %-6d  %-7d  %-8s     %s",
			1+i, hs[i].name, hs[i].total, hs[i].words,
			hs[i].blocks, hs[i].dict, ctime(&hs[i].date));
exit(0);
}

static void show_help()
{
	printf(_("usage: lexter [ -v | -h | -s | -d dict | -u username]\n"));
	printf(_("-v           show version\n"));
	printf(_("-h           show help\n"));
	printf(_("-d dict      run game with a different dictionary\n"));
	printf(_("-s           show high score table\n"));
}

static char *repeatstr(char ch, int count)
{
	static char *ptr = NULL;

	if(ptr)
		free(ptr);
	ptr = malloc(count+1);
	if(!ptr)
		abort();
	memset(ptr, ch, count);
	ptr[count] = 0;

	return ptr;
}

void if_init()
{
	int mx, my;

	initscr();

	getmaxyx(stdscr, my, mx);
	if(mx<80 || my<24) {
		endwin();
		printf(_("Terminal too small, Lexter requires at least 80x24\n"));
		exit(-1);
	}

	cbreak();		/* Disable line buffering */
	noecho();
	nonl();			/* Disable NewLine translation */
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);	/* Make getch() non blocking */
	/* Invis cursor, or make it small if possible */
	if(ERR == curs_set(0))
		curs_set(1);
	scrollok(stdscr, FALSE);

	input_fd = open("/dev/tty", O_RDONLY);
	if(input_fd < 0) {
		if_error(_("Open /dev/tty: %s"), strerror(errno));
		if_exit(-1);
	}

	if_drawpit();

	if_refresh();
}

void if_drawpit()
{
	int c;

	/* Side borders */
	for(c=0; c<PITDEPTH; c++) {
		mvaddch(c, 0, '|');
		mvaddch(c, PITWIDTH*3+1, '|');
	}
	/* Bottom border */
	mvaddch(PITDEPTH, 0, '+');
	for(c=0; c<PITWIDTH*3+1; c++)
		mvaddch(PITDEPTH, 1+c, '-');
	mvaddch(PITDEPTH, PITWIDTH*3+1, '+');

	mvprintw(0, INFOX, _("Lexter v%s"), VERSION);
}


/* If letter==0 you can modify the border without touching the letter */
void if_putpit(char letter, enum btype bt, int x, int y)
{
	assert(0<=x && x<PITWIDTH && 0<=y && y<PITDEPTH);

	switch(bt) {
	case NONE:
		mvaddch(y, x*3+1, ' ');
		mvaddch(y, x*3+2, ' ');
		mvaddch(y, x*3+3, ' ');
		break;
	case NORMAL:
		mvaddch(y, x*3+1, '[');
		if(letter)
			mvaddch(y, x*3+2, letter&255);	/* &255 helps int */
		mvaddch(y, x*3+3, ']');
		break;
	case HIGHLIGHT:
		mvaddch(y, x*3+1, '*');
		if(letter)
			mvaddch(y, x*3+2, letter&255);
		mvaddch(y, x*3+3, '*');
		break;
	}
}

/* Called whenever a block comes to rest */
void if_putscore(const struct score *scr, char next)
{
	mvprintw(2, INFOX, _("Dictionary: %-8s    Blocks: %d"), dict_get(), scr->blocks);
	mvprintw(3, INFOX, _("Next: [%c]  Words: %-5d Score: %d"), next, scr->words, scr->total);

	if_refresh();
}

void if_putwords(const struct list *wordlist)
{
	const struct word *w;
	const char *b;
	int y;

	b = repeatstr(' ', 80-INFOX);

	mvaddstr(5, INFOX, b);
	mvaddstr(5, INFOX, _("Pts:  Found:"));

	y = 6;
	for(y=6; y<22; y++) {
		mvaddstr(y, INFOX, b);
		if(wordlist) {
			w = (struct word *)wordlist->data;
			mvprintw(y, INFOX, "%d", w->score);
			mvaddstr(y, INFOX+6, w->str);
			wordlist = wordlist->next;
		}
	}

	if_refresh();
}

/* Display help, overwrites word list area */
void if_showhelp()
{
	int y;

	/* Blank info area */
	for(y=5; y<22; y++)
		mvaddstr(y, INFOX, repeatstr(' ', 80-INFOX));

	mvaddstr(5, INFOX, _("Help"));
	mvaddstr(6, INFOX, _("----"));
	mvaddstr(7, INFOX, _("left/right   Move the block"));
	mvaddstr(8, INFOX, _("down         Lower the block"));
	mvaddstr(9, INFOX, _("space        Drop the block"));
	mvaddstr(10,INFOX, _("P            Pause game"));
	mvaddstr(11,INFOX, _("L            Show letter scores"));
	mvaddstr(12,INFOX, _("W            Show found words"));
	mvaddstr(13,INFOX, _("S            Take screenshot"));
	mvaddstr(14,INFOX, _("H            Show help"));
	mvaddstr(15,INFOX, _("Q            Quit game"));

	mvaddstr(17,INFOX, "Marisa Giancarla");
	mvaddstr(18,INFOX, "<marisag@synchronetbbs.org>");

	if_refresh();
}

static int next_char(int c)
{
	if(c<0 || 255<c)
		return 256;
	for(; c<256; c++)
		if(letter_valid(c))
			return c;
	return 256;
}

void if_showletters()
{
	int c=-1, y;

	for(y=5; y<22; y++)
		mvaddstr(y, INFOX, repeatstr(' ', 80-INFOX));

	mvaddstr(5, INFOX, _("  Score  Chance       Score  Chance"));
	for(y=6; y<22; y++) {
		c = next_char(c+1);
		if(letter_valid(c&255))
			mvprintw(y, INFOX, "%c %4d   %5.2f%%", c, letter_score(c&255), letter_prob(c&255)*100);
		c = next_char(c+1);
		if(letter_valid(c&255))
			mvprintw(y, INFOX+20, "%c %4d   %5.2f%%", c, letter_score(c&255), letter_prob(c&255)*100);
	}

	if_refresh();
}

void if_putmsg(const char *fmt, ...)
{
	va_list args;
	char buf[81];

	va_start(args, fmt);
	vsnprintf(buf, 81, fmt, args);
	va_end(args);
	
	mvaddstr(23, 0, repeatstr(' ', 80));
	mvaddstr(23, 0, buf);
	if_refresh();
}

/* get_action() can take up to *delay time waiting for an action
 * It is not required to wait the entire time if nothing is found.
 * This makes the game more friendly to the cpu (if possible)
 *
 * get_action(NULL) should wait until any key has been pressed
 */
int if_getaction(struct timeval *delay)
{
	fd_set set;
	int err;
	int c;

	/* Using select for more fine grained timeouts */
	if(delay) {
		FD_ZERO(&set);
		FD_SET(input_fd, &set);

		err = select(FD_SETSIZE, &set, NULL, NULL, delay);
		if(err < 0) {
			if(errno == EINTR)
				return NO_ACT;
			if_error(_("Error getting key: %s"), strerror(errno));
			if_exit(-1);
		}

		if(err == 0)
			return NO_ACT;

		c = getch();
	} else {
		nodelay(stdscr, FALSE);
		c = getch();
		nodelay(stdscr, TRUE);
	}

	switch(c) {
	case ERR:
		return NO_ACT;
	case KEY_LEFT:
		return LEFT_ACT;
	case KEY_DOWN:
		return DOWN_ACT;
	case KEY_RIGHT:
		return RIGHT_ACT;
	case ' ':
		return DROP_ACT;
	case 'Q':
		return QUIT_ACT;
	case 'P':
		return PAUSE_ACT;
	case 'H':
		return HELP_ACT;
	case 'S':
		return SSHOT_ACT;
	case 'L':
		return LETTER_ACT;
	case 'W':
		return WORDL_ACT;
	default:
		if(letter_valid(c))
			return c;
		else
			return NO_ACT;
	}
}

/* Removes all keys in the input buffer */
void if_flushinput()
{
	struct timeval t;
	int act;

	do {
		t.tv_sec = 0;
		t.tv_usec = 0;
		act = if_getaction(&t);
	} while(act != NO_ACT);
}

void if_refresh()
{
	int mx, my;

	getmaxyx(stdscr, my, mx);
	move(my-1, mx-1);

	if(refresh() == ERR) {
		if_error(_("Error refreshing screen"));
		if_exit(-1);
	}
}

void if_exit(char code)
{
	close(input_fd);
	endwin();
	exit(code);
}

/* Save screenshot & alert user */
void if_screenshot()
{
	int y;
	chtype line[81];	/* mvinchstr returns null terminated array */
	char buf[81];
	int fd, i;
	char fn[32];

	/* Open next free screenshot file */
	for(i=0; i<10; i++) {
		snprintf(fn, 32, "lexter%0d.txt", i);
		fd = open(fn, O_CREAT|O_EXCL|O_WRONLY,
			S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
		if(fd >= 0 || errno != EEXIST)
			break;
	}
	if(fd < 0) {
		/* Don't use if_error, otherwise they can pause the game */
		if_putmsg(_("Screenshot open failed: %s"), strerror(errno));
		return;
	}
	
	for(y=0; y<24; y++) {
		mvinchnstr(y, 0, line, 80);
		
		/* Convert from chtype -> char */
		for(i=0; i<80; i++)
			buf[i] = line[i]&255;
		buf[80] = '\n';
		
		i = write(fd, buf, 81);
		if(i != 81) {
			char *e;

			if(-1 == i)
				e = strerror(errno);
			else
				e = _("partial write");
			if_putmsg(_("Screenshot write failed: %s"), e);
			close(fd);
			return;
		}
	}

	close(fd);

	if_putmsg(_("Saved %s"), fn);
}

/* Display high score table
 * Last entry in array is hs->name[0]==0 */
void if_highscore(const struct hiscore *hs, time_t hl)
{
	int i;
	const char *b;

	/* Blank screen */
	b = repeatstr(' ', 80);
	for(i=0; i<23; i++)
		mvaddstr(i, 0, b);

	mvaddstr(0, 0, _("#   Name       Total   Words   Blocks   Dictionary   Date"));
	mvaddstr(1, 0, repeatstr('-', 80));

	/* While there is an entry (name[0]==0 terminator) */
	for(i=0; hs[i].name[0]; i++) {
		if(hs[i].date == hl)
			attron(A_STANDOUT);
		mvprintw(2+i, 0, "%-2d  %-8s   %-6d  %-6d  %-7d  %-8s     %s",
				1+i, hs[i].name, hs[i].total, hs[i].words,
				hs[i].blocks, hs[i].dict, ctime(&hs[i].date));
		if(hs[i].date == hl)
			attroff(A_STANDOUT);
	}
}

void if_error(const char *fmt, ...)
{
	va_list args;
	char buf[81];
	int len;

	strcpy(buf, _("ERROR: "));
	len = strlen(buf);
	va_start(args, fmt);
	vsnprintf(buf+len, 81-len, fmt, args);
	va_end(args);
	strncat(buf, _(" <press key>"), 80-strlen(buf));

	if_putmsg(buf);
	if_getaction(NULL);
}
