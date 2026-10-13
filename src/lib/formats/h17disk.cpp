// license:BSD-3-Clause
// copyright-holders:Mark Garlanger
/*********************************************************************

Heath H17D disk image format (version 2.0.0)

   Format for Heath hard-sectored 5.25" disk images.

   See https://heathkit.garlanger.com/diskformats/ for more information

   TODO - implement writing to H17D image

*********************************************************************/

#include "h17disk.h"

#include "imageutl.h"

#include "ioprocs.h"

#include <algorithm>
#include <cstring>


namespace {

using heath_h17::SECTORS_PER_TRACK;
using heath_h17::SECTOR_DATA_SIZE;
using heath_h17::format;
using heath_h17::format_size;
using heath_h17::formats;
using heath_h17::sector_index;

constexpr int SECTOR_METADATA_SIZE = 16;

struct block_header {
	uint32_t block_name;
	uint32_t length;
};

enum {
	DskF  = 0x466b7344, //!< "DskF", Disk Format
	Parm  = 0x6b726150, //!< "Parm", Parameters
	Date  = 0x65746144, //!< "Date", Date
	Imgr  = 0x72676d49, //!< "Imgr", Imager

	Prog  = 0x676f7250, //!< "Prog", Program (creation)
	Padd  = 0x64646150, //!< "Padd", Padding
	H8DB  = 0x42443848, //!< "H8DB", H8D data block
	SecM  = 0x4d636553, //!< "SecM", Sector Metadata
	Labl  = 0x6c62614c, //!< "Labl", Label
	Comm  = 0x6d6d6f43, //!< "Comm", Comment
};

std::pair<std::uint64_t, std::size_t> find_block(util::random_read &io, uint32_t block_id)
{
	LOG_FORMATS("find_block: 0x%x\n", block_id);

	// start of file
	int          pos    = 0;
	block_header header = { 0, 0 };

	do
	{
		pos += header.length + 8;
		auto const [err, actual] = read_at(io, pos, (void *) &header, 8);
		if (err || actual !=8)
		{
			return std::make_pair(0, 0);
		}
		header.length = swapendian_int32(header.length);
	}
	while (header.block_name != block_id);

	// update position to point to data portion of the block
	return std::make_pair(pos + 8, header.length);
}

format find_format(util::random_read &io)
{
	auto const [pos, length] = find_block(io, DskF);
	if ((pos == 0) || (length < 2) || (length > 3))
	{
		LOG_FORMATS("Can't find valid DskF block %d/%d\n", pos, length);

		return {};
	}

	uint8_t buf[3];

	auto const [err, actual] = read_at(io, pos, buf, length);
	if (err || (actual != length))
	{
		LOG_FORMATS("read error\n");

		return {};
	}

	int head_count  = buf[0];
	int track_count = buf[1];

	for (int i = 0; formats[i].head_count; i++)
	{
		if ((formats[i].head_count == head_count) && (formats[i].track_count == track_count))
		{
			LOG_FORMATS("find_format format found: %d - variant: 0x%x\n", i, formats[i].variant);

			return formats[i];
		}
	}

	LOG_FORMATS("Invalid disk format - heads: %d, tracks: %d\n", head_count, track_count);
	return {};
}

} // anonymous namespace


heath_h17d_format::heath_h17d_format() : heath_h17_format()
{
}

int heath_h17d_format::identify(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants) const
{
	uint8_t h[4];
	auto const [err, actual] = read_at(io, 0, h, 4);

	if (err || (actual != 4))
	{
		return 0;
	}

	// Verify "H17D" Signature.
	if ((h[0] == 0x48) && (h[1] == 0x31) && (h[2] == 0x37) && (h[3] == 0x44))
	{
		return FIFID_SIGN;
	}

	return 0;
}

bool heath_h17d_format::load(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants, floppy_image &image) const
{
	const format fmt = find_format(io);

	if (!fmt.head_count)
	{
		LOG_FORMATS("invalid format\n");

		return false;
	}

	auto const [secm_pos, secm_length] = find_block(io, SecM);

	// the sector data, in sector_index() order; sectors points into it
	std::vector<uint8_t> img(format_size(fmt));
	std::vector<heath_h17::sector_fields> sectors(img.size() / SECTOR_DATA_SIZE);

	uint8_t sector_meta_data[SECTOR_METADATA_SIZE];

	for (int head = 0; head < fmt.head_count; head++)
	{
		for (int track = 0; track < fmt.track_count; track++)
		{
			for (int sector = 0; sector < SECTORS_PER_TRACK; sector++)
			{
				int const index = sector_index(fmt, head, track, sector);
				int sect_meta_pos = index * SECTOR_METADATA_SIZE + secm_pos;

				auto const [err, actual] = read_at(io, sect_meta_pos, sector_meta_data, SECTOR_METADATA_SIZE);

				if (err || (actual != SECTOR_METADATA_SIZE))
				{
					LOG_FORMATS("unable to read sect meta data %d/%d/%d\n", head, track, sector);

					return false;
				}
				int data_offset = sector_meta_data[0] << 24 | sector_meta_data[1] << 16 | sector_meta_data[2] << 8 | sector_meta_data[3];

				uint8_t *const sector_data = &img[size_t(index) * SECTOR_DATA_SIZE];
				auto const [err2, actual2] = read_at(io, data_offset, sector_data, SECTOR_DATA_SIZE);

				if (err2 || (actual2 != SECTOR_DATA_SIZE))
				{
					LOG_FORMATS("unable to read sect data %d/%d/%d\n", head, track, sector);

					return false;
				}

				// SecM bytes 5-9 are the header (sync, volume, track, sector,
				// checksum), byte 10 the data sync and byte 11 the data checksum
				heath_h17::sector_fields &fields = sectors[index];

				std::copy(&sector_meta_data[5], &sector_meta_data[10], fields.header.begin());
				fields.data_sync     = sector_meta_data[10];
				fields.data          = sector_data;
				fields.data_checksum = sector_meta_data[11];
			}
		}
	}

	return generate_tracks(image, fmt, sectors);
}

const heath_h17d_format FLOPPY_H17D_FORMAT;
