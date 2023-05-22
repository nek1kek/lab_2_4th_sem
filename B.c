#include <readline/readline.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <wait.h>


char **GetArgsFromStr(char *argsStr) {
	char **argsArr = NULL;
	char *arg = strtok(argsStr, " \"");

	int i = 0;
	for (; arg != NULL;i++) {
		argsArr = realloc(argsArr, (i + 1) * sizeof(char *));
		argsArr[i] = arg;
		arg = strtok(NULL, " \"");
	}

	argsArr = realloc(argsArr, (i + 1) * sizeof(char *));
	argsArr[i] = NULL;

	return argsArr;
}


/*
более наглядная схема:
      pipe1           pipe2
       ====           =====
  ...    |   grep "c"   |   ....
   |	 	  |             |
мы         arg_main      arg_next
тут        то какую    вызовутся в нас
           функцию    будут нашими детьми
           рожаем     куда передадим конец
                      нашего контейнера




Друг за другом идут с промежутком в виде дескриптера 0 - инпут 1 - оутпут, пока строка не закончится
Прикольно на самом деле сделано, на работе часто конвейером пользуюсь
кстати это работает и у нас:
	у нас 4-7 уровневая архитектура по передаче json ФНС можно сказать с каждым новым уровня в lowlevel to higlevel у нас добавляется какая-то инфа в json
типо конвеер)))


0 cat 1, перекинул, 1  стало 0, а 1 чиста, и теперь 0 grep 1, потому одни данные гоняет по кругу в течение n+1 "|" 
*/

int main() {
	char *cmd;
	cmd = readline("ProgB is ready: ");

	char *foo_main = strtok(cmd, "|"); //выделяем функцию, что смотрим щас
	int pipe1[2] = {-1, -1};
	int pipe2[2];
	kill(getppid(), SIGUSR1);//отправь отцу что я родился, если я первый сын, то пусть на счетчик поставит еще

	while (foo_main != NULL) {
	    char *others_foo = strtok(NULL, "|");//отделим остальные
    	if (others_foo != NULL) { // остаются ли у нас следующие процессы
     		 pipe(pipe2);//создадим канал для общения нас сыном
    	}

    	int childPID = fork();

	    if (childPID == -1) {
    	  perror("error fork");
     	 exit(EXIT_FAILURE);
    	}

	    if (childPID == 0) {
			char **argsArr = GetArgsFromStr(foo_main); // получим аргументы к нынешней команде и саму команду

			if (pipe1[0] != -1) { // чекнем сущестовал ли предыдущий pipe
		        close(pipe1[1]);
				dup2(pipe1[0], 0);
      		}

			if (others_foo != NULL) { // check if next pipe exists
		        close(pipe2[0]);
        		dup2(pipe2[1], 1);
      		}
			prctl(PR_SET_PDEATHSIG, SIGTERM);//я тебя породил, я тебя убью, или он же ON_DELETE(CASCADE) - короче убьет ребенка при смерти нас, чтобы обработать случай >3сек

			if (execvp(argsArr[0], argsArr) == -1) {// опять же обработаем, что execvp сработал нормально
		        perror("error execvp");
		        kill(getppid(), SIGTERM);
		        exit(EXIT_FAILURE);
      		}
    	}
		close(pipe1[0]);//ну все к нам все пришло, что нужно, закроем
    	close(pipe1[1]);
    	pipe1[0] = pipe2[0];// однако у нас же конвейер, потому передадим ребенку тот же вход и выход)))
    	pipe1[1] = pipe2[1];
		foo_main = others_foo;//чтобы обрезать нынешнуюю функцию при следующей итерации, и не заюзать опять
	    int status;
	    wait(&status);//так же как в А ждем окончания дочек

    	if (status != 0){//если эта дочь заметила, что внучка не норм сработала откинем ошибочку
     		 exit(EXIT_FAILURE);
    	}
  	}

	free(cmd);//это не питон, это не питон...
	return 0;
}

