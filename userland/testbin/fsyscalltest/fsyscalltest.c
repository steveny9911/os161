/*
 * fsyscalltest.c
 *
 * Tests file-related system calls open, close, read and write.
 *
 * Should run on emufs. This test allows testing the file-related system calls
 * early on, before much of the functionality implemented. This test does not
 * rely on full process functionality (e.g., fork/exec).
 *
 * Much of the code is borrowed from filetest.c
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <limits.h>

static int openFDs[OPEN_MAX-3 + 1];

/*
 * This test makes sure that the underlying filetable implementation
 * allows us to open as many files as is allowed by the limit on the system.
 */
static void
test_openfile_limits()
{
	const char *file;
	int fd, rv, i;

	file = "testfile1";

	/* We should be allowed to open this file OPEN_MAX - 3 times, 
	 * because the first 3 file descriptors are occupied by stdin, 
	 * stdout and stderr. 
	 */
	printf("STEP 1\n");
	for(i = 0; i < (OPEN_MAX-3); i++)
	{
		fd = open(file, O_RDWR|O_CREAT|O_TRUNC, 0664);
		if (fd<0)
			err(1, "%s: open for %dth time", file, (i+1));

		if( (fd == 0) || (fd == 1) || (fd == 2))
			err(1, "open for %s returned a reserved file descriptor",
			    file);

		/* We do not assume that the underlying system will return
		 * file descriptors as consecutive numbers, so we just remember
		 * all that were returned, so we can close them. 
		 */
		openFDs[i] = fd;
	}

	printf("STEP 2\n");
	/* This one should fail. */
	fd = open(file, O_RDWR|O_CREAT|O_TRUNC, 0664);
	if(fd > 0)
		err(1, "Opening file for %dth time should fail, as %d "
		    "is the maximum allowed number of open files and the "
		    "first three are reserved. \n",
		    (i+1), OPEN_MAX);
	printf("open succeed!\n");

	printf("STEP 3\n");
	/* Let's close one file and open another one, which should succeed. */
	rv = close(openFDs[0]);
	if (rv<0)
		err(1, "%s: close for the 1st time", file);
	printf("close succeed!\n");
	
	fd = open(file, O_RDWR|O_CREAT|O_TRUNC, 0664);
	if (fd<0)
		err(1, "%s: re-open after closing", file);
	printf("re-open succeed!\n");

	rv = close(fd);
	if (rv<0)
		err(1, "%s: close for the 2nd time", file);
	printf("re-close succeed!\n");

	printf("STEP 4\n");
	/* Begin closing with index "1", because we already closed the one
	 * at slot "0".
	 */
	for(i = 1; i < OPEN_MAX - 3; i++)
	{
		rv = close(openFDs[i]);
		if (rv<0)
			err(1, "%s: close file descriptor %d", file, i);
	}
}

/* This test takes no arguments, so we can run it before argument passing
 * is fully implemented. 
 */
int
main()
{
	printf("\n===Starting fsyscalltest!===\n");

	test_openfile_limits();
	printf("Passed Part 1 of fsyscalltest\n");

	// simple_test();
	// printf("Passed Part 2 of fsyscalltest\n");
	
	// simultaneous_write_test();
	// printf("Passed Part 3 of fsyscalltest\n");
	
	// test_dup2();
	// printf("Passed Part 4 of fsyscalltest\n");

	// dir_test();
	// printf("Passed Part 5 of fsyscalltest\n");
	
	printf("All done!\n");
	
	return 0;
}
