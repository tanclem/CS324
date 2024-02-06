/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
char sbuf[MAXLINE];         /* for composing sprintf messages */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
int parseargs(char **argv, int *cmds, int *stdin_redir, int *stdout_redir);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
	int c;
	char cmdline[MAXLINE];
	int emit_prompt = 1; /* emit prompt (default) */

	/* Redirect stderr to stdout (so that driver will get all output
	 * on the pipe connected to stdout) */
	dup2(1, 2);

	/* Parse the command line */
	while ((c = getopt(argc, argv, "hvp")) >= 0) {
		switch (c) {
			case 'h':             /* print help message */
				usage();
				break;
			case 'v':             /* emit additional diagnostic info */
				verbose = 1;
				break;
			case 'p':             /* don't print a prompt */
				emit_prompt = 0;  /* handy for automatic testing */
				break;
			default:
				usage();
		}
	}

	/* Execute the shell's read/eval loop */
	while (1) {

		/* Read command line */
		if (emit_prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
			app_error("fgets error");
		if (feof(stdin)) { /* End of file (ctrl-d) */
			fflush(stdout);
			exit(0);
		}

		/* Evaluate the command line */
		eval(cmdline);
		fflush(stdout);
		fflush(stdout);
	} 

	exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit) then execute it
 * immediately. Otherwise, build a pipeline of commands and wait for all of
 * them to complete before returning.
*/
void eval(char *cmdline) 
{
	char *argv[MAXARGS];
	int argc = parseline(cmdline, argv);
	if (argc <= 0) {
	}

	int cmds[MAXARGS];
	int stdin_r[MAXARGS];
	int stdout_r[MAXARGS];
	int num_cmds = parseargs(argv, cmds, stdin_r, stdout_r);
	int prev_pipefd[2] = {-1, -1};
	int next_pipefd[2] = {-1, -1};
	int first_child;
	int child_pid[num_cmds];

	for (int i = 0; i < num_cmds; i++) {
		int is_bcmd = builtin_cmd(argv);
		if (is_bcmd != 0) {
			printf("builtin_cmd test returned nonzero: %d", is_bcmd);
		}


		if (num_cmds > 1) {
			if (i > 0) {
				prev_pipefd[0] = next_pipefd[0];
				prev_pipefd[1] = next_pipefd[1];
			}

			if (i < num_cmds - 1) {
				int pipe_r = pipe(next_pipefd);
				if (pipe_r < 0) {
					perror("pipe");
					exit(0);
				}
			}

		}

		int child = fork();
		if (child < 0) {
			perror("fork");
			exit(0);
		}

		else if (child > 0) {
			int setpgid_r;
			if (i == 0) {
				setpgid_r = setpgid(child, child);
			}
			else {
				setpgid_r = setpgid(child, first_child);
			}

			if (setpgid_r < 0) {
				perror("setpgid child");
				exit(0);
			}
		}

		if (child == 0) {
			if (stdin_r[i] > -1) {
				int new_infile = fileno(fopen(argv[stdin_r[i]], "r"));
				int dup_r = dup2(new_infile, STDIN_FILENO);
				if (dup_r < 0) {
					perror("Duplicate FD");
					exit(0);
				}
				int close_r = close(new_infile);
				if (close_r < 0) {
					perror("Close FD");
					exit(0);
				}
			}

			if (stdout_r[i] > -1) {
				int new_outfile = fileno(fopen(argv[stdout_r[i]], "w"));
				int dup_r = dup2(new_outfile, STDOUT_FILENO);
				if (dup_r < 0) {
					perror("Duplicate FD");
					exit(0);
				}
				int close_r = close(new_outfile);
				if (close_r < 0) {
					perror("Close FD");
					exit(0);
				}
			}

			if (num_cmds > 1) {
				if (i > 0) {
					int dup_r = dup2(prev_pipefd[0], STDIN_FILENO);
					if (dup_r < 0) {
						perror("Duplicate pipe read FD");
						exit(0);
					}
					int close_r = close(prev_pipefd[0]);
					printf("closing read end");
					if (close_r < 0) {
						perror("Close pipe read FD");
						exit(0);
					}
					int close_r_w = close(prev_pipefd[1]);
					if (close_r_w < 0) {
						perror("close pipe read fd");
						exit(0);
					}

				}
				if (i < num_cmds - 1) {
					int dup_r = dup2(next_pipefd[1], STDOUT_FILENO);
					if (dup_r < 0) {
						perror("Duplicate pipe write FD");
						exit(0);
					}
					int close_r1 = close(next_pipefd[1]);
					printf("closing write end");
					if (close_r1 < 0) {
						perror("Close pipe write fd");
						exit(0);
					}
					int close_r2 = close(next_pipefd[0]);
					if (close_r2 < 0) {
						perror("close pipe read fd");
						exit(0);
					}
				}
			}
			int exec_r = execve(argv[cmds[i]], &argv[cmds[i]], environ);
			if (exec_r < 0) {
				perror("Execute command");
				exit(0);
			}
		}

		else {
			if (i == 0) {
				first_child = child;
			}
			if (num_cmds > 1 && i > 0) {
				int close_r1 = close(prev_pipefd[0]);
				if (close_r1 < 0) {
					perror("close parent pipe read end");
				}
				int close_r2 = close(prev_pipefd[1]);
				if (close_r2 < 0) {
					perror("close parent pipe write end");
				}
			}

			child_pid[i] = child;
		}
	}

	for (int i = 0; i < num_cmds; i++) {
		waitpid(child_pid[i], NULL, 0);
	}


	return;
}

