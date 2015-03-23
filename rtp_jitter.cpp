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
*   See rtp_jitter.h for requirements and design discussion.
*
******************************************************************************/

#include "rtp_jitter.h"
#include <iostream>
#include <cmath>
#include <cstdint>
#include <arpa/inet.h>

using namespace std;


/******************************************************************************
*   Just call _init to get initialization "things" done.
*
*   Returns n/a
******************************************************************************/
RTPJitter::RTPJitter(const unsigned depth, const uint32 sample_rate /* = 8000 */)
{
    init(depth, sample_rate);
}



/******************************************************************************
*   Need to clean out the memory we own in the buffer (unless we're expecting
*   the caller to be responsible for that).
*
*   Returns n/a
******************************************************************************/
RTPJitter::~RTPJitter()
{
    rscoped_lock lock(_mutex);

    _buffer.clear();
}



/******************************************************************************
*   Ensure a new empty buffer and associated parameters.  Reset buffer stats.
*
*   Returns none
******************************************************************************/
void RTPJitter::init(const unsigned depth, const uint32 sample_rate /* = 8000 */)
{
    if (!_buffer.empty()) {
        _clean_buffer();
    }

    _first_buf_sequence = 0;
    _last_buf_sequence = 0;
    _last_pop_sequence = 0;
    set_depth(depth);
    _payload_sample_rate = sample_rate;

    _buffering = true;
    _buffering_timestamp = timepoint::min();
    _reset_buffer_stats(sample_rate);
}



/******************************************************************************
*   Adds the given packet to the end of the buffer, or inserts it earlier in
*   the buffer if it is out of order.
*
*   Returns rtp jitter result code
******************************************************************************/
RTPJitter::RESULT RTPJitter::push(rawrtp_ptr p)
{
    RTPHeader  *rtp;
    uint16      rtp_sequence = 0;
    RESULT      rc = SUCCESS;

    if ((p != nullptr)
     && ((rtp = reinterpret_cast<PRTPHeader>(p->pData))))
    {
        rscoped_lock lock(_mutex);

        rtp_sequence = ntohs(rtp->sequence);

        if (_depth_ms > _max_buffer_depth) {
            LOGD("RTPJitter::push(): buffer overflow: buffer depth: %d  packet #%d", _depth_ms, rtp_sequence);
            rc = BUFFER_OVERFLOW;
            _stats.overflow_count++;

            // we are overflowing ... drop the front packet
            rawrtp_ptr old_packet = _buffer.front();
            _buffer.pop_front();
            _depth_ms -= old_packet->payload_ms;
            old_packet.reset();
        }

        // if this is our first packet since init, start the buffering clock ...
        if (_buffering && (_buffering_timestamp == timepoint::min())) {
            _buffering_timestamp = stdclock::now();
        }

        // for every packet, update jitter stats
        _calc_jitter(rtp);

        // TODO: sequence numbers are only 16 bits and will wrap around
        //  fairly often.  We need to be sure we're not naive here.

        if ((rtp_sequence >= _last_buf_sequence)
         || ((rtp_sequence == 0) && (_last_buf_sequence == UINT16_MAX))
         || (_last_pop_sequence == _first_buf_sequence))
        {
            // if this packet has a sequence number greater than
            //  any other I've seen so far, then we can be certain
            //  that this one belongs at the end.  As a caveat, I
            //  suppose packets could arrive with the same sequnce
            //  number.  If so, this goes right after the one we
            //  already have -- in this case, it still goes on the
            //  back end ... and we don't consider it to be out of
            //  order.
            _buffer.push_back(p);
            _last_buf_sequence = rtp_sequence;
            _depth_ms += p->payload_ms;

            // if this is the only packet we have, it obviously
            //  serves as both the first and last element.  Also,
            //  we will set the _last_pop_sequence as well so when
            //  we're popping, we don't think there was a dropped
            //  packet i.e. _first_buf == _last_pop means all good.
            if (_buffer.size() == 1) {
                _first_buf_sequence = _last_pop_sequence = rtp_sequence;
            }
        } else {
            LOGD("jitter.push(): ooo packet #%d", rtp_sequence);
            ++_stats.ooo_count;
            // This is an out-of-order packet.  One of two scenarios:
            //
            // 1. preceeds front packet by more than 1
            //      - packet is too old to use, ignore it
            // 2. immediately preceeds the front packet
            //      - packet is just in time, stick on front
            // 3. belogns in the middle of the buffer
            //      - find the home and insert
            // 4. sequence numbers have wrapped around to 0 again
            //      -
            if (rtp_sequence < (_first_buf_sequence - 1)) {
                rc = BAD_PACKET;
            } else if (rtp_sequence == (_first_buf_sequence - 1)) {
                _buffer.push_front(p);
                _first_buf_sequence = rtp_sequence;
            } else {
                // this packet has a sequence number that is strictly
                //  less than the last packet in our buffer, and greater
                //  than or equal to the first packet.  Either way, this
                //  one can go is anywhere from index 1 to n-2
                for (deque<rawrtp_ptr>::iterator i = _buffer.begin(); i != _buffer.end(); ++i) {
                    RTPHeader *item = reinterpret_cast<PRTPHeader>((*i)->pData);
                    if (rtp_sequence < item->sequence) {
                        _buffer.insert(i, p);
                        break;
                    }
                }
            }
        }
    } else {
        // we were given a null pointer ... is that bad enough?
        rc = BAD_PACKET;
    }
    return rc;
}



