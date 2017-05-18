#include "TestRacyInterruptReplies.h"

#include "IPDLUnitTests.h"      // fail etc.

namespace mozilla {
namespace _ipdltest {

//-----------------------------------------------------------------------------
// parent

TestRacyInterruptRepliesParent::TestRacyInterruptRepliesParent() : mReplyNum(0)
{
    MOZ_COUNT_CTOR(TestRacyInterruptRepliesParent);
}

TestRacyInterruptRepliesParent::~TestRacyInterruptRepliesParent()
{
    MOZ_COUNT_DTOR(TestRacyInterruptRepliesParent);
}

void
TestRacyInterruptRepliesParent::Main()
{
    int replyNum = -1;
    if (!CallR_(&replyNum))
        fail("calling R()");

    if (1 != replyNum)
        fail("this should have been the first reply to R()");

    if (!SendChildTest())
        fail("sending ChildStart");
}

mozilla::ipc::IPCResult
TestRacyInterruptRepliesParent::RecvA_()
{
    int replyNum = -1;
    // this R() call races with the reply being generated by the other
    // side to the R() call from Main().  This is a pretty nasty edge
    // case for which one could argue we're breaking in-order message
    // delivery, since this side will process the second reply to R()
    // before the first.
    if (!CallR_(&replyNum))
        fail("calling R()");

    if (2 != replyNum)
        fail("this should have been the second reply to R()");

    return IPC_OK();
}

mozilla::ipc::IPCResult
TestRacyInterruptRepliesParent::Answer_R(int* replyNum)
{
    *replyNum = ++mReplyNum;

    if (1 == *replyNum)
        if (!Send_A())
            fail("sending _A()");

    return IPC_OK();
}

//-----------------------------------------------------------------------------
// child

TestRacyInterruptRepliesChild::TestRacyInterruptRepliesChild() : mReplyNum(0)
{
    MOZ_COUNT_CTOR(TestRacyInterruptRepliesChild);
}

TestRacyInterruptRepliesChild::~TestRacyInterruptRepliesChild()
{
    MOZ_COUNT_DTOR(TestRacyInterruptRepliesChild);
}

mozilla::ipc::IPCResult
TestRacyInterruptRepliesChild::AnswerR_(int* replyNum)
{
    *replyNum = ++mReplyNum;

    if (1 == *replyNum)
        SendA_();

    return IPC_OK();
}

mozilla::ipc::IPCResult
TestRacyInterruptRepliesChild::RecvChildTest()
{
    int replyNum = -1;
    if (!Call_R(&replyNum))
        fail("calling R()");

    if (1 != replyNum)
        fail("this should have been the first reply to R()");

    Close();

    return IPC_OK();
}

mozilla::ipc::IPCResult
TestRacyInterruptRepliesChild::Recv_A()
{
    int replyNum = -1;

    if (!Call_R(&replyNum))
        fail("calling _R()");

    if (2 != replyNum)
        fail("this should have been the second reply to R()");

    return IPC_OK();
}

} // namespace _ipdltest
} // namespace mozilla
