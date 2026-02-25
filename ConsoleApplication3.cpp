/* Минимальный BASIC-интерпретатор */

#include <stdio.h>
#include <setjmp.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <windows.h>
#include <map>
#include <string>

#define NUM_LAB 100
#define LAB_LEN 10
#define FOR_NEST 25
#define SUB_NEST 25
#define PROG_SIZE 10000

#define DELIMITER 1
#define VARIABLE  2
#define NUMBER    3
#define COMMAND   4
#define STRING    5
#define QUOTE     6
#define EOL       9
#define FINISHED  10

#define PRINT   1
#define INPUT   2
#define IF      3
#define THEN    4
#define ELSE    5
#define FOR     6
#define NEXT    7
#define TO      8
#define GOTO    9
#define GOSUB   10
#define RETURN  11
#define END     12
#define WHILE   13
#define WEND    14

/* Глобальные переменные */
char* prog; /* указатель на программу */
char* prog_start;
jmp_buf e_buf; /* буфер для longjmp() */
std::map<std::string, double> variables; /* динамический массив переменных */
char token[80]; /* внешнее представление лексемы */
char token_type, tok; /* тип и внутреннее представление лексемы */
int ftos; /* индекс вершины стека FOR */
int gtos; /* индекс вершины стека GOSUB */
int wtos; /* индекс вершины стека WHILE */

/* Таблица команд */
struct commands {
    char command[20];
    char tok;
} table[] = {
    "print", PRINT,
    "input", INPUT,
    "if", IF,
    "then", THEN,
    "else", ELSE,
    "goto", GOTO,
    "for", FOR,
    "next", NEXT,
    "to", TO,
    "gosub", GOSUB,
    "return", RETURN,
    "end", END,
    "while", WHILE,
    "wend", WEND,
    "", END /* маркер конца таблицы */
};

/* Структуры для меток и стеков */
struct label {
    char name[LAB_LEN];
    char* p;
};
struct label label_table[NUM_LAB];

struct for_stack {
    char var[80]; /* имя управляющей переменной */
    double target; /* конечное значение */
    char* loc; /* местоположение в программе */
};
struct for_stack fstack[FOR_NEST]; /* стек для FOR/NEXT */

struct while_stack {
    char* loc; /* местоположение в программе */
    char* wend_loc; /* местоположение WEND */
};
struct while_stack wstack[FOR_NEST]; /* стек для WHILE/WEND */

char* gstack[SUB_NEST]; /* стек для GOSUB */

/* Прототипы функций */
void get_exp(double* result);
void level2(double* result);
void level3(double* result);
void level4(double* result);
void level5(double* result);
void level6(double* result);
void primitive(double* result);
void arith(char o, double* r, double* h);
void unary(char o, double* r);
double find_var(const char* s);
void serror(int error);
int get_token(void);
void putback(void);
int look_up(const char* s);
int isdelim(char c);
int iswhite(char c);
void print(void);
void scan_labels(void);
void find_eol(void);
void exec_goto(void);
void exec_if(void);
void exec_for(void);
void next(void);
void fpush(struct for_stack i);
struct for_stack fpop(void);
void input(void);
void gosub(void);
void greturn(void);
void gpush(char* s);
char* gpop(void);
void label_init(void);
int get_next_label(const char* s);
int load_program(char* p, const char* fname);
void assignment(void);
void exec_while(void);
void exec_wend(void);
void wpush(struct while_stack w);
struct while_stack wpop(void);

/* Форматирование числа с точкой в качестве десятичного разделителя */
void format_number(double num, char* buffer, size_t size) {
    char temp[32];
    // Используем точку как десятичный разделитель
    setlocale(LC_NUMERIC, "C");
    snprintf(temp, sizeof(temp), "%.2f", num);
    setlocale(LC_NUMERIC, "Russian");

    strncpy_s(buffer, size, temp, _TRUNCATE);
}

