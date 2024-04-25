#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <proc/readproc.h>

static struct option options_getopt[] = {
        {"help",    no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"all",     no_argument, 0, 'a'},
        {"name",    no_argument, 0, 'n'},
        {"pid",     no_argument, 0, 'p'},
        {"cmdline", no_argument, 0, 'c'},
        {"environ", no_argument, 0, 'e'},
        {"info",    no_argument, 0, 'i'},
        {0,         0,           0, 0  }
};

int main(int argc, char *argv[]) {
	bool invalid = false, force_name = false, force_pid = false;
	bool show_cmdline = false, show_environ = false, show_all = false, show_info = false;
	int opt;

	// argument handling
	while ((opt = getopt_long(argc, argv, ":hVanpcei", options_getopt, NULL)) != -1) {
		switch (opt) {
			case 'h':
				printf("Usage: %s [options]... <process name/ID>...\n", TARGET);
				printf("-h --help: Shows help text\n");
				printf("-V --version: Shows the version\n");
				printf("-a --all: Show all processes\n");
				printf("-n --name: Force match by process name\n");
				printf("-p --pid: Force match by process ID\n");
				printf("-c --cmdline: Show command line arguments\n");
				printf("-e --environ: Show environment variables\n");
				printf("-i --info: Show extra info\n");
				return 0;
			case 'V':
				printf("%s %s\n", TARGET, VERSION);
				return 0;
			default:
				if (!invalid) {
					switch (opt) {
#define BOOL_OPT(letter, name, invalid_condition) \
	case letter:                                  \
		if (invalid_condition) {                  \
			invalid = true;                       \
			break;                                \
		}                                         \
		name = true;                              \
		break;
						BOOL_OPT('a', show_all, show_all)
						BOOL_OPT('n', force_name, force_name || force_pid)
						BOOL_OPT('p', force_pid, force_name || force_pid)
						BOOL_OPT('c', show_cmdline, show_cmdline)
						BOOL_OPT('e', show_environ, show_environ)
						BOOL_OPT('i', show_info, show_info)
#undef BOOL_OPT
						default:
							invalid = true;
							break;
					}
				}
				break;
		}
	}

	if ((show_all ? (optind != argc) : (optind >= argc)) || invalid)
		errx(1, "Invalid usage, try --help");

	proc_t proc_info;
	memset(&proc_info, 0, sizeof(proc_info));

	bool res = 0;

	for (int arg = optind; arg < argc || show_all; arg++) {
		bool is_pid = force_pid;
		pid_t pid = 0;

		if (!show_all) {
			if (!force_name) {
				for (char *digit = argv[arg]; *digit; digit++) {
					if (*digit < '0' || *digit > '9') {
						if (force_pid) goto invalid_pid;
						is_pid = false;
						break;
					}
					is_pid = true;
				}
			}

			if (is_pid) {
				errno = 0;
				long pid_ = strtol(argv[arg], NULL, 10);
				if (errno || pid_ < 0 || pid_ == LONG_MAX) {
				invalid_pid:
					fprintf(stderr, "Invalid PID: %s\n", argv[arg]);
					res = 1;
					continue;
				}
				pid = pid_;
			}
		}

		bool found = false;

		int flags = PROC_FILLSTAT;
		if (show_cmdline) flags |= PROC_FILLCOM;
		if (show_environ) flags |= PROC_FILLENV;
		PROCTAB *proc = openproc(flags);
		if (!proc)
			errx(1, "Failed to open proc");

		while (readproc(proc, &proc_info)) {
			if (!show_all) {
				if (is_pid) {
					if (proc_info.tid != pid)
						continue;
				} else {
					if (strcmp(proc_info.cmd, argv[arg]) != 0)
						continue;
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

		if (show_all) break;

		if (!found) {
			if (is_pid) {
				fprintf(stderr, "No process found by ID %i\n", pid);
			} else {
				fprintf(stderr, "No process found by name %s\n", argv[arg]);
			}
			res = 1;
		}
	}

	return res;
}
