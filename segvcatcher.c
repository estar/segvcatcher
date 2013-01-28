/* vim: set fenc=utf-8 : */
/* Produce backtrace on SIGSEGV in an uncooperative process with its
 * own SIGSEGV handler.
 *
 * This is a shared library intended for use with a host binary via
 * LD_PRELOAD. The operating principle is this:
 * 1. Run LD_PRELOAD=libtracesegv.so /your/binary -args...
 * 2. setup() runs. It sets up a signal handler for SIGUNUSED
 *    (hopefully indeed unused in the host) and forks a child.
 * 3. The child waits DELAY seconds and sends SIGUNUSED to the host.
 * 4. The SIGUNUSED handler sets up this library’s SIGSEGV handler and
 *    saves a pointer to the host’s SIGSEGV handler.
 * 5. If a segmentation fault occurs, this library’s handler prints a
 *    backtrace, then calls the host’s handler for further actions.
 *
 * The idea behind the delay is to give the host enough time to set up
 * its own SIGSEGV handler before overwriting it with the new one. The
 * DELAY macro should be adjusted accordingly.
 *
 * Compilation instructions:
 *   gcc -o libtracesegv.so segvcatcher.c -shared -fPIC -Wall -Wextra
 */
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <execinfo.h>

#define DELAY 3 /* seconds */
#define IPC_SIG SIGUNUSED /* signal on which the SIGSEGV handler is
			   * installed */
#define TRACEBUFLEN 64 /* maximum number of stack frames to trace */

/* Pointer to the host’s SIGSEGV handler function. */
static void (*original_segv_handler)(int);

/* Macro for writing to stderr from the SIGSEGV handler. */
#define WRITE(msg) (write(STDERR_FILENO, (msg), sizeof(msg) - 1))

/* SIGSEGV handler
 * It is important that this function does not do anything which might
 * cause a recursive fault, such as memory allocation with malloc()
 * (which includes things like printf()).
 */
static void handle_segv(int x) {
	static void *trace[TRACEBUFLEN];
	static int traces;
	WRITE("SIGSEGV received. Backtrace:\n");
	/* Obtain the backtrace. */
	traces = backtrace(trace, TRACEBUFLEN);
	/* Print the backtrace to stderr.
	 * This function is specified not to use malloc() etc.
	 */
	backtrace_symbols_fd(trace, traces, STDERR_FILENO);
	WRITE("End of backtrace. ");
	/* Call the host’s SIGSEGV handler for anything else. */
	if(original_segv_handler != SIG_IGN &&
			original_segv_handler != SIG_DFL) {
		WRITE("Calling original SIGSEGV handler.\n");
		original_segv_handler(x);
	} else {
		WRITE("No other SIGSEGV handler available. "
			"Quitting.\n");
		_exit((1<<7) | x);
	}
}

/* Set up the SIGSEGV handler.
 * This function is the SIGUNUSED handler, used in setup().
 */
static void set_segv(__attribute__((unused)) int x) {
	struct sigaction orig, new = {
		.sa_handler = handle_segv,
		.sa_flags = 0,
	};
	sigemptyset(&new.sa_mask);
	sigaction(SIGSEGV, &new, &orig);
	/* Save the host’s SIGSEGV handler. */
	original_segv_handler = orig.sa_handler;
}

/* Child process:
 * Wait DELAY seconds.
 * Then send SIGUNUSED to the host, to set off set_segv().
 */
static void child(void) {
	time_t t, start_time = time(NULL), zzz;
	do {
		t = time(NULL);
		zzz = start_time + DELAY - t;
		if(zzz > 0)
			sleep(zzz);
	} while(t < start_time + DELAY);
	kill(getppid(), IPC_SIG);
}

/* This function is called when the library is loaded.
 * It sets up a SIGUNUSED handler in the host, to call set_segv, and
 * forks a child process which will send SIGUNUSED to the host after
 * DELAY seconds. The child process then terminates. Dealing with the
 * zombie is left to the host process… but it’s not important anyway.
 */
__attribute__((constructor)) static void setup(void) {
	struct sigaction a = {
		.sa_handler = set_segv,
		.sa_flags = SA_RESTART | SA_RESETHAND,
	};
	if(!fork()) {
		child();
		_exit(0);
	}
	sigemptyset(&a.sa_mask);
	sigaction(IPC_SIG, &a, NULL);
}
