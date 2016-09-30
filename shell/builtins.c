#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <config.h>

#include "builtins.h"

int echo(char*[]);
int lexit(char *[]);
int lcd(char *[]);
int lkill(char *[]);
int lls(char *[]);

builtin_pair builtins_table[]={
	{"exit",	&lexit},
	{"lecho",	&echo},
	{"lcd",		&lcd},
	{"lkill",	&lkill},
	{"lls",		&lls},
	{NULL,NULL}
};

int 
echo( char * argv[])
{
	int i =1;
	if (argv[i]) printf("%s", argv[i++]);
	while  (argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	return 0;
}

int 
lexit( char * argv[])
{
	exit(0);
	return 1;
}


int 
lls(char * argv[])
{
	DIR *dirp;

	char k[MAX_LINE_LENGTH * 3];
	getcwd(k, sizeof(k));
	
	dirp = opendir(k);


	struct dirent *dp;
	while ((dp = readdir(dirp)) != NULL){
		if(dp -> d_name[0] != '.')
			printf("%s\n", dp -> d_name);
	}

	return 0;
}

int 
lcd(char * argv[])
{   
    if(argv[2] != NULL) {
        fprintf(stderr,"Builtin %s error.\n", argv[0]);
        return 1;    
    }
    if(argv[1] == NULL) 
        return chdir(getenv("HOME"));
	
    if( chdir(argv[1]) != 0 ) {
        fprintf(stderr,"Builtin %s error.\n", argv[0]);
        return 1;    
    }
    return 0;
}

int 
lkill(char * argv[])
{
	int pid, sig = SIGTERM;
	errno = 0;
	
	if(argv[1] == NULL){
		fprintf(stderr,"Builtin %s error.\n", argv[0]);
		return 1;
	}
	else if(argv[1][0] != '-'){
		pid = strtol(argv[1], NULL, 10);
		if(pid == 0  && errno)	return 1;	
	}
	else {
		sig = strtol(argv[1]+1, NULL, 10);
		if(sig == 0 && errno)	return 1;
		
		if(argv[2] == NULL) {
			fprintf(stderr,"%s: syntax error\n", argv[0]);
			return 1;	
		}
		pid = strtol(argv[2], NULL, 10);
		if(pid == 0  && errno)	return 1;		
	}

	kill(pid, sig);
		
	return 0;
		
}




