#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <fnmatch.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>

typedef struct{
	void* m_previous;
	void* m_following;
	char* m_path;
}directory;

directory* first;
directory* last;
pthread_mutex_t qlock;
pthread_mutex_t read_lock;
pthread_mutex_t cancel_lock;
pthread_cond_t not_empty;
int num_threads_work;
pthread_t* threads;
int num_threads;
_Bool cancel;
int num_files;	

void sig_handler(int sig){
	signal(SIGINT, SIG_IGN);
	void* temp;
	if(threads == NULL){
		printf("Search stopped, found %d files\n", num_files);
		exit(0);
	}
	pthread_mutex_lock(&cancel_lock);
	if(!cancel){
		cancel = 1;
		pthread_mutex_unlock(&cancel_lock);
		for( int t = 0 ; t < num_threads; t++ ){
			if(threads[t]){
				pthread_cancel(threads[t]);
				pthread_mutex_unlock(&qlock);
				sleep(0.1);
			}
		}
	}
	for( int t = 0 ; t < num_threads; t++ ){
		if(threads[t]){
			pthread_join(threads[t], NULL);
		}
	}
	printf("Search stopped, found %d files\n", num_files);
	if(last == NULL){
		if(first == NULL){
			exit(0);
		}free((void*)first);
		exit(0);
	}
	while(first->m_following != (void*)last){
		temp = first->m_following;
		first->m_following = ((directory*)first->m_following)->m_following;
		free(temp);
	}free((void*)first);
	free((void*)last);
	free((void*)threads);
	pthread_mutex_destroy(&qlock);
	pthread_mutex_destroy(&read_lock);
	pthread_mutex_destroy(&cancel_lock);
  	pthread_cond_destroy(&not_empty);
	exit(0);
}

void init_queue(){
	if((first = (directory*)malloc(sizeof(directory))) == NULL){
		perror("==error in malloc.\n");
		exit(1);
	}
	if((last = (directory*)malloc(sizeof(directory))) == NULL){
		perror("==error in malloc.\n");
		exit(1);
	}
	first->m_previous = NULL;
	first->m_following = (void*)last;
	first->m_path = NULL;
	last->m_previous = (void*)first;
	last->m_following = NULL;
	first->m_path = NULL;
}

void enqueue(char* path){
	directory* m_dir;
	if((m_dir = (directory*)malloc(sizeof(directory))) == NULL){
		perror("==error in malloc.\n");
		exit(1);
	}
	if((m_dir->m_path = (char*)malloc(sizeof(char)*strlen(path))) == NULL){
		perror("==error in malloc.\n");
		exit(1);
	}
	strcpy(m_dir->m_path, path);
	pthread_mutex_lock(&qlock);
	m_dir->m_previous = last->m_previous;
	((directory*)last->m_previous)->m_following = (void*)m_dir;
	m_dir->m_following = (void*)last;
	last->m_previous = (void*)m_dir;
	pthread_cond_signal(&not_empty);
	pthread_mutex_unlock(&qlock);
}

void search(char* path, char* exp){
	DIR* dir;
	struct stat file;
	struct dirent *entry;
	struct stat dir_stat;
	if ((dir = opendir(path))<0){
		perror("error \n");
		exit(1);
	}pthread_mutex_lock(&read_lock);
	entry = readdir(dir);               //readdir() unsafe thread
	pthread_mutex_unlock(&read_lock);
	while(entry != NULL){
		if (entry < 0){
			perror("error %s\n");
			exit(-1);
		}
		char buff[strlen(path)+strlen(entry->d_name)+2];
        sprintf(buff,"%s/%s",path,entry->d_name);
		lstat(buff,&dir_stat);
		if(strcmp(entry->d_name,"..") != 0 && strcmp(entry->d_name, ".") != 0){
			if(((dir_stat.st_mode & __S_IFMT) == __S_IFDIR)){
				enqueue(buff);
			
			}else{
				lstat(buff, &file);
				if(fnmatch(exp,strrchr(buff,'/')+1 ,0) == 0){
					printf("%s\n", buff);
					__sync_fetch_and_add(&num_files, 1);
					}
			}
		 }pthread_mutex_lock(&read_lock);
		 entry = readdir(dir);              //readdir() unsafe thread
		 pthread_mutex_unlock(&read_lock);	
	 }
	 if (closedir(dir) < 0){
			perror("error \n");
			exit(-1);
	}
}

void* dequeue(void* m_exp){
	int buff;
	void* temp;
	char* exp = (char*)m_exp; 
	pthread_mutex_lock(&qlock);
	num_threads_work--;
	while(first->m_following == (void*)last){
		if (num_threads_work == 0){
			pthread_t tid = pthread_self();
			pthread_mutex_lock(&cancel_lock);
			if(!cancel){
				cancel = 1;
				pthread_mutex_unlock(&cancel_lock);
				for( int t = 0 ; t < num_threads; t++ ){
					if(threads[t] != tid){
						pthread_cancel(threads[t]);
						pthread_mutex_unlock(&qlock);
						sleep(0.1);
					}
				}
				pthread_exit(&buff);
			}
		}
		pthread_cond_wait(&not_empty, &qlock);
	}pthread_testcancel();
	num_threads_work++;
	temp = first->m_following;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &buff);
	first->m_following = ((directory*)first->m_following)->m_following;
	((directory*)first->m_following)->m_previous = (void*)first;
	pthread_mutex_unlock(&qlock);
	char path[strlen(((directory*)temp)->m_path)];
	strcpy(path, ((directory*)temp)->m_path);
	free(((directory*)temp)->m_path);
	free(temp);
	search(path, exp);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &buff);
	pthread_testcancel();
	dequeue((void*)exp);
	return NULL;
}

int main(int argc, char **argv){
	if(argc != 4){
		perror("error \n");
		exit(1);
	}
	num_threads = atoi(argv[3]);
	cancel = 0;
	num_files = 0;
	num_threads_work = 0;
	signal(SIGINT, sig_handler);
	if((threads = (pthread_t*)calloc(sizeof(pthread_t), num_threads)) == NULL){
		perror("==error in malloc.\n");
		exit(1);
	}
	init_queue();
	if( pthread_mutex_init(&qlock, NULL) ) {
	    perror("ERROR in pthread_mutex_init(): \n");
	    exit(-1);
	}
	if( pthread_mutex_init(&read_lock, NULL) ) {
	    perror("ERROR in pthread_mutex_init(): \n");
	    exit(-1);
	}
	if( pthread_mutex_init(&cancel_lock, NULL) ) {
	    perror("ERROR in pthread_mutex_init(): \n");
	    exit(-1);
	}
	if( pthread_cond_init (&not_empty, NULL) ) {
	    perror("ERROR in pthread_cond_init(): \n");
	    exit(1);
	}enqueue(argv[1]);
	for( int t = 0 ; t < num_threads; t++ ){
	    if (pthread_create( &threads[t], NULL, dequeue, (void*)argv[2])) {
	      perror("ERROR in pthread_create(): \n");
	      exit(1);
	    }
	    __sync_fetch_and_add(&num_threads_work, 1);
  	}
	for( int t = 0 ; t < num_threads; t++ ){
		pthread_join(threads[t], NULL);
	}
	free((void*)threads);
	pthread_mutex_destroy(&qlock);
	pthread_mutex_destroy(&read_lock);
	pthread_mutex_destroy(&cancel_lock);
  	pthread_cond_destroy(&not_empty);
	return 0;
	}
