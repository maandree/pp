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
const char *argv0;

/**
 * The index (0-based) of the current page.
 */
size_t current_page = 0;

/**
 * The number of pages.
 */
size_t page_count = 0;

/**
 * All loaded pages. Refer to `page_count`
 * for the number of contained pages.
 */
struct page *pages;



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
	ssize_t wrote;
	struct page p;

	p = pages[current_page];
	while (p.size) {
		wrote = write(STDOUT_FILENO, p.content, p.size);
		if (wrote < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p.content += wrote;
		p.size -= (size_t)wrote;
	}

	/* TODO */ (void) bar, (void) page;

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

	/* TOOD Load pages. */

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

	/* TODO Display file. */

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

