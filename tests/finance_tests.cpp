#include <cassert>
#include "Payroll.h"
int main(){
    // sanity check
    Payroll p; auto pc = p.compute(45.0);
    // base=800, ot=150, gross=950, tax=171, net=779
    assert((int)pc.net == 779);
    return 0;
}
