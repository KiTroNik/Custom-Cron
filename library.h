//
// Created by jakub on 09.05.22.
//

#ifndef CUSTOM_CRON_LIBRARY_H
#define CUSTOM_CRON_LIBRARY_H

#include <stdio.h>
#include <signal.h>
#include <semaphore.h>

#define ABS_DATE_FORMAT_FOR_LOG "%d/%m/%y-%H:%M:%S"
#define FILE_NAME "LOG"

enum log_level {MIN, STANDARD, MAX};
enum log_switch {ENABLED, DISABLED};

typedef enum log_level log_level;
typedef enum log_switch log_switch;
typedef void (*FUNCTION_PTR)(FILE * file);

int initialize_library(log_switch s, int switch_s, int dump_s);
void destroy_library();
void change_log_level(log_level l);
void write_log(log_level l, char* s, ...);
int add_function_to_register(FUNCTION_PTR f);

#endif //CUSTOM_CRON_LIBRARY_H
