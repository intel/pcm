/*
BSD 3-Clause License

Copyright (c) 2016-2020, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Use port allocated for PCM in prometheus:
// https://github.com/prometheus/prometheus/wiki/Default-port-allocations
constexpr unsigned int DEFAULT_HTTP_PORT = 9738;
constexpr unsigned int DEFAULT_HTTPS_PORT = DEFAULT_HTTP_PORT;

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sched.h>

#include <cstring>
#include <fstream>
#include <ctime>
#include <vector>
#include <unordered_map>

#include "cpucounters.h"
#include "debug.h"
#include "topology.h"
#include "dashboard.h"

#define PCMWebServerVersion "0.1"
#if defined (USE_SSL)
#  include <openssl/ssl.h>
#  include <openssl/err.h>

#  define CERT_FILE_NAME "./server.pem"
#  define KEY_FILE_NAME  "./server.pem"
#endif // USE_SSL

#include <chrono>
#include <algorithm>

#include "threadpool.h"

using namespace pcm;

std::string const HTTP_EOL( "\r\n" );
std::string const PROM_EOL( "\n" );

class Indent {
    public:
        explicit Indent( std::string const & is = std::string("    ") ) : indstr_(is), indent_(""), len_(0), indstrlen_(is.length())
    {
        }
        Indent() = delete;
        Indent(Indent const &) = default;
        ~Indent() = default;

        friend std::stringstream& operator <<( std::stringstream& stream, Indent in );

        void printIndentationString(std::stringstream& s) {
            s << indent_;
        }
        // We only need post inc und pre dec
        Indent& operator--() {
            if ( len_ > 0 )
                --len_;
            else
                throw std::runtime_error("Indent: Decremented len_ too often!");
            indent_.erase( len_ * indstrlen_ );
            return *this;
        }
        Indent operator++(int) {
            Indent copy( *this );
            ++len_;
            indent_ += indstr_; // add one more indstr_
            return copy;
        }

    private:
        std::string indstr_;
        std::string indent_;
        size_t len_;
        size_t const indstrlen_;
};

std::stringstream& operator <<( std::stringstream& stream, Indent in ) {
    in.printIndentationString( stream );
    return stream;
}

class datetime {
    public:
        datetime() {
            std::time_t t = std::time( nullptr );
            now = *(std::gmtime( &t ));
        }
        datetime( std::tm t ) : now( t ) {}
        ~datetime() = default;
        datetime( datetime const& ) = default;

    public:
        void printDateTimeString( std::ostream& os ) const {
            std::stringstream str("");
            char timeBuffer[64];
            std::memset( timeBuffer, 0, 64 );
            str.imbue( std::locale::classic() );
            if ( strftime( timeBuffer, 63, "%a, %d %b %Y %T GMT", &now ) )
                str << timeBuffer;
            else
                throw std::runtime_error("Error writing to timeBuffer, too small?");
            os << str.str();
        }
        std::string toString() const {
            std::stringstream str("");
            char timeBuffer[64];
            std::memset( timeBuffer, 0, 64 );
            str.imbue( std::locale::classic() );
            if ( strftime( timeBuffer, 63, "%a, %d %b %Y %T GMT", &now ) )
                str << timeBuffer;
            else
                throw std::runtime_error("Error writing to timeBuffer, too small?");
            return str.str();
        }

    private:
        std::tm now;
};

std::ostream& operator<<( std::ostream& os, datetime const & dt ) {
    dt.printDateTimeString(os);
    return os;
}

class date {
    public:
        date() {
            now = std::time(nullptr);
        }
        ~date() = default;
        date( date const& ) = default;

    public:
        void printDate( std::ostream& os ) const {
            char buf[64];
            std::strftime( buf, 64, "%F", std::localtime(&now) );
            os << buf;
        }

    private:
        std::time_t now;
};

std::ostream& operator<<( std::ostream& os, date const & d ) {
    d.printDate(os);
    return os;
}

std::string read_ndctl_info( std::ofstream& logfile ) {
    int pipes[2];
    if ( pipe( pipes ) == -1 ) {
        logfile << date() << ": ERROR Cannot create pipe, errno = " << errno << ", strerror: " << strerror(errno) << ". Exit 50.\n";
        exit(50);
    }
    std::stringstream ndctl;
    int pid;
    if ( (pid = fork()) == 0 ) {
        // child, writes to pipe, close read-end
        close( pipes[0] );
        dup2( pipes[1], fileno(stdout) );
        execl( "/usr/bin/ndctl", "ndctl", "list", (char*)NULL );
    } else {
        // parent, reads from pipe, close write-end
        close( pipes[1] );
        char buf[2049];
        memset( buf, 0, 2049 );
        ssize_t len = 0;
        while( (len = read( pipes[0], buf, 2048 )) > 0 ) {
            buf[len] = '\0';
            ndctl << buf;
        }
        close( pipes[0] );
        if ( len < 0 ) {
            logfile << ": ERROR Read from ndctl pipe failed. errno = " << errno << ". strerror(errno) = " << strerror(errno) << ". Exit 52.\n";
            exit(52);
        }
        logfile << datetime() << ": INFO Read JSON from ndctl pipe: " << ndctl.str() << ".\n";
    }
    return ndctl.str();
}

class HTTPServer;

class SignalHandler {
public:
    static SignalHandler* getInstance() {
        static SignalHandler instance;
        return &instance;
    }

    static void handleSignal( int signum );

    void setSocket( int s ) {
        networkSocket_ = s;
    }

    void setHTTPServer( HTTPServer* hs ) {
        httpServer_ = hs;
    }

    void ignoreSignal( int signum ) {
        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sa.sa_flags = 0;
        sigaction( signum, &sa, 0 );
    }

    void installHandler( void (*handler)(int), int signum ) {
        struct sigaction sa;
        sa.sa_handler = handler;
        sa.sa_flags = 0;
        sigaction( signum, &sa, 0 );
    }

    SignalHandler( SignalHandler const & ) = delete;
    void operator=( SignalHandler const & ) = delete;
    ~SignalHandler() = default;

private:
    SignalHandler() = default;

private:
    static int networkSocket_;
    static HTTPServer* httpServer_;
};

int SignalHandler::networkSocket_ = 0;
HTTPServer* SignalHandler::httpServer_ = nullptr;

class JSONPrinter : Visitor
{
public:
    enum LineEndAction {
        NewLineOnly = 0,
        DelimiterOnly,
        DelimiterAndNewLine,
        LineEndAction_Spare = 255
    };

    JSONPrinter( std::pair<std::shared_ptr<Aggregator>,std::shared_ptr<Aggregator>> aggregatorPair ) : indentation("  "), aggPair_( aggregatorPair ) {
        if ( nullptr == aggPair_.second.get() )
            throw std::runtime_error("BUG: second Aggregator == nullptr!");
        DBG(2, "Constructor: before=", std::hex, aggPair_.first.get(), ", after=", std::hex, aggPair_.second.get() );
    }

    JSONPrinter( JSONPrinter const & ) = delete;
    JSONPrinter() = delete;

    CoreCounterState const getCoreCounter( std::shared_ptr<Aggregator> ag, uint32 tid ) const {
        CoreCounterState ccs;
        if ( nullptr == ag.get() )
            return std::move( ccs );
        return std::move( ag->coreCounterStates()[tid] );
    }

    SocketCounterState const getSocketCounter( std::shared_ptr<Aggregator> ag, uint32 sid ) const {
        SocketCounterState socs;
        if ( nullptr == ag.get() )
            return std::move( socs );
        return std::move( ag->socketCounterStates()[sid] );
    }

    SystemCounterState getSystemCounter( std::shared_ptr<Aggregator> ag ) const {
        SystemCounterState sycs;
        if ( nullptr == ag.get() )
            return std::move( sycs );
        return std::move( ag->systemCounterState() );
    }


    virtual void dispatch( HyperThread* ht )  override {
        printCounter( "Object", "HyperThread" );
        printCounter( "Thread ID", ht->threadID() );
        printCounter( "OS ID", ht->osID() );
        CoreCounterState before = getCoreCounter( aggPair_.first,  ht->osID() );
        CoreCounterState after  = getCoreCounter( aggPair_.second, ht->osID() );
        printBasicCounterState( before, after );
    }

    virtual void dispatch( ServerUncore* su ) override {
        printCounter( "Object", "ServerUncore" );
        SocketCounterState before = getSocketCounter( aggPair_.first,  su->socketID() );
        SocketCounterState after  = getSocketCounter( aggPair_.second, su->socketID() );
        printUncoreCounterState( before, after );
    }

    virtual void dispatch( ClientUncore* ) override {
        printCounter( "Object", "ClientUncore" );
    }

    virtual void dispatch( Core* c ) override {
        printCounter( "Object", "Core" );
        auto vec = c->threads();
        printCounter( "Number of threads", vec.size() );
        startObject( "Threads", BEGIN_LIST );
        iterateVectorAndCallAccept( vec );
        endObject( JSONPrinter::LineEndAction::DelimiterAndNewLine, END_LIST );

        printCounter( "Tile ID", c->tileID() );
        printCounter( "Core ID", c->coreID() );
        printCounter( "Socket ID", c->socketID() );
    }

    virtual void dispatch( SystemRoot const & s ) override {
        using namespace std::chrono;
        auto interval = duration_cast<microseconds>( aggPair_.second->dispatchedAt() - aggPair_.first->dispatchedAt() ).count();
        startObject( "", BEGIN_OBJECT );
        printCounter( "Interval us", interval );
        printCounter( "Object", "SystemRoot" );
        auto vec = s.sockets();
        printCounter( "Number of sockets", vec.size() );
        startObject( "Sockets", BEGIN_LIST );
        iterateVectorAndCallAccept( vec );
        endObject( JSONPrinter::LineEndAction::DelimiterAndNewLine, END_LIST );
        SystemCounterState before = getSystemCounter( aggPair_.first );
        SystemCounterState after  = getSystemCounter( aggPair_.second  );
        startObject( "QPI/UPI Links", BEGIN_OBJECT );
        printSystemCounterState( before, after );
        endObject( JSONPrinter::LineEndAction::DelimiterAndNewLine, END_OBJECT );
        startObject( "Core Aggregate", BEGIN_OBJECT );
        printBasicCounterState( before, after );
        endObject( JSONPrinter::LineEndAction::DelimiterAndNewLine, END_OBJECT );
        startObject( "Uncore Aggregate", BEGIN_OBJECT );
        printUncoreCounterState( before, after );
        endObject( JSONPrinter::LineEndAction::NewLineOnly, END_OBJECT );
        endObject( JSONPrinter::LineEndAction::NewLineOnly, END_OBJECT );
    }

    virtual void dispatch( Socket* s ) override {
        printCounter( "Object", "Socket" );
        printCounter( "Socket ID", s->socketID() );
        auto vec = s->cores();
        printCounter( "Number of cores", vec.size() );
        startObject( "Cores", BEGIN_LIST );
        iterateVectorAndCallAccept( vec );
        endObject( JSONPrinter::LineEndAction::DelimiterAndNewLine, END_LIST );

        startObject( "Uncore", BEGIN_OBJECT );
        s->uncore()->accept( *this );
        endObject( JSONPrinter::LineEndAction::DelimiterAndNewLine, END_OBJECT );
        startObject( "Core Aggregate", BEGIN_OBJECT );
        SocketCounterState before = getSocketCounter( aggPair_.first,  s->socketID() );
        SocketCounterState after  = getSocketCounter( aggPair_.second, s->socketID() );
        printBasicCounterState( before, after );
        endObject( JSONPrinter::LineEndAction::NewLineOnly, END_OBJECT );
    }

    std::string str( void ) {
        return ss.str();
    }

private:
    void printBasicCounterState( BasicCounterState const& before, BasicCounterState const& after ) {
        startObject( "Core Counters", BEGIN_OBJECT );
        printCounter( "Instructions Retired Any", getInstructionsRetired( before, after ) );
        printCounter( "Clock Unhalted Thread",    getCycles             ( before, after ) );
        printCounter( "Clock Unhalted Ref",       getRefCycles          ( before, after ) );
        printCounter( "L3 Cache Misses",          getL3CacheMisses      ( before, after ) );
        printCounter( "L3 Cache Hits",            getL3CacheHits        ( before, after ) );
        printCounter( "L2 Cache Misses",          getL2CacheMisses      ( before, after ) );
        printCounter( "L2 Cache Hits",            getL2CacheHits        ( before, after ) );
        printCounter( "L3 Cache Occupancy",       getL3CacheOccupancy   ( after ) );
        printCounter( "Invariant TSC",            getInvariantTSC       ( before, after ) );
        printCounter( "SMI Count",                getSMICount           ( before, after ) );
        endObject( JSONPrinter::DelimiterAndNewLine, END_OBJECT );
        //DBG( 2, "Invariant TSC before=", before.InvariantTSC, ", after=", after.InvariantTSC, ", difference=", after.InvariantTSC-before.InvariantTSC );

        startObject( "Energy Counters", BEGIN_OBJECT );
        printCounter( "Thermal Headroom", after.getThermalHeadroom() );
        uint32 i = 0;
        for ( ; i < ( PCM::MAX_C_STATE ); ++i ) {
            std::stringstream s;
            s << "CStateResidency[" << i << "]";
            printCounter( s.str(), getCoreCStateResidency( i, before, after ) );
        }
        // Here i == PCM::MAX_STATE so no need to type so many characters ;-)
        std::stringstream s;
        s << "CStateResidency[" << i << "]";
        printCounter( s.str(), getCoreCStateResidency( i, before, after ) );
        endObject( JSONPrinter::DelimiterAndNewLine, END_OBJECT );

        startObject( "Core Memory Bandwidth Counters", BEGIN_OBJECT );
        printCounter( "Local Memory Bandwidth", getLocalMemoryBW( before, after ) );
        printCounter( "Remote Memory Bandwidth", getRemoteMemoryBW( before, after ) );
        endObject( JSONPrinter::NewLineOnly, END_OBJECT );
    }

    void printUncoreCounterState( SocketCounterState const& before, SocketCounterState const& after ) {
        startObject( "Uncore Counters", BEGIN_OBJECT );
        printCounter( "DRAM Writes",                   getBytesWrittenToMC    ( before, after ) );
        printCounter( "DRAM Reads",                    getBytesReadFromMC     ( before, after ) );
        printCounter( "Persistent Memory Writes",      getBytesWrittenToPMM   ( before, after ) );
        printCounter( "Persistent Memory Reads",       getBytesReadFromPMM    ( before, after ) );
        printCounter( "Embedded DRAM Writes",          getBytesWrittenToEDC   ( before, after ) );
        printCounter( "Embedded DRAM Reads",           getBytesReadFromEDC    ( before, after ) );
        printCounter( "Memory Controller IO Requests", getIORequestBytesFromMC( before, after ) );
        printCounter( "Package Joules Consumed",       getConsumedJoules      ( before, after ) );
        printCounter( "DRAM Joules Consumed",          getDRAMConsumedJoules  ( before, after ) );
        uint32 i = 0;
        for ( ; i < ( PCM::MAX_C_STATE ); ++i ) {
            std::stringstream s;
            s << "CStateResidency[" << i << "]";
            printCounter( s.str(), getPackageCStateResidency( i, before, after ) );
        }
        // Here i == PCM::MAX_STATE so no need to type so many characters ;-)
        std::stringstream s;
        s << "CStateResidency[" << i << "]";
        printCounter( s.str(), getPackageCStateResidency( i, before, after ) );
        endObject( JSONPrinter::NewLineOnly, END_OBJECT );
    }

    void printSystemCounterState( SystemCounterState const& before, SystemCounterState const& after ) {
        PCM* pcm = PCM::getInstance();
        uint32 sockets = pcm->getNumSockets();
        uint32 links   = pcm->getQPILinksPerSocket();
        for ( uint32 i=0; i < sockets; ++i ) {
            startObject( std::string( "QPI Counters Socket " ) + std::to_string( i ), BEGIN_OBJECT );
            for ( uint32 j=0; j < links; ++j ) {
                printCounter( std::string( "Incoming Data Traffic On Link " ) + std::to_string( j ), getIncomingQPILinkBytes      ( i, j, before, after ) );
                printCounter( std::string( "Outgoing Data And Non-Data Traffic On Link " ) + std::to_string( j ), getOutgoingQPILinkBytes      ( i, j, before, after ) );
                printCounter( std::string( "Utilization Incoming Data Traffic On Link " ) + std::to_string( j ), getIncomingQPILinkUtilization( i, j, before, after ) );
                printCounter( std::string( "Utilization Outgoing Data And Non-Data Traffic On Link " ) + std::to_string( j ), getOutgoingQPILinkUtilization( i, j, before, after ) );
            }
            endObject( JSONPrinter::DelimiterAndNewLine, END_OBJECT );
        }
    }

    template <typename Counter>
    void printCounter( std::string const & name, Counter c );

    template <typename Vector>
    void iterateVectorAndCallAccept( Vector const& v );

    void startObject(std::string const& s, char const ch ) {
        std::string name;
        if ( s.size() != 0 )
            name = "\"" + s + "\" : ";
        ss << (indentation++) << name << ch << HTTP_EOL;
    }

    void endObject( enum JSONPrinter::LineEndAction lea, char const ch ) {
        // look 3 chars back, if it is a ',' then delete it.
        // make read same as write position - 3
        std::stringstream::pos_type oldReadPos = ss.tellg();
        ss.seekg( -3, std::ios_base::end );
        if ( ss.peek() == ',' ) {
            ss.seekp( ss.tellg() ); // Make write same as read position
            ss << HTTP_EOL;
        }
        ss.seekg( oldReadPos );// Just making sure the readpointer is set back to where it was

        ss << (--indentation) << ch;

        if ( lea == LineEndAction::NewLineOnly )
            ss << HTTP_EOL;
        else if ( lea == LineEndAction::DelimiterAndNewLine )
            ss << "," << HTTP_EOL;
        else if ( lea == LineEndAction::DelimiterOnly )
            ss << ",";
        else
            throw std::runtime_error( "Unknown LineEndAction enum" );
    }

    void insertListDelimiter() {
        ss << "," << HTTP_EOL;
    }

private:
    Indent            indentation;
    std::pair<std::shared_ptr<Aggregator>,std::shared_ptr<Aggregator>> aggPair_;
    std::stringstream ss;

    const char BEGIN_OBJECT = '{';
    const char END_OBJECT = '}';
    const char BEGIN_LIST = '[';
    const char END_LIST = ']';
};

template <typename Counter>
void JSONPrinter::printCounter( std::string const & name, Counter c ) {
    if ( std::is_same<Counter, std::string>::value || std::is_same<Counter, char const*>::value )
        ss << indentation << "\"" << name << "\" : \"" << c << "\"," << HTTP_EOL;
    else
        ss << indentation << "\"" << name << "\" : " << c << "," << HTTP_EOL;
}

template <typename Vector>
void JSONPrinter::iterateVectorAndCallAccept(Vector const& v) {
    for ( auto* vecElem: v ) {
        // Inside a list objects are not named
        startObject( "", BEGIN_OBJECT );
        vecElem->accept( *this );
        endObject( JSONPrinter::DelimiterAndNewLine, END_OBJECT );
    }
};

class PrometheusPrinter : Visitor
{
public:
    PrometheusPrinter( std::pair<std::shared_ptr<Aggregator>,std::shared_ptr<Aggregator>> aggregatorPair ) : aggPair_( aggregatorPair ) {
        if ( nullptr == aggPair_.second.get() )
            throw std::runtime_error("BUG: second Aggregator == nullptr!");
        DBG(2, "Constructor: before=", std::hex, aggPair_.first.get(), ", after=", std::hex, aggPair_.second.get() );
    }

    PrometheusPrinter( PrometheusPrinter const & ) = delete;
    PrometheusPrinter() = delete;

    CoreCounterState const getCoreCounter( std::shared_ptr<Aggregator> ag, uint32 tid ) const {
        CoreCounterState ccs;
        if ( nullptr == ag.get() )
            return std::move( ccs );
        return std::move( ag->coreCounterStates()[tid] );
    }

    SocketCounterState const getSocketCounter( std::shared_ptr<Aggregator> ag, uint32 sid ) const {
        SocketCounterState socs;
        if ( nullptr == ag.get() )
            return std::move( socs );
        return std::move( ag->socketCounterStates()[sid] );
    }

    SystemCounterState getSystemCounter( std::shared_ptr<Aggregator> ag ) const {
        SystemCounterState sycs;
        if ( nullptr == ag.get() )
            return std::move( sycs );
        return std::move( ag->systemCounterState() );
    }

    virtual void dispatch( HyperThread* ht ) override {
        addToHierarchy( "thread=\"" + std::to_string( ht->threadID() ) + "\"" );
        printCounter( "OS ID", ht->osID() );
        CoreCounterState before = getCoreCounter( aggPair_.first,  ht->osID() );
        CoreCounterState after  = getCoreCounter( aggPair_.second, ht->osID() );
        printBasicCounterState( before, after );
        removeFromHierarchy();
    }

    virtual void dispatch( ServerUncore* su ) override {
        printComment( std::string( "Uncore Counters Socket " ) + std::to_string( su->socketID() ) );
        SocketCounterState before = getSocketCounter( aggPair_.first,  su->socketID() );
        SocketCounterState after  = getSocketCounter( aggPair_.second, su->socketID() );
        printUncoreCounterState( before, after );
    }

    virtual void dispatch( ClientUncore* ) override {
    }

    virtual void dispatch( Core* c ) override {
        addToHierarchy( std::string( "core=\"" ) + std::to_string( c->coreID() ) + "\"" );
        auto vec = c->threads();
        iterateVectorAndCallAccept( vec );

        // Useless?
        //printCounter( "Tile ID", c->tileID() );
        //printCounter( "Core ID", c->coreID() );
        //printCounter( "Socket ID", c->socketID() );
        removeFromHierarchy();
    }

    virtual void dispatch( SystemRoot const & s ) override {
        using namespace std::chrono;
        auto interval = duration_cast<microseconds>( aggPair_.second->dispatchedAt() - aggPair_.first->dispatchedAt() ).count();
        printCounter( "Measurement Interval in us", interval );
        auto vec = s.sockets();
        printCounter( "Number of sockets", vec.size() );
        iterateVectorAndCallAccept( vec );
        SystemCounterState before = getSystemCounter( aggPair_.first );
        SystemCounterState after  = getSystemCounter( aggPair_.second );
        addToHierarchy( "aggregate=\"system\"" );
        PCM* pcm = PCM::getInstance();
        if ( pcm->isServerCPU() && pcm->getNumSockets() >= 2 ) {
            printComment( "UPI/QPI Counters" );
            printSystemCounterState( before, after );
        }
        printComment( "Core Counters Aggregate System" );
        printBasicCounterState ( before, after );
        printComment( "Uncore Counters Aggregate System" );
        printUncoreCounterState( before, after );
        removeFromHierarchy(); // aggregate=system
    }

    virtual void dispatch( Socket* s ) override {
        addToHierarchy( std::string( "socket=\"" ) + std::to_string( s->socketID() ) + "\"" );
        printComment( std::string( "Core Counters Socket " ) + std::to_string( s->socketID() ) );
        auto vec = s->cores();
        iterateVectorAndCallAccept( vec );

        // Uncore writes the comment for the socket uncore counters
        s->uncore()->accept( *this );
        addToHierarchy( "aggregate=\"socket\"" );
        printComment( std::string( "Core Counters Aggregate Socket " ) + std::to_string( s->socketID() ) );
        SocketCounterState before = getSocketCounter( aggPair_.first,  s->socketID() );
        SocketCounterState after  = getSocketCounter( aggPair_.second, s->socketID() );
        printBasicCounterState( before, after );
        removeFromHierarchy(); // aggregate=socket
        removeFromHierarchy(); // socket=x
    }

    std::string str( void ) {
        return ss.str();
    }

private:
    void printBasicCounterState( BasicCounterState const& before, BasicCounterState const& after ) {
        addToHierarchy( "source=\"core\"" );
        printCounter( "Instructions Retired Any", getInstructionsRetired( before, after ) );
        printCounter( "Clock Unhalted Thread",    getCycles             ( before, after ) );
        printCounter( "Clock Unhalted Ref",       getRefCycles          ( before, after ) );
        printCounter( "L3 Cache Misses",          getL3CacheMisses      ( before, after ) );
        printCounter( "L3 Cache Hits",            getL3CacheHits        ( before, after ) );
        printCounter( "L2 Cache Misses",          getL2CacheMisses      ( before, after ) );
        printCounter( "L2 Cache Hits",            getL2CacheHits        ( before, after ) );
        printCounter( "L3 Cache Occupancy",       getL3CacheOccupancy   ( after ) );
        printCounter( "Invariant TSC",            getInvariantTSC       ( before, after ) );
        printCounter( "SMI Count",                getSMICount           ( before, after ) );
        //DBG( 2, "Invariant TSC before=", before.InvariantTSC, ", after=", after.InvariantTSC, ", difference=", after.InvariantTSC-before.InvariantTSC );

        printCounter( "Thermal Headroom", after.getThermalHeadroom() );
        uint32 i = 0;
        for ( ; i <= ( PCM::MAX_C_STATE ); ++i ) {
            std::stringstream s;
            s << "index=\"" << i << "\"";
            addToHierarchy( s.str() );
            printCounter( "CStateResidency", getCoreCStateResidency( i, before, after ) );
            // need a raw CStateResidency metric because the precision is lost to unacceptable levels when trying
            // to compute CStateResidency for the last second using the existing CStateResidency metric
            printCounter( "RawCStateResidency", getCoreCStateResidency( i, after ) );
            removeFromHierarchy();
        }

        printCounter( "Local Memory Bandwidth", getLocalMemoryBW( before, after ) );
        printCounter( "Remote Memory Bandwidth", getRemoteMemoryBW( before, after ) );
        removeFromHierarchy();
    }

    void printUncoreCounterState( SocketCounterState const& before, SocketCounterState const& after ) {
        addToHierarchy( "source=\"uncore\"" );
        printCounter( "DRAM Writes",                   getBytesWrittenToMC    ( before, after ) );
        printCounter( "DRAM Reads",                    getBytesReadFromMC     ( before, after ) );
        printCounter( "Persistent Memory Writes",      getBytesWrittenToPMM   ( before, after ) );
        printCounter( "Persistent Memory Reads",       getBytesReadFromPMM    ( before, after ) );
        printCounter( "Embedded DRAM Writes",          getBytesWrittenToEDC   ( before, after ) );
        printCounter( "Embedded DRAM Reads",           getBytesReadFromEDC    ( before, after ) );
        printCounter( "Memory Controller IO Requests", getIORequestBytesFromMC( before, after ) );
        printCounter( "Package Joules Consumed",       getConsumedJoules      ( before, after ) );
        printCounter( "DRAM Joules Consumed",          getDRAMConsumedJoules  ( before, after ) );
        uint32 i = 0;
        for ( ; i <= ( PCM::MAX_C_STATE ); ++i ) {
            std::stringstream s;
            s << "index=\"" << i << "\"";
            addToHierarchy( s.str() );
            printCounter( "CStateResidency", getPackageCStateResidency( i, before, after ) );
            // need a CStateResidency raw metric because the precision is lost to unacceptable levels when trying
            // to compute CStateResidency for the last second using the existing CStateResidency metric
            printCounter( "RawCStateResidency", getPackageCStateResidency( i, after ) );
            removeFromHierarchy();
        }
        removeFromHierarchy();
    }

    void printSystemCounterState( SystemCounterState const& before, SystemCounterState const& after ) {
        addToHierarchy( "source=\"uncore\"" );
        PCM* pcm = PCM::getInstance();
        uint32 sockets = pcm->getNumSockets();
        uint32 links   = pcm->getQPILinksPerSocket();
        for ( uint32 i=0; i < sockets; ++i ) {
            addToHierarchy( std::string( "socket=\"" ) + std::to_string( i ) + "\"" );
            for ( uint32 j=0; j < links; ++j ) {
                printCounter( std::string( "Incoming Data Traffic On Link " ) + std::to_string( j ),                          getIncomingQPILinkBytes      ( i, j, before, after ) );
                printCounter( std::string( "Outgoing Data And Non-Data Traffic On Link " ) + std::to_string( j ),             getOutgoingQPILinkBytes      ( i, j, before, after ) );
                printCounter( std::string( "Utilization Incoming Data Traffic On Link " ) + std::to_string( j ),              getIncomingQPILinkUtilization( i, j, before, after ) );
                printCounter( std::string( "Utilization Outgoing Data And Non-Data Traffic On Link " ) + std::to_string( j ), getOutgoingQPILinkUtilization( i, j, before, after ) );
            }
            removeFromHierarchy();
        }
        removeFromHierarchy();
    }

    std::string replaceIllegalCharsWithUnderbar( std::string const& s ) {
        size_t pos = 0;
        std::string str(s);
        while ( ( pos = str.find( '-', pos ) ) != std::string::npos ) {
            str.replace( pos, 1, "_" );
        }
        pos = 0;
        while ( ( pos = str.find( ' ', pos ) ) != std::string::npos ) {
            str.replace( pos, 1, "_" );
        }
        return str;
    }

    void addToHierarchy( std::string const& s ) {
        hierarchy_.push_back( s );
    }

    void removeFromHierarchy() {
        hierarchy_.pop_back();
    }

    std::string printHierarchy() {
        std::string s(" ");
        if (hierarchy_.size() == 0 )
            return s;
        s = "{";
        for( auto level : hierarchy_ ) {
            s += level + ',';
        }
        s.pop_back();
        s += "} ";
        return s;
    }

    template <typename Counter>
    void printCounter( std::string const & name, Counter c );

    void printComment( std::string const &comment ) {
        ss << "# " << comment << PROM_EOL;
    }

    template <typename Vector>
    void iterateVectorAndCallAccept( Vector const& v );

private:
    std::pair<std::shared_ptr<Aggregator>,std::shared_ptr<Aggregator>> aggPair_;
    std::stringstream ss;
    std::vector<std::string> hierarchy_;
};

template <typename Counter>
void PrometheusPrinter::printCounter( std::string const & name, Counter c ) {
        ss << replaceIllegalCharsWithUnderbar(name) << printHierarchy() << c << PROM_EOL;
}

template <typename Vector>
void PrometheusPrinter::iterateVectorAndCallAccept(Vector const& v) {
    for ( auto* vecElem: v ) {
        vecElem->accept( *this );
    }
};

template <std::size_t SIZE = 256, class CharT = char, class Traits = std::char_traits<CharT>>
class basic_socketbuf : public std::basic_streambuf<CharT> {
public:
    using Base = std::basic_streambuf<CharT>;
    using char_type   = typename Base::char_type;
    using int_type    = typename Base::int_type;
    using traits_type = typename Base::traits_type;

    basic_socketbuf(): socketFD_(0) {
        // According to http://en.cppreference.com/w/cpp/io/basic_streambuf
        // epptr and egptr point beyond the buffer, so start + SIZE
        Base::setp( outputBuffer_, outputBuffer_ + SIZE );
        Base::setg( inputBuffer_, inputBuffer_, inputBuffer_ );
        // Default timeout of 10 seconds and 0 microseconds
        timeout_ = { 10, 0 };
#if defined (USE_SSL)
        ssl_ = nullptr;
#endif
    }

    virtual ~basic_socketbuf() {
        sync();
#if defined (USE_SSL)
        if ( nullptr != ssl_ ) {
            SSL_free( ssl_ );
        }
#endif
        if ( 0 != socketFD_ )
            ::close( socketFD_ );
    }

    int socket() {
        return socketFD_;
    }

    void setSocket( int socketFD ) {
        socketFD_ = socketFD;
        // When receiving the socket descriptor, set the timeout
        setsockopt( socketFD_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_, sizeof(struct timeval) );
    }

    void setTimeout( struct timeval t ) {
        timeout_ = t;
        setsockopt( socketFD_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_, sizeof(struct timeval) );
    }

#if defined (USE_SSL)
    SSL* ssl() {
        return ssl_;
    }

    void setSSL( SSL* ssl ) {
        if ( nullptr != ssl_ )
            throw std::runtime_error( "You can set the SSL pointer only once" );
        if ( nullptr == ssl )
            throw std::runtime_error( "Trying to set a nullptr as ssl" );
        ssl_ = ssl;
    }
#endif

protected:
    int_type writeToSocket() {
        size_t bytesToSend;
        ssize_t bytesSent;
        bytesToSend = (char*)Base::pptr() - (char*)Base::pbase();

#if defined (USE_SSL)
        if ( nullptr == ssl_ ) {
#endif
            bytesSent= ::send( socketFD_, (void*)outputBuffer_, bytesToSend, MSG_NOSIGNAL );
            if ( -1 == bytesSent ) {
                strerror( errno );
                return traits_type::eof();
            }
#if defined (USE_SSL)
        }
        else {
            while( 1 ) {
                // FIXME: openSSL has no support for setting the MSG_NOSIGNAL during send
                bytesSent = SSL_write( ssl_, (void*)outputBuffer_, bytesToSend );
                if ( 0 >= bytesSent ) {
                    int sslError = SSL_get_error( ssl_, bytesSent );
                    switch ( sslError ) {
                        case SSL_ERROR_WANT_READ:
                        case SSL_ERROR_WANT_WRITE:
                            // retry
                            continue; // Should continue in the while loop and attempt to write again
                            break;
                        case SSL_ERROR_ZERO_RETURN:
                        case SSL_ERROR_SYSCALL:
                        case SSL_ERROR_SSL:
                        default:
                            return traits_type::eof();
                    }
                } else {
                    // Valid write
                    break; // out of the while loop
                }
            }
        }
#endif
        Base::pbump( -bytesSent );
        return bytesSent;
    }

    virtual int sync() {
        int_type ret = writeToSocket();
        if ( traits_type::eof() == ret )
            return -1;
        return 0;
    }

    virtual int_type overflow( int_type ch ) {
        // send data in buffer and reset it
        if ( traits_type::eof() != ch ) {
            *Base::pptr() = ch;
            Base::pbump(1);
        }
        int_type bytesWritten = 0;
        if ( traits_type::eof() == (bytesWritten = writeToSocket()) ) {
            return traits_type::eof();
        }
        return bytesWritten; // Anything but traits_type::eof() to signal ok.
    }

    virtual int_type underflow() {
        memset( inputBuffer_, 0, SIZE * sizeof( char_type ) );
        ssize_t bytesReceived;

#if defined (USE_SSL)
        if ( nullptr == ssl_ ) {
#endif
            DBG( 3, "Socketbuf: Read from socket:" );
            bytesReceived = ::read( socketFD_, static_cast<char*>(inputBuffer_), SIZE * sizeof( char_type ) );
            if ( 0 == bytesReceived ) {
                // Client closed the socket normally, we will do the same
                ::close( socketFD_ );
                return traits_type::eof();
            }
            if ( -1 == bytesReceived ) {
                if ( errno )
                    DBG( 3, "Errno: ", errno, ", (", strerror( errno ) , ")" );
                ::close( socketFD_ );
                Base::setg( nullptr, nullptr, nullptr );
                return traits_type::eof();
            }
            DBG( 3, "Bytes received: ", bytesReceived );
            debug::dyn_hex_table_output( 3, std::cout, bytesReceived, inputBuffer_ );
            DBG( 3, "End", std::dec );
#if defined (USE_SSL)
        }
        else {
            while (1) {
                bytesReceived = SSL_read( ssl_, static_cast<void*>(inputBuffer_), SIZE * sizeof( char_type ) );
                if ( 0 >= bytesReceived ) {
                    int sslError = SSL_get_error( ssl_, bytesReceived );
                    switch ( sslError ) {
                        case SSL_ERROR_WANT_READ:
                        case SSL_ERROR_WANT_WRITE:
                            // retry
                            continue; // Should continue in the while loop and attempt to read again
                            break;
                        case SSL_ERROR_ZERO_RETURN:
                        case SSL_ERROR_SYSCALL:
                        case SSL_ERROR_SSL:
                        default:
                            Base::setg( nullptr, nullptr, nullptr );
                            return traits_type::eof();
                    }
                } else {
                    // Valid read
                    break; // out of the while loop
                }
            }
        }
#endif
        // In case the number of bytes read is not the size of the buffer, we have to set
        // egptr to start plus the number of bytes received
        Base::setg( inputBuffer_, inputBuffer_, inputBuffer_ + bytesReceived );
        return *inputBuffer_;
    }

protected:
    CharT outputBuffer_[SIZE];
    CharT inputBuffer_[SIZE];
    int   socketFD_;
    struct timeval timeout_;
#if defined (USE_SSL)
    SSL*  ssl_;
#endif
};

template <class CharT, class Traits = std::char_traits<CharT>>
class basic_socketstream : public std::basic_iostream<CharT, Traits> {
public:
    using Base = std::basic_iostream<CharT, Traits>;
    using stream_type = typename std::basic_iostream<CharT, Traits>;
    using buf_type = basic_socketbuf<16385, CharT, Traits>;
    using traits_type = typename Base::traits_type;

public:
    basic_socketstream() : stream_type( &socketBuffer_ ) {}
#if defined (USE_SSL)
    basic_socketstream( int socketFD, SSL* ssl ) : stream_type( &socketBuffer_ ) {
#else
    basic_socketstream( int socketFD ) : stream_type( &socketBuffer_ ) {
#endif
        socketBuffer_.setSocket( socketFD );
#if defined (USE_SSL)
        if ( nullptr != ssl )
            socketBuffer_.setSSL( ssl );
        else
#endif
        {
            CharT ch = Base::peek();
            // for SSLv2 bit 7 is set and for SSLv3 and up the first ClientHello Message is 0x16
            if ( ( ch & 0x80 ) || ( ch == 0x16 ) ) {
                ::close( socketFD );
                throw std::runtime_error( "Client tries to initiate https" );
            }
        }
    }
    virtual ~basic_socketstream() {}

public:
    // For clients only, servers will have to create a socketstream
    // by providing a socket descriptor in the constructor
    int open( std::string& hostname, uint16_t port ) {
        if ( hostname.empty() )
            return -1;
        if ( port == 0 )
            return -2;

        struct addrinfo* address;
        int retval = 0;

        retval = getaddrinfo( hostname.c_str(), nullptr, nullptr, &address );
        if ( 0 != retval ) {
            perror( "getaddrinfo" );
            return -3;
        }

        int sockfd = socket( address->ai_family, address->ai_socktype, address->ai_protocol );
        if ( -1 == sockfd ) {
            freeaddrinfo( address );
            return -4;
        }

        retval = connect( sockfd, address->ai_addr, address->ai_addrlen );
        if ( -1 == retval ) {
            ::close( sockfd );
            freeaddrinfo( address );
            return -5;
        }

        freeaddrinfo( address );

        socketBuffer_.setSocket( sockfd );
    }

    // might be useful in the future so leaving it in
//    std::string getLine() {
//        if ( !socketBuffer_.socket() )
//            throw std::runtime_error( "The socket is not or no longer open!" );
//        std::string result;
//        CharT chr;
//        while( '\n' != ( chr = Base::get()) ) {
//            result += chr;
//        }
//        result += chr;
//        return result;
//    }

    void putLine( std::string& line ) {
        if ( !socketBuffer_.socket() )
            throw std::runtime_error( "The socket is not or no longer open!" );
        DBG( 3, "socketstream::putLine: putting \"", line, "\" into the socket." );
        Base::write( line.c_str(), line.size() );
    }

    void close() {
        ::close( socketBuffer_.socket() );
        socketBuffer_.setSocket( 0 );
    }

protected:
    buf_type socketBuffer_;
};

typedef basic_socketstream<char> socketstream;
typedef basic_socketstream<wchar_t> wsocketstream;

class Server {
public:
    Server() = delete;
    Server( std::string listenIP, uint16_t port ) noexcept( false ) : listenIP_(listenIP), port_( port ) {
        serverSocket_ = initializeServerSocket();
        SignalHandler* shi = SignalHandler::getInstance();
        shi->setSocket( serverSocket_ );
        shi->ignoreSignal( SIGPIPE ); // Sorry Dennis Ritchie, we do not care about this, we always check return codes
        shi->installHandler( SignalHandler::handleSignal, SIGTERM );
        shi->installHandler( SignalHandler::handleSignal, SIGINT );
    }
    Server( Server const & ) = delete;
    ~Server() = default;

public:
    virtual void run() = 0;

private:
    int initializeServerSocket() {
        if ( port_ == 0 )
            throw std::runtime_error( "Server Constructor: No port specified." );

        int sockfd = ::socket( AF_INET, SOCK_STREAM, 0 );
        if ( -1 == sockfd )
            throw std::runtime_error( "Server Constructor: Can´t create socket" );

        int retval = 0;

        struct sockaddr_in serv;
        serv.sin_family = AF_INET;
        serv.sin_port = htons( port_ );
        if ( listenIP_.empty() )
            serv.sin_addr.s_addr = INADDR_ANY;
        else {
            if ( 1 != ::inet_pton( AF_INET, listenIP_.c_str(), &(serv.sin_addr) ) )
                throw std::runtime_error( "Server Constructor: Cannot convert IP string" );
        }
        socklen_t len = sizeof( struct sockaddr_in );
        retval = ::bind( sockfd, reinterpret_cast<struct sockaddr*>(&serv), len );
        if ( 0 != retval ) {
            ::close( sockfd );
            throw std::runtime_error( std::string("Server Constructor: Cannot bind to port ") + std::to_string(port_) );
        }

        retval = listen( sockfd, 64 );
        if ( 0 != retval ) {
            ::close( sockfd );
            throw std::runtime_error( "Server Constructor: Cannot listen on socket" );
        }
        // Here everything should be fine, return socket fd
        return sockfd;
    }

protected:
    std::string& listenIP_;
    WorkQueue    wq_;
    int          serverSocket_;
    uint16_t     port_;
};

enum HTTPRequestMethod {
    GET = 1,
    HEAD,
    POST,
    PUT,
    DELETE,
    CONNECT,
    OPTIONS,
    TRACE,
    PATCH,
    HTTPRequestMethod_Spare = 255 // To save some space for future methods
};

enum HTTPProtocol {
    HTTP_0_9 = 1,
    HTTP_1_0,
    HTTP_1_1,
    HTTP_2_0,
    HTTPProtocol_Spare = 255
};

enum HTTPResponseCode {
    RC_100_Continue = 100,
    RC_101_SwitchingProtocols,
    RC_102_Processing,
    RC_200_OK = 200,
    RC_201_Created,
    RC_202_Accepted,
    RC_203_NonAuthorativeInformation,
    RC_204_NoContent,
    RC_205_ResetContent,
    RC_206_PartialContent,
    RC_207_MultiStatus,
    RC_208_AlreadyReported,
    RC_226_IMUsed = 226,
    RC_300_MultipleChoices = 300,
    RC_301_MovedPermanently,
    RC_302_Found,
    RC_303_SeeOther,
    RC_304_NotModified,
    RC_305_UseProxy,
    RC_307_TemporaryRedirect = 307,
    RC_308_PermanentRedirect,
    RC_400_BadRequest = 400,
    RC_401_Unauthorized,
    RC_402_PaymentRequired,
    RC_403_Forbidden,
    RC_404_NotFound,
    RC_405_MethodNotAllowed,
    RC_406_NotAcceptable,
    RC_407_ProxyAuthenticationRequired,
    RC_408_RequestTimeout,
    RC_409_Conflict,
    RC_410_Gone,
    RC_411_LengthRequired,
    RC_412_PreconditionFailed,
    RC_413_PayloadTooLarge,
    RC_414_RequestURITooLong,
    RC_415_UnsupportedMediaType,
    RC_416_RequestRangeNotSatisfiable,
    RC_417_ExpectationFailed,
    RC_418_ImATeapot,
    RC_421_MisdirectedRequest = 421,
    RC_422_UnprocessableEntity,
    RC_423_Locked,
    RC_424_FailedDependency,
    RC_426_UpgradeRequired = 426,
    RC_428_PreconditionRequired = 428,
    RC_429_TooManyRequests,
    RC_431_RequestHeaderFieldsTooLarge = 431,
    RC_444_ConnectionClosedWithoutResponse = 444,
    RC_451_UnavailableForLegalReasons = 451,
    RC_499_ClientClosedRequest = 499,
    RC_500_InternalServerError,
    RC_501_NotImplemented,
    RC_502_BadGateway,
    RC_503_ServiceUnavailable,
    RC_504_GatewayTimeout,
    RC_505_HTTPVersionNotSupported,
    RC_506_VariantAlsoNegotiates,
    RC_507_InsufficientStorage,
    RC_508_LoopDetected,
    RC_510_NotExtended = 510,
    RC_511_NetworkAuthenticationRequired,
    RC_599_NetworkConnectTimeoutError = 599,
    HTTPReponseCode_Spare = 1000 // Filler
};

enum HTTPRequestHasBody {
    No = 0,
    Optional = 1,
    Required = 2
};

class HTTPMethodProperties {
private:
    // Embedded declaration, no need for this info outside of this container class
    struct HTTPMethodProperty {
        enum HTTPRequestMethod method_;
        std::string methodName_;
        enum HTTPRequestHasBody requestHasBody_;
        bool responseHasBody_;
    };

public:
    static enum HTTPRequestMethod getMethodAsEnum( std::string const& rms ) {
        static HTTPMethodProperties props_;
        struct HTTPMethodProperty const& prop = props_.findProperty( rms );
        return prop.method_;
    }
    static std::string const& getMethodAsString( enum HTTPRequestMethod rme ) {
        static HTTPMethodProperties props_;
        struct HTTPMethodProperty const& prop = props_.findProperty( rme );
        return prop.methodName_;
    }
    static enum HTTPRequestHasBody requestHasBody( enum HTTPRequestMethod rme ) {
        static HTTPMethodProperties props_;
        struct HTTPMethodProperty const& prop = props_.findProperty( rme );
        return prop.requestHasBody_;
    }
    static bool responseHasBody( enum HTTPRequestMethod rme ) {
        static HTTPMethodProperties props_;
        struct HTTPMethodProperty const& prop = props_.findProperty( rme );
        return prop.responseHasBody_;
    }

private:
    struct HTTPMethodProperty const& findProperty( std::string rm ) {
        for( auto& prop : httpMethodProperties )
            if ( prop.methodName_ == rm )
                return prop;
        throw std::runtime_error( "HTTPMethodProperties::findProperty: HTTPRequestMethod as string not found." );
    }
    struct HTTPMethodProperty const& findProperty( enum HTTPRequestMethod rm ) {
        for( auto& prop : httpMethodProperties )
            if ( prop.method_ == rm )
                return prop;
        throw std::runtime_error( "HTTPMethodProperties::findProperty: HTTPRequestMethod as enum not found." );
    }

    std::vector<struct HTTPMethodProperty> const httpMethodProperties = {
        { GET,     "GET",     HTTPRequestHasBody::No,       true  },
        { HEAD,    "HEAD",    HTTPRequestHasBody::No,       false },
        { POST,    "POST",    HTTPRequestHasBody::Required, true  },
        { PUT,     "PUT",     HTTPRequestHasBody::Required, true  },
        { DELETE,  "DELETE",  HTTPRequestHasBody::No,       true  },
        { CONNECT, "CONNECT", HTTPRequestHasBody::Required, true  },
        { OPTIONS, "OPTIONS", HTTPRequestHasBody::Optional, true  },
        { TRACE,   "TRACE",   HTTPRequestHasBody::No,       true  },
        { PATCH,   "PATCH",   HTTPRequestHasBody::Required, true  }
    };
};

enum HeaderType {
    ServerSet = -2,
    Invalid = -1,
    Unspecified = 0,
    String = 1,
    Integer = 2,
    Float = 3,
    Date = 4,
    Range = 5,
    True = 7, // Only allowed value is "true", all lowercase
    Email = 8,
    ETag = 9,
    DateOrETag = 10,
    Parameters = 11,
    Url = 12,
    HostPort = 13,
    ProtoHostPort = 14,
    DateOrSeconds = 15,
    NoCache = 16,
    IP = 17,
    Character = 18,
    OnOff = 19,
    ContainsOtherHeaders = 20,
    StarOrFQURL = 21,
    CustomHeader = 22,
    HeaderType_Spare = 127 // Reserving some values
};

class HTTPHeaderProperties {
private:
    struct HTTPHeaderProperty {
        HTTPHeaderProperty( std::string name, enum HeaderType ht, bool w = false, bool l = false, char lsc = ',' ) :
            name_( name ), type_( ht ), canBeWeighted_( w ), canBeAList_( l ), listSeparatorChar_( lsc ) {}

        std::string     name_;
        enum HeaderType type_;
        bool canBeWeighted_;
        bool canBeAList_;
        char listSeparatorChar_;
    };

    std::unordered_map<enum HeaderType, std::string, std::hash<int>> const headerTypeToString_ = {
        { ServerSet, "ServerSet" },
        { Invalid, "Invalid" },
        { Unspecified, "Unspecified" },
        { String, "String" },
        { Integer, "Integer" },
        { Float, "Float" },
        { Date, "Date" },
        { Range, "Range" },
        { True, "True" },
        { Email, "Email" },
        { ETag, "ETag" },
        { DateOrETag, "DateOrETag" },
        { Parameters, "Parameters" },
        { Url, "Url" },
        { HostPort, "HostPort" },
        { ProtoHostPort, "ProtoHostPort" },
        { DateOrSeconds, "DateOrSeconds" },
        { NoCache, "NoCache" },
        { IP, "IP" },
        { Character, "Character" },
        { OnOff, "OnOff" },
        { ContainsOtherHeaders, "ContainsOtherHeaders" },
        { StarOrFQURL, "StarOrFQURL" },
        { CustomHeader, "CustomHeader" }
    };

public:
    static enum HeaderType headerType( std::string const & str ) {
        static HTTPHeaderProperties props;
        for ( auto& prop : props.httpHeaderProperties ) {
            if ( prop.name_ == str )
                return prop.type_;
        }
        return CustomHeader;
    }
    static char listSeparatorChar( std::string const & headerName ) {
        static HTTPHeaderProperties props;
        for ( auto& prop : props.httpHeaderProperties ) {
            if ( prop.name_ == headerName )
                return prop.listSeparatorChar_;
        }
        return ',';
    }
    static std::string const& headerTypeAsString( enum HeaderType ht ) {
        static HTTPHeaderProperties props;
        return props.headerTypeToString_.at( ht );
    }

private:
    // FIXME: Incomplete, needs adding to
    std::vector<HTTPHeaderProperty> const httpHeaderProperties = {
        { "Accept", HeaderType::String, true, true, ',' },
        { "Accept-Charset", HeaderType::String, true, true, ',' },
        { "Accept-Encoding", HeaderType::String, true, true, ',' },
        { "Accept-Language", HeaderType::String, true, true, ',' },
        { "Accept-Ranges", HeaderType::String, false, false },
        { "Access-Control-Allow-Credentials", HeaderType::True, false, false },
        { "Access-Control-Allow-Headers", HeaderType::String, false, true, ',' },
        { "Access-Control-Allow-Methods", HeaderType::String, false, true, ',' },
        { "Access-Control-Allow-Origin", HeaderType::StarOrFQURL, false, false },
        { "Access-Control-Expose-Headers", HeaderType::String, false, true },
        { "Access-Control-Max-Age", HeaderType::Integer, false, false },
        { "Access-Control-Request-Headers", HeaderType::String, false, true },
        { "Access-Control-Request-Method", HeaderType::String, false, false },
        { "Age", HeaderType::Integer, false ,false },
        { "Allow", HeaderType::String, false, true },
        { "Authorization", HeaderType::String, false, false },
        { "Cache-Control", HeaderType::String, false, true },
        { "Connection", HeaderType::String, false, false },
        { "Content-Disposition", HeaderType::String, false, false },
        { "Content-Encoding", HeaderType::String, false, true },
        { "Content-Language", HeaderType::String, false, true },
        { "Content-Length", HeaderType::Integer, false, false },
        { "Content-Location", HeaderType::Url, false, false },
        { "Content-Range", HeaderType::Range, false, true },
        { "Content-Security-Policy", HeaderType::String, false, false },
        { "Content-Security-Policy-Report-Only", HeaderType::String, false, false },
        { "Content-Type", HeaderType::String, false, false },
        { "Cookie", HeaderType::Parameters, false, false },
        { "Cookie2", HeaderType::String, false, false }, // Obsolete by RFC 6265
        { "DNT", HeaderType::Integer, false, false },
        { "Date", HeaderType::Date, false, false },
        { "ETag", HeaderType::ETag, false, false },
        { "Expect", HeaderType::String, false, false },
        { "Expires", HeaderType::Date, false, false },
        { "Forwarded", HeaderType::String, false, false },
        { "From", HeaderType::Email, false, false },
        { "Host", HeaderType::HostPort, false, false },
        { "If-Match", HeaderType::ETag, false, true },
        { "If-Modified-Since", HeaderType::Date, false, false },
        { "If-None-Match", HeaderType::ETag, false, true },
        { "If-Range", HeaderType::DateOrETag, false ,false },
        { "If-Unmodified-Since", HeaderType::Date, false, false },
        { "Keep-Alive", HeaderType::Parameters, false, true },
        { "Large-Allocation", HeaderType::Integer, false, false }, // Not Standard yet
        { "Last-Modified", HeaderType::Date,false ,false },
        { "Location", HeaderType::Url, false ,false },
        { "Origin", HeaderType::ProtoHostPort, false, false },
        { "Pragma", HeaderType::NoCache, false, false },
        { "Proxy-Authenticate", HeaderType::String, false ,false },
        { "Proxy-Authorization", HeaderType::String, false, false },
        { "Public-Key-Pins", HeaderType::Parameters, false, false },
        { "Public-Key-Pins-Report-Only", HeaderType::Parameters, false, false },
        { "Range",  HeaderType::Range, false, true  },
        { "Referer", HeaderType::Url, false, false },
        { "Referrer-Policy", HeaderType::String, false, false },
        { "Retry-After", HeaderType::DateOrSeconds, false, false },
        { "Server", HeaderType::String, false, false },
        { "Set-Cookie", HeaderType::Parameters, false, false },
        { "Set-Cookie2",  HeaderType::Parameters, false, false  }, // Obsolete
        { "SourceMap",  HeaderType::Url, false, false  },
        { "Strict-Transport-Security",  HeaderType::Parameters, false, false },
        { "TE", HeaderType::String, true, true },
        { "Tk", HeaderType::Character, false, false },
        { "Trailer", HeaderType::ContainsOtherHeaders, false, false, ',' },
        { "Transfer-Encoding", HeaderType::String, false, true },
        { "Upgrade-Insecure-Requests", HeaderType::Integer },
        { "User-Agent", HeaderType::String, false, false },
        { "Vary", HeaderType::String, false, true },
        { "Via", HeaderType::String, false, true },
        { "WWW-Authenticate", HeaderType::String, false, false },
        { "Warning", HeaderType::String, false, false },
        { "X-Content-Type-Options", HeaderType::String, false, false },
        { "X-DNS-Prefetch-Control", HeaderType::OnOff, false, false },
        { "X-Forwarded-For", HeaderType::IP, false, true },
        { "X-Forwarded-Host", HeaderType::String, false, false },
        { "X-Forwarded-Proto", HeaderType::String, false, false },
        { "X-Frame-Options", HeaderType::String, false, false },
        { "X-XSS-Protection", HeaderType::String, false, false }
//        { "",  HeaderType:: , , },
    };
};

struct URL {
public:
    URL() : scheme_( "" ), user_( "" ), passwd_( "" ), host_( "" ), path_( "" ), fragment_( "" ), port_( 0 ),
            hasScheme_( false ), hasUser_ ( false ), hasPasswd_( false ), hasHost_( false ), hasPort_( false ),
            hasQuery_( false ), hasFragment_( false ), pathIsStar_( false ) {}
    URL( URL const & ) = default;
    ~URL() = default;
    URL& operator=( URL const & ) = default;

public:
    static URL parse( std::string fullURL ) {
        DBG( 3, "fullURL: '", fullURL, "'" );
        URL url;
        size_t pathBeginPos = 0;
        size_t pathEndPos = std::string::npos;
        size_t questionMarkPos = 0;
        size_t numberPos = 0;
        if ( fullURL.empty() ) {
            url.path_ = '/';
            return url;
        }
        if ( fullURL.size() == 1 && fullURL[0] == '*' ) {
            url.path_ = fullURL;
            url.pathIsStar_ = true;
            return url;
        }

        questionMarkPos = fullURL.find( '?' );
        numberPos = fullURL.find( '#' );

        if ( fullURL[0] == '/' ) {
            pathBeginPos = 0;
        } else {
            // If first character is not a / then the first colon is end of scheme
            size_t schemeColonPos = fullURL.find( ':' );
            if ( std::string::npos != schemeColonPos ) {
                std::string scheme;
                scheme = fullURL.substr( 0, schemeColonPos );
                std::string validSchemeChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-.";
                DBG( 3, "scheme: '", scheme, "'" );
                if ( scheme.find_first_not_of( validSchemeChars ) != std::string::npos )
                    throw std::runtime_error( "Scheme contains invalid characters" );
                url.scheme_ = scheme;
                url.hasScheme_ = true;
            } else
                throw std::runtime_error( "Does not start with / and no scheme" );

            size_t authorityPos = fullURL.find( "//", schemeColonPos+1 );
            size_t authorityEndPos;
            std::string authority;

            if ( std::string::npos != authorityPos ) {
                if ( (schemeColonPos+1) != authorityPos )
                    throw std::runtime_error( "Something between : and //" );

                pathBeginPos = fullURL.find( '/', authorityPos+2 );
                authorityEndPos = std::min( { pathBeginPos, questionMarkPos, numberPos } );
                authority = fullURL.substr( authorityPos+2, authorityEndPos - (authorityPos + 2) );
                DBG( 3, "authority: '", authority, "'" );

                size_t atPos = authority.find( '@' );
                bool atFound = (atPos != std::string::npos);
                if ( atFound ) {
                    if ( atPos == 0 )
                        throw std::runtime_error( "'@' found in the first column, username would be empty" );
                    // User (+passwd) found user : passwd @ host
                    size_t passwdColonPos = authority.rfind( ':', atPos );
                    size_t userEndPos = std::string::npos;
                    DBG( 3, "1 userEndPos '", userEndPos, "'" );
                    if ( passwdColonPos != std::string::npos ) {
                        std::string passwd = authority.substr( passwdColonPos+1, atPos-(passwdColonPos+1) );
                        DBG( 3, "passwd: '", passwd, "', passwdColonPos: ", passwdColonPos );
                        userEndPos = passwdColonPos;
                        DBG( 3, "2a userEndPos '", userEndPos, "'" );
                        // passwd is possibly percent encoded FIXME
                        url.passwd_ = passwd;
                        url.hasPasswd_ = true;
                    } else {
                        userEndPos = atPos;
                        DBG( 3, "2b userEndPos '", userEndPos, "'" );
                    }
                    DBG( 3, "3 userEndPos '", userEndPos, "'" );
                    std::string user = authority.substr( 0, userEndPos );
                    DBG( 3, "user: '", user, "'" );
                    // user is possibly percent encoded FIXME
                    url.user_ = user;
                    url.hasUser_ = true;
                    // delete user/pass including the at
                    authority.erase( 0, atPos+1 );
                } else
                    atPos = 0;

                // Instead of all the logic it is easier to work on substrings

                // authority now at most contains hostname possibly in ipv6 notation plus port
                bool angleBracketOpenFound = (authority[0] == '[');
                size_t angleBracketClosePos;
                bool angleBracketCloseFound = false;
                if ( angleBracketOpenFound ) {
                    angleBracketClosePos = authority.find( ']', 0 );
                    angleBracketCloseFound = (angleBracketClosePos != std::string::npos);
                    if ( !angleBracketCloseFound )
                        throw std::runtime_error( "No matching  ']' found." );
                    url.host_ = authority.substr( 0, angleBracketClosePos );
                    url.hasHost_ = true;
                    DBG( 3, "angleBracketCloseFound: host: '", url.host_, "'" );
                    authority.erase( 0, angleBracketClosePos+1 );
                }

                if ( !authority.empty() ) {
                    // authority now at most has host and port, port is now the definitive separator
                    // (cant be part of the ipv6 address anymore)
                    size_t portColonPos = authority.rfind( ':' );
                    bool portColonFound = (portColonPos != std::string::npos);

                    if ( portColonFound ) {
                        if ( portColonPos == 0 && !url.hasHost_ )
                            throw std::runtime_error( "No hostname found" );
                        if ( portColonPos != 0 ) {
                            url.host_ = authority.substr( 0, portColonPos );
                            DBG( 3, "portColonFound: host: '", url.host_, "'" );
                            url.hasHost_ = true;
                        }
                        size_t port = 0;
                        std::string portString = authority.substr( portColonPos+1 );
                        DBG( 3, "portString: '", portString, "'" );
                        if ( portString.empty() )
                            // Use the default port number, use scheme and the /etc/services file
                            port = 0; // FIXME
                        else {
                            size_t pos = 0;
                            try {
                                port = std::stoull( portString, &pos );
                            } catch ( std::invalid_argument& e ) {
                                DBG( 3, "invalid_argument exception caught in stoull: ", e.what() );
                                DBG( 3, "number of characters procesed: ", pos );
                            } catch ( std::out_of_range& e ) {
                                DBG( 3, "out_of_range exception caught in stoull: ", e.what() );
                                DBG( 3, "errno: ", errno, strerror(errno) );
                            }
                        }
                        if ( port >= 65536 )
                            throw std::runtime_error( "URL::parse: port too large" );
                        url.port_ = (unsigned short)port;
                        url.hasPort_ = true;
                        DBG( 3, "port: ", port );
                    } else {
                        url.host_ = authority;
                        url.hasHost_ = true;
                        DBG( 3, "portColonNotFound: host: '", url.host_, "'" );
                    }
                } else if ( !url.hasHost_ )
                    throw std::runtime_error( "No hostname found" );
            }
        }

        pathEndPos = std::min( {questionMarkPos, numberPos} );
        url.path_ = fullURL.substr( pathBeginPos, pathEndPos - pathBeginPos );
        DBG( 3, "path: '", url.path_, "'" );

        if ( std::string::npos != questionMarkPos ) {
            url.hasQuery_ = true;
            std::string queryString = fullURL.substr( questionMarkPos+1, numberPos-(questionMarkPos+1) );
            DBG( 3, "queryString: '", queryString, "'" );
            size_t ampPos = 0;
            while ( !queryString.empty() ) {
                ampPos = queryString.find( '&' );
                std::string query = queryString.substr( 0, ampPos );
                DBG( 3, "query: '", query, "'" );
                size_t equalsPos = query.find( '=' );
                if ( std::string::npos == equalsPos )
                    throw std::runtime_error( "Did not find a '=' in the query" );
                std::string one, two;
                one = query.substr( 0, equalsPos );
                DBG( 3, "one: '", one, "'" );
                two = query.substr( equalsPos+1 );
                DBG( 3, "two: '", two, "'" );
                url.arguments_.push_back( std::make_pair( one ,two ) );
                // npos + 1 == 0... ouch
                if ( std::string::npos == ampPos )
                    queryString.erase( 0, ampPos );
                else
                    queryString.erase( 0, ampPos+1 );
            }
        }

        if ( std::string::npos != numberPos ) {
            url.hasFragment_ = true;
            url.fragment_ = fullURL.substr( numberPos+1 );
            DBG( 3, "path: '", url.path_, "'" );
        }

        // Now make sure the URL does not contain %xx values
        size_t percentPos = url.path_.find( '%' );
        if ( std::string::npos != percentPos ) {
            // FIXME: implement conversion from percent encoded to ascii
        }

        // Done!
        return url;
    }

    void printURL( std::ostream& os ) const {
        DBG( 3, "URL::printURL: debug level 3 to see more" );
        std::stringstream ss;
        DBG( 3, " hasScheme_: ", hasScheme_, ", scheme_: ", scheme_ );
        if ( hasScheme_ ) {
            ss << scheme_ << ':';
        }
        DBG( 3, " hasHost_: ", hasHost_, ", host_: ", host_ );
        if ( hasHost_ ) {
            ss << "//";
            DBG( 3, " hasUser_: ", hasUser_, ", user_: ", user_ );
            if ( hasUser_ ) {
                ss << user_;
            }
            DBG( 3, " hasPasswd_: ", hasPasswd_, ", passwd_: ", passwd_ );
            if ( hasPasswd_ ) {
                ss << ':' << passwd_;
            }
            DBG( 3, " hasUser_: ", hasUser_, ", user_: ", user_ );
            if ( hasUser_ ) {
                ss << '@';
            }
            DBG( 3, " hasHost_: ", hasHost_, ", host_: ", host_ );
            if ( hasHost_ ) {
                ss << host_;
            }
            DBG( 3, " hasPort_: ", hasPort_, ", port_: ", port_ );
            if ( hasPort_ ) {
                ss << ':' << port_;
            }
        }
        DBG( 3, " path_: '", path_, "'" );
        ss << (path_.empty() ? "/" : path_);
        if ( hasQuery_ ) {
            DBG( 3, " hasQuery_: ", hasQuery_ );
            ss << '?';
            size_t i;
            for ( i = 0; i < (arguments_.size()-1); ++i ) {
                DBG( 3, " query[", i, "]: ", arguments_[i].first, " ==> ", arguments_[i].second );
                ss << arguments_[i].first << '=' << arguments_[i].second << "&";
            }
            DBG( 3, " query[", i, "]: ", arguments_[i].first, " ==> ", arguments_[i].second );
            ss << arguments_[i].first << '=' << arguments_[i].second;
        }
        if ( hasFragment_ ) {
            DBG( 3, " hasFragment_: ", hasFragment_, ", fragment_: ", fragment_ );
            ss << '#' << fragment_;
        }
        os << ss.str() << "\n";
        DBG( 3, "URL::printURL: done" );
    }

public:
    std::string scheme_;
    std::string user_;
    std::string passwd_;
    std::string host_;
    std::string path_;
    std::string fragment_;
    std::vector<std::pair<std::string,std::string>> arguments_;
    unsigned short port_;
    bool hasScheme_;
    bool hasUser_;
    bool hasPasswd_;
    bool hasHost_;
    bool hasPort_;
    bool hasQuery_;
    bool hasFragment_;
    bool pathIsStar_;
};

std::ostream& operator<<(  std::ostream& os, URL const & url ) {
    url.printURL( os );
    return os;
}

enum MimeType {
    CatchAll = 0,
    TextHTML,
    TextXML,
    TextPlain,
    TextPlainProm_0_0_4,
    ApplicationJSON,
    ImageXIcon,
    MimeType_spare = 255
};

std::unordered_map<enum MimeType, std::string, std::hash<int>> mimeTypeMap = {
    { CatchAll,            "*/*" },
    { TextHTML,            "text/html" },
    { TextPlain,           "text/plain" },
    { TextPlainProm_0_0_4, "text/plain; version=0.0.4" },
    { ImageXIcon,          "image/x-icon" },
    { ApplicationJSON,     "application/json" }
};

