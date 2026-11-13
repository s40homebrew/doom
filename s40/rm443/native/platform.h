#ifndef RM443_PLATFORM_H
#define RM443_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
/* All firmware pointers are exact RM-443 10.12 addresses. See docs/imports.json and the constants below. */
void *fw_os_block_alloc_no_wait(unsigned); void fw_os_block_dealloc(void *);
uint32_t fw_os_current_time_read_in_ms(void); unsigned fw_os_task_own_id_read(void);
unsigned fw_msg_count(void); uintptr_t fw_os_msg_receive(void);
void fw_CM_ComMgr_ProcessPhonetMessage(void *); int fw_CM_ComMgr_ProcessInternalQueue(void);
void fw_CM_ComMgr_ScanISISubscriptions(void); void fw_CM_Timer_Timeout(unsigned); int fw_vm_timer(unsigned);
int fw_app_id(void); int fw_event_pop(int,int *,int *,int *);
int fw_filesys_open(const uint16_t *,unsigned,unsigned); int fw_filesys_close(int);
int fw_filesys_read(int,void *,unsigned); int fw_filesys_write(int,const void *,unsigned);
int fw_filesys_lseek(int,int,int); int fw_filesys_unlink(const uint16_t *); int fw_filesys_mkdir(const uint16_t *,unsigned);
int fw_gdi_display_master_reserve_enable(unsigned); void fw_gdi_display_master_reserve_disable(unsigned,int);
void fw_gdi_display_master_update(void *,int,int,int,int,int);
void s40_cleanup(void); void s40_set_root(const char *); const char *s40_get_root(void);
void s40_set_log(FILE *); void s40_log_close(void); void s40_pump(void);
void s40_finish(int) __attribute__((noreturn));
extern uint32_t *s40_result;
#endif
