#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>

#define NUM_PIPES 3

// change to non blocking mode
static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


int main(void)
{
    int pipes[NUM_PIPES][2]; // pipes[i][0] : read end, pipes[i][1] : write end
    int delays[NUM_PIPES] = {1, 3, 5};

    printf("=== io multiplexing - select() ===");
    printf("pipe 3개를 동시에 감시하며 각각 %d초, %d초, %d초 후 데이터 도착\n\n", delays[0], delays[1], delays[2]);
    fflush(stdout);
    
    // pipe 3개 생성
    for (int i = 0; i < NUM_PIPES; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
        set_nonblocking(pipes[i][0]); // read end nonblocking 설정
    }

    // writer process 들 : 각 delay 후 데이터 전송
    for (int i = 0; i < NUM_PIPES; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        
        // child process
        if (pid == 0)
        {
            for (int j = 0; j < NUM_PIPES; j++)
            {
                close(pipes[j][0]); // 모든 read end 닫기
                if (i != j) close(pipes[j][1]); // 내 것 빼고 write end 닫기
            }

            printf("[writer-%d] %d 초 후 데이터 전송 예정\n", i, delays[i]);
            fflush(stdout);
            sleep(delays[i]);

            char msg[64];
            snprintf(msg, sizeof(msg), "pipe[%d]에서 온 데이터 (딜레이: %d초)", i, delays[i]);
            write(pipes[i][1], msg, strlen(msg));
            printf("[Writer-%d] 전송 완료: \"%s\"\n", i, msg);
            fflush(stdout);

            close(pipes[i][1]);
            exit(EXIT_SUCCESS);
        }
    }

    // parent: select() 로 3개 pipe 동시 감시
    for (int i = 0; i < NUM_PIPES; i++)
    {
        close(pipes[i][1]); // parent 는 read 만 하기 때문에 write end 모두 닫기
    }

    int remaining = NUM_PIPES;
    int done[NUM_PIPES] = {0};

    printf("\n[parent] select() loop 시작 - 3개 파이프 동시 감시!\n\n");
    fflush(stdout);

    time_t t_start = time(NULL);

    while (remaining > 0)
    {
        fd_set readfds; // 감시할 fd 들 집합
        FD_ZERO(&readfds); // fd_set 초기화

        int maxfd = -1;

        for (int i = 0; i < NUM_PIPES; i++)
        {
            if (!done[i])
            {
                FD_SET(pipes[i][0], &readfds);
                if (pipes[i][0] > maxfd) maxfd = pipes[i][0];
            }
        }

        // timeout : null 이면 무한 대기
        struct timeval timeout;
        timeout.tv_sec = 5; // 최대 5초 대기
        timeout.tv_usec = 0;

        printf("[parent] select() 호출 - %d개 fd 감시 중... (블록)\n", remaining);
        fflush(stdout);

        int nready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);

        if (nready < 0)
        {
            perror("select");
            break;
        } else if (nready == 0){
            printf("[parent] select() timeout!\n");
            break;
        } 

        // 어떤 fd 가 준비되었는지 확인
        for (int i = 0; i < NUM_PIPES; i++)
        {
            if (!done[i] && FD_ISSET(pipes[i][0], &readfds))
            {
                char buf[256];
                ssize_t n = read(pipes[i][0], buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    time_t elapsed = time(NULL) - t_start;
                    printf("[Parent] ★ pipe[%d] 읽기 완료! (%ld초 경과): \"%s\"\n\n", i, elapsed, buf);
                    fflush(stdout);
                    done[i] = 1;
                    remaining--;
                }
                close(pipes[i][0]);
            }
        }
    }

    for (int i = 0; i < NUM_PIPES; i++) {
        wait(NULL);
    }

    printf("[결론]\n");
    printf("  - select()로 N개 fd를 하나의 스레드에서 동시에 감시.\n");
    printf("  - select()는 블로킹이지만, 어떤 fd든 준비되면 깨어남.\n");
    printf("  - 준비된 fd에만 read()→ 블로킹 없이 즉시 반환.\n");
    printf("  - 단점: fd가 많아지면 O(n) 스캔 비효율. → epoll로 해결.\n");

    return 0;
}
