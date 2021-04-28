/**
 * @file server.c
 * @brief Server side of a local client-server system, implemented using named pipes
 * @version 0.1
 * @date 2021-04-22
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#define ERROR -1
#define RECVD "RECVD"
#define TSKEX "TSKEX"
#define TSKDN "TSKDN"
#define TLATE "2LATE"
#define FAILD "FAILD"

//structure used to communicate with client
typedef struct message { int rid; pid_t pid; pthread_t tid; int tskload; int tskres; } message_t; 

/*--------------------GLOBAL VARIABLES--------------------*/

pthread_mutex_t lock; //structure used to implement mutex

char public_fifo[100];  //holds the public fifo name/path

bool time_is_up = false;  //to keep track of server execution time

bool public_fifo_closed = true;  //to keep track of public fifo

bool debug = true;  //used to keep track of debugging printfs

int bufsz = 1;  //server buffer size (default 1)

bool buffer_is_empty = false;  //to keep track if buffer is empty or not

/*------------------END GLOBAL VARIABLES------------------*/


/*---------------------UTIL FUNCTIONS---------------------*/

/**
 * @brief treats user argument input errors
 * 
 */
void input_error(){
    fprintf(stderr,"Usage: ./s <-t nsecs> [-l bufsize] fifoname\n");
    exit(1);
}

/**
 * @brief compares two strings (wrapper function for strcmp)
 * 
 * @param string1 first string to be compare with
 * @param string2 second string to compare with the first
 * @return true if strings are the same, false otherwise
 */
bool str_cmp(char *string1, char *string2){
    if(strcmp(string1, string2) == 0) return true;
    else return false;
}

/**
 * @brief checks if a string represents a valid number
 * 
 * @param str the string to be checked
 * @return true if it a number, false otherwise
 */
bool is_number(char *str){
     for (int i = 0; str[i] != '\0'; i++)
        if (isdigit(str[i]) == false)
            return false;
    return true;
}

/**
 * @brief handles signals (for now just needed sigalam)
 * 
 * @param signum the identifier of the signal
 */
void sig_handler(int signum){
    time_t now;
    time(&now);
    fprintf(stderr,"[server] timeout reached: %ld\n", now);

    //deleting public fifo file
    if(remove(public_fifo) != 0){
        fprintf(stderr, "Not successfully deleted public file\n");
    }

    //main thread waits for all threads to exit
    pthread_exit(NULL);

    //releasing pthread mutex structure
    pthread_mutex_destroy(&lock);
}

/**
 * @brief executed by prodcuer threads, that produce a task into buffer
 * 
 * @param arg the request message from client
 * @return void* 
 */
void *producer_thread(void * arg){
    //request message from client
    message_t *request = (message_t*) arg;

    //get current time and current thread info
    time_t cur_secs;
    time(&cur_secs);
    pthread_t tid = pthread_self();
    int pid = getpid();
   
    fprintf(stdout, "%ld; %d; %d; %d; %lu; %d; %s\n", cur_secs, request->rid, request->tskload, pid, tid, request->tskres, RECVD);

    //!!!!!!!!!!!!!!!!!!!!!!NEW UNTESTED PART!!!!!!!!!!!!!!!!!!!!!!!!!!!!

    //doing the designated task calling library B
    int task_res = task(request->tskload);

    //getting time after task execution
    time(&cur_secs);

    fprintf(stdout, "%ld; %d; %d; %d; %lu; %d; %s\n", cur_secs, request->rid, request->tskload, pid, tid, task_res, TSKEX);

    //send task result to buffer
    message_t *request_result;

    request_result->pid = pid;
    request_result->rid = request->rid;
    request_result->tid = tid;
    request_result->tskload = request->tskload;
    request_result->tskres = task_res;

    //locking code wiht mutex
    pthread_mutex_lock(&lock);

    insert(request_result);

    //unlocking code wiht mutex
    pthread_mutex_unlock(&lock);
}

/**
 * @brief executed by the consumer thread, wich will retrieve tasks from buffer
 * 
 * @param arg 
 * @return void* 
 */
void *consumer_thread(void * arg){

 //!!!!!!!!!!!!!!!!!!!!!!NEW UNTESTED PART!!!!!!!!!!!!!!!!!!!!!!!!!!!!

    //info about the consumer thread itself
    pthread_t tid;
    int pid;
    time_t now;

    //info that if going to be retrieved from buffer
    message_t *answer;

    //loop while time isn't up
    while(!time_is_up){
        int bytes_written = ERROR;

        //locking code wiht mutex
        pthread_mutex_lock(&lock);

        if(!empty()){
            answer = front();
            pop();
        }

        if(answer != NULL){
            char priv_path[100];

            sprintf(priv_path, "/tmp/%d.%lu", answer->pid, answer->tid);

            //open private fifo
            int priv_fd = open(priv_path, O_WRONLY);

            //write answer to client in the private fifo
            bytes_written = write(priv_fd, &answer, sizeof(message_t));

            //maybe sleep(1) in the end?
            
            //unlocking code wiht mutex
            pthread_mutex_unlock(&lock);

            if(bytes_written < 0){
                time(&now);
                fprintf(stdout, "%ld; %d; %d; %d; %lu; %d; %s\n", now, answer->rid, answer->tskload, answer->pid, answer->tid, answer->tskres, FAILD);
            }
            else{
                time(&now);
                fprintf(stdout, "%ld; %d; %d; %d; %lu; %d; %s\n", now, answer->rid, answer->tskload, answer->pid, answer->tid, answer->tskres, TSKDN);
            }
        }
    }
    
    if(answer != NULL){
        time(&now);
        fprintf(stdout, "%ld; %d; %d; %d; %lu; %d; %s\n", now, answer->rid, answer->tskload, answer->pid, answer>tid, answer->tskres, TLATE);
    }
}


