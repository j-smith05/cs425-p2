#define _POSIX_C_SOURCE 200809L

#include "lab.h"
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// Get the current time in milliseconds
static long long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// Encode and send a packet
static int send_packet(int fd, int type, uint32_t seq,
                       const uint8_t *data, size_t len)
{
    Packet packet = {
        .type = (uint8_t)type,
        .seq = seq,
        .length = (uint16_t)len
    };

    uint8_t bytes[MAX_PACKET];

    if (len)
        memcpy(packet.payload, data, len);

    int count = encode_packet(&packet, bytes);

    if (count <= 0)
        return -1;

    return send(fd, bytes, (size_t)count, 0) == count ? 0 : -1;
}

// Connect to the UDP relay
static int connect_relay(const char *host, const char *port)
{
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM
    };

    struct addrinfo *list;

    if (getaddrinfo(host, port, &hints, &list))
        return -1;

    int fd = -1;

    for (struct addrinfo *a = list; a; a = a->ai_next)
    {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);

        if (fd >= 0 && connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            break;

        if (fd >= 0)
            close(fd);

        fd = -1;
    }

    freeaddrinfo(list);
    return fd;
}

// Register with the relay
static int hello(int fd, const char *mode, const char *session,
                 double loss, double corrupt, double dup)
{
    char msg[160], reply[256];

    if (!strcmp(mode, "recv"))
        snprintf(msg, sizeof(msg), "HELLO %s recv", session);
    else
        snprintf(msg, sizeof(msg), "HELLO %s send %.6g %.6g %.6g",
                 session, loss, corrupt, dup);

    // Try registering up to five times
    for (int i = 0; i < 5; i++)
    {
        if (send(fd, msg, strlen(msg), 0) < 0)
            return -1;

        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int ready = poll(&pfd, 1, 1000);

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                i--;
                continue;
            }
            return -1;
        }

        if (!ready)
            continue;

        ssize_t n = recv(fd, reply, sizeof(reply) - 1, 0);

        if (n <= 0)
            return -1;

        reply[n] = '\0';

        if (n == 2 && !strcmp(reply, "OK"))
            return 0;

        fprintf(stderr, "Relay: %s\n", reply);
        return -1;
    }

    fprintf(stderr, "Relay did not respond\n");
    return -1;
}

// Receive the file and send ACKs
static int receive_file(int fd, const char *path)
{
    FILE *f = fopen(path, "wb");

    if (!f)
    {
        perror(path);
        return 2;
    }

    Receiver receiver;
    receiver_init(&receiver);

    long long last = now_ms();
    long long linger_until = -1;

    unsigned char buffer[MAX_PACKET];
    int status = 2;

    for (;;)
    {
        long long now = now_ms();
        long long until = linger_until >= 0
                        ? linger_until : last + 30000;

        if (now >= until)
        {
            status = linger_until >= 0 ? 0 : 2;
            break;
        }

        struct pollfd pfd = {.fd = fd, .events = POLLIN};

        int ready = poll(&pfd, 1, (int)(until - now));

        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        if (!ready)
            continue;

        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        Packet packet;

        if (n < 0 ||
            decode_packet(buffer, (size_t)n, &packet) ||
            packet.type == ACK)
            continue;

        last = now_ms();

        int action = receiver_packet(&receiver, &packet);

        if (action == 1)
        {
            // Write the packet data to the file
            if (fwrite(packet.payload, 1, packet.length, f)
                != packet.length)
                break;
        }
        else if (action == 2)
        {
            // FIN received, close the file but keep listening
            if (fclose(f))
            {
                f = NULL;
                break;
            }

            f = NULL;
            linger_until = now_ms() + 2000;
        }

        // Tell the sender which packet we need next
        if (send_packet(fd, ACK, receiver.expected, NULL, 0))
            break;
    }

    if (f)
        fclose(f);

    if (status != 0)
        fprintf(stderr, "Receiver timed out or failed\n");

    return status;
}

