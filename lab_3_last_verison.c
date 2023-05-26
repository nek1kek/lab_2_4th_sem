#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#define BLOCK_SIZE 24 // чтобы s_block весил 24 байта
#define NUM_OF_THREADS 12 // кол-во потоков
#define NUM_OF_CYCLES 2 // кол-во потоков


void *first_block = NULL; // откуда память начинается
pthread_mutex_t mutex; // мьютекс


//организуем функции
typedef struct s_block *t_block;

struct s_block {
    size_t size; // размер
    t_block prev; // связный список
    t_block next; // будущий
    int free; // флаг чисто(1) или нет
    void *ptr; // указатель на данные перед освобождением, для валидности
    char data[1]; // это способ обращения строго к данным после метаданных выше, то есть  выделяется динамически, после всех ресурсов, ровно для этого
};

t_block find_block(t_block *last, size_t size) {// ищем первый свободный блок
    t_block b = first_block;

    while (b && !(b->free && b->size >= size)) { //если удается найти свободный и удовлетворяющего размера туда ставим
        *last = b;
        b = b->next;
    }
    return b;
}

t_block extend_heap(t_block last, size_t s) {//если не находим блок, то перевыделяем память под новый блок
    t_block b;
    b = sbrk(0);// нынешняя граница
    if (sbrk(BLOCK_SIZE + s) == (void *)-1) { //если sbrk выдал ошибку
        return NULL;
    }
    b->size = s;
    b->next = NULL; //двусвязный список передает вам привет
    if (last) {
        last->next = b;
    }
    b->free = 0;// сразу флаг ставим, что не чист
    return b;
}

void split_block(t_block b, size_t s) { // здесь добавляем промежуточный между ними
    t_block new; // алгоритм двусвязного цикла чисто

    new = (t_block)(b->data + s);
    new->size = b->size - s - BLOCK_SIZE;
    new->next = b->next;
    new->free = 1;

    b->size = s;
    b->next = new;
}

size_t align8(size_t s) {//округляем к 8 все 
    if ((s & 0x7) == 0) { // побитово сравним, если в нуль обратся, то значит все окей
        return s;
    }
    return ((s >> 3) + 1) << 3; // хардкор в смысле бреда, по факту сдвигаем побитого направо на 8, добавляем 1 и направо
}

t_block get_block(void *p) { // находим предыдущий блок
    char *tmp;
    tmp = p;
    return (p = tmp -= BLOCK_SIZE);
}

int valid_addr(void *p) { //является ли валидным адрессом (лежит ли между первым и границей)
    if (first_block) {
        if (first_block < p && p < sbrk(0)) {
            return p == (get_block(p))->ptr;
        }
    }
    return 0; // для случая если не первого блока
}

t_block fusion(t_block b) {//объединение 
    if (b->next && b->next->free) {
        b->size += BLOCK_SIZE + b->next->size;
        b->next = b->next->next;
        if (b->next) {
            b->next->prev = b;
        }
    }
    return b;
}

void copy_block(t_block src, t_block dst) { // для реалока чтобы перенести инфу
    size_t *sdata;
    size_t *ddata;
    sdata = src->ptr;
    ddata = dst->ptr;
    for (size_t i = 0; (i * 8) < src->size && (i * 8) < dst->size; ++i) {
        ddata[i] = sdata[i];
    }
}

void *my_malloc(size_t size) {
    t_block b, last;
    size_t s;
    s = align8(size); // округлим до 8 размер, чтобы не парится

    if (first_block) {
        last = (t_block)first_block;
        b = find_block(&last, s); // постараемся найти в списке блок, что удовлетворяет нашим требованиям: есть s байт для инфы
        if (b) {
            if ((b->size - s) >= (BLOCK_SIZE + 8)) {
                split_block(b, s);// и если блок с не до конца заполненной памятью, от просто разделим его и добавим в конце
            }
            b->free = 0;
        }
        else {
            b = extend_heap(last, s); // расширяем кучу, если не смогли найти свободный блок
            if (!b) {
                return NULL; // если выдала ошибку
            }
        }
    }
    else {
        b = extend_heap(NULL, s); // если мы впервые импользуем malloc
        if (!b) {
            return NULL;
        }
        first_block = b;
    }
    return b->data;
}

void *my_calloc(size_t number, size_t size) {
    size_t *new = NULL;
    size_t s8;
    new = (size_t *)my_malloc(number * size);

    if (new) { // по факту тоже самое, только надо забить все нулями, calloc же
        s8 = align8(number * size) >> 3;

        for (size_t i = 0; i < s8; ++i) {
            new[i] = 0;
        }
    }

    return new;
}

