/******************************************************************************
*   Copyright (c) 2013-2015 thundernet development group, inc.
*   http://thundernet.com
*
*   Permission is hereby granted, free of charge, to any person obtaining a
*   copy of this software and associated documentation files (the "Software"),
*   to deal in the Software without restriction, including without limitation
*   the rights to use, copy, modify, merge, publish, distribute, sublicense,
*   and/or sell copies of the Software, and to permit persons to whom the
*   Software is furnished to do so, subject to the following conditions:
*
*   1. The above copyright notice and this permission notice shall be included
*      in all copies or substantial portions of the Software.
*
*   2. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
*      OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
*      MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
*      IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
*      CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
*      TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
*      SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
*   -----
*
*   jitter buffer for RTP packets based on C++11 and STL.
*
*   Memory management is something to be wary of; we will store our own
*   pointer to given packets in our buffer and will return copies of these
*   pointers when packets are popped off the buffer.  In the case of a
*   buffer reset, or similar activity, we will go ahead and delete any
*   packets in our buffer at the time.
*
*   This implementation assumes that it is operating in a multi-threaded
*   environment and syncronizes all operations/methods.
*
*   Requirements:
*       R0: eliminate effects of jitter in RTP packet arrival
*       R1: detect and track missing packets in RTP stream
*       R2: allow configuration of depth and packet size (in milliseconds)
*       R3: track and report ongoing jitter stats:
*               dropped packets
*               out of order packets
*               missed packets
*               jitter (lifetime)
*               max jitter (lifetime)
*               current depth
*
*   The RTPJitter will manage an internal buffer of RTP frames.  The application
*   layer adds and removes packets from this buffer though the .push() and
*   .pop() interface.  It is up to the application to manage and schedule this
*   process on its own thread.
*
*   A jitter buffer introduces a configured delay in the delivery and
*   processing of packets; this is the "depth" in milliseconds of the buffer.
*   Once the first packet is received into an empty buffer, the "depth" timer
*   starts.  Once it expires, packets will be delivered upon request in the
*   .pop() function until the buffer becomes empty again.
*
*   The remaining logic involves how to handle newly arriving packets and
*   handle the exception cases: missing or out-of-order packets.  ... and how
*   to detect and address jitter.
*
*   Jitter will be calculated as the mean deviation from the expected based on
*   details, definitions, and sample code from RFC3550 section 6.4.1 and
*   Appendix A.8.
*
*   The jitter buffer will not examine, or differentiate, packets based on SSRC.
*
*
*   note 1 - we use a std::deque to implement the internal buffer because it
*       allows us to ignore the details of memory management for the buffer
*       as it may grow and shrink.
*
******************************************************************************/

#ifndef RTP_JITTER_H_cc8e302e_b008_4588_a29a_79a9f555804d
#define RTP_JITTER_H_cc8e302e_b008_4588_a29a_79a9f555804d

#include <deque>
#include <memory>
#include "stdinc.h"
#include "rtp.h"



class RTPJitter
{
public:
    enum RESULT
    {
        SUCCESS = 0,
        BUFFERING,
        BAD_PACKET,
        BUFFER_OVERFLOW,
        BUFFER_EMPTY,
        DROPPED_PACKET
    };


    RTPJitter(const unsigned depth, const uint32 sample_rate = 8000);
    ~RTPJitter();

    void    init(const unsigned depth, const uint32 sample_rate = 8000);
    RESULT  push(rawrtp_ptr packet);
    RESULT  pop(rawrtp_ptr& packet);
    RESULT  reset();
    void    set_depth(const unsigned ms_depth, const unsigned max_depth = 0);
    int     get_depth();
    int     get_depth_ms();
    int     get_nominal_depth();
    bool    buffering()         { return _buffering; }
    void    eot_detected();

    // - statistics retrieval
    int overflow_count()        { return _stats.overflow_count; }
    int out_of_order_count()    { return _stats.ooo_count; }
    int empty_count()           { return _stats.empty_count; }
    uint32 jitter()             { return (uint32)_stats.jitter; }
    uint32 max_jitter()         { return (uint32)_stats.max_jitter; }

private:
    static const int DEFAULT_BUFFER_ELEMENTS = 18;  // 360ms given 20ms packets
    static const int DEFAULT_MS_PER_PACKET   = 20;

    std::recursive_mutex    _mutex;
    std::deque<rawrtp_ptr>  _buffer;
    unsigned                _nominal_depth_ms;      // requested buffer depth - may dynamically adjust
    int                     _max_buffer_depth;      // as measured in milliseconds
    unsigned                _payload_sample_rate;   // rate of audio in packet payloads
    unsigned                _depth_ms;              // actual current buffer depth
    uint16                  _first_buf_sequence;    // at head of buffer (next to be popped)
    uint16                  _last_buf_sequence;     // at tail of buffer (most recent arrival)
    uint16                  _last_pop_sequence;
    bool                    _buffering;             // while buffering, don't pop packets

    timepoint               _buffering_timestamp;   // the time we start buffering

    struct stats {
        uint32      ooo_count;          // count of out of order packets
        uint32      empty_count;        // how many times was buffer empty
        uint32      overflow_count;     //
        double      jitter;
        double      max_jitter;
        uint32      prev_arrival;
        uint32      prev_transit;
        timepoint   prev_rx_timestamp;
        int         conversion_factor_timestamp_units;
    } _stats;

    void        _calc_jitter(RTPHeader *rtp);
    void        _clean_buffer();
    uint8       _get_payload_type(RTPHeader *packet);
    uint8      *_get_payload(RTPHeader *packet);
    void        _log(std::string s);
    void        _reset_buffer_stats(const uint32 sample_rate);
};

#endif  // RTP_JITTER_H_cc8e302e_b008_4588_a29a_79a9f555804d
