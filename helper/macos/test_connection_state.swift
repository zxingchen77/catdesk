import Foundation

@main
struct StateChecks {
    static func main() {
        let now = Date(timeIntervalSince1970: 100)
        var state = ConnectionState()
        state.ingest("CatDeskHelper: connected to /dev/cu.usbmodemTEST", at: now)
        assert(!state.synchronized(at: now))
        state.ingest("CatDeskHelper: TX \"CD1 TIME 100\"", at: now)
        state.ingest("CatDeskHelper: RX raw \"CD1 READY CATDESK 1\\r\" hex=00", at: now)
        state.ingest("CatDeskHelper: RX raw \"CD1 OK TIME extra\\r\" hex=00", at: now)
        assert(!state.synchronized(at: now))
        state.ingest("CatDeskHelper: RX raw \"CD1 OK TIME\\r\\r\" hex=00", at: now)
        assert(!state.synchronized(at: now))
        state.ingest("CatDeskHelper: RX raw \"CD1 OK MAC\\r\" hex=00", at: now)
        assert(state.synchronized(at: now.addingTimeInterval(6)))
        assert(!state.synchronized(at: now.addingTimeInterval(6.01)))
        assert(!state.synchronized(at: now.addingTimeInterval(-1)))
        state.ingest("CatDeskHelper: TX \"CD1 MAC 78 1 29 52\"", at: now)
        assert(state.statistics == "电量 78% · 充电中   CPU 29%   内存 52%")
        state.ingest("CatDeskHelper: /dev/cu.usbmodemTEST: CatDesk acknowledgment timed out; reconnecting", at: now)
        assert(!state.synchronized(at: now))
        state.ingest("CatDeskHelper: connected to /dev/cu.usbmodemTEST2", at: now)
        assert(!state.synchronized(at: now) && state.issue.isEmpty)
        state.ingest("CatDeskHelper: another CatDeskHelper is running; stop it before flashing or testing", at: now)
        assert(state.anotherHelper && !state.synchronized(at: now))
        state.reset()
        assert(state.port.isEmpty && !state.anotherHelper && state.statistics.isEmpty)
        print("PASS: both acknowledgments required, exact replies, double CR, expiration, reconnect and duplicate instance")
    }
}