/* Точка входа */
int main(void) {
    setlocale(LC_ALL, "Russian");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    char* p_buf;
    char fname[256];

    printf("Введите имя файла: ");
    scanf_s("%255s", fname, (unsigned)_countof(fname));

    /* Выделение памяти для программы */
    if (!(p_buf = (char*)malloc(PROG_SIZE))) {
        printf("Ошибка при выделении памяти\n");
        exit(1);
    }

    /* Загрузка программы */
    if (!load_program(p_buf, fname)) {
        printf("Ошибка загрузки программы\n");
        exit(1);
    }

    if (setjmp(e_buf)) exit(1); /* Инициализация буфера longjmp */

    prog = p_buf;
    scan_labels(); /* Поиск меток */
    ftos = 0; /* Инициализация стека FOR */
    gtos = 0; /* Инициализация стека GOSUB */
    wtos = 0; /* Инициализация стека WHILE */

    /* Основной цикл интерпретатора */
    do {
        token_type = get_token();
        if (token_type == VARIABLE) {
            putback();
            assignment();
        }
        else {
            switch (tok) {
            case PRINT:
                print();
                break;
            case IF:
                exec_if();
                break;
            case GOTO:
                exec_goto();
                break;
            case FOR:
                exec_for();
                break;
            case NEXT:
                next();
                break;
            case INPUT:
                input();
                break;
            case GOSUB:
                gosub();
                break;
            case RETURN:
                greturn();
                break;
            case WHILE:
                exec_while();
                break;
            case WEND:
                exec_wend();
                break;
            case END:
                exit(0);
            }
        }
    } while (tok != FINISHED);

    free(p_buf);
    return 0;
}

/* Поиск значения переменной */
double find_var(const char* s) {
    if (!isalpha(*s)) {
        serror(4);
        return 0;
    }
    return variables[s];
}

/* Присваивание значения переменной */
void assignment(void) {
    char var_name[80];
    double value;

    get_token();
    if (token_type != VARIABLE) {
        serror(4); return;
    }

    strcpy_s(var_name, token);

    // Пропускаем возможные пробелы между переменной и '='
    get_token();
    while (*token == '\n' || *token == '\r') get_token();

    if (token_type != DELIMITER || *token != '=') {
        serror(3); return;
    }


    get_exp(&value);
    variables[var_name] = value;
}

/* Реализация команды PRINT */
void print(void) {
    double answer;
    int len = 0, spaces;
    char last_delim;
    char num_buffer[32];

    do {
        get_token();
        if (tok == EOL || tok == FINISHED) break;

        if (token_type == QUOTE) {
            printf("%s", token);
            len += strlen(token);
            get_token();
        }
        else {
            putback();
            get_exp(&answer);
            get_token();
            format_number(answer, num_buffer, sizeof(num_buffer));
            len += printf("%s", num_buffer);
        }

        last_delim = *token;

        if (*token == ';') {
            spaces = 8 - (len % 8);
            len += spaces;
            while (spaces--) printf(" ");
        }
        else if (*token == ',') {
            /* Ничего не делать */
        }
        else if (tok != EOL && tok != FINISHED) {
            serror(0);
        }
    } while (*token == ';' || *token == ',');

    if (tok == EOL || tok == FINISHED) {
        if (last_delim != ';' && last_delim != ',') printf("\n");
    }
    else {
        serror(0);
    }
}

/* Реализация команды INPUT */
void input(void) {
    char var_name[80];
    double value;
    char input_buffer[256];

    get_token();
    if (token_type == QUOTE) {
        printf("%s", token);
        get_token();
        if (*token != ',') serror(1);
        get_token();
    }
    else {
        printf("? ");
    }

    if (!isalpha(*token)) {
        serror(4);
        return;
    }

    strcpy_s(var_name, token);

    // Читаем строку и преобразуем в число
    scanf_s("%255s", input_buffer, (unsigned)_countof(input_buffer));
    setlocale(LC_NUMERIC, "C");
    char* endptr;
    value = strtod(input_buffer, &endptr);
    if (*endptr != '\0') {
        serror(0); // Синтаксическая ошибка при разборе числа
    }
    setlocale(LC_NUMERIC, "Russian");

    variables[var_name] = value;
}

/* Реализация FOR */
void exec_for(void) {
    struct for_stack i;
    double value;

    get_token();
    if (token_type != VARIABLE) {
        serror(4);
        return;
    }

    strcpy_s(i.var, token);

    get_token();
    if (token_type != DELIMITER || *token != '=') {
        serror(3); // предполагается '='
        return;
    }

    get_exp(&value);
    variables[i.var] = value;

    get_token();
    if (tok != TO) serror(9);
    get_exp(&i.target);

    if (value <= i.target) {
        i.loc = prog;
        fpush(i);
    }
    else {
        // пропускаем до NEXT
        while (tok != NEXT && tok != FINISHED) get_token();
    }
}


/* Реализация NEXT */
void next(void) {
    struct for_stack i;

    i = fpop();
    variables[i.var] += 1.0;
    if (variables[i.var] > i.target) {
        // Находим следующую строку после NEXT
        while (*prog != '\n' && *prog != '\0') prog++;
        if (*prog == '\n') prog++;
        return;
    }
    fpush(i);
    prog = i.loc;
}

