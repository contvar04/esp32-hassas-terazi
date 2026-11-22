#include "../scan_core.h"
#include <map>
#include <vector>
#include <iostream>
struct Bus {
    unsigned address = 0;
    std::vector<unsigned> probes;
    std::map<unsigned, unsigned> replies;
    void beginTransmission(uint8_t a) { address = a; probes.push_back(a); }
    uint8_t endTransmission() { return replies.count(address) ? replies[address] : 2; }
};
struct Reporter {
    std::vector<unsigned> foundAddresses, errorAddresses;
    void found(uint8_t a) { foundAddresses.push_back(a); }
    void error(uint8_t a, uint8_t) { errorAddresses.push_back(a); }
};
int failures = 0;
void check(bool ok, const char* label) {
    std::cout << (ok ? "PASS: " : "FAIL: ") << label << "\n";
    if (!ok) ++failures;
}
int main() {
    Bus b; Reporter p;
    b.replies[0x27] = 0; b.replies[0x3F] = 0;
    auto r = scanI2c(b, p, true, true);
    check(r.completed && r.found == 2 && r.errors == 0 && p.foundAddresses == std::vector<unsigned>({0x27, 0x3F}), "ACK addresses are reported exactly");
    check(b.probes.size() == (0x78 - 0x08) && b.probes.front() == 0x08 && b.probes.back() == 0x77, "reserved addresses excluded");
    Bus empty; Reporter ep; auto er = scanI2c(empty, ep, true, true);
    check(er.completed && er.found == 0 && er.errors == 0, "NACK is not a found device or bus fault");
    Bus faulty; Reporter fp; faulty.replies[0x30] = 4;
    auto fr = scanI2c(faulty, fp, true, true);
    check(fr.completed && fr.errors == 1 && fr.found == 0 && fp.errorAddresses == std::vector<unsigned>({0x30}), "bus fault is not reported as device");
    Bus lowSda; Reporter sp; auto sr = scanI2c(lowSda, sp, false, true);
    check(!sr.completed && lowSda.probes.empty(), "SDA low blocks probing");
    Bus lowScl; Reporter cp; auto cr = scanI2c(lowScl, cp, true, false);
    check(!cr.completed && lowScl.probes.empty(), "SCL low blocks probing");
    return failures ? 1 : 0;
}
