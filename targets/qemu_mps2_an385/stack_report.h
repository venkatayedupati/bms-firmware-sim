#ifndef STACK_REPORT_H
#define STACK_REPORT_H

/* Starts a one-shot debug task that prints every task's real stack
   high-water mark a few seconds after boot, then deletes itself. See
   stack_report.c and docs/ARCHITECTURE.md "Memory footprint". */
void stack_report_start(void);

#endif /* STACK_REPORT_H */