class HTTPHeader {
public:
    HTTPHeader() = default;
    HTTPHeader( std::string n, std::string v ) : name_( n ), value_( v ) {
        type_ = HeaderType::ServerSet;
    }
    HTTPHeader( char const * n, char const * v ) : name_( n ), value_( v ) {
        type_ = HeaderType::ServerSet;
    }
    HTTPHeader( HTTPHeader const & ) = default;
    HTTPHeader( HTTPHeader&& ) = default;
    HTTPHeader& operator=( HTTPHeader const& ) = default;
    ~HTTPHeader() = default;

public:
    static HTTPHeader parse( std::string& header ) {
        HTTPHeader hh;

        DBG( 3, "Raw Header : '", header, "'" );

        std::string::size_type colonPos = header.find( ':' );
        if ( std::string::npos == colonPos )
            throw std::runtime_error( "Not a valid header, no : found" );

        std::string headerName  = header.substr( 0, colonPos );
        std::string headerValue = header.substr( colonPos+1 ); // FIXME: possible whitespace before, between and after

        // Spaces in header names are illegal but be lenient und just remove them
        headerName.erase( std::remove ( headerName.begin(), headerName.begin() + colonPos, ' ' ), headerName.end() );

        hh.name_  = headerName;
        hh.value_ = headerValue;
        hh.type_  = HTTPHeaderProperties::headerType( hh.name_ );

        DBG( 3, "Headername : '", headerName, "'" );
        DBG( 3, "Headervalue: '", headerValue, "'" );
        DBG( 3, "HeaderType : '", HTTPHeaderProperties::headerTypeAsString(hh.type_), "'" );

        if ( hh.type_ == HeaderType::Invalid )
            throw std::runtime_error( "Parsing with Invalid HeaderType" );

        std::string::size_type quotes = std::count( headerValue.begin(), headerValue.end(), '"' );
        bool properlyQuoted = (quotes % 2 == 0);
        if ( !properlyQuoted ) {
            DBG( 3, "Parse: header not properly quoted: uneven number of  quotes (", quotes, ") found" );
            throw std::runtime_error( "parse header: header improperly quoted" );
        }

        return hh;
    }

