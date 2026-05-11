#pragma once

namespace prod {

class ChargePoint {
public:
    static ChargePoint& instance();
    
    void boot();

private:
    ChargePoint() = default;

    void initStorage();
    void cleanStaleTransactions();
    void initSecurity();
    void checkCrashLoop();
    void launchTasks();
};

} // namespace prod