/* Реализация IF */
void exec_if(void) {
    double x, y;
    char op;
    int cond = 0;

    get_exp(&x);
    get_token();
    if (!strchr("=<>", *token)) {
        serror(0);
        return;
    }

    op = *token;
    get_exp(&y);

    switch (op) {
    case '=': cond = (x == y); break;
    case '<': cond = (x < y); break;
    case '>': cond = (x > y); break;
    }

    get_token();
    if (tok != THEN) {
        serror(8);
        return;
    }

    if (cond) {
        get_token();
        if (tok == PRINT) {
            print();
        }
        else if (token_type == VARIABLE) {
            prog -= strlen(token);
            assignment();
        }
        else {
            serror(0);
        }
        find_eol(); // ✅ Переход на новую строку, чтобы НЕ выполниться ELSE
    }
    else {
        while (tok != ELSE && tok != EOL && tok != FINISHED) {
            get_token();
        }

        if (tok == ELSE) {
            get_token();
            if (tok == PRINT) {
                print();
            }
            else if (token_type == VARIABLE) {
                prog -= strlen(token);
                assignment();
            }
            else {
                serror(0);
            }
            find_eol();
        }
        else {
            find_eol();
        }
    }
}


/* Реализация WHILE */
void exec_while(void) {
    struct while_stack w;
    double x, y;
    char op;
    int cond = 0;

    // Найдём начало строки WHILE (важно!)
    char* start = prog;
    while (start > prog_start && *(start - 1) != '\n' && *(start - 1) != '\r') {
        start--;
    }
    w.loc = start;

    get_exp(&x);
    get_token();
    if (!strchr("=<>", *token)) {
        serror(0);
        return;
    }

    op = *token;
    get_exp(&y);

    switch (op) {
    case '=': cond = (x == y); break;
    case '<': cond = (x < y); break;
    case '>': cond = (x > y); break;
    }

    if (cond) {
        w.wend_loc = NULL;
        wpush(w);
    }
    else {
        while (tok != WEND && tok != FINISHED) get_token();
        if (tok == WEND) get_token(); // выйти за пределы WEND
    }
}


/* Реализация WEND */
void exec_wend(void) {
    struct while_stack w;
    w = wpop();
    if (w.wend_loc == nullptr) {
        w.wend_loc = prog;
        wpush(w);
    }
    prog = w.loc; // Возвращаемся к началу цикла
}

/* Помещение в стек WHILE */
void wpush(struct while_stack w) {
    if (wtos >= FOR_NEST)
        serror(10);
    wstack[wtos] = w;
    wtos++;
}

/* Извлечение из стека WHILE */
struct while_stack wpop(void) {
    wtos--;
    if (wtos < 0) serror(11);
    return wstack[wtos];
}

/* Помещение в стек FOR */
void fpush(struct for_stack i) {
    if (ftos >= FOR_NEST)
        serror(10);
    fstack[ftos] = i;
    ftos++;
}

/* Извлечение из стека FOR */
struct for_stack fpop(void) {
    ftos--;
    if (ftos < 0) serror(11);
    return fstack[ftos];
}

/* Выполнение арифметической операции */
void arith(char o, double* r, double* h) {
    register double t, ex;

    switch (o) {
    case '-':
        *r = *r - *h;
        break;
    case '+':
        *r = *r + *h;
        break;
    case '*':
        *r = *r * *h;
        break;
    case '/':
        if (*h == 0) serror(0); /* Деление на ноль */
        *r = *r / *h;
        break;
    case '%':
        if (*h == 0) serror(0);
        t = floor(*r / *h);
        *r = *r - (t * *h);
        break;
    case '^':
        ex = *r;
        if (*h == 0) {
            *r = 1;
            break;
        }
        *r = pow(ex, *h);
        break;
    }
}

/* Унарная операция */
void unary(char o, double* r) {
    if (o == '-') *r = -(*r);
}

/* Вывод сообщения об ошибке */
void serror(int error) {
    static const char* e[] = {
        "Синтаксическая ошибка",
        "Непарные круглые скобки",
        "Это не выражение",
        "Предполагается символ равенства",
        "Не переменная",
        "Таблица меток переполнена",
        "Дублирование меток",
        "Неопределенная метка",
        "Необходим оператор THEN",
        "Необходим оператор TO",
        "Уровень вложенности цикла FOR слишком велик",
        "NEXT не соответствует FOR",
        "Уровень вложенности GOSUB слишком велик",
        "RETURN не соответствует GOSUB",
        "Необходим оператор ELSE",
        "Необходим оператор WEND"
    };
    printf("%s\n", e[error]);
    longjmp(e_buf, 1);
}