/*-------------------END UTIL FUNCTIONS-------------------*/
/**
 * @brief main server function, where program it is intialized, main loop and ended
 * 
 * @param argc the number of arguments passed
 * @param argv the arguments itself
 * @return int 0 on success, ERROR on fail
 */
int main(int argc, char* argv[]){
    /*
    # ARGV 0 -> ./s         ARGV 3 -> fifoname / -l   #
    # ARGV 1 -> -t          ARGV 4 -> bufsize         #
    # ARGV 2 -> nsecs       ARGV 5 -> fifoname        #
    */

    //getting initial time of program execution
    time_t initial_time;
    time(&initial_time);

   /*--------------------INPUT ERROR VERIFICATION--------------------*/

    //when less than the minimun arguments number is given
    if(argc < 4) input_error();

    //when -t or time is not passed correctly
    if(!str_cmp(argv[1], "-t") || !is_number(argv[2])) input_error();

    //-l passed but with no arguments
    if(str_cmp(argv[3], "-l") && argc != 6) input_error();

    //-l not passed or bufsize argmuent is not a number
    if(argc == 6 && (!str_cmp(argv[3], "-l") || !is_number(argv[4]))) input_error();

    //after this the program assumes inputs are correctly inputted

    /*------------------END INPUT ERROR VERIFICATION------------------*/ 

    /*---------------------PROGRAM INITIALIZATION---------------------*/   

    //initializing pthread mutex structure
    if (pthread_mutex_init(&lock, NULL) != 0) {
        fprintf(stderr, "[server] Mutex init has failed\n");
        return ERROR;
    }

    //registreing signal handler
    signal(SIGALRM, sig_handler); 

    //retrieving arguments inputted
    int nsecs = atoi(argv[2]);

    //setting alarm
    alarm(nsecs);

    if(str_cmp(argv[3], "-l")){
        bufsz = atoi(argv[4]);
        strcat(public_fifo, argv[5]);
    }
    else{
        strcat(public_fifo, argv[3]);
    }

    if(debug) printf("Fifo path: %s\n", public_fifo);  //DEBUG

    //printing final and initial time to user and the rest of the parameters to user
    long int nsecs_long = (long) (int) nsecs;
    fprintf(stderr,"[server] initial time: %lu, expected final time: %lu\n", initial_time, initial_time + nsecs_long);
    fprintf(stderr,"[server] got: nsecs=%d, bufsize=%d, fifoname=%s\n", nsecs, bufsz, public_fifo);

    //creating public fifo file
    if(mkfifo(public_fifo, 0777) == ERROR){
        fprintf(stderr, "[server] Error when creating public fifo: %s\n", strerror(errno));
        return ERROR;                                                
    }

    //thread unique identifier
    long int id = 1;

    //used to get current seconds in main thread loop
    time_t cur_secs;


    int bytes_read = ERROR, public_fd = ERROR;
    int tout = 1;  //DEBUG

    //getting the request from client
    message_t *request;

    //openning public fifo
    public_fd = open(public_fifo, O_RDONLY); 

    if(public_fd < 0){ 
        if(debug) printf("[server] Error while opening public fifo file: %s\n", strerror(errno));  //DEBUG
    }

    //creating consumer thread
    pthread_t con_thread_id;

    if(debug) printf("created consumer thread!\n"); //DEBUG

    if(pthread_create(&con_thread_id, NULL, &consumer_thread, (void*)0) != 0) return ERROR;
    
     /*---------------------MAIN THREAD (C0) LOOP---------------------*/

    //reading requests from client
    while(!time_is_up){
        bytes_read = read(public_fd, request, sizeof(message_t)); 

        //atualize time_is_up
        time_t now;
        time(&now);
        
        //checking if nsecs have passed already
        if(now - initial_time >= nsecs){
            fprintf(stderr,"[server] timeout reached: %ld\n", now);
            time_is_up = true;
        }

        //to check if there was a resquest or not
        if(bytes_read > 0){
            //creating producer threads
            pthread_t prod_thread_id;

            if(debug) printf("created producer thread number: %ld\n", id); //DEBUG

            if(pthread_create(&prod_thread_id, NULL, &producer_thread, (void*)request) != 0) return ERROR;

            id++;
        }
        else{ //TODO: IF READ TIMEOUTS FOR LONG ASSUME CLIENT IT'S CLOSED AND EXIT
            if(debug) printf("timeout read: %d", tout);  //DEBUG
            tout++;  //DEBUG
            sleep(1);
        }
    }
            
    /*-------------------------ENDING PROGRAM-------------------------*/

    //closing public fifo
    close(public_fd);

    //deleting public fifo file
    if(remove(public_fifo) != 0){
        fprintf(stderr, "[server] Not able to delete public file\n");
    }

    //main thread waits for all threads to exit
    pthread_exit(NULL);

    //releasing pthread mutex structure
    pthread_mutex_destroy(&lock);

    return 0;
}
