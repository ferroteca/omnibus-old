// license:BSD-3-Clause
// copyright-holders:Mark Garlanger, Paul Galbraith
/*********************************************************************

    formats/h17_common.h

    Definitions and the heath_h17_format base class shared by the Heath
    hard-sectored disk image formats, h17disk and h8d_dsk.  If the two
    formats disagree on anything here, the same disk loads differently
    depending on which file format holds it, and nothing reports the
    difference.  Change these values here, not with a local copy in one
    format.

    See https://heathkit.garlanger.com/diskformats/ for the H17Disk
    specification, which also documents the physical format.

*********************************************************************/
#ifndef MAME_FORMATS_H17_COMMON_H
#define MAME_FORMATS_H17_COMMON_H

#pragma once

#include "flopimg.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>


namespace heath_h17 {

// Fixed parameters for all Heath hard-sectored disks: 10 sectors per track,
// 256 data bytes per sector, 300 RPM, FM encoding.
constexpr int SECTORS_PER_TRACK = 10;
constexpr int SECTOR_DATA_SIZE  = 256;

// Cell timings.  fm_w() writes each data bit as two half-cells of
// BITCELL_SIZE ns, so TRACK_SIZE half-cells fill one 200ms revolution of a
// 300 RPM drive.
//
// These values come from the controller, not from the nominal 250 kbps of
// single density.  The H17 controller has no PLL and reads at a fixed rate
// (see heath_h17_fdc_base_device::rx_timer_cb()), so a generated track must
// run at that same rate.  The card's USRT runs at 2.048MHz / 16 = 128kHz and
// sends one bit per clock.  The divider is U810 on the H-88-1 schematic, a
// 4-bit counter with its control pins tied to +5V so it runs freely.  So a
// bit cell is 7812.5ns, a revolution holds 25600 bits, a track holds 3200
// bytes, and a sector holds 320.  The sector layout that
// heath_h17_format::write_sector() writes fits 320 bytes, not the 312.5 that
// 250 kbps would give: 290 bytes of leading zeros, header, data and
// checksums, then about 30 bytes of trailing zeros.
//
// Only TRACK_SIZE sets the rate of a generated track.
// generate_track_from_levels() scales its buffer to one 200ms revolution, so
// each half-cell comes out at 200ms / 51200 = 3906.25ns whatever unit the
// buffer used.  BITCELL_SIZE is the unit the buffer is built in.  It is also
// the cell size generate_bitstream_from_track() samples at when saving.
// 3906ns is 0.006% short of 3906.25ns, much less than a real drive's speed
// varies.
constexpr int TRACK_SIZE   = 51'200;
constexpr int BITCELL_SIZE = 3906;

// Sync byte before both the header and the data of every sector.
constexpr uint8_t H17_SYNC_BYTE = 0xfd;


// The disk layouts the H17 controller uses.  variant is the hard-sectored
// variant an image of this layout loads as, and drive_variant is the matching
// soft-sectored variant.  is_compatible() accepts a drive reporting either.
struct format {
	int      head_count;
	int      track_count;
	uint32_t variant;
	uint32_t drive_variant;
};

// The order matters.  find_format() in h8d_dsk.cpp takes the first layout of
// the right file size, and DSSD and SSQD are both 200K.  find_format() below
// takes the first layout that holds the image, so the list runs from smallest
// to largest.
inline constexpr format formats[] = {
	{ 1, 40, floppy_image::SSSD10, floppy_image::SSSD }, // H-17-1
	{ 2, 40, floppy_image::DSSD10, floppy_image::DSSD },
	{ 1, 80, floppy_image::SSQD10, floppy_image::SSQD },
	{ 2, 80, floppy_image::DSQD10, floppy_image::DSQD }, // H-17-4
	{}
};

// Returns the number of data bytes a disk of this layout holds.
uint64_t format_size(format const &fmt);

// Returns true when the drive reports one of the layout's variants.  An empty
// list accepts any layout.  floptool passes an empty list.
bool is_compatible(format const &fmt, std::vector<uint32_t> const &variants);

// Returns where a sector sits in an image's data: the sides are interleaved
// one track at a time, so track 0 head 0 comes first, then track 0 head 1.
int sector_index(format const &fmt, int head, int track, int sector);


// Returns the layout save() writes an image as: the smallest layout that
// holds every formatted track.
//
// A blank image must still get a layout.  floppy_image_device::commit_image()
// truncates the file before it calls save() and ignores the result.  If
// save() returns false, the file is left with 0 bytes, and no format will
// identify it the next time it is mounted.  This returns no layout only for an
// image with more heads or tracks than the largest layout.
//
// A blank image has no formatted tracks, so its layout comes from the variant
// that unformatted_image::format() set when the image was created.  The H17
// controller writes single density on any media, so a double density variant
// gets the single density layout with the same heads and tracks.  A blank
// image with no variant gets the smallest layout.
format find_format(floppy_image const &image);


// H17 checksum over a run of bytes, as the ROM's RDB/WNB routines compute it.
// The sync byte is not part of the sum.
uint8_t h17_checksum(uint8_t const *data, size_t length);

// Header checksum, taken over the volume, track and sector bytes.
uint8_t h17_checksum(uint8_t volume, uint8_t track, uint8_t sector);


// Identifies an HDOS volume from its label sector and returns the volume
// number in lab_ser.  A disk counts as HDOS if it has either the copyright
// notice INIT writes or a known HDOS version byte.
//
// A missed HDOS disk silently becomes unreadable.  Every sector header on
// tracks above 0 holds the volume number, and HDOS rejects a sector whose
// volume does not match the label.  A format that builds the sector headers
// itself writes volume 0 for a disk this rejects.
bool is_hdos(std::vector<uint8_t> const &img, uint8_t &lab_ser);


// Reverses the bit order of a byte.  The disk records each byte least
// significant bit first.
uint8_t reverse_byte(uint8_t val);


// Decodes one FM byte from a bitstream, starting at half-cell pos.  The data
// half-cells are the odd entries.
bool fm_byte_from_bitstream(std::vector<bool> const &bitstream, size_t pos, uint8_t &val);


// How many bytes after a sector header decode_track() searches for the data
// sync byte.  When the emulated machine rewrites a sector, it writes only the
// data field.  The write starts wherever the head is after the machine reads
// the header and turns on the write gate, so the gap before the data can
// differ from the 12 zero bytes write_sector() writes.  A sector is 320 bytes
// and the data field uses 258 of them, so a gap longer than this cannot fit.
constexpr int MAX_HEADER_TO_DATA_BYTES = 54;


// One sector as found on a track.
struct sector_read
{
	bool    found           = false;  // a header, and a data field to go with it
	bool    data_valid      = false;  // the data checksum matched
	uint8_t volume          = 0;
	uint8_t track           = 0;
	uint8_t sector          = 0;
	uint8_t header_checksum = 0;
	uint8_t data_checksum   = 0;

