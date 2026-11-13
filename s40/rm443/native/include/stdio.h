#ifndef S40_STDIO_H
#define S40_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct s40_file FILE;
extern FILE *stdout,*stderr,*stdin;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char *,const char *); int fclose(FILE *); size_t fread(void *,size_t,size_t,FILE *); size_t fwrite(const void *,size_t,size_t,FILE *);
int fseek(FILE *,long,int); long ftell(FILE *); int fflush(FILE *); int feof(FILE *); int ferror(FILE *);
int fgetc(FILE *); int fputc(int,FILE *); char *fgets(char *,int,FILE *); int fputs(const char *,FILE *);
int putchar(int); int puts(const char *); int printf(const char *,...); int fprintf(FILE *,const char *,...);
int vprintf(const char *,va_list); int vfprintf(FILE *,const char *,va_list);
int sprintf(char *,const char *,...); int snprintf(char *,size_t,const char *,...); int vsnprintf(char *,size_t,const char *,va_list);
int sscanf(const char *,const char *,...); int remove(const char *); int rename(const char *,const char *);
#define getc fgetc
#define putc fputc
#endif
