// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1
#define SSIZE		256
#define SIZE		1024

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	/* Execute cd. */
	if (!dir)
		return false;

	return chdir(dir->string);
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	return SHELL_EXIT;
}

/**
 * Internal print-working-directory command.
 */
static bool shell_pwd(simple_command_t *s)
{
	char cwd[SIZE];
	int fd_out;
	int fd_err;
	int org_stdout = dup(STDOUT_FILENO);
	int org_stderr = dup(STDERR_FILENO);

	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return EXIT_FAILURE;

	if (s->out && !s->err) {
		if (s->io_flags & IO_OUT_APPEND)
			fd_out = open(get_word(s->out), O_WRONLY | O_CREAT | O_APPEND, 0666);
		else
			fd_out = open(get_word(s->out), O_WRONLY | O_CREAT | O_TRUNC, 0666);

		DIE(fd_out == -1, "cannot open fd");

		dup2(fd_out, STDOUT_FILENO);
		printf("%s\n", cwd);
		fflush(stdout);
		close(fd_out);
	}

	if (s->err && !s->out) {
		if (s->io_flags & IO_ERR_APPEND)
			fd_err = open(get_word(s->err), O_WRONLY | O_CREAT | O_APPEND, 0666);
		else
			fd_err = open(get_word(s->err), O_WRONLY | O_CREAT | O_TRUNC, 0666);

		DIE(fd_err == -1, "cannot open fd");

		dup2(fd_err, STDERR_FILENO);
		printf("%s\n", cwd);
		fflush(stderr);
		close(fd_err);
	}

	dup2(org_stdout, STDOUT_FILENO);
	close(org_stdout);
	dup2(org_stderr, STDERR_FILENO);
	close(org_stderr);

	return EXIT_SUCCESS;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	int size = SSIZE;

	if (s->verb->next_part) {
		if (!strcmp(s->verb->next_part->string, "=")) {
			char *wd = get_word(s->verb);
			char *tmp = malloc(strlen(wd) * sizeof(char));
			int ok = 0;
			int ind = 0;

			for (int i = 0; i < strlen(wd); i++) {
				if (ok == 1) {
					tmp[ind] = wd[i];
					ind++;
				}

				if (wd[i] == '=')
					ok = 1;
			}

			tmp[ind] = 0;
			setenv(s->verb->string, tmp, 1);
			free(wd);
			free(tmp);
			return EXIT_SUCCESS;
		}
	}

	if (!strcmp("pwd", s->verb->string))
		return shell_pwd(s);

	if (!strcmp("cd", s->verb->string)) {
		int fd_out;
		int fd_err;
		int org_stdout = dup(STDOUT_FILENO);
		int org_stderr = dup(STDERR_FILENO);

		if (s->out && !s->err) {
			if (s->io_flags & IO_OUT_APPEND)
				fd_out = open(s->out->string, O_WRONLY | O_CREAT | O_APPEND, 0666);
			else
				fd_out = open(s->out->string, O_WRONLY | O_CREAT | O_TRUNC, 0666);

			DIE(fd_out == -1, "cannot open fd");

			dup2(fd_out, STDOUT_FILENO);
			close(fd_out);
		}

		if (s->err && !s->out) {
			if (s->io_flags & IO_ERR_APPEND)
				fd_err = open(s->err->string, O_WRONLY | O_CREAT | O_APPEND, 0666);
			else
				fd_err = open(s->err->string, O_WRONLY | O_CREAT | O_TRUNC, 0666);

			DIE(fd_err == -1, "cannot open fd");

			dup2(fd_err, STDERR_FILENO);
			close(fd_err);
		}

		dup2(org_stdout, STDOUT_FILENO);
		close(org_stdout);
		dup2(org_stderr, STDERR_FILENO);
		close(org_stderr);

		return shell_cd(s->params);
	}

	pid_t child = fork();

	DIE(child < 0, "fork failed");

	if (child == 0) {
		if (s->out && s->err) {
			if (!strcmp(s->out->string, s->err->string)) {
				int fd = open(get_word(s->out), O_WRONLY | O_CREAT | O_TRUNC, 0666);

				DIE(fd == -1, "cannot open fd");

				dup2(fd, STDOUT_FILENO);
				dup2(fd, STDERR_FILENO);
				close(fd);
			} else {
				int fd_out = -1;
				int fd_err = -1;

				fd_out = open(get_word(s->out), O_WRONLY | O_CREAT | O_TRUNC, 0666);

				DIE(fd_out == -1, "cannot open fd");

				fd_err = open(get_word(s->err), O_WRONLY | O_CREAT | O_TRUNC, 0666);

				DIE(fd_err == -1, "cannot open fd");

				dup2(fd_out, STDOUT_FILENO);
				close(fd_out);

				dup2(fd_err, STDERR_FILENO);
				close(fd_err);
			}
		}

		if (s->out && !s->err) {
			int fd;

			if (s->io_flags & IO_OUT_APPEND)
				fd = open(get_word(s->out), O_WRONLY | O_CREAT | O_APPEND, 0666);
			else
				fd = open(get_word(s->out), O_WRONLY | O_CREAT | O_TRUNC, 0666);

			DIE(fd == -1, "cannot open fd");

			dup2(fd, STDOUT_FILENO);
			close(fd);
		}

		if (s->in) {
			int fd = open(s->in->string, O_RDONLY, 0666);

			DIE(fd == -1, "cannot open fd");

			dup2(fd, STDIN_FILENO);
			close(fd);
		}

		if (s->err && !s->out) {
			int fd;

			if (s->io_flags & IO_ERR_APPEND)
				fd = open(get_word(s->err), O_WRONLY | O_CREAT | O_APPEND, 0666);
			else
				fd = open(get_word(s->err), O_WRONLY | O_CREAT | O_TRUNC, 0666);

			DIE(fd == -1, "cannot open fd");

			dup2(fd, STDERR_FILENO);
			close(fd);
		}

		if (execvp(get_word(s->verb), get_argv(s, &size)) == -1) {
			printf("Execution failed for '%s'\n", get_word(s->verb));
			exit(1);
		}
	}

	int status;

	waitpid(child, &status, 0);

	if (WIFEXITED(status)) {
		int exit_status = WEXITSTATUS(status);
		return exit_status;
	}

	return shell_exit();
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
							command_t *father)
{
	int ret1 = 0, ret2 = 0;
	int status, status2;
	pid_t child1 = fork();

	DIE(child1 == -1, "fork failure for child1");

	if (child1 == 0) {
		parse_command(cmd1, level, father);
		exit(ret1);
	} else {
		pid_t child2 = fork();

		DIE(child2 == -1, "fork failure for child2");

		if (child2 == 0) {
			ret2 = parse_command(cmd2, level, father);
			exit(ret2);
		} else {
			waitpid(child1, &status, 0);
			waitpid(child2, &status2, 0);
		}
	}

	if (WIFEXITED(status2) && WIFEXITED(status))
		return WEXITSTATUS(status2);
	else
		return 0;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
						command_t *father)
{
	int ret = 0;
	int status, status2;
	int pipe_fd[2];

	DIE(pipe(pipe_fd) == -1, "pipe");

	pid_t child1 = fork();

	DIE(child1 == -1, "fork failure for child1");

	if (child1 == 0) {
		close(pipe_fd[0]);
		dup2(pipe_fd[1], STDOUT_FILENO);
		close(pipe_fd[1]);

		parse_command(cmd1, level, father);
		exit(EXIT_FAILURE);
	} else {
		pid_t child2 = fork();

		DIE(child2 == -1, "fork failure for child2");

		if (child2 == 0) {
			close(pipe_fd[1]);
			dup2(pipe_fd[0], STDIN_FILENO);
			close(pipe_fd[0]);

			ret = parse_command(cmd2, level, father);
			exit(ret);
		} else {
			close(pipe_fd[0]);
			close(pipe_fd[1]);

			waitpid(child1, &status, 0);
			waitpid(child2, &status2, 0);
		}
	}

	if (WIFEXITED(status2))
		return WEXITSTATUS(status2);
	else
		return 0;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	if (c->op == OP_NONE) {
		/* Execute a simple command. */
		if (!strcmp("exit", c->scmd->verb->string) || !strcmp("quit", c->scmd->verb->string))
			return shell_exit();
		return parse_simple(c->scmd, level, father);
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* Execute the commands one after the other. */
		parse_command(c->cmd1, level, father);
		parse_command(c->cmd2, level, father);
		break;
	case OP_PARALLEL:
		/* Execute the commands simultaneously. */
		return run_in_parallel(c->cmd1, c->cmd2, level, father);
	case OP_CONDITIONAL_NZERO:
		/* Execute the second command only if the first one
		 * returns non zero.
		 */
		if (!parse_command(c->cmd1, level, father))
			break;

		return parse_command(c->cmd2, level, father);
	case OP_CONDITIONAL_ZERO:
		/* Execute the second command only if the first one
		 * returns zero.
		 */
		if (parse_command(c->cmd1, level, father))
			break;
		return parse_command(c->cmd2, level, father);
	case OP_PIPE:
		/* Redirect the output of the first command to the
		 * input of the second.
		 */
		return run_on_pipe(c->cmd1, c->cmd2, level, father);
	default:
		return SHELL_EXIT;
	}

	return 0;
}