void *my_realloc(void *p, size_t size) {
    size_t s;
    t_block b, new;
    void *newp;
    if (!p) {
        return my_malloc(size); // если p = Null, то нам нужно будет инфу добавить
    }
    if (valid_addr(p)) { // если адрес правильный, то есть такой блок есть
        s = align8(size); //округлим размер
        b = get_block(p); // получим этот блок
        if (b->size >= s) { // обрезмаем, если меньше
            if (b->size - s >= (BLOCK_SIZE + 8)) {
                split_block(b, s);
            }
        }
        else { // а вот если больше
            if (b->next && b->next->free &&
                    (b->size + BLOCK_SIZE + b->next->size) >= s) { // если дальше можем расширить, то
                fusion(b); // объединим
                if (b->size - s >= (BLOCK_SIZE + 8)) {
                    split_block(b, s); // и разделим под наш размер
                }
            } else {
                newp = my_malloc(s); //если дальше нет, то заюзаем маллок
                if (!newp) {
                    return NULL;
                }
                new = get_block(newp); // получим блок
                copy_block(b, new); // и перекинем данные от прошлого
                free(p); // очистим старый
                return newp;
            }
        }
        return p;
    }
    return NULL;
}

void my_free(void *p) {// очищаем ссылаясь на первый указатель
    t_block b;
    if (valid_addr(p)) { // если адрес валидный(есть)
        b = get_block(p); // получим блок по адресуу
        b->free = 1; // говорим, что он теперь чист
        if (b->prev && b->prev->free) { // если предыдуий чист тоже, то просто из объединим
            b = fusion(b->prev);
        }
        if (b->next) {  // если следующий есть, то объединим с ним
            fusion(b);
        }
        else { // в ином же случае ставим всем нули
            if (b->prev) {
                b->prev->prev = NULL;
            } else {
                first_block = NULL;
            }
            brk(b); // двигаем границу кучи вниз
        }
    }
}


// тут уже тестирование идет
struct testing {
    int *biggest;   // для 1кб
    int *smallest;  // для 16б
    int i;
};

void *alloc_f(struct testing *arg) {

    pthread_mutex_lock(&mutex);

    printf("    %d поток:\n",arg->i);//чтобы посмотреть крутизну многопоточки
    // так как инт весит 4 байт, то:
    arg->biggest = my_malloc(sizeof(int) * 256);
    arg->smallest = my_malloc(sizeof(int) * 4);
    printf("        Address biggest:\t%p\n        Address smallest:\t%p\n",
                                (arg->biggest), (arg->smallest));

    pthread_mutex_unlock(&mutex);
    pthread_exit(NULL);
}

void *add_elements(struct testing *arg) {

    pthread_mutex_lock(&mutex);

    for (int i = 0; i < 256; ++i) {
        arg->biggest[i] = i;
    }
    for (int i = 0; i < 4; ++i) {
        arg->smallest[i] = i;
    }

    pthread_mutex_unlock(&mutex);

    pthread_exit(NULL);
}

void *print_result(struct testing *arg) {
    pthread_mutex_lock(&mutex);

    FILE* file = fopen("testing.txt", "a"); //дескриптор
    fprintf(file, "\n\nBiggest (count: 16):\n");
    for (int i = 0; i < 256;i+=16) {
        fprintf(file, "%d ", arg->biggest[i]);
    }
    fprintf(file, "\n\nSmallest (count: 1):\n");
    for (int i = 0; i < 4; i++) {
        fprintf(file, "%d ", arg->smallest[i]);
    }
    fprintf(file, "\n");

    my_free(arg->biggest);
    my_free(arg->smallest);

    fclose(file);
    pthread_mutex_unlock(&mutex);
    pthread_exit(NULL);
}


int check_decorator(result){
    if (result){
        exit(EXIT_FAILURE);
    }
    return 0;
}


int main() {
    for (int z = 0; z<NUM_OF_CYCLES;z++){
        printf("\n%d итерация\n",z);
        pthread_t threads[NUM_OF_THREADS];
        struct testing testing[NUM_OF_THREADS/3];
        size_t i;

        //Инициализация мьютекса
        pthread_mutex_init(&mutex, NULL);

        // первая треть потоков - создающая
        for (i = 0; i < NUM_OF_THREADS/3; i++) {
            struct testing* potok_testing = &(testing[i%(NUM_OF_THREADS/3)]);
            potok_testing->i = i;
            check_decorator(pthread_create(&threads[i], NULL, alloc_f, potok_testing));
        }
        // ждем пока эта часть потоки закроются
        for (i = 0; i < NUM_OF_THREADS/3; i++) {
            check_decorator(pthread_join(threads[i], NULL));
        }
        // вторая треть потоков - заполняющая
        for (i = NUM_OF_THREADS/3; i < 2*NUM_OF_THREADS/3; i++) {
            check_decorator(pthread_create(&threads[i], NULL, add_elements, &(testing[i%(NUM_OF_THREADS/3)])));
        }
        // ждем пока эта часть потоки закроются
        for (i = NUM_OF_THREADS/3; i < 2*NUM_OF_THREADS/3; i++) {
            check_decorator(pthread_join(threads[i], NULL));
        }
        // последняя треть потоков - записывающая в файл
        for (i = 2*NUM_OF_THREADS/3; i < NUM_OF_THREADS; i++) {
            check_decorator(pthread_create(&threads[i], NULL, print_result, &(testing[i%(NUM_OF_THREADS/3)])));
        }
        // ждем пока все потоки закроются
        for (i = 2*NUM_OF_THREADS/3; i < NUM_OF_THREADS; i++) {
            check_decorator(pthread_join(threads[i], NULL));
        }
        //Уничтожение мьютекса
        pthread_mutex_destroy(&mutex);
    }
    return 0;
}


