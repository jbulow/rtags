#include "Client.h"
#include "Server.h"
#include <rct/Connection.h>
#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <rct/Messages.h>
#include <rct/Rct.h>
#include <unistd.h>

Client::Client(const Path &path, int timeout, unsigned flags, const List<String> &rdmArgs)
    : mConnectTimeout(timeout), mConnection(0), mFlags(flags), mRdmArgs(rdmArgs), mName(path)
{
    if ((mFlags & (RestartRdm|AutostartRdm)) == (RestartRdm|AutostartRdm)) {
        mFlags &= ~AutostartRdm; // this is implied and would upset connectToServer
    }

    Client::initMessages();
    const bool ret = connectToServer();
    if (mFlags & RestartRdm) {
        if (ret) {
            QueryMessage msg(QueryMessage::Shutdown);
            message(&msg);
            delete mConnection;
            mConnection = 0;
        }
        mFlags |= AutostartRdm;
        connectToServer();
        mFlags &= ~AutostartRdm;
    }
}

void Client::initMessages()
{
    Messages::registerMessage<QueryMessage>();
    Messages::registerMessage<CompletionMessage>();
    Messages::registerMessage<CompileMessage>();
    Messages::registerMessage<CreateOutputMessage>();
}

bool Client::message(const Message *msg, SendFlag flag)
{
    if (!mConnection && !connectToServer() && !(mFlags & (RestartRdm|AutostartRdm))) {
        if (!(mFlags & DontWarnOnConnectionFailure))
            error("Can't seem to connect to server");
        return false;
    }

    if (flag != SendDontRunEventLoop) {
        mConnection->disconnected().connect(this, &Client::onDisconnected);
        mConnection->newMessage().connect(this, &Client::onNewMessage);
    }
    mConnection->send(msg);
    if (flag != SendDontRunEventLoop)
        EventLoop::instance()->run();
    return true;
}

void Client::onNewMessage(Message *message, Connection *)
{
    if (message->messageId() == ResponseMessage::MessageId) {
        const String response = static_cast<ResponseMessage*>(message)->data();
        if (!response.isEmpty()) {
            error("%s", response.constData());
            fflush(stdout);
        }
    } else {
        error("Unexpected message: %d", message->messageId());
    }
}


void Client::onDisconnected(Connection *)
{
    if (mConnection) {
        mConnection->deleteLater();
        mConnection = 0;
        EventLoop::instance()->exit();
    }
}
bool Client::connectToServer()
{
    assert(!mConnection);
    mConnection = new Connection;
    if (!mConnection->connectToServer(mName, mConnectTimeout)) {
        if (mFlags & AutostartRdm) {
            const Path cmd = Rct::executablePath().parentDir() + "/rdm";
            warning("trying to start rdm %s [%s]", cmd.nullTerminated(), String::join(mRdmArgs, " ").constData());
            if (Rct::startProcess(cmd, mRdmArgs)) {
                warning("Started successfully");
                for (int i=0; i<5; ++i) {
                    if (mConnection->connectToServer(mName, mConnectTimeout)) {
                        return true;
                    }
                    sleep(1);
                }
            } else {
                error("Couldn't start");
            }

        }

        warning("Can't connect to host");
        delete mConnection;
        mConnection = 0;
        return false;
    }
    return true;
}

