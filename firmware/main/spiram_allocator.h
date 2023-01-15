#ifndef _SPIRAM_ALLOCATOR_H_
#define _SPIRAM_ALLOCATOR_H_
#include <limits>

#include "esp_heap_caps.h"

template <class T>
class PSRAMAllocator {
    public:
    // type definitions
    typedef T        value_type;
    typedef T*       pointer;
    typedef const T* const_pointer;
    typedef T&       reference;
    typedef const T& const_reference;
    typedef std::size_t    size_type;
    typedef std::ptrdiff_t difference_type;

    // rebind allocator to type U
    template <class U>
    struct rebind {
        typedef PSRAMAllocator<U> other;
    };

    // return address of values
    pointer address (reference value) const {
        return &value;
    }
    const_pointer address (const_reference value) const {
        return &value;
    }

    /* constructors and destructor
    * - nothing to do because the allocator has no state
    */
    PSRAMAllocator() throw() {
    }
    PSRAMAllocator(const PSRAMAllocator&) throw() {
    }
    template <class U>
        PSRAMAllocator (const PSRAMAllocator<U>&) throw() {
    }
    ~PSRAMAllocator() throw() {
    }

    // return maximum number of elements that can be allocated
    size_type max_size () const throw() {
        return std::numeric_limits<std::size_t>::max() / sizeof(T);
    }

    // allocate but don't initialize num elements of type T
    pointer allocate (size_type num, const void* = 0) {
        // print message and allocate memory with global new
        pointer ret = (pointer)heap_caps_malloc(num*sizeof(T), MALLOC_CAP_SPIRAM);
        return ret;
    }

    // initialize elements of allocated storage p with value value
    void construct (pointer p, const T& value) {
        // initialize memory with placement new
        new((void*)p)T(value);
    }

    // destroy elements of initialized storage p
    void destroy (pointer p) {
        // destroy objects by calling their destructor
        p->~T();
    }

    // deallocate storage p of deleted elements
    void deallocate (pointer p, size_type num) {
        // print message and deallocate memory with global delete
        free(p);
    }
};

// return that all specializations of this allocator are interchangeable
template <class T1, class T2>
bool operator== (const PSRAMAllocator<T1>&,
                const PSRAMAllocator<T2>&) throw() {
    return true;
}
template <class T1, class T2>
bool operator!= (const PSRAMAllocator<T1>&,
                const PSRAMAllocator<T2>&) throw() {
    return false;
}
#endif