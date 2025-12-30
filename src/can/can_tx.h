#ifndef CAN_TX_H
#define CAN_TX_H

void can_tx_init();
void can_tx_task(void *arg);
void chargerCommTask(void *arg);

#endif