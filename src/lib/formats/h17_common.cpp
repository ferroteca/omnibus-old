// license:BSD-3-Clause
// copyright-holders:Mark Garlanger, Paul Galbraith
/*********************************************************************

    formats/h17_common.cpp

    Code shared by the Heath hard-sectored disk image formats.

*********************************************************************/

#include "h17_common.h"

#include "imageutl.h"


namespace heath_h17 {

uint64_t format_size(format const &fmt)
{
	return uint64_t(fmt.head_count) * fmt.track_count * SECTORS_PER_TRACK * SECTOR_DATA_SIZE;
}

bool is_compatible(format const &fmt, std::vector<uint32_t> const &variants)
{
	if (variants.empty())
	{
		return true;
	}

	for (uint32_t variant : variants)
	{
		if ((variant == fmt.variant) || (variant == fmt.drive_variant))
		{
			return true;
		}
	}

	return false;
}

int sector_index(format const &fmt, int head, int track, int sector)
{
	return ((track * fmt.head_count) + head) * SECTORS_PER_TRACK + sector;
}

// D = RLCA(byte XOR D) per byte.  The sync byte resets D to 0 before the
// header or data bytes, so it is not part of the sum.
uint8_t h17_checksum(uint8_t const *data, size_t length)
{
	uint8_t d = 0;

	for (size_t i = 0; i < length; i++)
	{
		uint8_t const x = d ^ data[i];
		d = uint8_t((x << 1) | (x >> 7));  // RLCA
	}

	return d;
}

uint8_t h17_checksum(uint8_t volume, uint8_t track, uint8_t sector)
{
	uint8_t const data[3] = { volume, track, sector };

	return h17_checksum(data, sizeof(data));
}

bool is_hdos(std::vector<uint8_t> const &img, uint8_t &lab_ser)
{
	if (img.size() < 0x0a00)
	{
		return false;
	}

	uint8_t const *const label = &img[0x0900]; // track 0, sector 9

	// LAB.VLT: 0 = data, 1 = system, 2 = no directory.
	if (label[0x08] > 2)
	{
		return false;
	}

	// INIT writes a copyright notice at a fixed offset after the label text.
	// Every disk checked so far has it, including data volumes and third party
	// software.  HDOS 3.x changed the wording from "SYSTEM COPYRIGHT HEATH CO.,
	// 10/1977" to "System Copyright (c) Heath Co., 19xx", so compare only the
	// first 16 characters and ignore case.
	static char const marker[] = "SYSTEM COPYRIGHT";

	bool found = (label[0x54] == '\r') && (label[0x55] == '\n');

	for (int i = 0; found && (i < int(sizeof(marker) - 1)); i++)
	{
		uint8_t const c = label[0x56 + i];

		found = (((c >= 'a') && (c <= 'z')) ? uint8_t(c - 0x20) : c) == marker[i];
	}

	// Fall back to the version byte for a disk without the notice.  The
	// version byte cannot be the only test: HDOS 1.0 writes 0 there, and so
	// does a sector that was never written.
	uint8_t const lab_ver = label[0x09];

	if (!found && (lab_ver != 0x15) && (lab_ver != 0x16) && (lab_ver != 0x20) && ((lab_ver < 0x30) || (lab_ver > 0x39)))
	{
		return false;
	}

	lab_ser = label[0x00];

	return true;
}

uint8_t reverse_byte(uint8_t val)
{
	static constexpr uint8_t lookup[16] = { 0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe, 0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf };

	return lookup[val & 0x0f] << 4 | lookup[val >> 4];
}

sector_fields make_sector(format const &fmt, std::vector<uint8_t> const &img, int head, int track, int sector,
		bool hdos, uint8_t lab_ser)
{
	// See https://sebhc.github.io/sebhc/project8080/design_h17.html for the
	// H-17 logical track and track 0 volume rules.
	uint8_t const logical_track = uint8_t((track * fmt.head_count) + head);
	uint8_t const volume = (hdos && (logical_track > 0)) ? lab_ser : 0;

	sector_fields fields;

	fields.header        = { H17_SYNC_BYTE, volume, logical_track, uint8_t(sector), h17_checksum(volume, logical_track, uint8_t(sector)) };
	fields.data_sync     = H17_SYNC_BYTE;
	fields.data          = &img[size_t(sector_index(fmt, head, track, sector)) * SECTOR_DATA_SIZE];
	fields.data_checksum = h17_checksum(fields.data, SECTOR_DATA_SIZE);

	return fields;
}

} // namespace heath_h17


bool heath_h17_format::generate_tracks(floppy_image &image, heath_h17::format const &fmt,
		std::vector<heath_h17::sector_fields> const &sectors) const
{
	int max_tracks, max_heads;
	image.get_maximal_geometry(max_tracks, max_heads);

	if ((fmt.head_count > max_heads) || (fmt.track_count > max_tracks))
	{
		LOG_FORMATS("%d head %d track image does not fit a %d head %d track drive\n", fmt.head_count, fmt.track_count, max_heads, max_tracks);

		return false;
	}

	image.set_variant(fmt.variant);

	std::vector<uint32_t> buf;

	for (int head = 0; head < fmt.head_count; head++)
	{
		for (int track = 0; track < fmt.track_count; track++)
		{
			for (int sector = 0; sector < heath_h17::SECTORS_PER_TRACK; sector++)
			{
				heath_h17::sector_fields const &fields = sectors[heath_h17::sector_index(fmt, head, track, sector)];

				write_sector(buf, sector, fields.header.data(), fields.data_sync, fields.data, fields.data_checksum);
			}

			generate_track_from_levels(track, head, buf, 0, image);
			buf.clear();
		}
	}

	return true;
}


void heath_h17_format::fm_reverse_byte_w(std::vector<uint32_t> &buffer, uint8_t val) const
{
	fm_w(buffer, 8, heath_h17::reverse_byte(val), heath_h17::BITCELL_SIZE);
}

void heath_h17_format::write_sector(std::vector<uint32_t> &buffer, int slot, uint8_t const *header,
		uint8_t data_sync, uint8_t const *data, uint8_t data_checksum) const
{
	for (int i = 0; i < 15; i++)
	{
		fm_reverse_byte_w(buffer, 0);
	}

	for (int i = 0; i < 5; i++)
	{
		fm_reverse_byte_w(buffer, header[i]);
	}

	for (int i = 0; i < 12; i++)
	{
		fm_reverse_byte_w(buffer, 0);
	}

	fm_reverse_byte_w(buffer, data_sync);

	for (int i = 0; i < heath_h17::SECTOR_DATA_SIZE; i++)
	{
		fm_reverse_byte_w(buffer, data[i]);
	}

	fm_reverse_byte_w(buffer, data_checksum);

	// trailing zeros up to the next sector hole, usually about 30 bytes
	while (buffer.size() < heath_h17::TRACK_SIZE / heath_h17::SECTORS_PER_TRACK * (slot + 1))
	{
		fm_reverse_byte_w(buffer, 0);
	}
}
