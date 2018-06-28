
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>

#define MSR_TSC	0x10
#define MSR_PKG_C6_RESIDENCY	0x3F9

unsigned int interval_sec = 5;	/* set with -i interval_sec */
unsigned int debug;		/* set with -d */
unsigned int do_pkg;
unsigned int iterations;
unsigned int units = 1000000000.0;	/* Ghz etc */
unsigned int do_non_stop_tsc;
unsigned int do_nehalem_c_state_residency;
int num_cpus;
int *fd_msr;

typedef struct per_core_counters {
	unsigned long long tsc;
	unsigned long long pc6;
	
} PCC;

PCC *pcc_even;
PCC *pcc_odd;
PCC *pcc_delta;
PCC *pcc_average;
struct timeval tv_even;
struct timeval tv_odd;
struct timeval tv_delta;

unsigned long long get_msr(int cpu, off_t offset)
{
	ssize_t retval;
	unsigned long long msr;

	retval = pread(fd_msr[cpu], &msr, sizeof msr, offset);
	if (retval != sizeof msr) {
		fprintf(stderr, "pread cpu%d 0x%x = %d\n", cpu, offset, retval);
		_exit(-2);
	}
	return msr;
}


void print_counters(PCC *cnt, PCC *avg) 
{
	int i;
	double interval_float;

	interval_float = tv_delta.tv_sec + tv_delta.tv_usec/1000000.0;

	if (debug)
		fprintf(stderr, "%.6f sec\n", interval_float);

		PCC *p;

		if (i == -1)
                {
			p = avg;
			fprintf(stderr, "avg");
		} 
		else 
	    	{
			p = &cnt[i];
		}

		
		if (do_pkg)
			fprintf(stderr, "%7.2f",100.0 * p->pc6/p->tsc);
		
		putc('\n', stderr);
	}



#define SUBTRACT_COUNTER(after, before, delta) (delta = (after - before), (before > after))
int compute_delta(PCC *after, PCC *before)
{
	int i;
	int error, error1, error2;

	for (i = 0; i < num_cpus; ++i) 
	{
		error = SUBTRACT_COUNTER(after[i].tsc, before[i].tsc, pcc_delta[i].tsc);
		if (error)
		 {
			fprintf(stderr, "TSC went backwards %llX to %llX\n",after[i].tsc, before[i].tsc);
		}
		if (pcc_delta[i].tsc < (1000 * 1000) ) 
		{ /* check for TSC < 1 Mcycles over interval */
			fprintf(stderr, "Insanely slow TSC rate, TSC stops in idle?\n");
			fprintf(stderr, "You can disable all c-states by booting with \"idle=poll\"\n");
			fprintf(stderr, "or just the deep ones with \"processor.max_cstate=1\"\n");
			_exit(-3);
		}
		
		error1 = SUBTRACT_COUNTER(after[i].pc6, before[i].pc6, pcc_delta[i].pc6);
		if ( error1 )
	        {
			fprintf(stderr, "package residency counter went backwards\n");
			_exit(-1);
		}

	}
}


void get_counters(PCC *c)
{
	int i;

	for (i = 0; i < num_cpus; ++i)
	{
		c[i].tsc = get_msr(i, MSR_TSC);
		if (do_pkg) c[i].pc6 = get_msr(i, MSR_PKG_C6_RESIDENCY);
		
	}

}

void turbostat_loop()
{
	get_counters(pcc_even);
	gettimeofday(&tv_even, (struct timezone *)NULL);

	for (iterations = 1; ; iterations++) 
	{
		sleep(interval_sec);
		get_counters(pcc_odd);
		gettimeofday(&tv_odd, (struct timezone *)NULL);

		compute_delta(pcc_odd, pcc_even);
		timersub(&tv_odd, &tv_even, &tv_delta);
		print_counters(pcc_delta, pcc_average);

		sleep(interval_sec);
		get_counters(pcc_even);
		gettimeofday(&tv_even, (struct timezone *)NULL);
		compute_delta(pcc_even, pcc_odd);
		timersub(&tv_even, &tv_odd, &tv_delta);
		print_counters(pcc_delta, pcc_average);
	}
}

void check_dev_msr() {
	struct stat sb;

	if (stat("/dev/cpu/0/msr", &sb)) {
		fprintf(stderr, "no /dev/cpu/0/msr\n");
		fprintf(stderr, "Please load the msr driver\n");
		_exit(-5);
	}
}

int has_non_stop_tsc(unsigned int family, unsigned int model)
{
	if (family != 6)
		return 0;

	switch(model)
      {
	case 0x25:	/* Westmere */
		return 1;
	default:
		return 0;
	}
}

void do_cpuid()
{
	unsigned int eax, ebx, ecx, edx, max_level;
	char brand[16];
	unsigned int fms, family, model, stepping;

	eax = ebx = ecx = edx = 0;

	asm("cpuid" : "=a" (max_level), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (0));
	asm("cpuid" : "=a" (fms), "=c" (ecx), "=d" (edx) : "a" (1) : "ebx");

	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	stepping = fms & 0xf;
	if (family == 6 || family == 0xf)
		model += ((fms >> 16) & 0xf) << 4;

	do_non_stop_tsc = has_non_stop_tsc(family, model);
	do_nehalem_c_state_residency =  do_non_stop_tsc;
	
}

void turbostat_init()
{
	int i;
	struct rlimit rl;

	num_cpus = sysconf(_SC_NPROCESSORS_ONLN);

	if (debug)
		fprintf(stderr, "num_cpus %d\n", num_cpus);

	getrlimit(RLIMIT_NOFILE, &rl);
	if (rl.rlim_cur < num_cpus)
	 {
		/*
		 * We have more cpus than available file descriptors.
		 * Increase the number of file descriptors to prevent
		 * open(msr_path, O_RDONLY) from failing with EMFILE
		 * "Too many open files".
		 */
		if (rl.rlim_max < num_cpus)
			fprintf(stderr, "More cpus (%d) than file descriptors (%d).  Cpus will be limited\n", num_cpus, rl.rlim_max);
		rl.rlim_cur = rl.rlim_max;
		setrlimit(RLIMIT_NOFILE, &rl);
	}

	fd_msr = (int *)calloc(num_cpus, sizeof(int));
	pcc_even = (PCC *)calloc(num_cpus, sizeof(PCC));
	pcc_odd = (PCC *)calloc(num_cpus, sizeof(PCC));
	pcc_delta = (PCC *)calloc(num_cpus, sizeof(PCC));
	pcc_average = (PCC *)calloc(1, sizeof(PCC));

	if ((fd_msr == 0) || (pcc_even == 0) || (pcc_odd == 0) ||(pcc_delta == 0) || (pcc_average == 0)) 
	{
		perror("calloc");
		_exit(-1);
	}

	do_cpuid();

	do_pkg = do_nehalem_c_state_residency;

	check_dev_msr();

	for (i = 0; i < num_cpus; ++i) 
	{
		char msr_path[32];

		sprintf(msr_path, "/dev/cpu/%d/msr", i);
		fd_msr[i] = open(msr_path, O_RDONLY);
		if (fd_msr[i] < 0) 
		{
			perror(msr_path);
			break;
		}
	}
	num_cpus = i;
	if (num_cpus == 0)
		_exit(-1);
	
}

int main(int argc, char **argv)
{
	turbostat_init();
	turbostat_loop();	
	return 0;
}