/*
 * parseargs - Parse the arguments to identify pipelined commands
 * 
 * Walk through each of the arguments to find each pipelined command.  If the
 * argument was | (pipe), then the next argument starts the new command on the
 * pipeline.  If the argument was < or >, then the next argument is the file
 * from/to which stdin or stdout should be redirected, respectively.  After it
 * runs, the arrays for cmds, stdin_redir, and stdout_redir all have the same
 * number of items---which is the number of commands in the pipeline.  The cmds
 * array is populated with the indexes of argv corresponding to the start of
 * each command sequence in the pipeline.  For each slot in cmds, there is a
 * corresponding slot in stdin_redir and stdout_redir.  If the slot has a -1,
 * then there is no redirection; if it is >= 0, then the value corresponds to
 * the index in argv that holds the filename associated with the redirection.
 * 
 */
int parseargs(char **argv, int *cmds, int *stdin_redir, int *stdout_redir) 
{
	int argindex = 0;    /* the index of the current argument in the current cmd */
	int cmdindex = 0;    /* the index of the current cmd */

	if (!argv[argindex]) {
		return 0;
	}

	cmds[cmdindex] = argindex;
	stdin_redir[cmdindex] = -1;
	stdout_redir[cmdindex] = -1;
	argindex++;
	while (argv[argindex]) {
		if (strcmp(argv[argindex], "<") == 0) {
			argv[argindex] = NULL;
			argindex++;
			if (!argv[argindex]) { /* if we have reached the end, then break */
				break;
			}
			stdin_redir[cmdindex] = argindex;
		} else if (strcmp(argv[argindex], ">") == 0) {
			argv[argindex] = NULL;
			argindex++;
			if (!argv[argindex]) { /* if we have reached the end, then break */
				break;
			}
			stdout_redir[cmdindex] = argindex;
		} else if (strcmp(argv[argindex], "|") == 0) {
			argv[argindex] = NULL;
			argindex++;
			if (!argv[argindex]) { /* if we have reached the end, then break */
				break;
			}
			cmdindex++;
			cmds[cmdindex] = argindex;
			stdin_redir[cmdindex] = -1;
			stdout_redir[cmdindex] = -1;
		}
		argindex++;
	}

	return cmdindex + 1;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
	static char array[MAXLINE]; /* holds local copy of command line */
	char *buf = array;          /* ptr that traverses command line */
	char *delim;                /* points to first space delimiter */
	int argc;                   /* number of args */
	int bg;                     /* background job? */

	strcpy(buf, cmdline);
	buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) /* ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
	}
	else {
		delim = strchr(buf, ' ');
	}

	while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'') {
			buf++;
			delim = strchr(buf, '\'');
		}
		else {
			delim = strchr(buf, ' ');
		}
	}
	argv[argc] = NULL;

	if (argc == 0)  /* ignore blank line */
		return 1;

	/* should the job run in the background? */
	if ((bg = (*argv[argc-1] == '&')) != 0) {
		argv[--argc] = NULL;
	}
	return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	if (!strcmp(argv[0], "quit")){
		exit(0);
	}

	return 0;     /* not a builtin command */
}

/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
	printf("Usage: shell [-hvp]\n");
	printf("   -h   print this message\n");
	printf("   -v   print additional diagnostic information\n");
	printf("   -p   do not emit a command prompt\n");
	exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

