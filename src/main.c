#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <proc/readproc.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

static struct option options_getopt[] = {
        {"help",      no_argument, 0, 'h'},
        {"version",   no_argument, 0, 'V'},
        {"all",       no_argument, 0, 'a'},
        {"name",      no_argument, 0, 'n'},
        {"pid",       no_argument, 0, 'p'},
        {"substring", no_argument, 0, 's'},
        {"regex",     no_argument, 0, 'r'},
        {"cmdline",   no_argument, 0, 'c'},
        {"environ",   no_argument, 0, 'e'},
        {"info",      no_argument, 0, 'i'},
        {0,           0,           0, 0  }
};

int main(int argc, char *argv[]) {
	enum match_mode {
		MATCH_AUTO,
		MATCH_ALL,
		MATCH_NAME,
		MATCH_PID,
		MATCH_SUBSTRING,
		MATCH_REGEX
	} match_mode = MATCH_AUTO;
	bool invalid = false;
	bool show_cmdline = false, show_environ = false, show_info = false;
	int opt;

	// argument handling
	while ((opt = getopt_long(argc, argv, ":hVanpsrcei", options_getopt, NULL)) != -1) {
		switch (opt) {
			case 'h':
				printf("Usage: %s [options]... <process name/ID>...\n", TARGET);
				printf("-h --help: Shows help text\n");
				printf("-V --version: Shows the version\n\n");
				printf("Matching options:\n");
				printf("-a --all: Show all processes\n");
				printf("-n --name: Force matching by process name\n");
				printf("-p --pid: Force matching by process ID\n");
				printf("-s --substring: Match process by substring (assumes -n)\n");
				printf("-r --regex: Match process by regular expression (PCRE, assumes -n)\n\n");
				printf("Info options:\n");
				printf("-c --cmdline: Show command line arguments\n");
				printf("-e --environ: Show environment variables\n");
				printf("-i --info: Show extra info\n\n");
				return 0;
			case 'V':
				printf("%s %s\n", TARGET, VERSION);
				return 0;
			default:
				if (!invalid) {
					switch (opt) {
#define OPT(letter, code, invalid_condition) \
	case letter:                             \
		if (invalid_condition)               \
			invalid = true;                  \
		else                                 \
			code;                            \
		break;
#define OPT_BOOL(letter, name, invalid_condition) OPT(letter, name = true, name || (invalid_condition))
#define all_match_options match_mode != MATCH_AUTO // these are mutually exclusive
						OPT('a', match_mode = MATCH_ALL, all_match_options)
						OPT('n', match_mode = MATCH_NAME, all_match_options)
						OPT('p', match_mode = MATCH_PID, all_match_options)
						OPT('s', match_mode = MATCH_SUBSTRING, all_match_options)
						OPT('r', match_mode = MATCH_REGEX, all_match_options)
						OPT_BOOL('c', show_cmdline, false)
						OPT_BOOL('e', show_environ, false)
						OPT_BOOL('i', show_info, false)
#undef all_match_options
#undef OPT
						default:
							invalid = true;
							break;
					}
				}
				break;
		}
	}

	if ((match_mode == MATCH_ALL ? (optind != argc) : (optind >= argc)) || invalid)
		errx(1, "Invalid usage, try --help");

	bool ret = 0; // return value

	for (int arg = optind; arg < argc || match_mode == MATCH_ALL; arg++) {
		bool is_pid = match_mode == MATCH_PID || match_mode == MATCH_AUTO;
		pid_t pid = 0;
		pcre2_code *regex; // regular expression to match

		if (match_mode != MATCH_ALL) { // ignore if --all is set
			if (is_pid) {              // either auto match or match by PID only
				// check if the argument is numeric and set is_pid accordingly
				// error if the argument is not numeric and --pid is set
				for (char *digit = argv[arg]; *digit; digit++) {
					if (*digit < '0' || *digit > '9') {
						if (match_mode == MATCH_PID) goto invalid_pid;
						is_pid = false;
						break;
					}
				}
			}

			if (is_pid) { // parse the integer
				errno = 0;
				long pid_ = strtol(argv[arg], NULL, 10);
				if (errno || pid_ < 0 || pid_ == LONG_MAX) {
				invalid_pid:
					fprintf(stderr, "Invalid PID: %s\n", argv[arg]);
					ret = 1;
					continue;
				}
				pid = pid_; // set the PID to match
			}

			if (match_mode == MATCH_REGEX) {
				int errorcode;
				PCRE2_SIZE erroroffset;
				regex = pcre2_compile((PCRE2_SPTR) argv[arg], PCRE2_ZERO_TERMINATED, 0, &errorcode, &erroroffset, NULL);
				if (!regex) {
					PCRE2_UCHAR error[256];
					pcre2_get_error_message(errorcode, error, sizeof(error));
					fprintf(stderr, "Invalid regular expression: %s (offset %ld)\n%s\n", error, erroroffset, argv[arg]);
					for (int i = 0; i < erroroffset; i++) putc('-', stderr);
					putc('^', stderr);
					for (int i = 0; i < erroroffset; i++) putc('-', stderr);
					putc('\n', stderr);
					ret = 1;
					continue;
				}
			}
		}

		bool found = false;

		int flags = PROC_FILLSTAT;
		if (show_cmdline) flags |= PROC_FILLCOM;
		if (show_environ) flags |= PROC_FILLENV;

		PROCTAB *proc = openproc(flags);
		if (!proc)
			errx(1, "Failed to open proc");

		proc_t proc_info;
		memset(&proc_info, 0, sizeof(proc_info));
		while (readproc(proc, &proc_info)) {
			if (match_mode != MATCH_ALL) { // ignore if --all is set
				if (is_pid) {
					if (proc_info.tid != pid)
					next_proc: // skip to the next process
						continue;
				} else {
					switch (match_mode) {
						case MATCH_SUBSTRING:
							// match a substring of the command name
							if (!strstr(proc_info.cmd, argv[arg])) goto next_proc;
							break;
						case MATCH_REGEX:
							// match the command name by a regular expression
							pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
							int rc = pcre2_match(regex, (PCRE2_SPTR8) proc_info.cmd, PCRE2_ZERO_TERMINATED, 0, 0, match_data, NULL);
							if (rc == PCRE2_ERROR_NOMATCH) goto next_proc;
							else if (rc < 0) {
								PCRE2_UCHAR error[256];
								pcre2_get_error_message(rc, error, sizeof(error));
								fprintf(stderr, "PCRE error: %s\n", error);
								ret = 1;
								goto next_proc;
							}
							break;
						default:
							// match whole command name
							if (strcmp(proc_info.cmd, argv[arg]) != 0) goto next_proc;
							break;
					}
				}
			}

			found = true;
			printf("%s - ", proc_info.cmd);
			printf("pid=%d", proc_info.tid);
			if (show_info) {
				printf(" ppid=%d", proc_info.ppid);
				printf(" state=%c", proc_info.state);
				printf(" uid=%d", proc_info.euid);
				printf(" gid=%d", proc_info.egid);
				printf(" priority=%ld", proc_info.priority);
				printf(" nice=%ld", proc_info.nice);
			}
			printf("\n");
			if (show_cmdline) {
				printf("cmdline:");
				if (!proc_info.cmdline)
					printf(" no permission");
				printf("\n");
				if (proc_info.cmdline)
					for (size_t i = 0; proc_info.cmdline[i]; ++i)
						printf("  %li: %s\n", i, proc_info.cmdline[i]);
			}
			if (show_environ) {
				printf("environ:");
				if (!proc_info.environ)
					printf(" no permission");
				printf("\n");
				if (proc_info.environ)
					for (size_t i = 0; proc_info.environ[i]; ++i)
						printf("  %s\n", proc_info.environ[i]);
			}
			if (show_cmdline || show_environ)
				printf("\n");
			if (is_pid) break;
		}
		closeproc(proc);

		if (!found) {
			// show error message based on the matching mode
			fprintf(stderr, "No processes found");
			if (is_pid) {
				fprintf(stderr, " by ID %i\n", pid);
				break;
			} else {
				switch (match_mode) {
					case MATCH_ALL:
						fprintf(stderr, "\n");
						break;
					case MATCH_SUBSTRING:
						fprintf(stderr, " by substring '%s'\n", argv[arg]);
						break;
					case MATCH_REGEX:
						fprintf(stderr, " by regular expression /%s/\n", argv[arg]);
						break;
					default:
						fprintf(stderr, " by name '%s'\n", argv[arg]);
						break;
				}
			}
			ret = 1;
		}

		if (match_mode == MATCH_ALL) break;
	}

	return ret;
}
