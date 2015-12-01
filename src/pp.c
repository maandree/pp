/**
 * MIT/X Consortium License
 * 
 * Copyright © 2015  Mattias Andrée <maandree@member.fsf.org>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define  t(...)  do { if (__VA_ARGS__) goto fail; } while (0)



/**
 * The content of a page.
 */
struct page {
	/**
	 * The content to print for display the page.
	 * It is not NUL terminated.
	 */
	char* content;

	/**
	 * The number of bytes stored in `.content`.
	 */
	size_t size;
};



/**
 * The name of the process.
 */
static const char *argv0;

/**
 * The index (0-based) of the current page.
 */
static size_t current_page = 0;

/**
 * The number of pages.
 */
static size_t page_count = 0;

/**
 * All loaded pages. Refer to `page_count`
 * for the number of contained pages.
 */
static struct page *pages;

/**
 * Have the terminal been resized?
 */
static volatile sig_atomic_t caught_sigwinch = 1;

/**
 * The width of the terminal.
 */
static size_t width = 72;

/**
 * The height of the terminal.
 */
static size_t height = 56;




/**
 * Signal handler for SIGWINCH.
 * Invoked when the terminal resizes.
 */
static void sigwinch(int signo)
{
  signal(signo, sigwinch);
  caught_sigwinch = 1;
}


/**
 * Get the size of the terminal.
 */
static void get_terminal_size(void)
{
	struct winsize winsize;

	if (!caught_sigwinch)
		return;

	caught_sigwinch = 0;

	while (ioctl(STDOUT_FILENO, (unsigned long)TIOCGWINSZ, &winsize) < 0)
		if (errno != EINTR)
			return;

	height = winsize.ws_row;
	width = winsize.ws_col;
}


/**
 * Displays the current pages.
 * This function will not clear the screen.
 * 
 * @param   bar   Print progress bar?
 * @param   page  Print page number and page count?
 * @return        0 on success, -1 on error.
 */
static int display_page(int bar, int page)
{
	ssize_t n;
	size_t stop;
	struct page p;

	p = pages[current_page];
	while (p.size) {
		n = write(STDOUT_FILENO, p.content, p.size);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p.content += n;
		p.size -= (size_t)n;
	}

	if ((bar | page) == 0)
		return 0;

	get_terminal_size();

	t (snprintf(NULL, 0, "%zu (%zu)%zn", current_page, page_count, &n) < 0);
	if (bar && current_page) {
		t (fprintf(stdout, "\033[%zu;1H\033[0;7m\033[%zu@\033[27m",
			   height, stop = current_page * width / (page_count - 1)) < 0);
	} else if (page) {
		t (fprintf(stdout, "\033[%zu;%zuH\033[0m%zu (%zu)",
			   height, width - 1 - (size_t)n, current_page, page_count) < 0);
	}
	else if (bar && page && current_page) {
		char buffer[2 * 3 * sizeof(size_t) + sizeof(" ()\033[27m")];
		sprintf(buffer, "%zu (%zu)", current_page, page_count);
		if (width - 1 - (size_t)n < stop)
			stop = 0;
		else
			stop -= width - 1 - (size_t)n;
		if (stop > (size_t)n)
			stop = (size_t)n;
		memmove(buffer + stop + sizeof("\033[27m") - 1, buffer + stop, strlen(buffer + stop));
		memcpy(buffer + stop, "\033[27m", sizeof("\033[27m") - 1);
		t (fprintf(stdout, "\033[%zu;%zuH\033[0;7m%s",
			   height, width - 1 - (size_t)n, buffer) < 0);
	}

	t (fflush(stdout));

	return 0;
fail:
	return -1;
}


/**
 * Interactively display the file.
 * 
 * @param   fd    File descriptor for reading from the terminal.
 * @param   bar   Print progress bar?
 * @param   page  Print page number and page count?
 * @return        0 on success, -1 on error.
 */
static int display_file(int fd, int bar, int page)
{
	ssize_t n;
	char c;
	int state = 0;
	int command = 0;

next:
	t (fprintf(stdout, "\033[H\033[2J") < 0);
	t (fflush(stdout));
	t (display_page(bar, page));

	for (command = 99; command == 99;) {
		n = read(fd, &c, sizeof(c));
		if (n == 0) {
			return 0;
		} else if (n < 0) {
			t (errno != EINTR);
			continue;
		}

		if (state == 1) { /* ESC */
			state = (c == '[' ? 2 : 0);
		} else if (state == 2) { /* CSI */
			if (strchr("AC", c)) /* up or right */
				command = -1;
			else if (strchr("BD", c)) /* down or left */
				command = +1;
			else if (c == '5') /* CSI 5 */
				state = 5;
			else if (c == '6') /* CSI 6 */
				state = 6;
			else
				state = 0;
		} else if ((state == 5) || (state == 6)) { /* CSI 5 or CSI 6*/
			if (c == '~')
				command = (state == 5 ? -1 : +1); /* page up or page down*/
			else
				state = 0;
		} else if (c == '\033') { /* ESC*/
			state = 1;
		} else if (c == 'L' - '@') { /* C-l */
			command = 0;
		} else if (c == 'q') { /* q */
			return 0;
		}
	}

	/*
	 * Up   = \e[A
	 * Down = \e[B
	 * 
	 * Page Up   = \e[5~
	 * Page Down = \e[6~
	 * 
	 * Right = \e[C
	 * Left  = \e[D
	 */

	if ((command == -1) && current_page) {
		current_page -= 1;
	} else if ((command == +1) && (current_page + 1 < page_count)) {
		current_page += 1;
	}

	goto next;
fail:
	return -1;
}


