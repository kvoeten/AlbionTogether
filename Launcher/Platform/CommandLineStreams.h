#pragma once

namespace fable::launcher::platform
{
    // A Windows GUI executable has no CRT console streams by default. Bind the
    // launcher's existing wide output to an inherited pipe or parent console
    // only for command-line mode, without creating a console for the GUI.
    void AttachCommandLineStreams();
}
