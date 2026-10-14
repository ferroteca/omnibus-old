// license:BSD-3-Clause
// copyright-holders:Paul Galbraith
/*********************************************************************

Heath H8D disk image format

Raw-sector disk images for Heath hard-sectored floppy controllers.

See https://sebhc.github.io/sebhc/h8d.html for the raw sector layout.

*********************************************************************/

#include "h8d_dsk.h"

#include "h17_common.h"
#include "imageutl.h"

#include "ioprocs.h"

#include <algorithm>
#include <vector>


namespace {

using heath_h17::SECTORS_PER_TRACK;
using heath_h17::SECTOR_DATA_SIZE;
using heath_h17::format;
using heath_h17::format_size;
using heath_h17::formats;
using heath_h17::is_compatible;
using heath_h17::is_hdos;
using heath_h17::sector_index;

// An H8D file does not say which layout it holds, so pick one from the file
// size.  The 200K and 400K sizes are the double-sided images the H8D page
// describes, with the sides interleaved one track at a time.  400K needs 80
// tracks a side.  SSQD is also 200K, but DSSD comes first in formats[], so a
// 200K image loads as double sided unless the drive cannot hold it.
format find_format(uint64_t size, std::vector<uint32_t> const &variants)
{
	format first_size_match = {};

	for (int i = 0; formats[i].head_count; i++)
	{
		if (size == format_size(formats[i]))
		{
			if (!first_size_match.head_count)
			{
				first_size_match = formats[i];
			}

			if (is_compatible(formats[i], variants))
			{
				return formats[i];
			}
		}
	}

	return variants.empty() ? first_size_match : format{};
}


} // anonymous namespace


heath_h8d_format::heath_h8d_format() : heath_h17_format()
{
}

int heath_h8d_format::identify(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants) const
{
	uint64_t size;
	if (io.length(size))
	{
		return 0;
	}

	return find_format(size, variants).head_count ? FIFID_SIZE : 0;
}

bool heath_h8d_format::load(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants, floppy_image &image) const
{
	uint64_t size;
	if (io.length(size))
	{
		return false;
	}

	const format fmt = find_format(size, variants);

	if (!fmt.head_count)
	{
		LOG_FORMATS("invalid H8D format\n");

		return false;
	}

	std::vector<uint8_t> img(size);

	auto const [err, actual] = read_at(io, 0, img.data(), img.size());
	if (err || (actual != img.size()))
	{
		LOG_FORMATS("unable to read H8D image\n");

		return false;
	}

	uint8_t lab_ser = 0;
	bool const hdos = is_hdos(img, lab_ser);

	std::vector<heath_h17::sector_fields> sectors(size / SECTOR_DATA_SIZE);

	for (int head = 0; head < fmt.head_count; head++)
	{
		for (int track = 0; track < fmt.track_count; track++)
		{
			for (int sector = 0; sector < SECTORS_PER_TRACK; sector++)
			{
				sectors[sector_index(fmt, head, track, sector)] = heath_h17::make_sector(fmt, img, head, track, sector, hdos, lab_ser);
			}
		}
	}

	return generate_tracks(image, fmt, sectors);
}

bool heath_h8d_format::save(util::random_read_write &io, const std::vector<uint32_t> &variants, const floppy_image &image) const
{
	format const fmt = heath_h17::find_format(image);

	if (!fmt.head_count)
	{
		LOG_FORMATS("H8D save: no layout holds this image\n");
		return false;
	}

	std::vector<uint8_t> img(format_size(fmt), 0);
	std::vector<heath_h17::sector_read> const sectors = read_sectors(image, fmt);

	// a sector that would not decode keeps the zeros img starts with
	for (size_t index = 0; index < sectors.size(); index++)
	{
		if (sectors[index].found)
		{
			std::copy(sectors[index].data.begin(), sectors[index].data.end(), &img[index * SECTOR_DATA_SIZE]);
		}
	}

	auto const [err, actual] = write_at(io, 0, img.data(), img.size());
	return !err && (actual == img.size());
}

const heath_h8d_format FLOPPY_H8D_FORMAT;
