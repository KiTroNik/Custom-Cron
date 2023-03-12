#include "library.c"
#include "library.h"
#include <iostream>
#include <spawn.h>
#include <mqueue.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <bits/sigaction.h>
#include "cron.h"
#include <vector>
#include <sstream>


#define MQ_QUERIES_QUEUE "/mq_queries_queue"
#define ABS_DATE_FORMAT "%Y-%m-%d:%H:%M:%S"
#define DATE_FORMAT "%H:%M:%S"
#define SWITCH_SIGNAL SIGRTMIN
#define LOG_SIGNAL SIGRTMIN+1

std::vector<task *> tasks;
pthread_mutex_t vector_mutex;
int last_id = 0;

void init_sig_handling();
void close_server_sig_handler(int signo);
void run_server();
void run_client(mqd_t client_queue, int argc, char * argv[]);
int parse_arguments(struct client_msg *msg, int argc, char* argv[]);
int fill_the_itimerspec(struct client_msg *msg, int argc, char* argv[]);
int fill_program_name_and_arguments(struct client_msg *msg, int argc, char* argv[]);
std::string get_message_type(struct client_msg msg);

int main(int argc, char* argv[]) {
    mqd_t client_queue = mq_open(MQ_QUERIES_QUEUE, O_WRONLY);

    if (client_queue == -1) {
        std::cout << "Serwer " << client_queue << std::endl;
        run_server();
    } else {
        std::cout << "Klient " << client_queue << std::endl;
        run_client(client_queue, argc, argv);
    }
    return 0;
}

void* timer_thread(void *args) {
    struct task t = *(struct task *)args;
    char *anyargs[20];
    char *program_name = t.program_name;
    int i = 0;
    for (i = 0; i < t.num_of_args; ++i) {
        anyargs[i] = strdup(t.program_args[i]);
    }
    anyargs[i] = NULL;

    if (fork() != 0) {
        if (t.t_type != CYCLE) {
            pthread_mutex_lock(&vector_mutex);
            for (int j = 0; j < tasks.size(); ++j) {
                if (tasks[j]->id == t.id) {
                    free(tasks[j]);
                    tasks.erase(tasks.begin() + j);
                }
            }
            pthread_mutex_unlock(&vector_mutex);
        }
        return NULL;
    } else {
        write_log(MAX, (char *)"Executing client program.");
        execvp(program_name, anyargs);
    }
    return NULL;
}

