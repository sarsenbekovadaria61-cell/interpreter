#ifndef SIGNALS_H
#define SIGNALS_H
int signals_shell(int interactive);
int signals_child(void);
int signals_pending(void);
int signals_interrupted(void);
#endif
