
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
#define MSR_NEHALEM_PLATFORM_INFO	0xCE
#define MSR_NEHALEM_TURBO_RATIO_LIMIT	0x1AD
#define MSR_APERF	0xE8
#define MSR_MPERF	0xE7
#define MSR_PKG_C3_RESIDENCY	0x3F8
#define MSR_PKG_C6_RESIDENCY	0x3F9
#define MSR_PKG_C7_RESIDENCY	0x3FA
#define MSR_CORE_C3_RESIDENCY	0x3FC
#define MSR_CORE_C3_RESIDENCY	0x3FC
#define MSR_CORE_C3_RESIDENCY	0x3FC
#define MSR_CORE_C6_RESIDENCY	0x3FD


unsigned int interval_sec = 5;	/* set with -i interval_sec */
unsigned int verbose;		/* set with -v */
unsigned int debug;		/* set with -d */
unsigned int do_c0, skip_c0;
unsigned int do_c1, skip_c1;
unsigned int do_c3;
unsigned int do_c6;
unsigned int do_pkg;
unsigned int do_aperf = 1;	/* TBD set with CPUID */
unsigned int iterations;
unsigned int units = 1000000000.0;	/* Ghz etc */
unsigned int do_non_stop_tsc;
unsigned int do_nehalem_c_state_residency;
unsigned int do_nehalem_platform_info;
unsigned int do_nehalem_turbo_ratio_limit;
unsigned int do_nehalem_c_state_residency;

int aperf_mperf_unstable;
int backwards_count;
char *progname;

int num_cpus;
int *fd_msr;

typedef struct per_core_counters {
	unsigned long long tsc;
	unsigned long long c1;
	unsigned long long c3;
	unsigned long long c6;
	unsigned long long aperf;
	unsigned long long mperf;
	unsigned long long pc3;
	unsigned long long pc6;
	unsigned long long pc7;
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
void print_header()
{
	
	if (do_pkg) fprintf(stderr, "  %%pc6 ");

	putc('\n', stderr);
}

void print_counters(PCC *cnt, PCC *avg) 
{
	int i;
	double interval_float;

	interval_float = tv_delta.tv_sec + tv_delta.tv_usec/1000000.0;

	if (debug)
		fprintf(stderr, "%.6f sec\n", interval_float);

	//print_header();
    
		PCC *p;


		if (i == -1)
                {
			p = avg;
			fprintf(stderr, "avg");
		} 
		else 
		{
			p = &cnt[i];
			//fprintf(stderr, "%4d",i);
		}


		if (do_aperf)
	        {
			if (!aperf_mperf_unstable)
			 {
				//fprintf(stderr, "%7.2f",1.0 * p->tsc / units * p->aperf /p->mperf / interval_float);
			 } 
			 else
			 {
			   if (p->aperf > p->tsc || p->mperf > p->tsc) 
                           {
                            fprintf(stderr, "   ****");
			   } 
			 else
			   {
			    fprintf(stderr, "%6.1f*",1.0 * p->tsc / units * p->aperf /p->mperf / interval_float);
			   }
		        }
	       }

		
		if (do_pkg)
			fprintf(stderr, "%7.2f",100.0 * p->pc6/p->tsc);
		
		putc('\n', stderr);
	}
//}


#define SUBTRACT_COUNTER(after, before, delta) (delta = (after - before), (before > after))


int compute_delta(PCC *after, PCC *before)
{
	int i;
	int error, error1, error2;

	skip_c0 = skip_c1 = 0;

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
		error1 = SUBTRACT_COUNTER(after[i].c3, before[i].c3, pcc_delta[i].c3);
		error2 = SUBTRACT_COUNTER(after[i].c6, before[i].c6, pcc_delta[i].c6);
		if (error1 || error2)
	        {
			fprintf(stderr, "c3 or c6 residency counter went backwards\n");
			_exit(-1);
		}
		error = SUBTRACT_COUNTER(after[i].pc3, before[i].pc3, pcc_delta[i].pc3);
		error1 = SUBTRACT_COUNTER(after[i].pc6, before[i].pc6, pcc_delta[i].pc6);
		error2 = SUBTRACT_COUNTER(after[i].pc7, before[i].pc7, pcc_delta[i].pc7);
		if (error || error1 || error2)
	        {
			fprintf(stderr, "package residency counter went backwards\n");
			_exit(-1);
		}

		error1 = SUBTRACT_COUNTER(after[i].aperf, before[i].aperf, pcc_delta[i].aperf);
		error2 = SUBTRACT_COUNTER(after[i].mperf, before[i].mperf, pcc_delta[i].mperf);
		if (error1 || error2) 
		{
			if (!aperf_mperf_unstable) 
			{
				fprintf(stderr, "%s: APERF or MPERF went backwards *\n", progname);
				fprintf(stderr, "* Frequency results do not cover entire interval *\n");
				fprintf(stderr, "* fix this by running Linux-2.6.30 or later *\n");

				aperf_mperf_unstable = 1;
			}
			/*
			 * mperf delta is likely a huge "positive" number
			 * can not use it for calculating c0 time
			 */
			skip_c0 = 1;
			skip_c1 = 1;
		}

		/*
 		 * As mperf and tsc collection are not atomic,
 		 * it is possible for mperf's non-halted cycles
 		 * to exceed TSC's all cycles: show c1l = 0% in that case.
 		 */
		if (pcc_delta[i].mperf > pcc_delta[i].tsc)
			pcc_delta[i].c1 = 0;
		else /* normal case, derive c1 */
			pcc_delta[i].c1 = pcc_delta[i].tsc - pcc_delta[i].mperf - pcc_delta[i].c3 - pcc_delta[i].c6;

		if (pcc_delta[i].mperf == 0)
			pcc_delta[i].mperf = 1;	/* divide by 0 protection */
	}
}