/**
 * Load pages.
 * 
 * @param   fd          File descriptor for reading the file.
 * @param   keep_empty  Display empty pages?
 * @return              0 on success, -1 on error.
 */
static int load_pages(int fd, int keep_empty)
{
	/* TODO */ (void) fd, (void) keep_empty;
	return 0;
}


/**
 * Displays a plain-text file page-by-page. It assumes
 * that the file includes page breaks. If the file does
 * not include page breaks the entire file will be printed
 * to the terminal and the program will exit. If the
 * file only includes one page, the the entire page will
 * be printed to the terminal and the program will exit.
 * 
 * Escape sequences are printed as-is. This means for
 * example that colour can be used in the file, but if
 * the colours span over page breaks, you would best
 * pass it through a preprocessor first that files this.
 * Or better yet; fix the file.
 * 
 * If no file is specified, or if '-' i specified, stdin
 * will be paged.
 * 
 * @param   argc  The number of elements in `argv`.
 * @param   argv  Command line arguments.
 *                  -e  Display empty pages, except final page.
 *                  -b  Display progress bar.
 *                  -p  Display page number and page count.
 * @return        0 on success, 1 on error, 2 on usage error.
 */
int main(int argc, char *argv[])
{
	int dashed = 0, f_empty = 0, f_bar = 0, f_page = 0;
	char *arg;
	const char *file = NULL;
	int fd = -1, tty_configured = 0;
	struct termios stty;
	struct termios saved_stty;
	struct stat _attr;

	/* Check that we have a stdout. */
	if (fstat(STDOUT_FILENO, &_attr))
		t (errno == EBADF);

	/* Parse arguments. */
	argv0 = argv ? (argc--, *argv++) : "pp";
	while (argc) {
		if (!dashed && !strcmp(*argv, "--")) {
			dashed = 1;
			argv++;
			argc--;
		} else if (!dashed && **argv == '-') {
			arg = *argv++;
			argc--;
			for (arg++; *arg; arg++) {
				if (*arg == 'e')
					f_empty = 1;
				else if (*arg == 'b')
					f_bar = 1;
				else if (*arg == 'p')
					f_page = 1;
				else
					goto usage;
			}
		} else {
			if (file)
				goto usage;
			file = *argv++;
			argc--;
		}
	}

	/* Open file. */
	if (!file || !strcmp(file, "-")) {
		fd = STDIN_FILENO;
	} else {
		fd = open(file, O_RDONLY);
		t (fd == -1);
	}

	/* Load pages. */
	t (load_pages(fd, f_empty));

	/* We do not need the file anymore. */
	close(fd), fd = -1;

	/* No need to be interactive if there is just one page. */
	if (page_count < 2)
		goto print_current_page;

	/* Configure terminal. */
	t (fprintf(stdout, "\033[?1049h\033[?25l") < 0);
	t (fflush(stdout));
	t (tcgetattr(STDIN_FILENO, &stty));
	saved_stty = stty;
	stty.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
	t (tcsetattr(STDIN_FILENO, TCSAFLUSH, &stty));
	tty_configured = 1;

	/* Get a readable file descriptor for the controlling terminal. */
	fd = open("/dev/tty", O_RDONLY);
	t (fd == -1);

	/* Display file. */
	signal(SIGWINCH, sigwinch);
	t (display_file(fd, f_bar, f_page));

	/* We do not need the input from the terminal anymore. */
	close(fd), fd = -1;

	/* Restore terminal configurations. */
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_stty);
	fprintf(stdout, "\033[?25h\033[?1049l");
	fflush(stdout);
	tty_configured = 0;

print_current_page:
	if (page_count == 0)
		return 0;

	/* Print current page. */
	t (display_page(0, 0));

	while (page_count--)
		free(pages[page_count].content);
	free(pages);
	return 0;

fail:
	perror(argv0);
	if (fd >= 0)
		close(fd);
	if (tty_configured) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_stty);
		fprintf(stdout, "\033[?25h\033[?1049l");
		fflush(stdout);
	}
	while (page_count--)
		free(pages[page_count].content);
	free(pages);
	return 1;

usage:
	fprintf(stderr, "%s: Invalid arguments, see `man 1 pp'.\n", argv0);
	return 2;
}

