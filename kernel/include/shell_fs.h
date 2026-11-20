#ifndef SHELL_FS_H
#define SHELL_FS_H

/* File system commands of the kernel shell (kernel/shell_fs.c). */

void cmd_ls(int argc, char **argv);
void cmd_cat(int argc, char **argv);
void cmd_touch(int argc, char **argv);
void cmd_write(int argc, char **argv);
void cmd_append(int argc, char **argv);
void cmd_rm(int argc, char **argv);
void cmd_mkdir(int argc, char **argv);
void cmd_rmdir(int argc, char **argv);
void cmd_cd(int argc, char **argv);
void cmd_pwd(int argc, char **argv);
void cmd_mv(int argc, char **argv);
void cmd_cp(int argc, char **argv);
void cmd_stat(int argc, char **argv);
void cmd_df(int argc, char **argv);
void cmd_mkfs(int argc, char **argv);

#endif