    std::string headerName() { return name_; }
    // Not sure what I needed it for but leaving it for now
//     std::string headerValue() const {
//         std::cout << "Calling headerValue for HeaderName: " << name_ << "\n";
//         std::cout.flush();
//         std::string value;
//         switch ( type_ ) {
//         case ServerSet:
//             return value_;
//         case String:
//             if ( valueList_.size() > 0 )
//                 return valueList_[0];
//             throw std::runtime_error( "headerValue(): Empty valuelist" );
//             break;
//         case Integer:
//             if ( integers_.size() > 0 )
//                 return std::to_string( integers_[0] );
//             throw std::runtime_error( "headerValue(): Empty valuelist" );
//             break;
//         case Float:
//             if ( floats_.size() > 0 )
//                 return std::to_string( floats_[0] );
//             throw std::runtime_error( "headerValue(): Empty valuelist" );
//             break;
//         case Date:
//             return date_.toString();
//             break;
//         case Range:
//             return "";
//             break;
//         default:
//             return std::string("Not implemented yet for '") + name_ + "', type is '" + std::to_string((int)type_) + "'";
//         }
//     }

    std::vector<std::string> const headerValueAsList() const {
        return splitHeaderValue();
    }

    void debugPrint() const {
        DBG( 3, "Headername: '", name_, "', Headervalue: '", value_, "'" );
    }

