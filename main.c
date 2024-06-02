#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>

#define BUFFER_SIZE 20

struct Client
{
    int inner_state_update; long int inner_state;
    long int seq_number;
    size_t length;
    char buffer[BUFFER_SIZE + 1];
};

struct FDArray
{
    int length;
    struct pollfd *poll_array;
    struct Client *client_array;
};

long int inner_state, sequence_number = 0;
char *socket_filename = NULL;
struct FDArray *fd_array = NULL;
struct timeval start_time;
struct timeval stop_time;


double diff_time(struct timeval t1, struct timeval t2)
{
    double res = 0.0;
    res += t2.tv_sec - t1.tv_sec;
    res += (t2.tv_usec - t1.tv_usec) / 1e6;
    return res;
}

struct FDArray * get_FDArray()
{
    struct FDArray *array = (struct FDArray *)malloc(sizeof(struct FDArray));

    array->length = 0;
    array->poll_array = NULL; array->client_array = NULL;
    return array;
}

void free_FDArray(struct FDArray *array)
{
    for (int i = 0; i < array->length; i++)
        close(array->poll_array[i].fd);
    if (array->poll_array != NULL)
        free(array->poll_array);
    if (array->client_array != NULL)
        free(array->client_array);
    free(array);
}

void add_client(struct FDArray *array, struct pollfd pf, struct Client client)
{
    struct pollfd *new_poll_array = (struct pollfd *)realloc(
        array->poll_array,
        sizeof(struct pollfd) * ((array->length) + 1));
    struct Client *new_client_array = (struct Client *)realloc(
        array->client_array,
        sizeof(struct Client) * ((array->length) + 1));
    if ((new_poll_array == NULL) || (new_client_array == NULL))
    {
        fprintf(stderr, "ERROR: can't memory allocation\n");
        exit(EXIT_FAILURE);
    }
    array->poll_array = new_poll_array;
    array->client_array = new_client_array;
    memcpy(&array->poll_array[array->length], &pf, sizeof(struct pollfd));
    memcpy(&array->client_array[array->length], &client, sizeof(struct Client));
    array->length++;
}

void del_client(struct FDArray *array, int index)
{
    if ((index < 0) || (index > array->length - 1))
    {
        fprintf(stderr, "ERROR: function del_client: uncorrect index\n");
        exit(EXIT_FAILURE);
    }
    close(array->poll_array[index].fd);
    for (int i = index + 1; i < array->length; i++)
    {
        memcpy(&array->poll_array[i - 1], &array->poll_array[i], sizeof(struct pollfd));
        memcpy(&array->client_array[i - 1], &array->client_array[i], sizeof(struct Client));
    }
    array->length--;
}

void cl_sleep(double time)
{
    if (time <= 0.0)
        return;
    struct timespec req;
    struct timespec rem;
    memset(&rem, 0, sizeof(struct timespec));
    req.tv_sec = (long int)floor(time);
    req.tv_nsec = (long int)floor(1000000000L * (time - floor(time)));
    while (1)
    {
        int result = nanosleep(&req, &rem);
        if (result == 0)
            break;
        if ((result == -1) && (errno != EINTR))
        {
            fprintf(stderr, "ERROR: can't run function: cl_sleep\n");
            exit(1);
        }
        memcpy(&rem, &req, sizeof(struct timespec));
    }
}

int receive_number(int fd, long int *number)
{
    char buffer[BUFFER_SIZE + 2];
    memset(buffer, '\0', BUFFER_SIZE + 2);

    ssize_t rec;
    rec = recv(fd, &buffer, BUFFER_SIZE + 1, 0);

    if (rec == -1)
    {
        fprintf(stderr, "ERROR: can't get data\n");
        return 0;
    }
    else if (rec == BUFFER_SIZE + 1)
    {
        if (sscanf(buffer, "%ld", number) != 1)
        {
            fprintf(stderr, "ERROR: get uncorrect data: '%s'\n", buffer);
            return 0;
        }
    }
    else
    {
        fprintf(stderr, "ERROR: getting only %ld byte in data\n", rec);
        return 0;
    }
    return 1;
}

int send_number(int fd, long int number)
{
    char buffer[BUFFER_SIZE + 1];
    memset(buffer, '\0', BUFFER_SIZE + 1);
    sprintf(buffer, "%ld", number);
    ssize_t sent = send(fd, buffer, BUFFER_SIZE + 1, MSG_NOSIGNAL);
    if (sent == -1 || sent != BUFFER_SIZE + 1)
    {
        fprintf(stderr, "ERROR: can't send data\n");
        return 0;
    }
    return 1;
}

int receive_char(int fd, char *ch)
{
    ssize_t rec = recv(fd, ch, 1, 0);
    if (rec == -1 || rec != 1)
    {
        fprintf(stderr, "ERROR: can't getting char:(\n");
        return 0;
    }
    return 1;
}

int send_char(int fd, char ch)
{
    ssize_t sent = send(fd, &ch, 1, MSG_NOSIGNAL);
    if (sent == -1 || sent == 0)
    {
        fprintf(stderr, "ERROR: can't sending char\n");
        return 0;
    }
    return 1;
}

