//
// Created by jakub on 11.05.22.
//

#ifndef CUSTOM_CRON_CRON_H
#define CUSTOM_CRON_CRON_H

#include <ctime>
#include <iostream>

enum command_type {
    ADD = 0,
    LIST = 1,
    DELETE = 2
};

enum timer_type {
    NORMAL,
    ABS,
    CYCLE
};

struct client_msg {
    int index;
    command_type type;
    itimerspec timer_time;
    timer_type t_type;
    char program_name[50];
    char program_args[20][100];
    char queue_name[256];
    int num_of_args;
};

struct server_reply {
    char msg[8000];
};

struct task {
    timer_t timer_id;
    int num_of_args;
    char program_name[50];
    char program_args[20][100];
    int id;
    timer_type t_type;
};

#endif //CUSTOM_CRON_CRON_H
