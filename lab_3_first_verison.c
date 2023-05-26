#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

//https://russianblogs.com/article/6014372543/ брал от сюда

#define BLOCK_SIZE 24 // чтобы s_block весил 24 байта

void *first_block = NULL; // откуда память начинается

//организуем функции
typedef struct s_block *t_block;

struct s_block {
    size_t size; // размер
    t_block prev; // связный список
    t_block next; // будущий
    int free; // флаг чисто или нет
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

void split_block(t_block b, size_t s) { // чтобы не позволить небольшому размеру занимать большой блок. будем сплитить
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

void copy_block(t_block src, t_block dst) {
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

    if (first_block) { //если
        last = (t_block)first_block;
        b = find_block(&last, s);
        if (b) {
            if ((b->size - s) >= (BLOCK_SIZE + 8)) {
                split_block(b, s);//разделим если возможно
            }
            b->free = 0;
        }
        else {
            b = extend_heap(last, s);
            if (!b) {
                return NULL;
            }
        }
    }
    else {
        b = extend_heap(NULL, s);
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

    if (new) {
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
        return my_malloc(size);
    }
    if (valid_addr(p)) {
        s = align8(size);
        b = get_block(p);
        if (b->size >= s) {
            if (b->size - s >= (BLOCK_SIZE + 8)) {
                split_block(b, s);
            }
        }
        else {
            if (b->next && b->next->free &&
                    (b->size + BLOCK_SIZE + b->next->size) >= s) {
                fusion(b);
                if (b->size - s >= (BLOCK_SIZE + 8)) {
                    split_block(b, s);
                }
            } else {
                newp = my_malloc(s);
                if (!newp) {
                    return NULL;
                }
                new = get_block(newp);
                copy_block(b, new);
                free(p);
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
        b = get_block(p);
        b->free = 1;
        if (b->prev && b->prev->free) {
            b = fusion(b->prev);
        }
        if (b->next) {
            fusion(b);
        }
        else {
            if (b->prev) {
                b->prev->prev = NULL;
            } else {
                first_block = NULL;
            }
            brk(b);
        }
    }
}


// тут уже тестирование идет
struct testing {
    FILE *file;     // дескриптор файла
    int *biggest;   // для 1 мб
    int *average;   // для 16 Кб
    int *smallest;  // для 1 Кб
};

void *alloc_f(void *arg) {
    struct testing *testing = (struct testing *)arg;
    // так как инт весит 4 байт, то:
    testing->biggest = my_malloc(sizeof(int) * 256 * 1024);
    testing->average = my_malloc(sizeof(int) * 256);
    testing->smallest = my_malloc(sizeof(int) * 4);
    printf("Address biggest: %p\nAddress average: %p\nAddress smallest: %p\n",
                                (testing->biggest),(testing->average), (testing->smallest));
    pthread_exit(NULL);
}

void *add_elements(void *arg) {
    struct testing *testing = (struct testing *)arg;
    for (int i = 0; i < 250000; i++) {
        testing->biggest[i] = i;
    }
    for (int i = 0; i < 1000; ++i) {
        testing->average[i] = i;
    }
    for (int i = 0; i < 10; ++i) {
        testing->smallest[i] = i;
    }
    pthread_exit(NULL);
}

void *print_result(void *arg) {
    struct testing *testing = (struct testing *)arg;
    fprintf(testing->file, "\n another one bite to thread: \n\nBiggest (count 250000):\n");
    for (int i = 0; i < 250000;i+=10000) {
        fprintf(testing->file, "%d ", testing->biggest[i]); //file printf
    }
    fprintf(testing->file, "\n\nAverage (count: 1000):\n");
    for (int i = 0; i < 1000;i+=100) {
        fprintf(testing->file, "%d ", testing->average[i]);
    }
    fprintf(testing->file, "\n\nSmallest (count: 10):\n");
    for (int i = 0; i < 10; i++) {
        fprintf(testing->file, "%d ", testing->smallest[i]);
    }
    fprintf(testing->file, "\n");
    pthread_exit(NULL);
}


int check_decorator(result){
    if (result){
        exit(EXIT_FAILURE);
    }
    return 0;
}

void testings(int i){
    struct testing testing; // струкктура
    testing.file = fopen("testing.txt", "a");
    pthread_t thread_i;

    // первая треть потоков - создающая
    check_decorator(pthread_create(&thread_i, NULL, alloc_f, &testing));
    check_decorator(pthread_join(thread_i, NULL));

    // вторая треть потоков - заполняющая
    check_decorator(pthread_create(&thread_i, NULL, add_elements, &testing));
    check_decorator(pthread_join(thread_i, NULL));

    // последняя треть потоков - записывающая в файл
    check_decorator(pthread_create(&thread_i, NULL, print_result, &testing));
    check_decorator(pthread_join(thread_i, NULL));

    my_free(testing.biggest);
    my_free(testing.average);
    my_free(testing.smallest);

    printf("DONE\n\n");
}


int main() {
    // протестируем, что вообще работает my_malloc
    int* p = my_malloc(sizeof(int));
    my_free(p);

    //теперь уже делаем
    for (int M = 0; M<3;M++) {
        testings(M);
    }
    return 0;
}


