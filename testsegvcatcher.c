#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

static char *prg_name;

void segv_handler(int sig) {
	fprintf(stderr, "%s caught signal %d.\n", prg_name, sig);
	exit(0);
}

int main(__attribute__((unused)) int argc, char *argv[]) {
	struct sigaction segv = {
		.sa_handler = segv_handler,
		.sa_flags = 0
	};
	sigemptyset(&segv.sa_mask);
	sigaction(SIGSEGV, &segv, NULL);
	prg_name = argv[0];
	fprintf(stderr, "%s started.\n", prg_name);
	while(1)
		pause();
	return 0;
}