void run_server() {
    init_sig_handling();

    if (initialize_library(ENABLED, SWITCH_SIGNAL, LOG_SIGNAL) != 0) {
        std::cout << "Error when initializing logging library" << std::endl;
        return;
    }
    change_log_level(MAX);

    int error = pthread_mutex_init(&vector_mutex, NULL);
    if (error != 0) {
        std::cout << "Error when initializing mutex" << std::endl;
        return;
    }

    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct client_msg);
    attr.mq_flags = 0;
    mqd_t client_queue = mq_open(MQ_QUERIES_QUEUE, O_CREAT | O_RDONLY, 0666, &attr);

    while (true) {
        struct client_msg msg;
        mq_receive(client_queue, (char*)&msg, sizeof(struct client_msg), NULL);
        std::cout <<  get_message_type(msg) << std::endl;

        if (msg.type == ADD) {
            struct task * new_task = (struct task *) calloc(1, sizeof(struct task));
            new_task->id = last_id++;
            new_task->num_of_args = msg.num_of_args;
            new_task->t_type = msg.t_type;
            mempcpy(new_task->program_name, msg.program_name, sizeof(char)*50);
            mempcpy(new_task->program_args, msg.program_args, sizeof(char)*20*100);
            struct sigevent timer_event;
            timer_event.sigev_notify = SIGEV_THREAD;
            timer_event.sigev_notify_function = reinterpret_cast<void (*)(__sigval_t)>(timer_thread);
            timer_event.sigev_value.sival_ptr = (void *)new_task;
            timer_event.sigev_notify_attributes = NULL;

            if (msg.t_type == ABS) {
                timer_create(CLOCK_REALTIME, &timer_event, &new_task->timer_id);
                timer_settime(new_task->timer_id, TIMER_ABSTIME, &msg.timer_time, NULL);
            } else if (msg.t_type == NORMAL || msg.t_type == CYCLE) {
                timer_create(CLOCK_REALTIME, &timer_event, &new_task->timer_id);
                timer_settime(new_task->timer_id, 0, &msg.timer_time, NULL);
            }

            pthread_mutex_lock(&vector_mutex);
            tasks.push_back(new_task);
            pthread_mutex_unlock(&vector_mutex);

            struct server_reply reply;
            sprintf(reply.msg, "Task created");
            mqd_t mq_reply_to_client = mq_open(msg.queue_name, O_WRONLY);
            mq_send(mq_reply_to_client, (const char*)&reply, sizeof(struct server_reply), 0);
            mq_close(mq_reply_to_client);
            write_log(MAX, (char *)"Task created");

        } else if (msg.type == LIST) {
            std::string r;
            pthread_mutex_lock(&vector_mutex);
            for (int i = 0; i < tasks.size(); ++i) {
                std::stringstream ss;
                ss << i << ": " << tasks[i]->program_name << "\n";
                r.append(ss.str());
            }
            pthread_mutex_unlock(&vector_mutex);
            struct server_reply reply;
            strcpy(reply.msg, r.c_str());
            mqd_t mq_reply_to_client = mq_open(msg.queue_name, O_WRONLY);
            mq_send(mq_reply_to_client, (const char*)&reply, sizeof(struct server_reply), 0);
            mq_close(mq_reply_to_client);
            write_log(MAX, (char *)"list sent to client.");
        } else if (msg.type == DELETE) {
            struct server_reply reply;
            pthread_mutex_lock(&vector_mutex);
            if (tasks.size() < msg.index) {
                sprintf(reply.msg, "Invalid index.");
                write_log(MAX, (char *)"Client gave incorrect index.");
            } else {
                timer_delete(tasks[msg.index]->timer_id);
                tasks.erase(tasks.begin()+msg.index);
                sprintf(reply.msg, "Task deleted.");
                write_log(MAX, (char *)"Task deleted.");
            }
            pthread_mutex_unlock(&vector_mutex);
            mqd_t mq_reply_to_client = mq_open(msg.queue_name, O_WRONLY);
            mq_send(mq_reply_to_client, (const char*)&reply, sizeof(struct server_reply), 0);
            mq_close(mq_reply_to_client);
        }
    }
}

std::string get_message_type(struct client_msg msg) {
    if (msg.type == ADD) {
        return "ADD";
    } else if (msg.type == LIST) {
        return "LIST";
    } else {
        return "DELETE";
    }
}

void run_client(mqd_t client_queue, int argc, char * argv[]) {
    struct client_msg msg;
    sprintf(msg.queue_name, "/pid_%d", getpid());

    if(parse_arguments(&msg, argc, argv) == EXIT_FAILURE) {
        return;
    }

    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct server_reply);
    attr.mq_flags = 0;
    mqd_t mq_reply_from_server = mq_open(msg.queue_name, O_CREAT | O_RDONLY, 0666, &attr);

    mq_send(client_queue, (const char*)&msg, sizeof(struct client_msg), 0);

    struct server_reply reply;
    mq_receive(mq_reply_from_server, (char*)&reply, sizeof(struct server_reply), NULL);
    std::cout << "Server replied: " << reply.msg << std::endl;

    mq_close(client_queue);
    mq_close(mq_reply_from_server);
    mq_unlink(msg.queue_name);
}

int parse_arguments(struct client_msg *msg, int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Invalid number of arguments" << std::endl;
        return EXIT_FAILURE;
    }

    if (strcmp("ADD", argv[1]) == 0) {
        if (argc < 4) {
            std::cout << "Invalid number of arguments" << std::endl;
            return EXIT_FAILURE;
        }
        if (fill_the_itimerspec(msg, argc, argv) == EXIT_FAILURE) {
            return EXIT_FAILURE;
        }
        if (fill_program_name_and_arguments(msg, argc, argv) == EXIT_FAILURE) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } else if (strcmp("LIST", argv[1]) == 0) {
        msg->type = LIST;
        return EXIT_SUCCESS;
    } else if (strcmp("DELETE", argv[1]) == 0) {
        msg->type = DELETE;
        std::stringstream ss(argv[2]);
        if (ss >> msg->index) {
           return EXIT_SUCCESS;
        }
        return EXIT_FAILURE;
    } else {
        std::cout << "UNKNOWN COMMAND" << std::endl;
        return EXIT_FAILURE;
    }
}