	std::array<uint8_t, SECTOR_DATA_SIZE> data{};
};


// Reads every sector on a track.  It finds each sector by its header, not by
// its position.  A sector the emulated machine rewrote starts wherever the
// head was, can shift the sectors after it, and can cross the index hole.  So
// the search covers the whole track and continues past the index.
//
// The data field is searched one bit at a time, not one byte at a time.  A
// rewritten data field need not line up on the same bit boundary as the
// header before it.
//
// Both checksums must match, so a sync byte inside sector data is not taken
// for the start of a sector.  A sector with a bad data checksum is still
// returned, because a real disk can hold one, and dropping it would silently
// replace its data with zeros.  A later copy with a good checksum replaces it.
void decode_track(std::vector<bool> const &bitstream, std::array<sector_read, SECTORS_PER_TRACK> &sectors);


// The fields of one sector, as heath_h17_format::generate_tracks() writes
// them to the track.  data points into the caller's image buffer, so that
// buffer must stay alive until generate_tracks() returns.
struct sector_fields
{
	std::array<uint8_t, 5> header{};  // sync, volume, track, sector, checksum
	uint8_t data_sync               = H17_SYNC_BYTE;
	uint8_t const *data             = nullptr;
	uint8_t data_checksum           = 0;
};

// Builds the fields of a sector whose header the image file does not store.
// The header holds the logical track, track * heads + head.  The volume is
// lab_ser on tracks above 0 of an HDOS disk, and 0 otherwise (see is_hdos()).
// Both checksums are calculated.  img holds the image's data, in the order
// sector_index() gives.
sector_fields make_sector(format const &fmt, std::vector<uint8_t> const &img, int head, int track, int sector,
		bool hdos, uint8_t lab_ser);

} // namespace heath_h17


// Base class for the Heath hard-sectored image formats.  It writes the H-17
// sector layout and reads it back, so every format that derives from it puts
// the same track on the disk.
class heath_h17_format : public floppy_image_format_t
{
protected:
	heath_h17_format() = default;

	// Builds every track of image from sectors, which holds one entry per
	// sector in sector_index() order, and sets the image's variant.
	//
	// Returns false for a layout with more heads or tracks than the drive.
	// The drive sets the size of the image's track array, and get_buffer()
	// checks its bounds only in a debug build, so without this check such a
	// disk would be written past the end of the array.
	bool generate_tracks(floppy_image &image, heath_h17::format const &fmt,
			std::vector<heath_h17::sector_fields> const &sectors) const;

	// Decodes every track of image and returns one entry per sector, in
	// sector_index() order.  An entry whose found is false is a sector that
	// would not decode; the caller saves it as zeros.
	std::vector<heath_h17::sector_read> read_sectors(floppy_image const &image, heath_h17::format const &fmt) const;

private:
	// Writes one byte to a track buffer as FM, least significant bit first.
	void fm_reverse_byte_w(std::vector<uint32_t> &buffer, uint8_t val) const;

	// Appends one sector to a track buffer: 15 zero bytes, the 5 header bytes
	// (sync, volume, track, sector, checksum), 12 zero bytes, the data sync
	// byte, the 256 data bytes and the data checksum.  Then pads with zeros to
	// the end of the slot for the slot-th sector hole.
	void write_sector(std::vector<uint32_t> &buffer, int slot, uint8_t const *header,
			uint8_t data_sync, uint8_t const *data, uint8_t data_checksum) const;
};

#endif // MAME_FORMATS_H17_COMMON_H
