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



/**
 * The name of the process
 */
const char *argv0;



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
	const char* file = NULL;
	int fd = -1;

	argv0 = argv ? (argc--, *argv++) : "pp";

	/* Parse arguments. */
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
		fd = 0;
	} else {
		fd = open(file, O_RDONLY);
		if (fd == -1)
			goto fail;
	}

	/* TODO */

	close(fd);
	return 0;

fail:
	perror(argv0);
	if (fd >= 0)
		close(fd);
	return 1;

usage:
	fprintf(stderr, "%s: Invalid arguments, see `man 1 pp'.\n", argv0);
	return 2;
}

