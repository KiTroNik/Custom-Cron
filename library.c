//
// Created by jakub on 09.05.22.
//


#include "library.h"
#include <signal.h>
#include <stdio.h>
#include <semaphore.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>

static sem_t switch_sem, dump_sem;
static pthread_t dump_thread, switch_thread;
static pthread_mutex_t register_fun_mutex, file_mutex, change_level_mutex;
static log_level current_log_level;
static log_switch current_switch;
static FILE* file;
static FUNCTION_PTR * function_register;
static int initialized = 0, error, dump_signal, switch_signal, fun_reg_capacity, fun_reg_size;

FILE* create_file() {
    char time_name[30];
    time_t t = time(0);
    struct tm* curr_time = localtime(&t);
    strftime(time_name, 30, ABS_DATE_FORMAT_FOR_LOG, curr_time);
    char file_name[100];
    sprintf(file_name, "%s.txt", time_name);
    return fopen(file_name, "w+");
}

int partial_destroy_on_failure(int d_thread, int s_thread,
                               int switch_s, int dump_s, int r_mutex, int f_mutex) {
    if (file != NULL) {
        fclose(file);
    }
    if (function_register != NULL) {
        free(function_register);
    }
    if (d_thread) {
        pthread_cancel(dump_thread);
    }
    if (s_thread) {
        pthread_cancel(switch_thread);
    }
    if (switch_s) {
        sem_destroy(&switch_sem);
    }
    if (dump_s) {
        sem_destroy(&dump_sem);
    }
    if (r_mutex) {
        pthread_mutex_destroy(&register_fun_mutex);
    }
    if (f_mutex) {
        pthread_mutex_destroy(&file_mutex);
    }

    return EXIT_FAILURE;
}

void switch_signal_handler(int signo, siginfo_t *info, void* args) {
    sem_post(&switch_sem);
}

void dump_signal_handler(int signo, siginfo_t* info, void* args) {
    sem_post(&dump_sem);
}

void* switch_thread_handler(void* args) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, switch_signal);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    while(1) {
        sem_wait(&switch_sem);
        current_switch = current_switch == DISABLED ? ENABLED : DISABLED;
    }
}

void* dump_thread_handler(void* args) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, dump_signal);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    while(1) {
        sem_wait(&dump_sem);
        FILE* f = create_file();
        if (f != NULL) {
            for(int i = 0; i < fun_reg_size; i++) {
                function_register[i](f);
            }
            fclose(f);
        }
    }
}

static char* log_level_string(log_level l) {
    if (l == MAX) return "MAX";
    else if (l == STANDARD) return "STANDARD";
    return "MIN";
}

int initialize_library(log_switch s, int switch_s, int dump_s)  {
    if (initialized) return EXIT_FAILURE;

    current_switch = s;
    dump_signal = dump_s;
    switch_signal = switch_s;
    fun_reg_capacity = 3;
    fun_reg_size = 0;

    sigset_t set;
    sigfillset(&set);
    struct sigaction action;
    action.sa_flags = SA_SIGINFO;
    action.sa_mask = set;

    file = fopen(FILE_NAME, "a+");
    if (file == NULL) return partial_destroy_on_failure( 0, 0, 0, 0, 0, 0);

    action.sa_sigaction = switch_signal_handler;
    error = sigaction(switch_signal, &action, NULL);
    if (error != 0) return partial_destroy_on_failure( 0, 0, 0, 0, 0, 0);

    action.sa_sigaction = dump_signal_handler;
    error = sigaction(dump_signal, &action, NULL);
    if (error != 0) return partial_destroy_on_failure( 0, 0, 0, 0, 0, 0);

    error = sem_init(&switch_sem, 0, 0);
    if (error != 0) return partial_destroy_on_failure( 0, 0, 0, 0, 0, 0);

    error = sem_init(&dump_sem, 0, 0);
    if (error != 0) return partial_destroy_on_failure(0, 0, 1, 0, 0, 0);

    error = pthread_mutex_init(&file_mutex, NULL);
    if (error != 0) return partial_destroy_on_failure( 0, 0, 1, 1, 0, 0);

    error = pthread_mutex_init(&register_fun_mutex, NULL);
    if (error != 0) return partial_destroy_on_failure(0, 0, 1, 1, 0, 1);

    function_register = (FUNCTION_PTR *) calloc(sizeof (FUNCTION_PTR), fun_reg_capacity);
    if (function_register == NULL) return partial_destroy_on_failure(0, 0, 1, 1, 1, 1);

    error = pthread_create(&dump_thread, NULL, dump_thread_handler, NULL);
    if (error != 0) return partial_destroy_on_failure( 0, 0, 1, 1, 1, 1);

    error = pthread_create(&switch_thread, NULL, switch_thread_handler, NULL);
    if (error != 0) return partial_destroy_on_failure( 1, 0, 1, 1, 1, 1);

    error = pthread_detach(dump_thread);
    if (error != 0) return partial_destroy_on_failure( 1, 1, 1, 1, 1, 1);

    error = pthread_detach(switch_thread);
    if (error != 0) return partial_destroy_on_failure( 1, 1, 1, 1, 1, 1);

    initialized = 1;
    return EXIT_SUCCESS;
}

void destroy_library() {
    partial_destroy_on_failure( 1, 1, 1, 1, 1, 1);
}

void change_log_level(log_level l) {
    pthread_mutex_lock(&change_level_mutex);
    current_log_level = l;
    pthread_mutex_unlock(&change_level_mutex);
}

void write_log(log_level l, char* s, ...) {
    if (initialized == 0 || current_switch == DISABLED || current_log_level < l) return;
    char time_name[30];
    time_t t = time(0);
    struct tm* curr_time = localtime(&t);
    strftime(time_name, 30, ABS_DATE_FORMAT_FOR_LOG, curr_time);

    pthread_mutex_lock(&file_mutex);
    fprintf(file,"| %s | %s | ", time_name, log_level_string(l));
    va_list list;
    va_start(list,s);
    vfprintf(file,s,list);
    fprintf(file," |\n");
    fflush(file);
    va_end(list);
    pthread_mutex_unlock(&file_mutex);
}

int add_function_to_register(FUNCTION_PTR f) {
    if (initialized == 0) return EXIT_FAILURE;

    pthread_mutex_lock(&register_fun_mutex);
    if (fun_reg_size >= fun_reg_capacity) {
        FUNCTION_PTR * funcs = (FUNCTION_PTR*) realloc(function_register, sizeof(FUNCTION_PTR)* fun_reg_capacity * 2);
        if (funcs == NULL) return EXIT_FAILURE;
        function_register = funcs;
        fun_reg_capacity *= 2;
    }
    function_register[fun_reg_size++] = f;
    pthread_mutex_unlock(&register_fun_mutex);
    return EXIT_SUCCESS;
}