    size_t headerValueAsNumber() const {
        size_t number = std::stoll( value_ );
        return number;
    }

    double headerValueAsDouble() const {
        double number = std::stod( value_ );
        return number;
    }

    std::string const & headerValueAsString() const {
        return value_;
    }

    enum MimeType headerValueAsMimeType() const {
        auto list = headerValueAsList();
        for ( auto& item : list ) {
            DBG( 3, "item: '", item, "'" );
            for( auto& mt : mimeTypeMap ) {
                DBG( 3, "comparing item: '", item, "' to '", mt.second, "'" );
                if ( mt.second.compare( item ) == 0 ) {
                    DBG( 3, "MimeType ", mt.second, " found." );
                    return mt.first;
                }
            }
        }
        // If we did not recognize the mimetype we will return TextHTML so the client can see the HTML page
        return TextHTML;
    }

private:
    std::vector<std::string> splitHeaderValue() const {
        std::vector<std::string> elementList;
        std::stringstream ss( value_ );
        std::string s;
        char listSeparatorChar = HTTPHeaderProperties::listSeparatorChar( name_ );
        while ( ss.good() ) {
            std::getline( ss, s, listSeparatorChar );
            // Remove leading whitespace
            s.erase( s.begin(), std::find_if( s.begin(), s.end(), std::bind1st( std::not_equal_to<char>(), ' ' ) ) );
            // Remove trailing whitespace
            s.erase( std::find_if( s.rbegin(), s.rend(), std::bind1st( std::not_equal_to<char>(), ' ') ).base(), s.end() );
            elementList.push_back( s );
        }
        return elementList;
    }

private:
    std::string name_;
    std::string value_;
    enum HeaderType type_;
    std::vector<std::string> valueList_;
    std::vector<double> floats_;
    std::vector<long long> integers_;
    std::vector<std::pair<size_t,size_t>> ranges_;
    std::vector<std::pair<std::string,std::string>> parameters_;
    datetime date_;
};

