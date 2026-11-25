/* public things from beam.c */

#ifndef _BEAM_H_
#define _BEAM_H_

int process_message(struct connsess *conn, char *buf, int len);
void cancel_edit(editsess *sp);
void abort_edit_fd(struct connsess *conn);
void mpi_identify(struct connsess *conn);
void message_edit(struct connsess *conn, char *text, int msglen, bool view,
                  bool builtin);
void sig_chld_bottomhalf(void);
/* the command of the editor or pager, and where it comes from */
const char *editor_command(bool view, bool builtin, const char **from);
/* add the output fds of background editors to 'set'; return the new
 * maximum fd */
int edit_output_fds(fd_set *set, int max_fd);
/* show the output of background editors ready in 'set'; return how many */
int edit_read_output(const fd_set *set);
/* the #editsuffix command, and the rules it defines */
void parse_edit_suffix(char *arg);
void reset_edit_suffixes(void);
void reset_user_edit_suffixes(void);
int save_edit_suffixes(FILE *f, bool mud, bool user, bool deleted);

extern char edit_start[];
extern char edit_end[];
extern editsess *edit_sess;

#endif /* _BEAM_H_ */