void signal_handler(int s)
{
    free_FDArray(fd_array);
    free(socket_filename);
    fprintf(stderr, "INFO: Server end, entry state: %ld\n", inner_state);
    exit(0);
}

void server()
{
    if (signal(SIGINT, signal_handler) == SIG_ERR ||
        signal(SIGTERM, signal_handler) == SIG_ERR)
    {
        fprintf(stderr, "ERROR: can't update SIGINT/SIGTERM\n");
        exit(1);
    }

    fd_array = get_FDArray();

    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_filename, sizeof(addr.sun_path) - 1);
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1)
    {
        fprintf(stderr, "ERROR: Ошибка привязки unix socket к файлу '%s'\n", socket_filename);
        exit(1);
    }

    struct pollfd s_pollfd;
    s_pollfd.fd = sfd;
    s_pollfd.events = POLLIN;
    s_pollfd.revents = 0;
    struct Client sd;
    sd.seq_number = -1;
    sd.length = 0;
    sd.inner_state_update = 0;

    add_client(fd_array, s_pollfd, sd);

    if (listen(sfd, 100) == -1)
    {
        perror("ERROR: Ошибка прослушивания сокета\n"); exit(1);
    }

    while (1)
    {
        int events = 0;
        if ((events = poll(fd_array->poll_array, fd_array->length, 100)) > 0)
        {
            if (fd_array->poll_array[0].revents & POLLIN)
            {
                int cfd = 0;
                struct sockaddr_storage claddr;
                socklen_t addrlen = sizeof(struct sockaddr_storage);
                if ((cfd = accept(sfd, (struct sockaddr *)&claddr, &addrlen)) == -1)
                {
                    fprintf(stderr, "ERROR: can't connect client(\n");
                    continue;
                }
                struct Client cd;
                cd.seq_number = ++sequence_number;
                cd.length = 0;
                cd.inner_state_update = 0;
                struct pollfd c_pollfd;
                c_pollfd.fd = cfd;
                c_pollfd.events = POLLIN;
                c_pollfd.revents = 0;

                add_client(fd_array, c_pollfd, cd);

                fprintf(stderr, "INFO: File descriptor in use: %d for new connection\n", cfd);
                fprintf(stderr, "INFO: heap pointer: %p\n", sbrk(0));
                if (fd_array->length == 2)
                    gettimeofday(&start_time, NULL);
            }
            for (int i = fd_array->length - 1; i > 0; i--)
            {
                int revents = fd_array->poll_array[i].revents;
                int fd = fd_array->poll_array[i].fd;
                fd_array->poll_array[i].revents = 0;
                if (revents == 0)
                    continue;
                if (revents & POLLHUP)
                {
                    fprintf(stderr, "INFO: Closing connection number: %ld\n", fd_array->client_array[i].seq_number);
                    del_client(fd_array, i);
                    if (fd_array->length == 1)
                    {
                        gettimeofday(&stop_time, NULL);
                        double time_active = diff_time(start_time, stop_time);
                        fprintf(stderr, "INFO: Last active period: %lf seconds\n", time_active);
                    }
                    continue;
                }
                if (revents & POLLERR)
                {
                    fprintf(stderr,
                            "ERROR: can't connect clinet number: %ld\n",
                            fd_array->client_array[i].seq_number);
                    del_client(fd_array, i);
                    continue;
                }
                if (revents & POLLNVAL)
                {
                    fprintf(stderr, "ERROR: Файловый дескриптор у клиента под номером %ld закрыт\n",
                            fd_array->client_array[i].seq_number);
                    del_client(fd_array, i);
                    continue;
                }
                if (revents & POLLIN)
                {
                    if (!fd_array->client_array[i].inner_state_update)
                    {
                        char ch;
                        if (!receive_char(fd, &ch))
                        {
                            fprintf(stderr, "ERROR: can't getting char in clinet number %ld\n",
                                    fd_array->client_array[i].seq_number);
                        }
                        else
                        {
                            long int number;
                            fd_array->client_array[i].buffer[fd_array->client_array[i].length++] = ch;
                            if (ch == '\0')
                            {
                                if (sscanf(fd_array->client_array[i].buffer, "%ld", &number) != 1)
                                {
                                    fprintf(stderr, "ERROR: client %ld getting wrong number: '%s'\n",
                                            fd_array->client_array[i].seq_number,
                                            fd_array->client_array[i].buffer);
                                }
                                else
                                {
                                    inner_state += number;
                                    fprintf(stderr, "INFO: client %ld sending number %ld\n",
                                            fd_array->client_array[i].seq_number,
                                            number);
                                    fprintf(stderr, "INFO: Update entry server state: %ld\n", inner_state);
                                    fd_array->client_array[i].inner_state = inner_state;
                                    fd_array->client_array[i].inner_state_update = 1;
                                    fd_array->poll_array[i].events = POLLOUT;
                                }
                                memset(fd_array->client_array[i].buffer, '\0', BUFFER_SIZE + 1);
                                fd_array->client_array[i].length = 0;
                            }
                        }
                    }
                }
                if (revents & POLLOUT)
                {
                    if (fd_array->client_array[i].inner_state_update)
                    {
                        if (!send_number(fd, fd_array->client_array[i].inner_state))
                        {
                            fprintf(stderr, "ERROR: can't send entry stat for client %ld\n",
                                    fd_array->client_array[i].seq_number);
                        }
                        else
                        {
                            fd_array->client_array[i].inner_state_update = 0;
                            fprintf(stderr, "INFO: Sending entry state %ld for client %ld\n",
                                    fd_array->client_array[i].inner_state,
                                    fd_array->client_array[i].seq_number);
                            fd_array->poll_array[i].events = POLLIN;
                        }
                    }
                }
            }
        }
    }
}