/******************************************************************************
*   Retrieves the RTP packet from the front of the buffer, or nothing if the
*   expected packet is missing.
*
*   NOTE: be very careful in this routine -- I broke the "one entry, one exit"
*   rule.
*
*   Returns rtp jitter result code
******************************************************************************/
RTPJitter::RESULT RTPJitter::pop(rawrtp_ptr& packet)
{
    rawrtp_ptr bp;      // buffer packet tmp pointer

    rscoped_lock lock(_mutex);

    // first things first -- do we need to enter or exit the buffering state?
    if (_buffer.empty()) {
        // the buffer is empty ... do we need to go back to buffering?  If the
        //  _buffering flag is not yet set, then yes.
        if (!_buffering) {
            _buffering = true;
        }
        _stats.empty_count++;
    } else {
        if (_buffering) {
            // check the time... come out of buffering once the buffering
            //  timer reaches the nominal jitter depth, or if we get a burst
            //  of packets that deepens the buffer to the nominal depth.
            //
            // It's possible that packets came bursting in i.e. we've reached
            //  our depth before the buffering delay expires.  In this case,
            //  we also come out of the buffering state.
            int buffer_time = clocks::duration_cast<clocks::milliseconds>(stdclock::now() - _buffering_timestamp).count();
            if ((buffer_time >= _nominal_depth_ms)
             || (_depth_ms >= _nominal_depth_ms))
            {
                _buffering = false;
                _buffering_timestamp = timepoint::min();
            }
        }
    }

    if (_buffering) {
        return RTPJitter::BUFFERING;
    }

    // take a look at the packet at the front of the buffer
    bp = _buffer.front();

    // let's see if we should take what's on the front of the buffer, or
    //  if we need to return nothing and indicate a dropped packet.
    //  There's a lot of logic here, so tread lightly.
    //
    //  good sequences:
    //      _last_pop and _first_buf are equal
    //      _last_pop is one less than _first_buf
    //      _last_pop is UINT16_MAX and _first_buf is 0
    //      dynamic payloads and _last_pop is 2 less than _first_buf
    if ((_last_pop_sequence == _first_buf_sequence)
     || (_last_pop_sequence == (_first_buf_sequence - 1))
     || ((_last_pop_sequence == UINT16_MAX) && (_first_buf_sequence == 0))
     || ((bp->payload_type == RTP_PAYLOAD_DYNAMIC) && (_last_pop_sequence == (_first_buf_sequence - 2))))
    {
        packet = bp;
        if ((bp->payload_type == RTP_PAYLOAD_DYNAMIC) && (_last_pop_sequence == (_first_buf_sequence - 2))) {
            // "special" case where we hang onto the front packet in the buffer
            //  but mark it becasue we expect to be able to reuse it.
            packet->use_redundant_payload = true;
        } else {
            // "normal" case where we can remove the front packet
            packet->use_redundant_payload = false;
            _buffer.pop_front();
            _depth_ms -= packet->payload_ms;
        }

        RTPHeader *p = reinterpret_cast<PRTPHeader>(packet->pData);
        _last_pop_sequence = ntohs(p->sequence);

        // did we just empty the buffer?  If so, reset the sequence counters
        if (_buffer.empty()) {
            _first_buf_sequence = _last_pop_sequence;
        } else {
            // now peek at the next packet to get it's sequence #
            bp = _buffer.front();
            p = reinterpret_cast<PRTPHeader>(bp->pData);
            _first_buf_sequence = ntohs(p->sequence);
        }
        return RTPJitter::SUCCESS;

    } else {
        ++_last_pop_sequence;
        return RTPJitter::DROPPED_PACKET;
    }
}



