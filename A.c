#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>



pid_t  cPID; // объявим глобальную переменную g_childPID, которая будет использоваться для хранения идентификатора дочернего процесса.

void SigHandler(int signum) {
	int status;
	clock_t clockStart = clock();//зафиксируем

	while ((clock() - clockStart ) / CLOCKS_PER_SEC < 3){
		if (waitpid(cPID, &status, WNOHANG) == cPID){//если у нас тут вернется идентификатор сына, то значит завершился со status, который и вернем
			exit(status);
		}
	}

	if (waitpid(cPID, &status, WNOHANG) == 0){
		kill(cPID, SIGTERM);
  	}
}

int main() {
  //объявим сразу обработчик своего сигнала
  signal(SIGUSR1, SigHandler);//10) in kill -l SIGUSR1 - сигнал юзера(прогами не юзается)

  cPID = fork();
  if (cPID == -1){
    perror("error in fork");
    exit(EXIT_FAILURE);
  }
  if (cPID == 0) {
 	execl("./b.out", "./b.out", NULL);//прикольно, что он принимает два раза одно и тоже
  }

  int status;
  wait(&status);//мы запустили программы, теперь дождемся, когда они что-то пришлют или закроются(потенциально должны присласть sigusr1)

  return 0;
}
