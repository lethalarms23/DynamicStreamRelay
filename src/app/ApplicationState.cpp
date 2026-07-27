#include "app/ApplicationState.h"

#include <array>

namespace rtsp {
QString toString(ApplicationState s) {
    switch (s) {
    case ApplicationState::Stopped: return "Stopped";
    case ApplicationState::StartingIngestServer: return "Starting ingest server";
    case ApplicationState::WaitingForSource: return "Waiting for source";
    case ApplicationState::ConnectingDestination: return "Connecting destination";
    case ApplicationState::SendingFiller: return "Sending filler";
    case ApplicationState::Buffering: return "Buffering";
    case ApplicationState::Relaying: return "Relaying";
    case ApplicationState::IncreasingDelay: return "Increasing delay";
    case ApplicationState::DecreasingDelay: return "Decreasing delay";
    case ApplicationState::ReconnectingDestination: return "Reconnecting destination";
    case ApplicationState::Error: return "Error";
    case ApplicationState::Stopping: return "Stopping";
    }
    return "Unknown";
}

bool canTransition(ApplicationState from, ApplicationState to) {
    if (from == to) return true;
    if (to == ApplicationState::Error || to == ApplicationState::Stopping) return from != ApplicationState::Stopped;
    if (from == ApplicationState::Error) return to == ApplicationState::Stopped || to == ApplicationState::StartingIngestServer;
    if (from == ApplicationState::Stopping) return to == ApplicationState::Stopped;
    switch (from) {
    case ApplicationState::Stopped: return to == ApplicationState::StartingIngestServer;
    case ApplicationState::StartingIngestServer: return to == ApplicationState::WaitingForSource;
    case ApplicationState::WaitingForSource: return to == ApplicationState::ConnectingDestination || to == ApplicationState::Buffering;
    case ApplicationState::ConnectingDestination: return to == ApplicationState::SendingFiller || to == ApplicationState::Buffering || to == ApplicationState::ReconnectingDestination;
    case ApplicationState::SendingFiller: return to == ApplicationState::Buffering || to == ApplicationState::Relaying || to == ApplicationState::ReconnectingDestination;
    case ApplicationState::Buffering: return to == ApplicationState::Relaying || to == ApplicationState::SendingFiller || to == ApplicationState::ReconnectingDestination;
    case ApplicationState::Relaying: return to == ApplicationState::IncreasingDelay || to == ApplicationState::DecreasingDelay || to == ApplicationState::SendingFiller || to == ApplicationState::ReconnectingDestination;
    case ApplicationState::IncreasingDelay: return to == ApplicationState::Relaying || to == ApplicationState::SendingFiller;
    case ApplicationState::DecreasingDelay: return to == ApplicationState::Relaying || to == ApplicationState::SendingFiller;
    case ApplicationState::ReconnectingDestination: return to == ApplicationState::SendingFiller || to == ApplicationState::Buffering || to == ApplicationState::Relaying;
    case ApplicationState::Error: case ApplicationState::Stopping: break;
    }
    return false;
}
}