void compute_average(PCC *delta, PCC *avg)
{
	int i;
	PCC *sum;

	sum = calloc(1, sizeof(PCC));
	if (sum == NULL)
        {
		perror("calloc sum");
		_exit(-1);
	}

	for (i = 0; i < num_cpus; ++i)
	{
		sum->tsc += delta[i].tsc;
		sum->c1 += delta[i].c1;
		sum->c3 += delta[i].c3;
		sum->c6 += delta[i].c6;
		sum->aperf += delta[i].aperf;
		sum->mperf += delta[i].mperf;
		sum->pc3 += delta[i].pc3;
		sum->pc6 += delta[i].pc6;
		sum->pc7 += delta[i].pc7;
	}
	avg->tsc = sum->tsc/num_cpus;
	avg->c1 = sum->c1/num_cpus;
	avg->c3 = sum->c3/num_cpus;
	avg->c6 = sum->c6/num_cpus;
	avg->aperf = sum->aperf/num_cpus;
	avg->mperf = sum->mperf/num_cpus;
	avg->pc3 = sum->pc3/num_cpus;
	avg->pc6 = sum->pc6/num_cpus;
	avg->pc7 = sum->pc7/num_cpus;

	free(sum);
}


void get_counters(PCC *c)
{
	int i;

	for (i = 0; i < num_cpus; ++i)
	{
		c[i].tsc = get_msr(i, MSR_TSC);
		if (do_c3) c[i].c3 = get_msr(i, MSR_CORE_C3_RESIDENCY);
		if (do_c6) c[i].c6 = get_msr(i, MSR_CORE_C6_RESIDENCY);
		if (do_aperf) c[i].aperf = get_msr(i, MSR_APERF);
		if (do_aperf) c[i].mperf = get_msr(i, MSR_MPERF);
		if (do_pkg) c[i].pc3 = get_msr(i, MSR_PKG_C3_RESIDENCY);
		if (do_pkg) c[i].pc6 = get_msr(i, MSR_PKG_C6_RESIDENCY);
		if (do_pkg) c[i].pc7 = get_msr(i, MSR_PKG_C7_RESIDENCY);
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
 	        compute_average(pcc_delta, pcc_average);
		print_counters(pcc_delta, pcc_average);

		sleep(interval_sec);
		get_counters(pcc_even);
		gettimeofday(&tv_even, (struct timezone *)NULL);
		compute_delta(pcc_even, pcc_odd);
		timersub(&tv_even, &tv_odd, &tv_delta);
		compute_average(pcc_delta, pcc_average);
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

int has_nehalem_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (family != 6)
		return 0;

	switch(model) {
	case 0x1A:	/* Core i7, Xeon 5500 series - Nehalem */
	case 0x1E:	/* Core i7 and i5 Processor - Lynnfield, Jasper Forest */
	case 0x1F:	/* Core i7 and i5 Processor - Nehalem */
	case 0x25:	/* Westmere */
	case 0x2C:	/* Westmere */
		return 1;
	case 0x2E:	/* Nehalem-EX Xeon */
	default:
		return 0;
	}
}

int has_non_stop_tsc(unsigned int family, unsigned int model)
{
	if (family != 6)
		return 0;

	switch(model) {
	case 0x1A:	/* Core i7, Xeon 5500 series */
	case 0x1E:	/* Core i7 and i5 Processor */
	case 0x1F:	/* Core i7 and i5 Processor */
	case 0x2E:	/* Nehalem Xeon */
	case 0x25:	/* Westmere */
	case 0x2C:	/* Westmere */
		return 1;
	default:
		return 0;
	}
}



void do_cpuid()
{
	unsigned int eax, ebx, ecx, edx, max_level;
	char brand[16];
	unsigned int fms, family, model, stepping, ht_capable;

	eax = ebx = ecx = edx = 0;

	asm("cpuid" : "=a" (max_level), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (0));

	sprintf(brand, "%.4s%.4s%.4s", &ebx, &edx, &ecx);

	if (strncmp(brand, "GenuineIntel", 12)) {
		fprintf(stderr, "CPUID: %s GenuineIntel\n", brand);
		_exit(-1);
	}

	asm("cpuid" : "=a" (fms), "=c" (ecx), "=d" (edx) : "a" (1) : "ebx");
	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	stepping = fms & 0xf;
	if (family == 6 || family == 0xf)
		model += ((fms >> 16) & 0xf) << 4;

	if (!(edx & (1 << 5))) {
		fprintf(stderr, "CPUID: no MSR\n");
		_exit(-1);
	}

	ht_capable = edx & (1 << 28);

	if (verbose)
		fprintf(stderr, "CPUID %s %d levels family:model:stepping %d:%d:%d\n",
			brand, max_level, family, model, stepping);

	do_non_stop_tsc = has_non_stop_tsc(family, model);
	do_nehalem_platform_info = do_non_stop_tsc;
	do_nehalem_c_state_residency =  do_non_stop_tsc;

	do_nehalem_turbo_ratio_limit = has_nehalem_turbo_ratio_limit(family, model);
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

	do_c0 = do_c1 = do_c3 = do_c6 = do_pkg = do_nehalem_c_state_residency;

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