/******************************************************************************
*   Empties the current buffer and reinitializes.  May block while waiting for
*   any other thread accessing the buffer.
*
*   Returns rtp jitter result code
******************************************************************************/
RTPJitter::RESULT RTPJitter::reset()
{
    rscoped_lock lock(_mutex);
    _clean_buffer();
    init(_nominal_depth_ms, _payload_sample_rate);

    return SUCCESS;
}



/******************************************************************************
*   Sets the nominal and maximum depths, in milliseconds, of the buffer.  If
*   max_depth is not given, or less than ms_depth, it will be calculated to
*   2 times ms_depth.
*
*   Returns nothing
******************************************************************************/
void RTPJitter::set_depth(const unsigned ms_depth, const unsigned max_depth /* = 0 */)
{
    rscoped_lock lock(_mutex);

    _nominal_depth_ms = ms_depth;
    if (max_depth >= ms_depth) {
        _max_buffer_depth = max_depth;
    } else {
        _max_buffer_depth = (_nominal_depth_ms * 2);
    }
}



/******************************************************************************
*   Rertieves the current number of packets in the buffer.
******************************************************************************/
int RTPJitter::get_depth()
{
    rscoped_lock lock(_mutex);
    return (_buffer.size());
}



/******************************************************************************
*   Retrieves the current 'depth' of the buffer in miliseconds.
******************************************************************************/
int RTPJitter::get_depth_ms()
{
    rscoped_lock lock(_mutex);
    return _depth_ms;
}



/******************************************************************************
*   Retrieves the current "requested/nominal" depth of the buffer in miliseconds.
******************************************************************************/
int RTPJitter::get_nominal_depth()
{
    rscoped_lock lock(_mutex);
    return _nominal_depth_ms;
}



/******************************************************************************
*   Some external agent is saying an end of transmission has been detected and
*   we might want to reset our sequence numbers since there's no guarantee
*   future numbers won't overlap current ones in an odd way.  Just sayin'
******************************************************************************/
void RTPJitter::eot_detected()
{
    _first_buf_sequence = 0;
    _last_buf_sequence = 0;
    _last_pop_sequence = 0;
}