class HTTPMessage {
protected:
    HTTPMessage() = default;
    HTTPMessage( HTTPMessage const & ) = default;
    ~HTTPMessage() = default;

public:
    // Data manipulators/extractors
    std::string const & body() const {
        return body_;
    }

    void addBody( std::string const& body ) {
        body_ = body;
    }

    void addHeader( std::string const & name, std::string const & value ) {
        if ( headers_.insert( std::make_pair( name, HTTPHeader( name, value ) ) ).second == false ) {
            throw std::runtime_error( "Header already exists in the headerlist" );
        }
    }

    void addHeader( HTTPHeader hh ) {
        if ( headers_.insert( std::make_pair( hh.headerName(), hh ) ).second == false ) {
            throw std::runtime_error( "Header already exists in the headerlist" );
        }
    }

    bool hasHeader( std::string const & header ) const {
        auto pos = headers_.find( header );
        if ( pos == headers_.end() )
            return false;
        return true;
    }

    HTTPHeader const & getHeader( std::string const & header ) const {
        auto pos = headers_.find( header );
        if ( pos == headers_.end() ) {
            std::stringstream ss;
            ss << "HTTPMessage::getHeader: Header '" << header << "' not found.";
            throw std::runtime_error( ss.str() );
        }
        return (*pos).second;
    }

    std::string const & protocolAsString() const {
        // will throw if key not found, it is a bug anyway
        return protocol_map_.at(protocol_);
    }

    enum HTTPProtocol protocol() const {
        return protocol_;
    }

    void setProtocol( enum HTTPProtocol protocol ) {
        protocol_ = protocol;
    }

    void setProtocol( std::string const & protocolString ) {
        auto it = protocol_map_.begin();
        while( it != protocol_map_.end() ) {
            if ( (*it).second == protocolString ) {
                protocol_ = (*it).first;
                break;
            }
            ++it;
        }
        if ( it == protocol_map_.end() ) {
            DBG( 3, "Protocol string '", protocolString, "' not found in map, protocol unsupported!" );
            throw std::runtime_error( "Protocol not found in the map" );
        }
    }

    std::string const host() const {
        std::string host;
        if ( hasHeader( "Host" ) ) {
            HTTPHeader host = getHeader( "Host" );
        } else {
            DBG(3, "HTTPMessage::host: header Host not found." );
            host = "";
        }
        return host;
    }

protected:
    std::string readData( socketstream& in, size_t length ) {
        std::string data( length, '\0' );
        in.read( &data[0], length );
        return data;
    }

    std::string readChunkedData( socketstream& in ) {
        std::string chunkHeader;
        std::string data;
        std::getline( in, chunkHeader, '\n' );
        // Final header starts with 0, rest of the line is not important
        while ( '0' != chunkHeader[0] ) {
            // chunkheader: hexadecimal numbers followed by an optional semi-colon with a comment and a \r
            // stoll should filter all that crap out for us and return just the hexadecimal digits
            DBG( 3, "chunkHeader (ater check for 0): '", chunkHeader, "'" );
            size_t length = std::stoll( chunkHeader, nullptr, 16 );
            DBG( 3, "length: '", length, "'" );
            // Initialize chunk to all zeros
            std::string chunk( length, '\0' );
            in.read( &chunk[0], length );
            DBG( 3, "chunk: '", chunk, "'" );
            data += chunk;
            // Reads trailing \r\n from the chunk
            std::getline( in, chunkHeader, '\n' );
            // Reads the empty line following the chunk
            std::getline( in, chunkHeader, '\n' );
            DBG( 3, "chunkHeader (should be empty line): '", chunkHeader, "'" );
            // Read a new line to check for 0\r header
            std::getline( in, chunkHeader, '\n' );
            DBG( 3, "chunkHeader (should be next chunk header): '", chunkHeader, "'" );
        }
        return data;
    }

protected:
    enum HTTPProtocol protocol_;
    std::unordered_map<std::string, HTTPHeader> headers_;
    std::string body_;
    std::unordered_map<enum HTTPProtocol, std::string, std::hash<int>> protocol_map_ = {
        { HTTPProtocol::HTTP_0_9, "HTTP/0.9" },
        { HTTPProtocol::HTTP_1_0, "HTTP/1.0" },
        { HTTPProtocol::HTTP_1_1, "HTTP/1.1" },
        { HTTPProtocol::HTTP_2_0, "HTTP/2.0" }
    };
};

class HTTPRequest : public HTTPMessage {
public:
    HTTPRequest() : method_( HTTPRequestMethod::GET ) {}
    HTTPRequest( HTTPRequest const & ) = default;
    ~HTTPRequest() = default;

    template <typename CharT, typename Traits>
    friend basic_socketstream<CharT,Traits>& operator>>(basic_socketstream<CharT,Traits>&, HTTPRequest& );

public:
    enum HTTPRequestMethod method() const {
        return method_;
    }

    URL const & url() const {
        return url_;
    }

    void debugPrint() {
        DBG( 3, "HTTPRequest::debugPrint:" );
        DBG( 3, "Method  : \"", method_, "\"" );
        DBG( 3, "URL     : \"", url_, "\"" );
        DBG( 3, "Protocol: \"", protocol_, "\"" );
        for ( auto& header: headers_ )
            DBG( 3, "Header : \"", header.first, "\" ==> \"", header.second.headerValueAsString(), "\"" );
        DBG( 3, "Body    : \"", body_, "\"" );
    }

private:
    enum HTTPRequestMethod method_;
    URL url_;
};

class HTTPResponse : public HTTPMessage {
public:
    HTTPResponse() : responseCode_( HTTPResponseCode::RC_200_OK ) {}
    HTTPResponse( HTTPResponse const & ) = default;
    virtual ~HTTPResponse() = default;

