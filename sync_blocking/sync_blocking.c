#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

// child process: reader
void do_child(int pipefd[2])
{
     // close write end
        close(pipefd[1]);

        char buf[256];
        // buffer 내용 null 로 초기화
        memset(buf, 0, sizeof(buf));

        printf("[child PID=%d] read() 호출 직전 - 지금부터 blocking.\n", getpid());
        printf("[child pid=%d] data 가 올때까지 child process sleep ... \n\n", getpid());
        fflush(stdout);

        time_t t_before = time(NULL);

        /**
            pipefd[0] 에 data 없으면 OS 가 child process 를 sleep 상태로 전환
            데이터가 들어오는 순간 OS 가 process 를 깨우고 read() 가 반환됨.
            CPU는 child process 가 기다리는 동안 다른 process 에게 넘어감
        */

        // 읽을 데이터가 없을 경우 blocking
        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
        time_t t_after = time(NULL);

       if (n < 0) {
            perror("[Child] read 실패");
            exit(EXIT_FAILURE);
        }

        
        buf[n] = '\0';
        printf("[Child PID=%d] read() 반환! 블록된 시간: %ld초\n", getpid(), t_after - t_before);
        printf("[Child PID=%d] 수신한 데이터: \"%s\" (%zd bytes)\n", getpid(), buf, n);

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

    printf("=== sync + blocking io example ===\n");
    printf("create pipe: read_end = %d, write_end = %d\n", pipefd[0], pipefd[1]);

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

    // parent procss : writer
    
    close(pipefd[0]); // close read end

    printf("[parent = %d] 10초 후 데이터를 전송\n", getpid());
    printf("[parent = %d] child 는 그 10초 동안 sleep 상태임.\n\n", getpid());
    fflush(stdout);

    // child 가 blocking 된 동안 parent 는 다른 일 할 수 있음
    for (int i = 10; i >= 1; i--)
    {
        printf("[parent = %d] %d 초 후 전송 ...\n", getpid(), i);
        fflush(stdout);
        sleep(1);
    }

    const char *msg = "blocking io 완료!!";
    ssize_t written = write(pipefd[1], msg, strlen(msg));
    printf("\n[Parent PID=%d] write() 완료: %zd bytes 전송\n", getpid(), written);
 
    close(pipefd[1]);   /* write end 닫기 → child의 read()가 EOF 인식 */
 
    wait(NULL);         /* child 종료 대기 */
 
    printf("\n[결론] read()는 데이터가 올 때까지 프로세스를 잠재웠다.\n");
    printf("CPU 낭비 없음. 하지만 그 동안 이 스레드로 다른 I/O 불가.\n");
 
    return 0;
}