void client(double delay)
{
    double delays_sum = 0.0;
    int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_filename, sizeof(addr.sun_path) - 1);
    if (connect(cfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1)
    {
        fprintf(stderr, "ERROR: can't connect with server\n");
        exit(1);
    }
    fprintf(stderr, "INFO: Success connecting with server!\n");

    struct pollfd c_pollfd;
    c_pollfd.fd = cfd;
    c_pollfd.events = POLLOUT;
    c_pollfd.revents = 0;

    int symbols_before_delay = 1 + (rand() % 255);
    int recv_server_is = 0;
    while (1)
    {
        int events = poll(&c_pollfd, 1, 100);
        if ((events == -1) && (errno != EINTR))
        {
            fprintf(stderr, "ERROR: wrong poll");
            break;
        }
        if (events > 0)
        {
            int revents = c_pollfd.revents;
            if (revents & POLLHUP)
            {
                fprintf(stderr, "INFO: Server close connection\n");
                break;
            }
            if (revents & POLLERR)
            {
                fprintf(stderr, "ERROR: can't connect with server((\n");
                break;
            }
            if (revents & POLLNVAL)
            {
                fprintf(stderr, "ERROR: Файловый дескриптор закрыт\n");
                break;
            }
            if ((revents & POLLIN) && (recv_server_is))
            {
                long int number = 0;
                if (!receive_number(cfd, &number))
                {
                    fprintf(stderr, "ERROR: can't get entry state of server(\n");
                    close(cfd);
                    exit(1);
                }
                else
                {
                    fprintf(stderr, "INFO: Entry state of server: %ld\n", number);
                }
                c_pollfd.events = POLLOUT;
                recv_server_is = 0;
                continue;
            }
            if ((revents & POLLOUT) && (!recv_server_is))
            {
                int ch = fgetc(stdin);
                if (ch == EOF)
                    break;
                if (ch == '\n')
                    ch = '\0';
                if (!send_char(cfd, ch))
                {
                    fprintf(stderr, "ERROR: can't sending char(\n");
                    close(cfd);
                    exit(1);
                }
                else
                {
                    fprintf(stderr, "INFO: On server send symbol '%c'\n", ch != '\0' ? (char)ch : ' ');
                }
                symbols_before_delay--;
                if (symbols_before_delay == 0)
                {
                    cl_sleep(delay);
                    delays_sum += delay;
                    symbols_before_delay = 1 + (rand() % 255);
                }
                if (ch == '\0')
                {
                    c_pollfd.events = POLLIN;
                    recv_server_is = 1;
                }
                continue;
            }
        }
    }
    close(cfd);
    fprintf(stderr, "INFO: Sum of delay: %lf second\n", delays_sum);
    fprintf(stderr, "INFO: Success close server connection\n");
}

int main(int argc, char *argv[])
{
    double delay = 0.0;
    long int num = 0;
    if (argc == 4)
    {
        if (strcmp(argv[1], "-s") == 0)
        {
            char *log_file = (char *)malloc(strlen(argv[3]) + 1);
            strcpy(log_file, argv[3]);
            int fd = open(log_file, O_CREAT | O_TRUNC | O_WRONLY, 0600);
            if (fd == -1 || dup2(fd, 1) == -1 || dup2(fd, 2) == -1)
            {
                exit(1);
            }
            close(fd);
            free(log_file);
            socket_filename = (char *)malloc(strlen(argv[2]) + 5 + 1);
            strcpy(socket_filename, "/tmp/");
            strcat(socket_filename, argv[2]);
            server();
        }
        else if (strcmp(argv[1], "-c") == 0)
        {
            int fd = open(argv[2], O_RDONLY);
            char *buffer = (char *)malloc(BUFFER_SIZE);
            if (fd == -1 || read(fd, buffer, BUFFER_SIZE) == -1)
            {
                exit(1);
            }
            close(fd);
            socket_filename = (char *)malloc(strlen(buffer) + 5 + 1);
            strcpy(socket_filename, "/tmp/");
            strcat(socket_filename, buffer);
            free(buffer);
            sscanf(argv[3], "%lf", &delay);
            client(delay);
        }
        else
        {
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        exit(EXIT_FAILURE);
    }
    return 0;
}
