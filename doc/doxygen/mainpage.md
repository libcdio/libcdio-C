# GNU Compact Disc Input and Control Library (`libcdio`) {#mainpage}

Welcome to the API documentation for **libcdio**, the GNU Compact Disc Input and Control Library.

`libcdio` provides a portable, high-level C interface for CD-ROM, CD-DA (Audio CD), Video CD (VCD), and ISO-9660 image access. It abstracts platform-specific OS calls and hardware-level SCSI/MMC commands, allowing applications to inspect disc layouts, extract audio tracks, read ISO filesystem directory trees, and parse CD-TEXT metadata seamlessly across different operating systems.

The specification of SCSI/MMC implemented is the MMC-6 draft 2g from 11 December 2009. It is described in [Working Draft ICITS XXX T10/1836D](https://www.13thmonkey.org/documentation/SCSI/mmc6r02g.pdf).

That draft mentions SCSI Primary Commands, SPC-3. For a draft of SPC-3 that we use, see [dpANS Project T10/1416D](https://www.13thmonkey.org/documentation/SCSI/spc3r23.pdf)

---

## Key Modules & Core API Components

The library is modularized into several core subsystems:

* **[CD-ROM & Drive Control](@ref cdio_cdio)** (`cdio/cdio.h`): Primary interfaces for drive detection, device opening, track/sector reading, and hardware control.
* **[CD-TEXT Handling](@ref cdio_cdtext)** (`cdio/cdtext.h`): Data structures and conversion utilities for parsing CD-TEXT field descriptors, language codes, and track metadata.
* **[ISO-9660 Filesystem](@ref cdio_iso9660)** (`cdio/iso9660.h`): Parsing ISO-9660 images, directory traversal, volume descriptors, and file extraction.
* **[MMC Subsystem](@ref cdio_mmc)** (`cdio/mmc.h`): Low-level SCSI Multimedia Commands (MMC) interface for direct drive feature querying and command execution.
* **[Audio & Device Utilities](@ref cdio_audio)** (`cdio/audio.h`, `cdio/sector.h`): Sector address conversions (LSN/LBA/MSF) and audio playback controls.

---

## Quick Start Example

Here is a basic example opening a default CD-ROM device and querying track details:

@code{.c}
#include <stdio.h>
#include <cdio/cdio.h>
#include <cdio/logging.h>

int main(void) {
    CdIo_t *p_cdio = cdio_open(NULL, DRIVER_UNKNOWN);
    if (!p_cdio) {
        fprintf(stderr, "No CD-ROM drive found or failed to open.\n");
        return 1;
    }

    track_t i_tracks = cdio_get_num_tracks(p_cdio);
    track_t i_first  = cdio_get_first_track_num(p_cdio);

    printf("Drive opened successfully. Total tracks: %d (First track: %d)\n",
           i_tracks, i_first);

    cdio_destroy(p_cdio);
    return 0;
}
@endcode
