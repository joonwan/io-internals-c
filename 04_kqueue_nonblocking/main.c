/*
 * ============================================================
 * 실습 04: kqueue + 논블로킹 I/O (macOS/BSD 전용)
 * ============================================================
 *
 * [epoll vs kqueue 대응]
 *
 *   Linux epoll                      macOS kqueue
 *   ─────────────────────────────    ──────────────────────────────
 *   epoll_create1(0)             →   kqueue()
 *   epoll_ctl(EPOLL_CTL_ADD)     →   kevent(kq, &chg, 1, NULL, 0, NULL)
 *   epoll_ctl(EPOLL_CTL_DEL)     →   kevent(kq, &chg, 1, NULL, 0, NULL)
 *   epoll_wait(epfd, events, n)  →   kevent(kq, NULL, 0, events, n, NULL)
 *   EPOLLIN                      →   EVFILT_READ
 *   EPOLLET (Edge Trigger)       →   EV_CLEAR
 *
 * [실행 방법]
 *   gcc -o 04 04_kqueue_nonblocking.c && ./04
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/wait.h>

#define NUM_PIPES   5
#define MAX_EVENTS  10

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl O_NONBLOCK");
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    int pipes[NUM_PIPES][2];
    int delays[NUM_PIPES] = {3, 1, 4, 1, 2};

    printf("=== kqueue + 논블로킹 I/O 실습 (macOS) ===\n");
    printf("pipe %d개, 딜레이: ", NUM_PIPES);
    for (int i = 0; i < NUM_PIPES; i++) printf("%d초 ", delays[i]);
    printf("\n\n");

    for (int i = 0; i < NUM_PIPES; i++) {
        if (pipe(pipes[i]) == -1) { perror("pipe"); exit(EXIT_FAILURE); }
        set_nonblocking(pipes[i][0]);
    }

    int kq = kqueue();
    if (kq == -1) { perror("kqueue"); exit(EXIT_FAILURE); }
    printf("[Parent] kqueue fd 생성: kq=%d\n", kq);

    for (int i = 0; i < NUM_PIPES; i++) {
        struct kevent change;
        EV_SET(&change, pipes[i][0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, (void *)(intptr_t)i);

        if (kevent(kq, &change, 1, NULL, 0, NULL) == -1) {
            perror("kevent register");
            exit(EXIT_FAILURE);
        }
        printf("[Parent] pipe[%d] read_end(fd=%d) 등록 완료\n", i, pipes[i][0]);
    }
    printf("\n");

    for (int i = 0; i < NUM_PIPES; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            close(kq);
            for (int j = 0; j < NUM_PIPES; j++) {
                close(pipes[j][0]);
                if (j != i) close(pipes[j][1]);
            }

            printf("[Writer-%d] %d초 후 전송 예정...\n", i, delays[i]);
            fflush(stdout);
            sleep(delays[i]);

            char msg[64];
            snprintf(msg, sizeof(msg), "[pipe %d] 데이터 도착! (딜레이 %d초)", i, delays[i]);
            write(pipes[i][1], msg, strlen(msg));
            printf("[Writer-%d] ✓ 전송 완료\n", i);
            fflush(stdout);
            close(pipes[i][1]);
            exit(EXIT_SUCCESS);
        }
    }

    for (int i = 0; i < NUM_PIPES; i++) close(pipes[i][1]);

    /* ── 이벤트 루프 ──────────────────────────────────────────── */
    struct kevent events[MAX_EVENTS];
    int remaining = NUM_PIPES;
    time_t t_start = time(NULL);

    printf("[Parent] kevent() 이벤트 루프 시작!\n\n");
    fflush(stdout);

    while (remaining > 0) {
        printf("[Parent] kevent() 호출 - %d개 fd 감시 중... (블록)\n", remaining);
        fflush(stdout);

        int nfds = kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("kevent wait");
            break;
        }

        time_t elapsed = time(NULL) - t_start;
        printf("[Parent] kevent 반환! %d개 이벤트 (%ld초 경과)\n", nfds, elapsed);

        for (int j = 0; j < nfds; j++) {
            int ready_fd = (int)events[j].ident;
            int pipe_idx = (int)(intptr_t)events[j].udata;
            long avail   = (long)events[j].data;

            printf("[Parent] pipe[%d](fd=%d): %ld바이트 읽기 가능\n",
                   pipe_idx, ready_fd, avail);

            while (1) {
                char buf[256];
                ssize_t n = read(ready_fd, buf, sizeof(buf) - 1);

                if (n > 0) {
                    buf[n] = '\0';
                    printf("[Parent] ★ pipe[%d] 읽기 완료: \"%s\"\n\n", pipe_idx, buf);
                    fflush(stdout);

                    struct kevent del;
                    EV_SET(&del, ready_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                    kevent(kq, &del, 1, NULL, 0, NULL);
                    close(ready_fd);
                    remaining--;
                    break;

                } else if (n == 0) {
                    struct kevent del;
                    EV_SET(&del, ready_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                    kevent(kq, &del, 1, NULL, 0, NULL);
                    close(ready_fd);
                    remaining--;
                    break;

                } else {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    perror("read");
                    break;
                }
            }
        }
    }

    close(kq);
    for (int i = 0; i < NUM_PIPES; i++) wait(NULL);

    printf("[결론]\n");
    printf("  kqueue()     = epoll_create1()\n");
    printf("  kevent(등록) = epoll_ctl()\n");
    printf("  kevent(대기) = epoll_wait()\n");
    printf("  EV_CLEAR     = EPOLLET\n");
    printf("  EVFILT_READ  = EPOLLIN\n");

    return 0;
}