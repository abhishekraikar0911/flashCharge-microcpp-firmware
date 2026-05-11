#pragma once

namespace prod {
namespace tasks {

void start_ocpp_task();
void start_network_task();
void start_hw_svc_task();
void start_can_rx_task();
void start_ui_task();

} // namespace tasks
} // namespace prod
