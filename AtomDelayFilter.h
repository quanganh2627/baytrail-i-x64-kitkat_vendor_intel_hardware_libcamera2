#ifndef _ATOM_DELAY_FILTER_H_
#define _ATOM_DELAY_FILTER_H_

template <class X> class AtomDelayFilter {
    X *buffer;
    unsigned int depth;
    unsigned int wrIdx; // write index
    X defaultVal;

    void push(X);
public:
    AtomDelayFilter(X defaultVal, unsigned int depth);
    virtual ~AtomDelayFilter();
    unsigned int delay() { return depth; };
    X enqueue(X);
    X dequeue();
    void reset(X val);
};

// depth == 0 means non delay
template <class X> AtomDelayFilter<X>::AtomDelayFilter(X val, unsigned int depth = 1)
{
    this->depth = depth;
    defaultVal = val;
    wrIdx = 0;
    buffer = NULL;
    if (depth == 0)
        return;

    buffer = new X[depth];
    reset(defaultVal);
}

template <class X> AtomDelayFilter<X>::~AtomDelayFilter()
{
    if (buffer) {
        delete[] buffer;
        buffer = NULL;
    }
}

template <class X> void AtomDelayFilter<X>::push(X val)
{
    if (depth == 0)
        return;

    buffer[wrIdx] = val;
    if (wrIdx == depth - 1)
        wrIdx = 0;
    else
        ++wrIdx;
}

// This works correctly even if depth == 0,
// it means through mode.
template <class X> X AtomDelayFilter<X>::enqueue(X val)
{
    X ret;

    if (depth == 0) {
        ret = val;
    } else {
        ret = buffer[wrIdx];
        push(val);
    }
    return ret;
}

template <class X> X AtomDelayFilter<X>::dequeue()
{
    return enqueue(defaultVal);
}

template <class X> void AtomDelayFilter<X>::reset(X val)
{
    defaultVal = val;
    wrIdx = 0;
    for (unsigned int i=0; i<depth; i++)
        buffer[i] = val;
}

#endif /* _ATOM_DELAY_FILTER_H_ */