    template <typename CharT, typename Traits>
    friend basic_socketstream<CharT,Traits>& operator<<(basic_socketstream<CharT,Traits>&, HTTPResponse& );

public:
    enum HTTPResponseCode responseCode() const {
        return responseCode_;
    }

    std::string responseCodeAsString() const {
        return response_map_.at( responseCode_ );
    }

    void setResponseCode( enum HTTPResponseCode rc ) {
        DBG( 3, "Setting response code to: '", std::dec, (int)rc, "'" );
        responseCode_ = rc;
    }

    void debugPrint() {
        DBG( 3, "HTTPReponse::debugPrint:" );
        DBG( 3, "Response Code: \"", (int)responseCode_, "\"" );
        for ( auto& header: headers_ )
            DBG( 3, "Header: \"", header.first, "\" ==> \"", header.second.headerValueAsString(), "\"" );
        DBG( 3, "Body: \"", body_, "\"" );
    }

    void createResponse( enum MimeType mimeType, std::string body, enum HTTPResponseCode rc ) {
        // mimetype validity checking?
        addHeader( HTTPHeader( "Content-Type", mimeTypeMap[mimeType] ) );
        addHeader( HTTPHeader( "Content-Length", std::to_string( body.size() ) ) );
        addBody( body );
        setResponseCode( rc );
    }

private:
    enum HTTPResponseCode responseCode_;
    std::unordered_map<enum HTTPResponseCode, std::string, std::hash<int>> response_map_ = {
        { RC_100_Continue, "Continue" },
        { RC_101_SwitchingProtocols, "Switching Protocols" },
        { RC_102_Processing, "Processing" },
        { RC_200_OK, "OK" },
        { RC_201_Created, "Created" },
        { RC_202_Accepted, "Accepted" },
        { RC_203_NonAuthorativeInformation, "Non-authorative Information" },
        { RC_204_NoContent, "No Content" },
        { RC_205_ResetContent, "Reset Content" },
        { RC_206_PartialContent, "Partial Content" },
        { RC_207_MultiStatus, "Multi-Status" },
        { RC_208_AlreadyReported, "Already Reported" },
        { RC_226_IMUsed, "IM Used" },
        { RC_300_MultipleChoices, "Multiple Choices" },
        { RC_301_MovedPermanently, "Moved Permanently" },
        { RC_302_Found, "Found" },
        { RC_303_SeeOther, "See Other" },
        { RC_304_NotModified, "Not Modified" },
        { RC_305_UseProxy, "Use Proxy" },
        { RC_307_TemporaryRedirect, "Temporary Redirect" },
        { RC_308_PermanentRedirect, "Permanent Redirect" },
        { RC_400_BadRequest, "Bad Request" },
        { RC_401_Unauthorized, "Unauthorized" },
        { RC_402_PaymentRequired, "Payment Required" },
        { RC_403_Forbidden, "Forbidden" },
        { RC_404_NotFound, "Not Found" },
        { RC_405_MethodNotAllowed, "Method Not Allowed" },
        { RC_406_NotAcceptable, "Not Acceptable" },
        { RC_407_ProxyAuthenticationRequired, "Proxy Authentication Required" },
        { RC_408_RequestTimeout, "Request Timeout" },
        { RC_409_Conflict, "Conflict" },
        { RC_410_Gone, "Gone" },
        { RC_411_LengthRequired, "Length Required" },
        { RC_412_PreconditionFailed, "Precondition Failed" },
        { RC_413_PayloadTooLarge, "Payload Too Large" },
        { RC_414_RequestURITooLong, "Request-URI Too Long" },
        { RC_415_UnsupportedMediaType, "Unsupported Media Type" },
        { RC_416_RequestRangeNotSatisfiable, "Request Range Not Satisfiable" },
        { RC_417_ExpectationFailed, "Expectation Failed" },
        { RC_418_ImATeapot, "I'm a teapot" },
        { RC_421_MisdirectedRequest, "Misdirected Request" },
        { RC_422_UnprocessableEntity, "Unprocessable Entity" },
        { RC_423_Locked, "Locked" },
        { RC_424_FailedDependency, "Failed Dependency" },
        { RC_426_UpgradeRequired, "Upgrade Required" },
        { RC_428_PreconditionRequired, "Precondition Required" },
        { RC_429_TooManyRequests, "Too Many Requests" },
        { RC_431_RequestHeaderFieldsTooLarge, "Request Header Fields Too Large" },
        { RC_444_ConnectionClosedWithoutResponse, "Connection Closed Without Response" },
        { RC_451_UnavailableForLegalReasons, "Unavailable For Legal Reasons" },
        { RC_499_ClientClosedRequest, "Client Closed Request" },
        { RC_500_InternalServerError, "Internal Server Error" },
        { RC_501_NotImplemented, "Not Implemented" },
        { RC_502_BadGateway, "Bad Gateway" },
        { RC_503_ServiceUnavailable, "Service Unavailable" },
        { RC_504_GatewayTimeout, "Gateway Timeout" },
        { RC_505_HTTPVersionNotSupported, "HTTP Version Not Supported" },
        { RC_506_VariantAlsoNegotiates, "Variant Also Negotiates" },
        { RC_507_InsufficientStorage, "Insufficient Storage" },
        { RC_508_LoopDetected, "Loop Detected" },
        { RC_510_NotExtended, "Not Extended" },
        { RC_511_NetworkAuthenticationRequired, "Network Authentication Required" },
        { RC_599_NetworkConnectTimeoutError, "Network Connect Timeout Error" }
    };
};

std::string& compressLWSAndRemoveCR( std::string& line ) {
    std::string::size_type pos = 0, end = line.size(), start = 0;

    for ( pos = 0; pos < end; ++pos ) {
        start = pos;
        if ( ::isspace( line[pos] ) ) {
            while ( pos < line.size() && ::isspace( line[++pos] ) ) {
            }
            if ( (pos - start) > 1 ) {
                line.erase( start+1,  pos-start-1 );
                end -= pos-start-1;
                pos = start+1;
            }
        }
    }

    // Remove trailing '\r'
    if ( line[line.size()-1] == '\r' )
        line.pop_back();

    return line;
}

template <class CharT, class Traits>
basic_socketstream<CharT, Traits>& operator>>( basic_socketstream<CharT, Traits>& rs, HTTPRequest& m ) {
    DBG( 3, "Reading from the socket" );

    std::string method, url, protocol;
    rs >> method >> url >> protocol;
    if ( rs.fail() ) {
        DBG( 5, "Could not read from socket, might have been closed due to e.g. timeout" );
        throw std::runtime_error( "Could not read from socket, might have been closed due to e.g. timeout" );
    }

    m.method_   = HTTPMethodProperties::getMethodAsEnum( method );
    m.url_      = URL::parse( url );
    m.setProtocol( protocol );

    //m.debugPrint();
    // ignore the '\n' after the protocol
    rs.ignore( std::numeric_limits<std::streamsize>::max(), '\n' );
    std::string line;
    std::string concatLine;
    while ( true ) {
        std::getline( rs, line );
        DBG( 3, "Line with whitespace: '", line, "'" );
        concatLine += compressLWSAndRemoveCR( line );

        DBG( 3, "Line without whitepsace: '", line, "'" );
        DBG( 3, "ConcatLine: '", concatLine, "'" );
        // empty line is separator between headers and body
        if ( concatLine.empty() ) {
            break;
        }

        // Header spans multiple lines if a line starts with SP or HTAB, fetch another line and append to concatLine
        if ( rs.peek() == ' ' || rs.peek() == '\t' )
            continue;

        HTTPHeader hh;
        hh = HTTPHeader::parse( concatLine );
        hh.debugPrint();
        m.addHeader( hh );
        // Parsing of header done, clear concatLine to start fresh
        concatLine.clear();
    }
    DBG( 3, "Done parsing headers" );

    enum HTTPRequestHasBody hasBody = HTTPMethodProperties::requestHasBody( m.method_ );
    DBG( 3, "Request has Body (0 No, 1 Optional, 2 Yes): ", (int)hasBody );
    if ( hasBody != HTTPRequestHasBody::No ) {
        // this mess of code checks if the body is chunked or regular and tests the pre conditions
        // that belong with them either content-length header or transfer-encoding header, both
        // means bad request, in case neither is there we need to check if body is optional
        bool validCL = false;
        size_t contentLength = 0;
        bool chunkedTE = false;
        std::string body( "" );
        // cl = Content Length
        if ( m.hasHeader( "Content-Length" ) ) {
            HTTPHeader const h = m.getHeader( "Content-Length" );
            contentLength = h.headerValueAsNumber();
            validCL = true;
            DBG( 3, "Content-Length: clValue: ", contentLength, ", validCL: ", validCL );
        } else {
            validCL = false;
            DBG( 3, "Content-Length: header not found." );
        }
        // te = Transfer Encoding
        if ( m.hasHeader( "Transfer-Encoding" ) ) {
            HTTPHeader const h = m.getHeader( "Transfer-Encoding" );
            std::string teString = h.headerValueAsString();
            // Validate header
            if ( teString.find( "chunked" ) != std::string::npos ) {
                chunkedTE = true;
            } else {
                chunkedTE = false;
            }
            DBG( 3, "Transfer-Encoding: teString: ", teString, ", chunkedTE: ", chunkedTE );
        } else {
            DBG( 3, "Transfer-Encoding: header not found " );
            chunkedTE = false;
        }
        size_t trailerLength = 0;
        if ( m.hasHeader( "Trailer" ) ) {
            HTTPHeader const trailer = m.getHeader( "Trailer" );
            trailerLength = trailer.headerValueAsList().size();

        } else {
            DBG( 3, "Trailer: header not found " );
        }

        if ( ( chunkedTE && !validCL ) || ( !chunkedTE && validCL ) ) {
            DBG( 3, "Good request" );
            // Good request, get body
            // but first check if the client sent the Expect header, if so we
            // need to respond with 100 Continue so it starts transmitting the body
            std::string expect( "" );
            if ( m.hasHeader( "Expect" ) ) {
                HTTPHeader const h = m.getHeader( "Expect" );
                expect = h.headerValueAsString();
            } else {
                expect = "";
            }
            if ( expect == "100-continue" ) {
                // We have to send a HTTP/1.1 100 Continue response followed by an empty line
                HTTPResponse resp;
                resp.setProtocol( HTTPProtocol::HTTP_1_1 );
                resp.setResponseCode( HTTPResponseCode::RC_100_Continue );
                rs << resp;
            }
            else if ( expect != "" )
                throw std::runtime_error( "Not a valid Expect header" );

            // now load the body
            if ( chunkedTE ) {
                m.body_ = m.readChunkedData( rs );
                // There is now either a \r\n pair in the stream, or footers/trailers, lets see:
                std::string remainder;
                size_t numHeadersAdded = 0;
                std::getline( rs, remainder, '\n' );
                DBG( 3, "Parsing remainder '", remainder, "'" );
                while ( remainder[0] != '\r' ) {
                    HTTPHeader hh = HTTPHeader::parse( remainder );
                    m.addHeader( hh );
                    ++numHeadersAdded;
                }
                // If trailer contains 3 headers then 3 headers should be added
                if ( numHeadersAdded != trailerLength )
                    throw std::runtime_error( "Trailing headers does not match Trailer header content" );
            } else {
                body = m.readData( rs, contentLength );
            }
        } else if ( hasBody == HTTPRequestHasBody::Optional && ! validCL && !chunkedTE ){
            // Good request, no body, done
            return rs;
        } else {
            // Bad request, respond, throw exception
            HTTPResponse resp;
            resp.setProtocol( HTTPProtocol::HTTP_1_1 );
            resp.setResponseCode( HTTPResponseCode::RC_400_BadRequest );
            rs << resp;
            throw std::runtime_error( "Bad Request received" );
        }
    }
    return rs;
}

template <class CharT, class Traits>
basic_socketstream<CharT, Traits>& operator<<( basic_socketstream<CharT, Traits>& ws, HTTPResponse& m ) {
    DBG( 3, "Writing the HTTPResponse to the socket" );
    m.debugPrint();

    DBG( 3, m.protocolAsString(), " ", (int)m.responseCode(), " ", m.responseCodeAsString() );
    ws << m.protocolAsString() << " " << (int)m.responseCode() << " " << m.responseCodeAsString() << HTTP_EOL;

    DBG( 3, "Headers:" );
    // write headers
    for( auto& header : m.headers_ ) {
        DBG( 3, header.first, ": ", header.second.headerValueAsString() );
        if ( header.first == "Content-Type" )
            ws << header.first << ": " << header.second.headerValueAsString() << "; charset=UTF-8" << HTTP_EOL;
        else
            ws << header.first << ": " << header.second.headerValueAsString() << HTTP_EOL;
    }

    ws << HTTP_EOL;

    DBG( 3, "Body:", m.body() );
    ws << m.body();

    ws.flush();
    return ws;
}

typedef void (*http_callback)( HTTPServer *, HTTPRequest const &, HTTPResponse & );

class HTTPConnection : public Work {
public:
    HTTPConnection() = delete;
#if defined (USE_SSL)
    HTTPConnection( HTTPServer* hs, int socketFD, struct sockaddr_in clientAddr, std::vector<http_callback> const & cl, SSL* ssl = nullptr ) : hs_( hs ), socketStream_( socketFD, ssl ), clientAddress_( clientAddr ), callbackList_( cl ) {}
#else
    HTTPConnection( HTTPServer* hs, int socketFD, struct sockaddr_in clientAddr, std::vector<http_callback> const & cl ) : hs_( hs ), socketStream_( socketFD ), clientAddress_( clientAddr ), callbackList_( cl ) {}
#endif
    HTTPConnection( HTTPConnection const & ) = delete;
    void operator=( HTTPConnection const & ) = delete;
    ~HTTPConnection() = default;

public:
    void close() {
        socketStream_.close();
    }

    virtual void execute() override {
        bool keepListening = false;
        int numRequests = 0;
        do {
            HTTPRequest  request;
            HTTPResponse response;

            try {
                socketStream_ >> request;
            } catch( std::exception& e ) {
                DBG( 3, "Reading request from socket: Exception caught: ", e.what(), "\n" );
                break;
            }
            ++numRequests;
            // Debug:
            // request.debugPrint();

            response.setProtocol( request.protocol() );

            // Check for protocol conformity
            if ( request.protocol() == HTTPProtocol::HTTP_1_1 ) {
                if ( ! request.hasHeader( "Host" ) ) {
                    DBG( 3, "Mandatory Host header not found." );
                    std::string body( "400 Bad Request. HTTP 1.1: Mandatory Host header is missing." );
                    response.createResponse( TextPlain, body, RC_400_BadRequest );
                    return;
                }
            }

            // Do processing of the request here
            if (*callbackList_[request.method()])
                (*callbackList_[request.method()])( hs_, request, response );
            else {
                std::string body( "501 Not Implemented." );
                body += " Method \"" + HTTPMethodProperties::getMethodAsString(request.method()) + "\" is not implemented (yet).";
                response.createResponse( TextPlain, body, RC_501_NotImplemented );
            }

            // Post-processing, adding some server specific reponse headers
            int const requestLimit = 100;
            int const connectionTimeout = 10;
            response.addHeader( HTTPHeader( "Server", std::string( "PCMWebServer " ) + PCMWebServerVersion ) );
            response.addHeader( HTTPHeader( "Date", datetime().toString() ) );
            if ( numRequests < requestLimit ) {
                std::string connection;
                if ( request.hasHeader( "Connection" ) ) {
                    HTTPHeader const h = request.getHeader( "Connection" );
                    connection = h.headerValueAsString();
                } else {
                    DBG( 3, "Connection: header not found" );
                    connection = "";
                }
                // FIXME: case insenstive compare
                if ( connection == "keep-alive" ) {
                    DBG( 3, "HTTPConnection::execute: keep-alive header found" );
                    response.addHeader( HTTPHeader( "Connection", "keep-alive" ) );
                    std::string tmp = "timeout=" + std::to_string(connectionTimeout) + ", max=" + std::to_string( requestLimit );
                    HTTPHeader header2( "Keep-Alive", tmp );
                    response.addHeader( header2 );
                    keepListening = true;
                }
            } else {
                DBG( 3, "Keep-Alive connection request limit (", requestLimit, ") reached" );
                // Now respond with the answer
                response.addHeader( HTTPHeader( "Connection", "close" ) );
                keepListening = false;
            }
            // Remove body if method is HEAD, it is using the same callback as GET but does not need the body
            if ( request.method() == HEAD ) {
                DBG( 1, "Method HEAD, removing body" );
                response.addBody( "" );
            }
            response.debugPrint();
            socketStream_ << response;
            socketStream_.flush();

        } while ( !keepListening );

        close();
    }

private:
    HTTPServer*  hs_;
    socketstream socketStream_;
    struct sockaddr_in clientAddress_;
    std::vector<http_callback> const & callbackList_;
    std::vector<std::string> responseHeader_;
    std::string responseBody_;
    std::string protocol_;
};