// Send a file using Go-Back-N
static int send_file(int fd, const char *path, int window, int timeout)
{
    FILE *f = fopen(path, "rb");

    if (!f)
    {
        perror(path);
        return 2;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);

    if (size < 0 || size > 16 * 1024 * 1024 ||
        fseek(f, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Invalid file size\n");
        fclose(f);
        return 2;
    }

    unsigned char *data = malloc(size ? (size_t)size : 1);

    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        perror("File read");
        free(data);
        fclose(f);
        return 2;
    }

    fclose(f);

    uint32_t count = ((uint32_t)size + MAX_PAYLOAD - 1)
                     / MAX_PAYLOAD;

    Sender state;
    sender_init(&state, window, timeout);

    unsigned char buffer[MAX_PACKET];
    int status = 2;

    while (state.base <= count)
    {
        // Send packets while there is room in the window
        uint32_t limit = state.base == count ? count + 1 : count;

        while (state.next < limit && sender_can_send(&state))
        {
            size_t off = (size_t)state.next * MAX_PAYLOAD;

            size_t len = state.next == count
                         ? 0 : (size_t)size - off;

            if (len > MAX_PAYLOAD)
                len = MAX_PAYLOAD;

            int type = state.next == count ? FIN : DATA;

            if (send_packet(fd, type, state.next,
                            data + (type == FIN ? 0 : off), len))
                goto done;

            if (state.timer_start < 0)
                state.timer_start = now_ms();

            state.next++;
        }

        // Wait for an ACK or timeout
        long long remaining = timeout;

        if (state.timer_start >= 0)
            remaining = state.timer_start + timeout - now_ms();

        struct pollfd pfd = {.fd = fd, .events = POLLIN};

        int ready = poll(&pfd, 1,
                         remaining > 0 ? (int)remaining : 0);

        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            goto done;
        }

        if (ready > 0)
        {
            ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            Packet ack;

            if (n <= 0 ||
                decode_packet(buffer, (size_t)n, &ack) ||
                ack.type != ACK)
                continue;

            sender_ack(&state, ack.seq, now_ms());

            if (state.base == count + 1)
            {
                status = 0;
                break;
            }
        }
        else
        {
            // Timeout, resend packets that haven't been ACKed
            int timed = sender_timeout(&state, now_ms());

            if (timed < 0)
            {
                fprintf(stderr, "Gave up after 10 timeouts\n");
                break;
            }

            if (!timed)
                continue;

            for (uint32_t seq = state.base; seq < state.next; seq++)
            {
                size_t off = (size_t)seq * MAX_PAYLOAD;

                size_t len = seq == count
                             ? 0 : (size_t)size - off;

                if (len > MAX_PAYLOAD)
                    len = MAX_PAYLOAD;

                int type = seq == count ? FIN : DATA;

                if (send_packet(fd, type, seq,
                                data + (type == FIN ? 0 : off), len))
                    goto done;
            }
        }
    }

done:
    free(data);
    return status;
}

// Print how to run the program
static void usage(void)
{
    puts("Usage: myapp send -s <session> [-w window] "
         "[-T timeout-ms] [-l loss] [-c corrupt] "
         "[-d dup] [-p port] <relay> <file>\n"
         "       myapp recv -s <session> [-p port] "
         "<relay> <file>");
}

#ifdef TEST
#define main main_exclude
#endif

int main(int argc, char **argv)
{
    if (argc == 1)
    {
        usage();
        return 0;
    }

    if (strcmp(argv[1], "send") && strcmp(argv[1], "recv"))
    {
        usage();
        return 1;
    }

    const char *mode = argv[1];
    const char *session = NULL;
    const char *port = "4250";

    int sender = !strcmp(mode, "send");
    int window = 8;
    int timeout = 250;
    int opt;

    double loss = 0, corrupt = 0, dup = 0;

    optind = 2;

    // Read the command line options
    while ((opt = getopt(argc, argv, "s:w:T:l:c:d:p:")) != -1)
    {
        switch (opt)
        {
            case 's': session = optarg; break;
            case 'w':
                if (!sender) return 1;
                window = atoi(optarg);
                break;
            case 'T':
                if (!sender) return 1;
                timeout = atoi(optarg);
                break;
            case 'l':
                if (!sender) return 1;
                loss = atof(optarg);
                break;
            case 'c':
                if (!sender) return 1;
                corrupt = atof(optarg);
                break;
            case 'd':
                if (!sender) return 1;
                dup = atof(optarg);
                break;
            case 'p': port = optarg; break;
            default:
                usage();
                return 1;
        }
    }

    // Make sure the arguments are valid
    if (!session || !*session || strlen(session) > 32 ||
        optind + 2 != argc ||
        window < 1 || window > 64 ||
        timeout < 1 ||
        loss < 0 || loss > 0.5 ||
        corrupt < 0 || corrupt > 0.5 ||
        dup < 0 || dup > 0.5)
    {
        usage();
        return 1;
    }

    // Check the session name
    for (const char *s = session; *s; s++)
    {
        if (!((*s >= 'a' && *s <= 'z') ||
              (*s >= '0' && *s <= '9') || *s == '-'))
            return 1;
    }

    int fd = connect_relay(argv[optind], port);

    if (fd < 0)
    {
        fprintf(stderr, "Cannot connect to relay\n");
        return 2;
    }

    if (hello(fd, mode, session, loss, corrupt, dup))
    {
        close(fd);
        return 2;
    }

    int result;

    if (sender)
        result = send_file(fd, argv[optind + 1], window, timeout);
    else
        result = receive_file(fd, argv[optind + 1]);

    close(fd);
    return result;
}