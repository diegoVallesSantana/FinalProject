/**
* \author {Diego Vallés}
 */
//Example test: make sensor_gateway_mini + make sensor_node
//Terminal 1: ./sensor_gateway_mini 5678 2
//Terminal 2: ./sensor_node 101 1 127.0.0.1 5678 add sensor 1
//Terminal 3: ./sensor_node 202 1 127.0.0.1 5678 add sensor 2
//Terminal 3: ./sensor_node 303 1 127.0.0.1 5678 add sensor 3
//close sensor 1 and 2,
//Terminal 4: ./sensor_node 404 1 127.0.0.1 5678 try to add sensor 4 (blocked bc 2 already disconected)
//Terminal 3: close sensor 3
//server should close by it-self
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include "config.h"
#include "sbuffer.h"
#include "connmgr.h"
#include "sensor_db.h"
#include "datamgr.h"

//Use of strtol: https://www.tutorialspoint.com/c_standard_library/c_function_strtol.htm

typedef struct {
    sbuffer_t *buffer;
    const char *csv_filename;
} storagemgr_args_t;

static int read_all(int fd, void *buf, size_t n)
{
    char *p = (char *)buf;
    size_t left = n;

    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r == 0) {return r;}
        if (r < 0) {return r;}
        p += (size_t)r;
        left -= (size_t)r;
    }
    return 1;
}

static void log_process_run(int pipe_read_fd)
{
    FILE *lf = fopen("gateway.log", "w");   // new empty log each run
    if (!lf) _exit(EXIT_FAILURE);

    unsigned long seq = 0;
    char msg[MSG_MAX];

    for (;;) {
        int rc = read_all(pipe_read_fd, msg, sizeof(msg));
        if (rc == 0) break;   // parent closed write end => EOF
        if (rc < 0) break;    // read error

        msg[MSG_MAX - 1] = '\0'; // safety

        time_t now = time(NULL);
        fprintf(lf, "%lu %ld %s\n", ++seq, (long)now, msg);
        fflush(lf);
    }

    fclose(lf);
    close(pipe_read_fd);
    _exit(EXIT_SUCCESS);
}

static void *storagemgr_thread(void *arg) {
    storagemgr_args_t *sa = (storagemgr_args_t *)arg;

    // Create a NEW empty CSV at server start (assignment requirement)
    FILE *f = open_db((char *)sa->csv_filename, false);
    if (f == NULL) {
        fprintf(stderr, "[SM] open_db failed\n");
        return NULL;
    }

    sensor_data_t data;

    for (;;) {
        int rc = sbuffer_remove(sa->buffer, &data, SBUFFER_READER_SM);

        if (rc == SBUFFER_SUCCESS) {
            if (insert_sensor(f, data.id, data.value, data.ts) != 0) {
                fprintf(stderr, "[SM] insert_sensor failed (id=%u)\n", (unsigned)data.id);
                // Decide policy: continue is usually safest for test harness
            }
        } else if (rc == SBUFFER_NO_DATA) {
            // buffer closed + drained for SM
            break;
        } else {
            fprintf(stderr, "[SM] sbuffer_remove failed\n");
            break;
        }
    }

    if (close_db(f) != 0) {
        fprintf(stderr, "[SM] close_db failed\n");
    }

    return NULL;
}

static void print_help(const char *prog) {
    fprintf(stderr, "Usage: %s <port> <max_conn>\n", prog);
    fprintf(stderr, "Example: %s 1234 3\n", prog);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    char *end = NULL;
    long port_l = strtol(argv[1], &end, 10);
    if (*argv[1] == '\0' || (end && *end != '\0') || port_l <= 0 || port_l > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    end = NULL;
    long max_conn_l = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || (end && *end != '\0') || max_conn_l <= 0 || max_conn_l > 1000000) {
        fprintf(stderr, "Invalid max_conn: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    int port = (int)port_l;
    int max_conn = (int)max_conn_l;


    int status = 0;
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t log_pid = fork();
    if (log_pid < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    if (log_pid == 0) {
        /* Child: log process */
        close(pipefd[1]);            // close write end
        log_process_run(pipefd[0]);  // never returns
    }

    /* Parent: server main */
    close(pipefd[0]);                // close read end

    if (logger_init(pipefd[1]) != 0) {
        fprintf(stderr, "logger_init failed (logging disabled)\n");
        close(pipefd[1]);
        waitpid(log_pid, &status, 0);
        return EXIT_FAILURE;
        /* You may continue; but you should still close pipefd[1] on shutdown */
    }

	log_event("Sensor gateway started (port=%d, max_conn=%d)", port, max_conn);

    sbuffer_t *buffer = NULL;
    if (sbuffer_init(&buffer) != SBUFFER_SUCCESS) {
        fprintf(stderr, "sbuffer_init failed\n");
        close(pipefd[1]);
        waitpid(log_pid, &status, 0);
        return EXIT_FAILURE;
    }

    // Start DM and SM
    pthread_t dm_tid;
    datamgr_args_t dm_args = {.buffer = buffer,.map_filename = "room_sensor.map"};

    if (pthread_create(&dm_tid, NULL, datamgr_run, &dm_args) != 0) {
        fprintf(stderr, "pthread_create(DM) failed\n");
        sbuffer_close(buffer);
        sbuffer_free(&buffer);
        close(pipefd[1]);
        waitpid(log_pid, &status, 0);
        return EXIT_FAILURE;
    }
	log_event("Data manager thread started");

    pthread_t sm_tid;
    storagemgr_args_t sm_args = {.buffer = buffer, .csv_filename = "data.csv"};
    if (pthread_create(&sm_tid, NULL, storagemgr_thread, &sm_args) != 0) {
        fprintf(stderr, "pthread_create(SM) failed\n");
        sbuffer_close(buffer);
        pthread_join(dm_tid, NULL);
        sbuffer_free(&buffer);
        close(pipefd[1]);
        waitpid(log_pid, &status, 0);
        return EXIT_FAILURE;
    }
	log_event("Storage manager thread started");


    // Start connection manager
    pthread_t conn_tid;
    connmgr_args_t conn_args = {
        .port = port,
        .max_conn = max_conn,
        .buffer = buffer
    };

    if (connmgr_start(&conn_tid, &conn_args) != 0) {
        fprintf(stderr, "connmgr_start failed\n");
        sbuffer_close(buffer);
        pthread_join(dm_tid, NULL);
        pthread_join(sm_tid, NULL);
        sbuffer_free(&buffer);
        close(pipefd[1]);
        waitpid(log_pid, &status, 0);
        return EXIT_FAILURE;
    }
	log_event("Connection manager thread started");


    // Wait for connmgr to stop (it should stop after max_conn clients disconnect)
    pthread_join(conn_tid, NULL);

    // At this point connmgr closes the buffer in your current implementation.
    // Consumers will drain and exit.
    pthread_join(dm_tid, NULL);
    pthread_join(sm_tid, NULL);

	log_event("Sensor gateway shutting down");
    close(pipefd[1]);

    if (waitpid(log_pid, &status, 0) < 0) {
		fprintf(stderr, "waitpid\n");
    }

    if (sbuffer_free(&buffer) != SBUFFER_SUCCESS) {
        fprintf(stderr, "sbuffer_free failed\n");
        return EXIT_FAILURE;
    }

    printf("Main completed: sbuffer + connmgr + storagemgr + datamgr + log process work\n");
    return EXIT_SUCCESS;
}
