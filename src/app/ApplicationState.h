#pragma once

#include <QString>

namespace rtsp {
enum class ApplicationState {
    Stopped, StartingIngestServer, WaitingForSource, ConnectingDestination,
    SendingFiller, Buffering, Relaying, IncreasingDelay, DecreasingDelay,
    ReconnectingDestination, Error, Stopping
};

QString toString(ApplicationState state);
bool canTransition(ApplicationState from, ApplicationState to);
}

