/******************************************************************************
*   Copyright (c) 2013 thundernet development group, inc. http://thundernet.com
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
******************************************************************************/

#ifndef RTP_JITTER_H_15dd71dc_f47a_4474_b2ee_0abc4f9c7197
#define RTP_JITTER_H_15dd71dc_f47a_4474_b2ee_0abc4f9c7197

#include <mutex>
#include <deque>

// from RFC 1889, fixed headers for RTP.  This should be all
//  that we need to reference from our perspective
/*
5.1 RTP Fixed Header Fields


      The RTP header has the following format:

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           timestamp                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           synchronization source (SSRC) identifier            |
   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
   |            contributing source (CSRC) identifiers             |
   |                             ....                              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

*/

typedef std::lock_guard<std::mutex> scoped_lock;

typedef struct {
   unsigned short   flags;          // 2 bytes
   unsigned short   sequence;       // 2 bytes
   unsigned int     timestamp;      // 4 bytes
   unsigned int     ssrc;           // 4 bytes 
} RTPHeader, *PRTPHeader;



class CRTPJitter
{
public:
    enum RESULT
    {
        SUCCESS = 0,
        BAD_PACKET,
        BUFFER_OVERFLOW,
        BUFFER_EMPTY,
        DROPPED_PACKET
    };


    CRTPJitter();
    ~CRTPJitter();

    RESULT push(void *packet);
    RESULT pop(void *packet);
    RESULT reset();
    void   set_depth(const int ms_depth);
    int    get_depth();

private:
    static const int DEFAULT_BUFFER_ELEMENTS = 18;  // 360ms given 20ms packets
    static const int DEFAULT_MS_PER_PACKET   = 20;

    std::deque<PRTPHeader>  _buffer;
    unsigned                _max_buffer_items;      // based on buffer depth
    std::mutex              _mutex;
    int                     _last_sequence;

    void _clean_buffer();   
    void _init();
    void _log(std::string s);
//    void _log(int level, std::string s);
//    void _log(int level, std::string s, ...);
};

#endif  // RTP_JITTER_H_15dd71dc_f47a_4474_b2ee_0abc4f9c7197
