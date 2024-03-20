/* $begin myprog1c */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_LINE 1024

int main(int argc, char *argv[]){
	char *buf;
	char arg1[512], content[MAX_LINE];
	int len;

	buf = getenv("QUERY_STRING");
	len = atoi(getenv("CONTENT_LENGTH"));
	char con[len + 1];
	con[len + 1] = '\0';
	fread(con, sizeof(con), 1, stdin);
	sprintf(content, "Hello CS324\nQuery string: %s\nRequest body: %s", buf, con);

	fprintf(stdout, "Content-Type: text/plain\r\n");
	fprintf(stdout, "Content-Length: %d\r\n", (int)strlen(content));
	fprintf(stdout, "\r\n");

	fprintf(stdout, "%s", content);
	fflush(stdout);

	return 0;
}
