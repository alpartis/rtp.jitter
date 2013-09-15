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
******************************************************************************/

#include "rtp_jitter.h"
#include <cmath>

/******************************************************************************
*   Just call _init to get initialization "things" done.
*   
*   Returns n/a
******************************************************************************/
CRTPJitter::CRTPJitter() 
{
    _init();
}



/******************************************************************************
*   Need to clean out the memory we own in the buffer (unless we're expecting   
*   the caller to be responsible for that).
*   
*   Returns n/a
******************************************************************************/
CRTPJitter::~CRTPJitter()
{
    scoped_lock lock(_mutex);

    _buffer.clear();
} 



/******************************************************************************
*   Adds the given packet to the end of the buffer, or inserts it earlier in    
*   the buffer if it is out of order.
*
*   NOTE: we will be storing our own pointer to the packet data.  The caller 
*   will need to NOT delete this memory until after it is popped off. 
*   
*   Returns rtp jitter result code
******************************************************************************/
CRTPJitter::RESULT CRTPJitter::push(void *p)
{
    PRTPHeader  packet;
    RESULT      rc = SUCCESS;

    if (p != nullptr) {
        if (packet = reinterpret_cast<PRTPHeader>(p)) {
        
            scoped_lock lock(_mutex);
        
            if (_buffer.size() >= _max_buffer_items) {
                rc = BUFFER_OVERFLOW;

                // we are overflowing ... drop the front packet
                packet = _buffer.front();
                _buffer.pop_front();

                // TODO: our buffer will probably hold shared pointers eventually,
                //  so we'll need to SAFE_RELEASE here instead.
                SAFE_DELETE(packet);
            }

            // TODO: look at the sequence number and timestamp and
            //  ensure we're putting this in the right place.  Maybe
            //  this should go earlier in the queue.
            _buffer.push_back(packet);
        }
    }

    return rc;
}



/******************************************************************************
*   Retrieves the RTP packet from the front of the buffer, or nothing if the    
*   expected packet is missing.
*   
*   Returns rtp jitter result code
******************************************************************************/
CRTPJitter::RESULT CRTPJitter::pop(void *packet)
{
    scoped_lock lock(_mutex);

    if (!_buffer.empty()) {
        packet = _buffer.front();
        _buffer.pop_front();
        return SUCCESS;
    } else {
        packet = nullptr;
        return BUFFER_EMPTY;
    }
}



/******************************************************************************
*   Empties the current buffer and reinitializes.  May block while waiting for
*   any other thread accessing the buffer.
*   
*   Returns rtp jitter result code
******************************************************************************/
CRTPJitter::RESULT CRTPJitter::reset()
{
    scoped_lock lock(_mutex);
    _clean_buffer();
    _init();

    return SUCCESS;
}



/******************************************************************************
*   Adjusts the "depth" of the buffer to the minimum number of packets necessary
*   to achieve the desired time delay.
*   
*   Returns rtp jitter result code
******************************************************************************/
void CRTPJitter::set_depth(const int ms_depth)
{
    scoped_lock lock(_mutex);

    // TODO: No, this isn't the 'right' way to calculate jitter buffer
    //  depth.  So yes, this needs to be rewritten.
    _max_buffer_items = (int)ceil((float)ms_depth / (float)DEFAULT_MS_PER_PACKET);
}



/******************************************************************************
*   Rerieves the actual 'depth' of the buffer in miliseconds based on the      
*   size of our RTP packets and the index size of our buffer.
*   
*   Returns rtp jitter result code
******************************************************************************/
int CRTPJitter::get_depth()
{
    scoped_lock lock(_mutex);

    // TODO: yeah, this needs correcting too
    return (_max_buffer_items * DEFAULT_MS_PER_PACKET);
}


/******************************************************************************
*   Clean items out of our buffer and delete/release memory resources.
*   
*   Returns none
******************************************************************************/
void CRTPJitter::_clean_buffer()
{
    while (_buffer.size() > 0) {
        PRTPHeader  packet = _buffer.front();
        SAFE_DELETE(packet);        // TODO: when moving to shared pointers, use SAFE_RELEASE() instead
        _buffer.pop_front();
    }
}



/******************************************************************************
*   Ensure a new empty buffer and associated parameters.
*   
*   Returns none
******************************************************************************/
void CRTPJitter::_init()
{
    _max_buffer_items = DEFAULT_BUFFER_ELEMENTS;

    if (!_buffer.empty()) {
        _clean_buffer();
    }
}



/******************************************************************************
*   Sends given string to stdout.
*   
*   Returns none
******************************************************************************/
void CRTPJitter::_log(std::string s)
{
    std::cout << s << std::endl;
}
