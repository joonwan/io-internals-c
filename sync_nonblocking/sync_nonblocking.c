#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

// fd 를 non blocking mode 로 변경하는 함수
// fcntl: fd 의 설정을 읽거나 변경하는 함수
// F_GETFL 을 사용해 fd 의 flag 를 읽어옴
// flags 에 fd 의 flag 값이 들어감
// 0 은 F_GETFL 에서는 특별히 의미 없는 자리라 그냥 넣는 것
static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0); // 현재 flag 읽기
    if (flags == -1)// fcntl 실패
    {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }

    flags |= O_NONBLOCK; // O_NONBLOCK 추가 -> 기존 설정 유지 + non-blocking 옵션 붙이기
    
    if (fcntl(fd, F_SETFL, flags) == -1) // 새 flag 값 fd 에 적용 실패할 경우
    {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }

    printf("[설정] fd=%d non blocking mode 활성화\n", fd);
}

void do_child(int pipefd[2])
{
    // close write end
    close(pipefd[1]);

    // read end 를 non blocking 으로 설정
    set_nonblocking(pipefd[0]);

    char buf[256];
    int eagain_count = 0;
    time_t t_start = time(NULL);

    printf("[Child PID=%d] non blocking polling loop 시작!!\n\n", getpid());
    fflush(stdout);

    /**
     * busy wait polling loop
     * 데이터가 올때 까지 계속 read 를 시도
     * 데이터가 없으면 EAGAIN -> count 하고 다시 시도
     * 
     * cpu 를 100% 사용하는 최악의 패턴
     * 실제로는 이렇게 쓰지 않음
     */

    while (1) 
    {
        memset(buf, 0, sizeof(buf));
        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);

        if (n > 0)
        {
            //read success
            time_t elapsed = time(NULL) - t_start;
            printf("[child]  읽기 성공! (%zd bytes): \"%s\"\n", n, buf);
            printf("[Child] ✓ EAGAIN 발생 횟수: %d회 (총 %ld초 폴링)\n",
                eagain_count, elapsed);
            break ;
        }
        else if (n == 0)
        {
            // eof
            printf("[child] EOF 수신 (write end 닫힘)\n");
            break;
        } else 
        {
            // n = 1
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                /**
                 * non blocking 핵심
                 * 데이터 없으면 블록되지 않고 EAGAIN(-1) 즉시 반환
                 * process 는 깨어 있고 cpu 사용중
                 */

                eagain_count++;
                if (eagain_count % 500000 == 0)
                {
                    printf("[child] EAGAIN #%d - 아직 데이터 없음. 재시도 .... \n", eagain_count);
                    fflush(stdout);
                }
                continue;
            } 
            else 
            {
                perror("[child] read error");
                break;
            }
        }
    } 

    close(pipefd[0]);
    exit(EXIT_SUCCESS);
}

int main(void)
{
    int pipefd[2];

    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    printf("=== sync + non blocking io ==\n\n");

    pid_t pid = fork();
    
    if (pid < 0)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
        do_child(pipefd);
    }

    close(pipefd[0]);

    printf("[Parent PID=%d] 3초 후 데이터 전송 예정.\n", getpid());
    printf("[Parent PID=%d] 그 동안 Child는 폴링 루프를 돌며 CPU를 소모 중.\n\n", getpid());
    fflush(stdout);

    sleep(3);

    const char *msg = "논블로킹 폴링 성공!";
    write(pipefd[1], msg, strlen(msg));
    printf("[Parent] write() 완료\n");
    close(pipefd[1]);

    wait(NULL);

    printf("\n[결론]\n");
    printf("  - 논블로킹 read()는 데이터 없을 때 EAGAIN을 즉시 반환.\n");
    printf("  - 프로세스가 잠들지 않아서 다른 작업 병행 가능.\n");
    printf("  - But! 폴링 루프는 CPU를 낭비한다. → select/epoll로 해결.\n");

    return 0;
}