class PeriodicCounterFetcher : public Work
{
public:
    PeriodicCounterFetcher( HTTPServer* hs ) : hs_(hs), run_(false), exit_(false) {}
    virtual ~PeriodicCounterFetcher() override {}

    void start( void ) {
        DBG( 3, "PeriodicCounterFetcher::start() called" );
        run_ = true;
    }

    void pause( void ) {
        DBG( 3, "PeriodicCounterFetcher::pause() called" );
        run_ = false;
    }

    void stop( void ) {
        DBG( 3, "PeriodicCounterFetcher::stop() called" );
        exit_ = true;
    }

    virtual void execute() override;

private:
    HTTPServer*       hs_;
    std::atomic<bool> run_;
    std::atomic<bool> exit_;
};

class HTTPServer : public Server {
public:
    HTTPServer() : Server( "", 80 ) {
        DBG( 3, "HTTPServer::HTTPServer()" );
        callbackList_.resize( 256 );
        createPeriodicCounterFetcher();
        pcf_->start();
        SignalHandler::getInstance()->setHTTPServer( this );
    }

    HTTPServer( std::string const & ip, uint16_t port ) : Server( ip, port ) {
        DBG( 3, "HTTPServer::HTTPServer( ip=", ip, ", port=", port, " )" );
        callbackList_.resize( 256 );
        createPeriodicCounterFetcher();
        pcf_->start();
        SignalHandler::getInstance()->setHTTPServer( this );
    }

    HTTPServer( HTTPServer const & ) = delete;

    virtual ~HTTPServer() {
        pcf_->stop();
        std::this_thread::sleep_for( std::chrono::seconds(1) );
        delete pcf_;
    }

public:
    virtual void run() override;

    virtual void stop() {
        pcf_->stop();
    }

    // Register Callbacks
    void registerCallback( HTTPRequestMethod rm, http_callback hc )
    {
        callbackList_[rm] = hc;
    }

    void unregisterCallback( HTTPRequestMethod rm )
    {
        callbackList_[rm] = nullptr;
    }

    void addAggregator( std::shared_ptr<Aggregator> agp ) {
        DBG( 3, "HTTPServer::addAggregator( agp=", std::hex, agp.get(), " ) called" );

        agVectorMutex_.lock();
        agVector_.insert( agVector_.begin(), agp );
        if ( agVector_.size() > 30 ) {
            DBG( 3, "HTTPServer::addAggregator(): Removing last Aggegator" );
            agVector_.pop_back();
        }
        agVectorMutex_.unlock();
    }

    std::pair<std::shared_ptr<Aggregator>,std::shared_ptr<Aggregator>> getAggregators( size_t index, size_t index2 ) {
        if ( index == index2 )
            throw std::runtime_error("BUG: getAggregator: both indices are equal. Fix the code!" );

        // simply wait until we have enough samples to return
        while( agVector_.size() < ( std::max( index, index2 ) + 1 ) )
            std::this_thread::sleep_for(std::chrono::seconds(1));

        agVectorMutex_.lock();
        auto ret = std::make_pair( agVector_[ index ], agVector_[ index2 ] );
        agVectorMutex_.unlock();
        return ret;
    }

private:
    void createPeriodicCounterFetcher() {
        pcf_ = new PeriodicCounterFetcher( this );
        wq_.addWork( pcf_ );
        pcf_->start();
    }

protected:
    std::vector<http_callback>               callbackList_;
    std::vector<std::shared_ptr<Aggregator>> agVector_;
    std::mutex agVectorMutex_;
    PeriodicCounterFetcher* pcf_;
};

// Here to break dependency on HTTPServer
void SignalHandler::handleSignal( int signum )
{
    // Clean up, close socket and such
    std::cerr << "handleSignal: signal " << signum << " caught.\n";
    std::cerr << "handleSignal: closing socket " << networkSocket_ << "\n";
    ::close( networkSocket_ );
    std::cerr << "Stopping HTTPServer\n";
    httpServer_->stop();
    std::cerr << "Cleaning up PMU:\n";
    PCM::getInstance()->cleanup();
    std::cerr << "handleSignal: exiting with exit code 1...\n";
    exit(1);
}

void PeriodicCounterFetcher::execute() {
    using namespace std::chrono;
    system_clock::time_point now = system_clock::now();
    now = now + std::chrono::seconds(1);
    std::this_thread::sleep_until( now );
    while( 1 ) {
        if ( exit_ )
            break;
        if ( run_ ) {
            auto before = steady_clock::now();
            // create an aggregator
            std::shared_ptr<Aggregator> sagp = std::make_shared<Aggregator>();
            DBG( 2, "PCF::execute(): AGP=", sagp.get(), " )" );
            // dispatch it
            sagp->dispatch( PCM::getInstance()->getSystemTopology() );
            // add it to the vector
            hs_->addAggregator( sagp );
            auto after = steady_clock::now();
            auto elapsed = duration_cast<std::chrono::milliseconds>(after - before);
            DBG( 2, "Aggregation Duration: ", elapsed.count(), "ms." );
        }
        now = now + std::chrono::seconds(1);
        std::this_thread::sleep_until( now );
    }
}

void HTTPServer::run() {
    struct sockaddr_in clientAddress;
    clientAddress.sin_family = AF_INET;
    int clientSocketFD = 0;
    while (1) {
        // Listen on socket for incoming requests
        socklen_t sa_len = sizeof( struct sockaddr_in );
        int retval = ::accept( serverSocket_, (struct sockaddr*)&clientAddress, &sa_len );
        if ( -1 == retval ) {
            ::strerror( errno );
            continue;
        }
        clientSocketFD = retval;

        // Client connected, let's determine the client ip as string.
        char ipbuf[INET_ADDRSTRLEN];
        ::memset( ipbuf, 0, 16 );
        char const * resbuf = ::inet_ntop( AF_INET, &(clientAddress.sin_addr), ipbuf, INET_ADDRSTRLEN );
        if ( nullptr == resbuf ) {
            ::strerror( errno );
            ::close( clientSocketFD );
            continue;
        }

        int port = ntohs( clientAddress.sin_port );
        DBG( 3, "Client IP is: ", ipbuf, ", and the port it uses is : ", port );

        HTTPConnection* connection;
        try {
            connection = new HTTPConnection( this, clientSocketFD, clientAddress, callbackList_ );
        } catch ( std::exception& e ) {
            DBG( 3, "Exception caught while creating a HTTPConnection: " );
            ::close( clientSocketFD );
            continue;
        }

        wq_.addWork( connection );
    }
}

#if defined (USE_SSL)
class HTTPSServer : public HTTPServer {
public:
    HTTPSServer() : HTTPServer( "", 443 ) {}
    HTTPSServer( std::string const & ip, uint16_t port ) : HTTPServer( ip, port ), sslCTX_( nullptr ) {}
    HTTPSServer( HTTPSServer const & ) = delete;
    virtual ~HTTPSServer() = default;

public:
    virtual void run() final;

public:
    void setPrivateKeyFile ( std::string const & privateKeyFile )  { privateKeyFile_  = privateKeyFile;  }
    void setCertificateFile( std::string const & certificateFile ) { certificateFile_ = certificateFile; }

    void initialiseSSL() {
        if ( nullptr != sslCTX_ )
            throw std::runtime_error( "HTTPSServer SSL already initialised" );
        if ( privateKeyFile_.empty() )
            throw std::runtime_error( "No private key file given" );
        if ( certificateFile_.empty() )
            throw std::runtime_error( "No certificate file given" );

        SSL_library_init();
        SSL_load_error_strings();
        // SSL too old on development machine, not available yet FIXME
        //OPENSSL_config(nullptr);

        sslCTX_ = SSL_CTX_new( SSLv23_method() );
        if ( nullptr == sslCTX_ )
            throw std::runtime_error( "Cannot create an SSL context" );
        if ( SSL_CTX_use_certificate_file( sslCTX_, certificateFile_.c_str(), SSL_FILETYPE_PEM ) <= 0 )
            throw std::runtime_error( "Cannot use certificate file" );
        if ( SSL_CTX_use_PrivateKey_file( sslCTX_, privateKeyFile_.c_str(), SSL_FILETYPE_PEM ) <= 0 )
            throw std::runtime_error( "Cannot use private key file" );
    }

private:
    SSL_CTX* sslCTX_;
    std::string certificateFile_;
    std::string privateKeyFile_;
};

void HTTPSServer::run() {
    struct sockaddr_in clientAddress;
    clientAddress.sin_family = AF_INET;
    int clientSocketFD = 0;
    // Check SSL CTX for validity
    if ( nullptr == sslCTX_ )
        throw std::runtime_error( "No SSL_CTX created" );

    while (1) {
        // Listen on socket for incoming requests, same as for regular connection
        socklen_t sa_len = sizeof( struct sockaddr_in );
        int retval = ::accept( serverSocket_, (struct sockaddr*)&clientAddress, &sa_len );
        if ( -1 == retval ) {
            ::strerror( errno );
            continue;
        }
        clientSocketFD = retval;

        // Create and setup SSL on the socket
        SSL* ssl = SSL_new( sslCTX_ );
        SSL_set_fd( ssl, clientSocketFD );

        // Check if the SSL handshake worked
        if ( SSL_accept( ssl ) <= 0 )
            throw std::runtime_error( "SSL handshake failure" );

        // Client connected, let's determine the client ip as string.
        char ipbuf[INET_ADDRSTRLEN];
        memset( ipbuf, 0, 16 );
        char const * resbuf = ::inet_ntop( AF_INET, &(clientAddress.sin_addr), ipbuf, INET_ADDRSTRLEN );
        if ( nullptr == resbuf ) {
            ::strerror( errno );
            ::close( clientSocketFD );
            continue;
        }

        int port = ntohs( clientAddress.sin_port );
        DBG( 3, "Client IP is: ", ipbuf, ", and the port it uses is : ", port );
        DBG( 3, "SSL info: version: ", SSL_get_version( ssl ), ", stuff" );

        HTTPConnection* connection = new HTTPConnection( this, clientSocketFD, clientAddress, callbackList_, ssl );

        wq_.addWork( connection );
    }
}
#endif // USE_SSL

// Hack needed to convert from unsigned char to signed char, favicon.h is changed to have _uc for each char
inline constexpr signed char operator "" _uc( unsigned long long arg ) noexcept {
    return static_cast<signed char>(arg);
}

#include "favicon.ico.h"

std::pair<std::shared_ptr<Aggregator>,std::shared_ptr<Aggregator>> getNullAndCurrentAggregator() {
    std::shared_ptr<Aggregator> current = std::make_shared<Aggregator>();
    std::shared_ptr<Aggregator> null    = std::make_shared<Aggregator>();
    current->dispatch( PCM::getInstance()->getSystemTopology() );
    return std::make_pair( null, current );
}

enum OutputFormat {
    Prometheus_0_0_4 = 1,
    JSON,
    HTML,
    XML,
    PlainText,
    OutputFormat_Spare = 255
};

std::unordered_map<enum MimeType, enum OutputFormat, std::hash<int>> mimeTypeToOutputFormat = {
    { TextHTML,            HTML },
    { TextXML,             XML  },
    { ApplicationJSON,     JSON },
    { TextPlainProm_0_0_4, Prometheus_0_0_4 },
    { CatchAll,            HTML }
};

std::unordered_map<enum MimeType, std::string, std::hash<int>> supportedOutputMimeTypes = {
    { TextPlainProm_0_0_4, "text/plain;version=0.0.4" },
    { ApplicationJSON,     "application/json" }
};

enum MimeType matchSupportedWithAcceptedMimeTypes( HTTPHeader const& h ) {
    auto list = h.headerValueAsList();
    // TODO: We should actually build up a list of accepted mimetypes and their preference, sort
    // the list and then compare against it. We now use the inherent order as preference which
    // is not entirely accurate but is good enough.
    for ( auto& item : list ) {
        DBG( 2, "Item: \"", item, "\"" );
        // Search for preference and remove it
        auto copy = item;
        size_t pos;
        // Using erase with npos as second parameter to be explicit about the intent: delete until end
        if ( std::string::npos != ( pos = item.find( "q=", 0 ) ) ){
            // found it, remove q=...
            copy.erase( pos, std::string::npos );
            DBG( 2, "q= found and erased: \"", copy, "\"" );
            if ( std::string::npos != ( pos = item.rfind( ";", pos ) ) ) {
                // remove trailing ;
                copy.erase( pos, std::string::npos );
                DBG( 2, "trailing ';' found and erased: \"", copy, "\"" );
            }
        }
        // remove all whitespace from the item
        copy.erase( std::remove_if( copy.begin(), copy.end(), isspace ), copy.end() );
        // compare mimetype with supported ones
        for ( auto mimetype : supportedOutputMimeTypes ) {
            auto str = mimetype.second;
            str.erase( std::remove_if( str.begin(), str.end(), isspace ), str.end() );
            DBG( 2, "Comparing mimetype '", copy, "' with known Mimetype '", str, "'" );
            if ( str == copy ) {
                DBG( 2, "Found a match!" );
                return mimetype.first;
            }
        }
    }
    return CatchAll;
}

/* Normally the Accept Header decides what format is returned but certain endpoints can override this,
 * therefore we have a seperate enum for output format */