int fill_the_itimerspec(struct client_msg *msg, int argc, char* argv[]) {
    struct tm tm{};
    memset(&tm, 0, sizeof(tm));

    if (strcmp("c", argv[2]) == 0) {
        char* end = strptime(argv[3], DATE_FORMAT, &tm);
        if (end == NULL || *end != '\0') {
            std::cout << "Invalid date format" << std::endl;
            return EXIT_FAILURE;
        }
        msg->timer_time.it_value.tv_sec = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
        msg->timer_time.it_value.tv_nsec = 0;
        msg->timer_time.it_interval.tv_sec = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
        msg->timer_time.it_interval.tv_nsec = 0;
        msg->t_type = CYCLE;
        return EXIT_SUCCESS;

    } else if (strcmp("b", argv[2]) == 0) {
        char* end = strptime(argv[3], ABS_DATE_FORMAT, &tm);
        tm.tm_isdst = 1;
        if (end == NULL || *end != '\0') {
            std::cout << "Invalid date format" << std::endl;
            return EXIT_FAILURE;
        } else {
            long timerr = mktime(&tm);
            if (time(NULL) > timerr) {
                std::cout << "Date is in the past" << std::endl;
                return EXIT_FAILURE;
            }
            msg->timer_time.it_value.tv_sec = timerr;
            msg->timer_time.it_value.tv_nsec = 0;
            msg->timer_time.it_interval.tv_sec = 0;
            msg->timer_time.it_interval.tv_nsec = 0;
            msg->t_type = ABS;
            return EXIT_SUCCESS;
        }
    } else if (strcmp("w", argv[2]) == 0) {
        char* end = strptime(argv[3], DATE_FORMAT, &tm);
        if (end == NULL || *end != '\0') {
            std::cout << "Invalid date format" << std::endl;
            return EXIT_FAILURE;
        }
        msg->timer_time.it_value.tv_sec = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
        msg->timer_time.it_value.tv_nsec = 0;
        msg->timer_time.it_interval.tv_sec = 0;
        msg->timer_time.it_interval.tv_nsec = 0;
        msg->t_type = NORMAL;
        return EXIT_SUCCESS;
    } else {
        std::cout << "Invalid date type argument. Possible options are: 'c', 'b' and 'w'" << std::endl;
        return EXIT_FAILURE;
    }
}

int fill_program_name_and_arguments(struct client_msg *msg, int argc, char* argv[]) {
    if (strlen(argv[4]) >= 20) {
        std::cout << "Invalid program name" << std::endl;
        return EXIT_FAILURE;
    }
    strncpy(msg->program_name, argv[4], strlen(argv[4]));
    strncpy(msg->program_args[0], argv[4], strlen(argv[4]));

    if (argc - 5 >= 20) {
        std::cout << "Too much arguments" << std::endl;
        return EXIT_FAILURE;
    }
    msg->num_of_args = argc - 4;
    int i , j;
    for (i = 1, j = 5; i <= argc - 5; ++i, ++j) {
        strncpy(msg->program_args[i], argv[j], strlen(argv[j]));
    }
    return EXIT_SUCCESS;
}

void init_sig_handling() {
    sigset_t set;
    struct sigaction action{};
    sigfillset(&set);
    action.sa_mask = set;
    action.sa_handler = close_server_sig_handler;
    sigaction(SIGTERM,&action,NULL);
}

void close_server_sig_handler(int signo) {
    mq_unlink ("/mq_queries_queue");
    pthread_mutex_lock(&vector_mutex);
    for (int i = 0; i < tasks.size(); ++i) {
        timer_delete(tasks[i]->timer_id);
    }
    pthread_mutex_unlock(&vector_mutex);
    pthread_mutex_destroy(&vector_mutex);
    destroy_library();
    std::cout << "Server closed" << std::endl;
    exit(EXIT_SUCCESS);
}
