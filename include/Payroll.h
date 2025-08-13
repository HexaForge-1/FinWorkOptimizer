#pragma once
#include <algorithm>
struct PayComponents { double base{}, overtime{}, tax{}, net{}; };
class Payroll {
public:
    double hourlyRate{20.0};
    double overtimeMultiplier{1.5};
    double taxRate{0.18};
    PayComponents compute(double hours) const {
        double ot = std::max(0.0, hours - 40.0);
        double reg = hours - ot;
        double base = reg * hourlyRate;
        double overtime = ot * hourlyRate * overtimeMultiplier;
        double gross = base + overtime;
        double tax = gross * taxRate;
        double net = gross - tax;
        return {base,overtime,tax,net};
    }
};
