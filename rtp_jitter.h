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

    // TODO: there should be no need for a recursive mutex unless there
    //  is a possibility of a single thread calling one function from
    //  another where it will attempt to get the lock twice, but I don't
    //  think such a case exists.  Certainly there are probably 2 threads
    //  accessing an instance of this class i.e. packets are pushed in
    //  from a socket thread and popped off by an application thread.
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
