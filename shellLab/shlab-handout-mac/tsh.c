/* 
 * tsh - A tiny shell program with job control
 * 
 * Jonathan Armknecht
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
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);
pid_t Fork(void); //Wrapper function for fork()


/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
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

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

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
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    char *argv[MAXARGS]; //Argument list execve()
    int bg;  //Should job be done in the background or foreground
    pid_t pid; //Process ID
    struct job_t *job = NULL; //initilize as NULL
    sigset_t mask; //Makes a signal set name mask
    sigemptyset(&mask); //Sets the mask set to empty
    sigaddset(&mask, SIGCHLD); //Set the mask set to include SIGCHLD
    sigaddset(&mask, SIGINT); //Set the mask set to include SIGINT
    sigaddset(&mask, SIGTSTP); //Set the mask set to include SIGTSTP

    
    bg = parseline(cmdline, argv); //parses cmd line and builds argv

    if (argv[0] == NULL) {
        return; //Ignore empty lines
    }
    if (!builtin_cmd(argv)) { //Runs only if it is not a built in command
        // no old set that's why NULL, also SIG_BLOCK sets sigs in mask as blocked so children don't terminate before adding to jobs list
        if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1){ 
            unix_error("sigprocmask: blocking failure");
        }
        if ((pid = Fork()) == 0) { //Child process created and runs users job
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) { //unblock signals in child process
                unix_error("sigprocmask: unblocking failure");
            }
            setpgid(0, 0); //Child is now in a new process group with pgid same as child's pid

            if (execve(argv[0], argv, environ) < 0) {
                printf("%s: Command not found\n", argv[0]);
                exit(0);
            }
        }
        //Parent waits for foreground job to terminate
        if (!bg) {
            addjob(jobs, pid, FG, cmdline); //takes the jobs array and adds the pid says its in the FG and adds cmdline
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) { //unblock signals in parent process
                unix_error("sigprocmask: unblocking failure");
            }
            waitfg(pid);
        }
        else {
            addjob(jobs, pid, BG, cmdline); //Adds job to jobs array and says its in the BG
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) { //unblock signals in parent process
                unix_error("sigprocmask: unblocking failure");
            }
            job = getjobpid(jobs, pid); //gets the job from jobs array through pid
            printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline); //prints the job in correct format
        }
    }
    return;
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
    if (!strcmp(argv[0], "quit")) { //if argv[0] is quit 0 is returned and turned to 1 so it exits
        exit(0);
    }
    if (!strcmp(argv[0], "fg") || !strcmp(argv[0], "bg")) { //if cmd is fg or bg run do_bgfg
        do_bgfg(argv);
        return 1; //returns 1 to show that there is a builtin_cmd that needs to be ran
    }
    if (!strcmp(argv[0], "jobs")) { //if cmd is jobs list all the jobs
        listjobs(jobs);
        return 1; //returns 1 to show that there is a builtin_cmd that needs to be ran
    }
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    struct job_t *job = NULL; //initialize job to NULL
    int jID = 0; //keeps track of the JOBID
    int pid = 0; //keeps track of the ProcessID

    if (argv[1] == NULL) { //if the person just typed fg or bg with no other cmds
        //prints out a help line for correct usage of fg or bg
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }
    if (argv[1][0] == '%') { 
        /*if the point at 1 its pointing to something check to see if it is % in index 0 
        * if it is % then we are looking at the JOBID
        * Next instruction takes what is actually pointed to by the pointer of a pointer*/
        jID = atoi(&argv[1][1]); 
        /*atoi will return number or 0 if it doesn't work
        * it reads till \0 which was added by parseline*/
        if (!jID) { //if jID is 0 then it was not a number
            printf("%s: argument must be a PID or %%jobid\n", argv[0]);
            return;
            //this error is only printed of if its not a number after %
        }
        job = getjobjid(jobs, jID);
        if (job == NULL) {
            printf("%s: No such job\n", argv[1]); //prints what was the command saying there isn't a job one for this
            return;
        }
    }
    else { //looking for the job by PID
        pid = atoi(argv[1]); 
        /*only one thing stored at argv[1] and thats the number read this number till \0
        * that was from the parseline function there isn't a double pointer cause there is only one thing there*/
        if (!pid) { //if pid is 0 then it was not a number
            printf("%s: argument must be a PID or %%jobid\n", argv[0]);
            return;
            //this error is only printed of if its not a number after fg/bg
        }
        job = getjobpid(jobs, pid);
        if (job == NULL) {
             printf("(%s): No such process\n", argv[1]); //prints what was the command saying there isn't a job one for this
            return;
        }
    }
    /*this multiplies the PID by -1 so that the PID is less than -1
    *when it is pid < -1 the signal is sent to every process whose pgid is pid*/
    if (kill((-1)*(job->pid), SIGCONT) < 0) {
        unix_error("Kill Error!");
    }
    if (!strcmp(argv[0], "fg")) { //if the process is in the foreground
        job->state = FG; //change the jobs state to foreground
        waitfg(job->pid); //wait for job to finish before doing anything else
        if (job->state != ST) {
            deletejob(jobs,job->pid); //delete the job only if it is not stopped this ensures one job is in the fg
        }
    }
    else { //proces is a background process
        job->state = BG; //set the jobs state to background
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline); //prints the job in correct format
    }

    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    struct job_t *job = getjobpid(jobs, pid); //assigns the fg job to job

    if (job == NULL) { //there isn't a fg procress to wait for
        return;
    }
    while (job->pid == pid && job->state == FG) { //if job is fg sleep until it is completed
        sleep(1);
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    pid_t childPID; //holds pid of child
    int status; //holds the status variable returned from waitpid
    struct job_t *job = NULL; //holds the job

    /*
    * This while loop uses waitpid, -1 means it will wait for any child process
    * The status which is not NULL, is used by waitpid to store the status of call
    * WNOHANG returns right away if there are no children that have exited (speeds things up)
    * WUNTRACED returns right away if a stopped child has been resumed by SIGCONT
    * The greater than zero is to see if there is a child process ID whose state has changed
    * IF there if one more more children exist with the pid but haven't changed state WNOHANG makes
    * waitpid return 0 and thus the loop isn't entered. Used to reap children
    */
    while ((childPID = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        job = getjobpid(jobs, childPID);
        if (job == NULL) {
            printf("(%d): no child exists\n", childPID);
            return;
        }
        if (WIFSTOPPED(status)) { //Did the child get stopped by a signal done using WUNTRACED
            job->state = ST; //change state to stop
            printf("Job [%d] (%d) stopped by signal 20\n", job->jid, childPID); //print what was stopped
        }
        else if (WIFSIGNALED(status)) { //was the child process terminated by a signal if so do this
            printf("Job [%d] (%d) terminated by signal 2\n", job->jid, childPID); //print terminated job
            deletejob(jobs, childPID); //delete job from jobs array
        }
        else if (WIFEXITED(status)) { //did the child terminate normally is so do this
            deletejob(jobs, childPID); //delete from job list
        }
        else {
            unix_error("wait error\n"); //all other statuses don't matter
        }
    }
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    pid_t fgPID; //stores the fg job pid
    fgPID = fgpid(jobs);
    if (fgPID == 0) { //if there is no job in the foreground just return
        return;
    }
    else {
    /*this multiplies the fgPID by -1 so that the fgPID is less than -1
    *when it is pid < -1 the signal is sent to every process whose pgid is pid*/
        if (kill((-1)*(fgPID), SIGINT) < 0) { //Send SIGINT to the foreground process and its group
            unix_error("Kill Error!");
        }
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    pid_t fgPID; //stores the fg job pid
    fgPID = fgpid(jobs);
    if (fgPID == 0) { //if there is no job in the foreground just return
        return;
    }
    else {
    /*this multiplies the fgPID by -1 so that the fgPID is less than -1
    *when it is pid < -1 the signal is sent to every process whose pgid is pid*/
        if (kill((-1)*(fgPID), SIGTSTP) < 0) { //Send SIGTSTP to the foreground process and its group
            unix_error("Kill Error!");
        }
    }
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


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

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
/*
 * Wrapper function for fork from the book
 */
pid_t Fork(void) {
    pid_t pid;
    if ((pid = fork()) < 0) {
        unix_error("Fork error");
    }
    return pid;
}