/* Получение очередной лексемы */
int get_token(void) {
    register char* temp;

    token_type = 0; tok = 0;
    temp = token;

    if (*prog == '\0') { /* Конец программы */
        *token = 0;
        tok = FINISHED;
        return (token_type = DELIMITER);
    }

    while (iswhite(*prog)) ++prog; /* Пропуск пробелов */

    if (*prog == '\r' || *prog == '\n') { /* Конец строки */
        if (*prog == '\r') {
            ++prog;
            if (*prog == '\n') ++prog;
        }
        else {
            ++prog;
        }
        tok = EOL; *token = '\r';
        token[1] = '\n'; token[2] = 0;
        return (token_type = DELIMITER);
    }

    if (strchr("+-*^/%=;(),><", *prog)) { /* Разделитель */
        *temp = *prog;
        prog++;
        temp++;
        *temp = 0;
        return (token_type = DELIMITER);
    }

    if (*prog == '"') { /* Строка в кавычках */
        prog++;
        while (*prog != '"' && *prog != '\r' && *prog != '\n' && *prog != '\0') {
            *temp++ = *prog++;
        }
        if (*prog != '"') serror(1);
        prog++; *temp = 0;
        return (token_type = QUOTE);
    }

    if (isdigit(*prog)) { /* Число */
        while (!isdelim(*prog)) *temp++ = *prog++;
        *temp = '\0';
        return (token_type = NUMBER);
    }

    if (isalpha(*prog)) { /* Переменная или команда */
        while (!isdelim(*prog)) *temp++ = *prog++;
        token_type = STRING;
    }

    *temp = '\0';

    if (token_type == STRING) {
        tok = look_up(token);
        if (!tok) token_type = VARIABLE;
        else token_type = COMMAND;
    }

    return token_type;
}

/* Возврат лексемы во входной поток */
void putback(void) {
    char* t = token;
    for (; *t; t++) prog--;
}

/* Поиск внутреннего представления лексемы */
int look_up(const char* s) {
    register int i;
    char* p = (char*)s;

    while (*p) { *p = tolower(*p); p++; }

    for (i = 0; *table[i].command; i++)
        if (!strcmp(table[i].command, s)) return table[i].tok;

    return 0;
}

/* Проверка, является ли символ разделителем */
int isdelim(char c) {
    if (strchr(" ;,+-<>/*%^=()", c) || c == 9 || c == '\r' || c == 0)
        return 1;
    return 0;
}

/* Проверка на пробел или табуляцию */
int iswhite(char c) {
    if (c == ' ' || c == '\t') return 1;
    return 0;
}

/* Загрузка программы */
int load_program(char* p, const char* fname) {
    FILE* fp;
    int i = 0;
    unsigned char buffer[3];
    int c;

    if (fopen_s(&fp, fname, "rb") != 0 || !fp) {
        printf("Ошибка открытия файла %s\n", fname);
        return 0;
    }

    // Пропускаем BOM если он есть
    if (fread(buffer, 1, 3, fp) == 3) {
        if (buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF) {
            // BOM найден, начинаем читать с текущей позиции
            printf("Файл в кодировке UTF-8 с BOM\n");
        }
        else {
            // BOM не найден, возвращаемся в начало файла
            fseek(fp, 0, SEEK_SET);
            printf("Файл в кодировке ASCII или UTF-8 без BOM\n");
        }
    }
    else {
        // Файл слишком короткий для BOM, возвращаемся в начало
        fseek(fp, 0, SEEK_SET);
        printf("Файл в кодировке ASCII или UTF-8 без BOM\n");
    }

    // Читаем файл посимвольно
    while ((c = getc(fp)) != EOF && i < PROG_SIZE - 1) {
        // Проверяем на допустимые символы
        if (c >= 0 && c <= 127) {  // ASCII символы
            *p++ = (char)c;
            i++;
        }
        else if (c >= 0xC0 && c <= 0xDF) {  // UTF-8 двухбайтовый символ
            int c2 = getc(fp);
            if (c2 != EOF) {
                *p++ = (char)c;
                *p++ = (char)c2;
                i += 2;
            }
        }
        else if (c >= 0xE0 && c <= 0xEF) {  // UTF-8 трёхбайтовый символ
            int c2 = getc(fp);
            int c3 = getc(fp);
            if (c2 != EOF && c3 != EOF) {
                *p++ = (char)c;
                *p++ = (char)c2;
                *p++ = (char)c3;
                i += 3;
            }
        }
    }

    *p = '\0'; /* Завершение программы */
    fclose(fp);

    if (i >= PROG_SIZE - 1) {
        printf("Предупреждение: программа была обрезана из-за превышения максимального размера\n");
    }

    return 1;
}

