#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/types.h>

int main(int argc, char *argv[]) {
	int pid;
	int pipefd[2];

	printf("Starting program; process has pid %d\n", getpid());
	FILE * fp; fp = fopen("fork-output.txt", "w+");
	fprintf(fp, "%s", "BEFORE FORK\n");
	fflush(fp);

	pipe(pipefd);

	if ((pid = fork()) < 0) {
		fprintf(stderr, "Could not fork()");
		exit(1);
	}

	/* BEGIN SECTION A */

	printf("Section A;  pid %d\n", getpid());
//	sleep(5);

	/* END SECTION A */
	if (pid == 0) {
		/* BEGIN SECTION B */

		printf("Section B\n");
		close(pipefd[0]);
		FILE * fd_one = fdopen(pipefd[1], "w");
//		sleep(10);
		fputs("hello from Section B\n", fd_one);

//		sleep(5);
		fprintf(fp, "SECTION B (%d)\n", fileno(fp));
//		sleep(30);
//		printf("Section B done sleeping\n");
//		sleep(10);
		close(pipefd[1]);

		char *newenviron[] = { NULL };

		printf("Program \"%s\" has pid %d. Sleeping.\n", argv[0], getpid());
//		sleep(30);

		if (argc <= 1) {
			printf("No program to exec. Exiting...\n");
			exit(0);
		}

		printf("Running exec of \"%s\"\n", argv[1]);

		int file_descriptor = fileno(fp);
		dup2(fileno(fp), 1);

		execve(argv[1], &argv[1], newenviron);
		printf("End of progam \"%s\".\n", argv[0]);


		exit(0);

		/* END SECTION B */
	} else {
		/* BEGIN SECTION C */

		printf("Section C\n");
		fprintf(fp, "SECTION C (%d)\n", fileno(fp));
		fclose(fp);

		close(pipefd[1]);
		FILE * fd;
		fd = fdopen(pipefd[0], "r");
		char buf[100];
		int numBytes = read(pipefd[0], buf, 100);
		printf("Bytes read: %d\n", numBytes);
		fgets(buf,100,fd);
		fputs(buf,stdout);

//		int numBytes2 = read(pipefd[0], buf, 100);
//		printf("Bytes read: %d\n", numBytes2);
//		fgets(buf,100,fd);
//		fputs(buf,stdout);
//		close(pipefd[0]);

//		sleep(5);
//		waitpid(pid,NULL,0);
//		sleep(30);
//		printf("Section C done sleeping\n");

		exit(0);

		/* END SECTION C */
	}
	/* BEGIN SECTION D */

	printf("Section D\n");
//	sleep(30);

	/* END SECTION D */
}

