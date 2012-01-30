/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MESSAGE_QUEUE
#define MESSAGE_QUEUE

#include <utils/Errors.h>
#include <utils/Timers.h>
#include <utils/threads.h>
#include <utils/Log.h>

namespace android {

template <class MessageType>
class MessageQueue {

    // constructor / destructor
public:
    MessageQueue(const char *name, // for debugging
            int numReply = 0) :    // set numReply only if you need synchronous messages
        mName(name)
        ,mCount(0)
        ,mHead(0)
        ,mNumReply(numReply)
        ,mReplyMutex(NULL)
        ,mReplyCondition(NULL)
        ,mReplyStatus(NULL)
    {
        if (mNumReply > 0) {
            mReplyMutex = new Mutex[numReply];
            mReplyCondition = new Condition[numReply];
            mReplyStatus = new status_t[numReply];
        }
    }

    ~MessageQueue()
    {
        if (mNumReply > 0) {
            delete [] mReplyMutex;
            delete [] mReplyCondition;
            delete [] mReplyStatus;
        }
    }

    // public methods
public:

    // Push a message onto the queue. If replyId is not -1 function will block until
    // the caller is signalled with a reply. Caller is unblocked when reply method is
    // called with the corresponding message id.
    status_t send(MessageType *msg, int replyId = -1)
    {
        status_t status = NO_ERROR;

        // someone is misusing the API. replies have not been enabled
        if (replyId != -1 && mNumReply == 0) {
            LOGE("Atom_MessageQueue error: %s replies not enabled\n", mName);
            return BAD_VALUE;
        }

        mQueueMutex.lock();
        if (mCount < MESSAGE_QUEUE_SIZE) {
            int tail = (mHead + mCount) % MESSAGE_QUEUE_SIZE;
            circularQueue[tail] = *msg;
            mCount++;
        } else {
            LOGE("Atom_MessageQueue error: %s message queue is full\n", mName);
            status = NOT_ENOUGH_DATA;
        }
        if (replyId != -1) {
            mReplyStatus[replyId] = WOULD_BLOCK;
        }
        mQueueCondition.signal();
        mQueueMutex.unlock();

        if (replyId >= 0 && status == NO_ERROR) {
            mReplyMutex[replyId].lock();
            while (mReplyStatus[replyId] == WOULD_BLOCK) {
                mReplyCondition[replyId].wait(mReplyMutex[replyId]);
                // wait() should never complete without a new status having
                // been set, but for diagnostic purposes let's check it.
                if (mReplyStatus[replyId] == WOULD_BLOCK) {
                    LOGE("Atom_MessageQueue - woke with WOULD_BLOCK\n");
                }
            }
            status = mReplyStatus[replyId];
            mReplyMutex[replyId].unlock();
        }

        return status;
    }

    // Pop a message from the queue
    status_t receive(MessageType *msg)
    {
        status_t status = NO_ERROR;

        mQueueMutex.lock();
        while (mCount == 0) {
            mQueueCondition.wait(mQueueMutex);
            // wait() should never complete without a message being
            // available, but for diagnostic purposes let's check it.
            if (mCount == 0) {
                LOGE("Atom_MessageQueue - woke with mCount == 0\n");
            }
        }

        *msg = circularQueue[mHead];
        mHead = (mHead + 1) % MESSAGE_QUEUE_SIZE;
        mCount--;
        mQueueMutex.unlock();

        return status;
    }

    // Unblock the caller of send and indicate the status of the received message
    void reply(int replyId, status_t status)
    {
        mReplyMutex[replyId].lock();
        mReplyStatus[replyId] = status;
        mReplyCondition[replyId].signal();
        mReplyMutex[replyId].unlock();
    }

    // Return true if the queue is empty
    inline bool isEmpty() { return mCount == 0; }

private:

    static const int MESSAGE_QUEUE_SIZE = 32;

    const char *mName;
    Mutex mQueueMutex;
    Condition mQueueCondition;
    int mCount;
    int mHead;
    MessageType circularQueue[MESSAGE_QUEUE_SIZE];

    int mNumReply;
    Mutex *mReplyMutex;
    Condition *mReplyCondition;
    status_t *mReplyStatus;

}; // class MessageQueue

}; // namespace android

#endif // MESSAGE_QUEUE