/* Поиск всех меток */
void scan_labels(void) {
    int addr;
    char* temp;

    label_init();
    temp = prog;

    get_token();
    if (token_type == NUMBER) {
        strcpy_s(label_table[0].name, token);
        label_table[0].p = prog;
    }
    find_eol();

    do {
        get_token();
        if (token_type == NUMBER) {
            addr = get_next_label(token);
            if (addr == -1 || addr == -2) {
                (addr == -1) ? serror(5) : serror(6);
            }
            strcpy_s(label_table[addr].name, token);
            label_table[addr].p = prog;
        }
        if (tok != EOL) find_eol();
    } while (tok != FINISHED);

    prog = temp;
}

/* Инициализация таблицы меток */
void label_init(void) {
    register int t;
    for (t = 0; t < NUM_LAB; ++t) label_table[t].name[0] = '\0';
}

/* Поиск следующей строки */
void find_eol(void) {
    while (*prog != '\n' && *prog != '\0') ++prog;
    if (*prog) prog++;
}

/* Получение индекса следующей метки */
int get_next_label(const char* s) {
    register int t;

    for (t = 0; t < NUM_LAB; ++t) {
        if (label_table[t].name[0] == 0) return t;
        if (!strcmp(label_table[t].name, s)) return -2;
    }
    return -1;
}

/* Поиск метки */
char* find_label(const char* s) {
    register int t;

    for (t = 0; t < NUM_LAB; ++t)
        if (!strcmp(label_table[t].name, s)) return label_table[t].p;
    return nullptr;
}

/* Реализация GOTO */
void exec_goto(void) {
    char* loc;

    get_token();
    loc = find_label(token);
    if (loc == nullptr)
        serror(7);
    else
        prog = loc;
}

/* Реализация GOSUB */
void gosub(void) {
    char* loc;

    get_token();
    loc = find_label(token);
    if (loc == nullptr)
        serror(7);
    else {
        gpush(prog); // сохраняем текущую позицию
        prog = loc;  // переходим по метке
    }
}

/* Реализация RETURN */
void greturn(void) {
    prog = gpop(); // возвращаемся к месту после GOSUB
    if (prog == nullptr)
        serror(13);
}

/* Помещение в стек GOSUB */
void gpush(char* s) {
    if (gtos >= SUB_NEST)
        serror(12);
    gstack[gtos] = s;
    gtos++;
}

/* Извлечение из стека GOSUB */
char* gpop(void) {
    if (gtos == 0)
        return nullptr;
    gtos--;
    return gstack[gtos];
}

/* Синтаксический анализатор выражений */
void get_exp(double* result) {
    get_token();
    if (!*token) {
        serror(2);
        return;
    }
    level2(result);
    putback();
}

/* Сложение или вычитание термов */
void level2(double* result) {
    register char op;
    double hold;

    level3(result);
    while ((op = *token) == '+' || op == '-') {
        get_token();
        level3(&hold);
        arith(op, result, &hold);
    }
}

/* Умножение, деление или остаток */
void level3(double* result) {
    register char op;
    double hold;

    level4(result);
    while ((op = *token) == '*' || op == '/' || op == '%') {
        get_token();
        level4(&hold);
        arith(op, result, &hold);
    }
}

/* Возведение в степень */
void level4(double* result) {
    double hold;

    level5(result);
    if (*token == '^') {
        get_token();
        level4(&hold);
        arith('^', result, &hold);
    }
}

/* Унарные + и - */
void level5(double* result) {
    register char op = 0;

    if (token_type == DELIMITER && (*token == '+' || *token == '-')) {
        op = *token;
        get_token();
    }
    level6(result);
    if (op)
        unary(op, result);
}

/* Обработка скобок */
void level6(double* result) {
    if (token_type == DELIMITER && *token == '(') {
        get_token();
        level2(result);
        if (*token != ')')
            serror(1);
        get_token();
    }
    else {
        primitive(result);
    }
}

/* Получение значения переменной или числа */
void primitive(double* result) {
    switch (token_type) {
    case VARIABLE:
        *result = find_var(token);
        get_token();
        break;
    case NUMBER:
        // Временно меняем локаль для корректного разбора чисел
        setlocale(LC_NUMERIC, "C");
        char* endptr;
        *result = strtod(token, &endptr);
        if (*endptr != '\0') {
            serror(0); // Синтаксическая ошибка при разборе числа
        }
        setlocale(LC_NUMERIC, "Russian");
        get_token();
        break;
    default:
        serror(0);
    }
}