/******************************************************************************
*   Calculates/updates jitter stats based on the given packet.  We adhere to
*   the formula estimating interarrival jitter as proscribed in RFC3550 section
*   6.4.1 and on the sample code in appendix A.8.
*
*   Returns none -- there's no return value, but internal stats are updated.
******************************************************************************/
void RTPJitter::_calc_jitter(RTPHeader *rtp)
{
    uint32      arrival;
    int         d;
    uint32      transit;

    timepoint   current_time = stdclock::now();
    int         interarrival_time_ms = clocks::duration_cast<clocks::milliseconds>(current_time - _stats.prev_rx_timestamp).count();

    // get the 'arrival time' of this packet as measured in 'timestamp units' and offset
    //  to match the timestamp range in this stream
    arrival = interarrival_time_ms * _stats.conversion_factor_timestamp_units;
    if (_stats.prev_arrival == 0) {
        arrival = ntohl(rtp->timestamp);
    } else {
        arrival += _stats.prev_arrival;
    }

    // TODO: there's a problem here in the calculation of arrival and
    //  prev_arrival times.  Need to work this out in some testing.
//    _stats.prev_arrival = arrival;
    _stats.prev_arrival = ntohl(rtp->timestamp);

    transit = arrival - ntohl(rtp->timestamp);
    d = transit - _stats.prev_transit;
    _stats.prev_transit = transit;
    if (d < 0) d = -d;

    _stats.jitter += (1.0/16.0) * ((double)d - _stats.jitter);

    _stats.prev_rx_timestamp = current_time;
    // is this a new high water mark for jitter?
    if (_stats.max_jitter < _stats.jitter) _stats.max_jitter = _stats.jitter;

//    LOGD("RTPJitter::_calc_jitter(): interarrival_time_ms[%03d]  arrival[%u]  rtp->timestamp[%u]  transit[%d]  jitter[%d]  max_jitter[%.4f] ", interarrival_time_ms, arrival, ntohl(rtp->timestamp), transit, (uint32)_stats.jitter, _stats.max_jitter);
}



/******************************************************************************
*   Clean items out of our buffer and delete/release memory resources.
*
*   Returns none
******************************************************************************/
void RTPJitter::_clean_buffer()
{
    _buffer.clear();
    _depth_ms = 0;
}



/******************************************************************************
*   Finds the index of the start of payload data in the given RTP packet by
*   accounting for the standard header and any possible extensions, etc.
*
*   Returns pointer to first byte of payload data, nullptr on error
******************************************************************************/
uint8 *RTPJitter::_get_payload(RTPHeader *packet)
{
    uint8  *payload = (uint8 *)packet;

    if (payload != nullptr) {

        payload += sizeof(RTPHeader);

        // TODO: account for CSRC identifiers

        // if the extension header bit is set, skip the dynamically
        //  sized header extension.
        uint16 flags = ntohs(packet->flags);
        if (flags & RTP_FLAGS_EXTENSION) {
            RTPHeaderExt *ext = (RTPHeaderExt *)payload;
            payload += sizeof(ext->profile_specific)
                    +  sizeof(ext->ext_length)
                    + (sizeof(ext->ext_data[0]) * ntohs(ext->ext_length));
        }

        // special handling for the Dynamic payload type
        if (_get_payload_type(packet) == RTP_PAYLOAD_DYNAMIC) {
            // skip over the redundant payload, etc.
            payload += 3;   // skip to the redundant block length
            payload += *payload + 1;    // should skip over the redundant size and the redundant payload
            payload += 1;   // ... and finally, skip over the primary Payload Type (which SHOULD be 18 G.729)
        }
    }
    return payload;
}



/******************************************************************************
*   Utility/convenience function to extract Payload Type from an RTP Header.
******************************************************************************/
uint8 RTPJitter::_get_payload_type(RTPHeader *packet)
{
    uint8 payload_type;

    uint16 flags = ntohs(packet->flags);
    payload_type = flags & RTP_FLAGS_PAYLOAD_TYPE;

    return payload_type;
}



/******************************************************************************
*   Sends given string to stdout.
*
*   Returns none
******************************************************************************/
void RTPJitter::_log(string s)
{
   // cout << s << endl;
}



/******************************************************************************
*   Just like the name says ... resets the jitter buffer statistics
*
*   Returns none
******************************************************************************/
void RTPJitter::_reset_buffer_stats(uint32 sample_rate)
{
    _stats.ooo_count = 0;
    _stats.empty_count = 0;
    _stats.overflow_count = 0;
    _stats.jitter = 0.0;
    _stats.max_jitter = 0.0;
    _stats.prev_arrival = 0;
    _stats.prev_transit = 0;
    _stats.prev_rx_timestamp = timepoint::min();
    _stats.conversion_factor_timestamp_units = sample_rate / 1000;
}