void my_get_callback( HTTPServer* hs, HTTPRequest const & req, HTTPResponse & resp )
{
    enum MimeType mt;
    enum OutputFormat format;

    HTTPHeader accept;
    if ( req.hasHeader( "Accept" ) ) {
        accept = req.getHeader( "Accept" );
        mt = matchSupportedWithAcceptedMimeTypes( accept );
    } else {
        // If there is no accept header then the assumption is that the client can handle anything
        mt = CatchAll;
    }
    format = mimeTypeToOutputFormat[ mt ];

    URL url;
    url = req.url();

    DBG( 3, "PATH=\"", url.path_, "\", size=", url.path_.size() );

    if ( url.path_ == "/favicon.ico" ) {
        DBG( 3, "my_get_callback: client requesting '/favicon.ico'" );
        std::string favicon( favicon_ico, favicon_ico + favicon_ico_len );
        resp.createResponse( ImageXIcon, favicon, RC_200_OK );
        return;
    }

    std::pair<std::shared_ptr<Aggregator>,std::shared_ptr<Aggregator>> aggregatorPair;

    if ( (1 == url.path_.size()) && (url.path_ == "/") ) {
        DBG( 3, "my_get_callback: client requesting '/'" );
        // If it is not Prometheus and not JSON just return this html code
        // It might violate the protocol but it makes coding this easier
        if ( ApplicationJSON != mt && TextPlainProm_0_0_4 != mt ) {
            // If you make changes to the HTML, please validate it
            // Probably best to put this in static files and serve this
            std::string body = "\
<!DOCTYPE html>\n\
<html lang=\"en\">\n\
  <head>\n\
    <title>PCM Sensor Server</title>\n\
  </head>\n\
  <body>\n\
    <h1>PCM Sensor Server</h1>\n\
    <p>PCM Sensor Server provides performance counter data through an HTTP interface. By default this text is served when requesting the endpoint \"/\".</p>\n\
    <p>The endpoints for retrieving counter data, /, /persecond and /persecond/X, support returning data in JSON or prometheus format. For JSON have your client send the HTTP header \"Accept: application/json\" and for prometheus \"Accept: text/plain; version=0.0.4\" along with the request, PCM Sensor Server will then return the counter data in the requested format.</p>\n\
    <p>Endpoints you can call are:</p>\n\
    <ul>\n\
      <li>/ : This will fetch the counter values since start of the daemon, minus overflow so should be considered absolute numbers and should be used for further processing by yourself.</li>\n\
      <li>/persecond : This will fetch data from the internal sample thread which samples every second and returns the difference between the last 2 samples.</li>\n\
      <li>/persecond/X : This will fetch data from the internal sample thread which samples every second and returns the difference between the last 2 samples which are X seconds apart. X can be at most 30 seconds without changing the source code.</li>\n\
      <li>/metrics : The Prometheus server does not send an Accept header to decide what format to return so it got its own endpoint that will always return data in the Prometheus format. pcm-sensor-server is sending the header \"Content-Type: text/plain; version=0.0.4\" as required. This /metrics endpoints mimics the same behavior as / and data is thus absolute, not relative.</li>\n\
      <li>/dashboard/influxdb : This will return JSON for a Grafana dashboard with InfluxDB backend that holds all counters. Please see the documentation for more information.</li>\n\
      <li>/dashboard/prometheus : This will return JSON for a Grafana dashboard with Prometheus backend that holds all counters. Please see the documentation for more information.</li>\n\
      <li>/dashboard : same as /dashboard/influxdb </li>\n\
      <li>/favicon.ico : This will return a small favicon.ico as requested by many browsers.</li>\n\
    </ul>\n\
  </body>\n\
</html>\n";
            resp.createResponse( TextHTML, body, RC_200_OK );
            return;
        }

        //std::shared_ptr<Aggregator> current;
        //std::shared_ptr<Aggregator> null;
        //current = std::make_shared<Aggregator>();
        //null    = std::make_shared<Aggregator>();
        //current->dispatch( PCM::getInstance()->getSystemTopology() );
        //aggregatorPair = std::make_pair( null, current );
        aggregatorPair = getNullAndCurrentAggregator();
    } else if ( url.path_ == "/dashboard" || url.path_ == "/dashboard/influxdb") {
        DBG( 3, "client requesting /dashboard path: '", url.path_, "'" );
        resp.createResponse( ApplicationJSON, getPCMDashboardJSON(InfluxDB), RC_200_OK );
        return;
    }
    else if (url.path_ == "/dashboard/prometheus") {
        DBG(3, "client requesting /dashboard path: '", url.path_, "'");
        resp.createResponse(ApplicationJSON, getPCMDashboardJSON(Prometheus), RC_200_OK);
        return;
    } else if ( 0 == url.path_.rfind( "/persecond", 0 ) ) {
        DBG( 3, "client requesting /persecond path: '", url.path_, "'" );
        if ( 10 == url.path_.size() || ( 11 == url.path_.size() && url.path_.at(10) == '/' ) ) {
            DBG( 3, "size == 10 or 11" );
            // path looks like /persecond or /persecond/
            aggregatorPair = hs->getAggregators( 1, 0 );
        } else {
            DBG( 3, "size > 11: size = ", url.path_.size() );
            // We're looking for value X after /persecond/X and possibly a trailing / anything else not
            url.path_.erase( 0, 10 ); // remove /persecond
            DBG( 3, "after removal: path = \"", url.path_, "\", size = ", url.path_.size() );
            if ( url.path_.at(0) == '/' ) {
                url.path_.erase( 0, 1 );
                if ( url.path_.at( url.path_.size() - 1 ) == '/' ) {
                    url.path_.pop_back();
                }
                if ( std::all_of( url.path_.begin(), url.path_.end(), ::isdigit ) ) {
                    size_t seconds;
                    try {
                        seconds = std::stoll( url.path_ );
                    } catch ( std::exception& e ) {
                        DBG( 3, "Error during conversion of /persecond/ seconds: ", e.what() );
                        seconds = 0;
                    }
                    if ( 1 <= seconds && 30 >= seconds ) {
                        aggregatorPair = hs->getAggregators( seconds, 0 );
                    } else {
                        DBG( 3, "seconds == 0 or seconds >= 30, not allowed" );
                        std::string body( "400 Bad Request. seconds == 0 or seconds >= 30, not allowed" );
                        resp.createResponse( TextPlain, body, RC_400_BadRequest );
                        return;
                    }
                } else {
                    DBG( 3, "/persecond/ Not followed by all numbers" );
                    std::string body( "400 Bad Request Request starts with /persecond/ but is not followed by numbers only." );
                    resp.createResponse( TextPlain, body, RC_400_BadRequest );
                    return;
                }
            } else {
                DBG( 3, "/persecond something requested: something=\"", url.path_, "\"" );
                std::string body( "404 Bad Request. Request starts with /persecond but contains bad characters." );
                resp.createResponse( TextPlain, body, RC_404_NotFound );
                return;
            }
        }
    } else if ( 8 == url.path_.size() && 0 == url.path_.find( "/metrics", 0 ) ) {
        DBG( 3, "Special snowflake prometheus wants a /metrics URL, it cant be bothered to use its own mimetype in the Accept header" );
        format = Prometheus_0_0_4;
        aggregatorPair = getNullAndCurrentAggregator();
    } else {
        DBG( 3, "Unknown path requested: \"", url.path_, "\"" );
        std::string body( "404 Unknown path." );
        resp.createResponse( TextPlain, body, RC_404_NotFound );
        return;
    }

    switch ( format ) {
    case JSON:
    {
        JSONPrinter jp( aggregatorPair );
        jp.dispatch( PCM::getInstance()->getSystemTopology() );
        resp.createResponse( ApplicationJSON, jp.str(), RC_200_OK );
        break;
    }
    case Prometheus_0_0_4:
    {
        PrometheusPrinter pp( aggregatorPair );
        pp.dispatch( PCM::getInstance()->getSystemTopology() );
        resp.createResponse( TextPlainProm_0_0_4, pp.str(), RC_200_OK );
        break;
    }
    default:
        std::string body( "406 Not Acceptable. Server can only serve \"" );
        body += req.url().path_ + "\" as application/json, text/plain (prometheus format).";
        resp.createResponse( TextPlain, body, RC_406_NotAcceptable );
    }
}

int startHTTPServer( unsigned short port ) {
    HTTPServer server( "", port );
    // HEAD is GET without body, we will remove the body in execute()
    server.registerCallback( HTTPRequestMethod::GET,  my_get_callback );
    server.registerCallback( HTTPRequestMethod::HEAD, my_get_callback );
    server.run();
    return 0;
}

#if defined (USE_SSL)
int startHTTPSServer( unsigned short port, std::string const & cFile, std::string const & pkFile) {
    HTTPSServer server( "", port );
    server.setPrivateKeyFile ( pkFile );
    server.setCertificateFile( cFile );
    server.initialiseSSL();
    // HEAD is GET without body, we will remove the body in execute()
    server.registerCallback( HTTPRequestMethod::GET,  my_get_callback );
    server.registerCallback( HTTPRequestMethod::HEAD, my_get_callback );
    server.run();
    return 0;
}
#endif

void printHelpText( std::string const & programName ) {
    std::cerr << "Usage: " << programName << " [OPTION]\n\n";
    std::cerr << "Valid Options:\n";
    std::cerr << "    -d                   : Run in the background\n";
#if defined (USE_SSL)
    std::cerr << "    -s                   : Use https protocol (default port " << DEFAULT_HTTPS_PORT << ")\n";
#endif
    std::cerr << "    -p portnumber        : Run on port <portnumber> (default port is " << DEFAULT_HTTP_PORT << ")\n";
    std::cerr << "    -r|--reset           : Reset programming of the performance counters.\n";
    std::cerr << "    -D|--debug level     : level = 0: no debug info, > 0 increase verbosity.\n";
    std::cerr << "    -R|--real-time       : If possible the daemon will run with real time\n";
    std::cerr << "                           priority, could be useful under heavy load to \n";
    std::cerr << "                           stabilize the async counter fetching.\n";
#if defined (USE_SSL)
    std::cerr << "    -C|--certificateFile : \n";
    std::cerr << "    -P|--privateKeyFile  : \n";
#endif
    std::cerr << "    -h|--help            : This information\n";
}

/* Main */
int main( int argc, char* argv[] ) {
    // Argument handling
    bool daemonMode = false;
#if defined (USE_SSL)
    bool useSSL = false;
#endif
    bool forcedProgramming = false;
    bool useRealtimePriority = false;
    unsigned short port = 0;
    unsigned short debug_level = 0;
    std::string certificateFile;
    std::string privateKeyFile;

    if ( argc > 1 ) {
        for ( int i=1; i < argc; ++i ) {
            if ( 0 == strncmp( argv[i], "-d", 2 ) )
                daemonMode = true;
            else if ( 0 == strncmp( argv[i], "-p", 2 ) )
            {
                if ( (++i) < argc ) {
                    std::stringstream ss( argv[i] );
                    try {
                        ss >> port;
                    } catch( std::exception& e ) {
                        std::cerr << "main: port number is not an unsigned short!\n";
                        ::exit( 2 );
                    }
                } else {
                    throw std::runtime_error( "main: Error no port argument given" );
                }
            }
#if defined (USE_SSL)
            else if ( 0 == strncmp( argv[i], "-s" , 2 ) )
            {
                useSSL = true;
            }
#endif
            else if ( 0 == strncmp( argv[i], "-r", 2 ) || 0 == strncmp( argv[i], "--reset", 7 ) )
            {
                forcedProgramming = true;
            }
            else if ( 0 == strncmp( argv[i], "-D", 2 ) || 0 == strncmp( argv[i], "--debug", 7 ) )
            {
                if ( (++i) < argc ) {
                    std::stringstream ss( argv[i] );
                    try {
                        ss >> debug_level;
                    } catch( std::exception& e ) {
                        std::cerr << "main: debug level is not an unsigned short!\n";
                        ::exit( 2 );
                    }
                } else {
                    throw std::runtime_error( "main: Error no debug level argument given" );
                }
            }
            else if ( 0 == strncmp( argv[i], "-R", 2 ) || 0 == strncmp( argv[i], "--real-time", 11 ) )
            {
                useRealtimePriority = true;
            }
            else if ( 0 == strncmp( argv[i], "-h", 2 ) || 0 == strncmp( argv[i], "--help", 6 ) )
            {
                printHelpText( argv[0] );
                exit(0);
            }
#if defined (USE_SSL)
            else if ( 0 == strncmp( "-C", argv[i], 2 ) || 0 == strncmp( "--certificateFile", argv[i], 17 ) ) {
                if ( (++i) < argc ) {
                    std::ifstream fp( argv[i] );
                    if ( ! fp.is_open() ) {
                        std::cerr << "Cannot open certificate file \"" << argv[i] << "\".\n";
                        printHelpText( argv[0] );
                        exit( 3 );
                    }
                    certificateFile = argv[i];
                } else {
                    std::cerr << "Missing certificate file argument.\n";
                    printHelpText( argv[0] );
                    exit( 3 );
                }
            }
            else if ( 0 == strncmp( "-P", argv[i], 2 ) || 0 == strncmp( "--privateKeyFile", argv[i], 16 ) ) {
                if ( (++i) < argc ) {
                    std::ifstream fp( argv[i] );
                    if ( ! fp.is_open() ) {
                        std::cerr << "Cannot open private key file \"" << argv[i] << "\".\n";
                        printHelpText( argv[0] );
                        exit( 4 );
                    }
                    privateKeyFile = argv[i];
                } else {
                    std::cerr << "Missing private key file argument.\n";
                    printHelpText( argv[0] );
                    exit( 4 );
                }
            }
#endif
            else
                throw std::runtime_error( "Unknown argument" );
        }
    }

    debug::dyn_debug_level( debug_level );

#if defined (USE_SSL)
    if ( useSSL ) {
        if ( certificateFile.empty() || privateKeyFile.empty() ) {
            std::cerr << "Error: wanting to use SSL but missing certificate and or private key file(s).\n";
            printHelpText( argv[0] );
            exit( 5 );
        }
    }
#endif

    if ( useRealtimePriority ) {
        int priority = sched_get_priority_min( SCHED_RR );
        if ( priority == -1 ) {
            std::cerr << "Could not get SCHED_RR min priority: " << strerror( errno ) << "\n";
            exit( 6 );
        } else {
            struct sched_param sp = { .sched_priority = priority };
            if ( sched_setscheduler(0, SCHED_RR, &sp ) == -1 ) {
                int errnosave = errno;
                std::cerr << "Could not set scheduler to realtime! Errno: " << errnosave << "\n";
                std::cerr << "Error message: \"" << strerror( errnosave ) << "\"\n";
                exit( 6 );
            } else {
                std::cerr << "Scheduler changed to SCHED_RR and priority to " << priority << "\n";
            }
        }
    }

    pid_t pid;
    if ( daemonMode )
        pid = fork();
    else
        pid = 0;

    if ( pid == 0 ) {
        /* child */
        // Default programming is to use normal core counters and memory bandwidth counters
        // and if pmem is available to also show this instead of partial writes
        // A HTTP interface to change the programming is planned
        PCM::ErrorCode status;
        PCM * pcmInstance = PCM::getInstance();
        do {
            status = pcmInstance->program();
            switch ( status ) {
                case PCM::PMUBusy:
                {
                    if ( forcedProgramming == false ) {
                        std::cout << "Warning: PMU appears to be busy, do you want to reset it? (y/n)\n";
                        char answer;
                        std::cin >> answer;
                        if ( answer == 'y' || answer == 'Y' )
                            pcmInstance->resetPMU();
                        else
                            exit(0);
                    } else {
                        pcmInstance->resetPMU();
                    }
                    break;
                }
                case PCM::Success:
                    break;
                case PCM::MSRAccessDenied:
                case PCM::UnknownError:
                default:
                    exit(1);
            }
        } while( status != PCM::Success );

        if ( pcmInstance->PMMTrafficMetricsAvailable() ) {
            DBG( 1, "Programmed PMEM R/W BW instead of Partial Writes" );
        } else {
            DBG( 1, "Programmed Partial Writes instead of PMEM R/W BW" );
        }

#if defined (USE_SSL)
        if ( useSSL ) {
            if ( port == 0 )
                port = DEFAULT_HTTPS_PORT;
            std::cerr << "Starting SSL enabled server on https://localhost:" << port << "/\n";
            startHTTPSServer( port, certificateFile, privateKeyFile );
        } else
#endif
        {
            if ( port == 0 )
                port = DEFAULT_HTTP_PORT;
            std::cerr << "Starting plain HTTP server on http://localhost:" << port << "/\n";
            startHTTPServer( port );
        }
    } else if ( pid > 0 ) {
        /* Parent, just leave */
        DBG( 2, "Child pid: ", pid );
        return 0;
    } else {
        /* Error */
        DBG( 2, "Error forking. " );
        return 200;
    }
}

