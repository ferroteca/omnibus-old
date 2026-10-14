// license:BSD-3-Clause
// copyright-holders:Paul Galbraith
/*********************************************************************

Heath H8D disk image format

Raw-sector disk images for Heath hard-sectored floppy controllers.

See https://sebhc.github.io/sebhc/h8d.html for the raw sector layout.

*********************************************************************/
#ifndef MAME_FORMATS_H8D_DSK_H
#define MAME_FORMATS_H8D_DSK_H

#pragma once

#include "h17_common.h"

class heath_h8d_format : public heath_h17_format
{
public:
	heath_h8d_format();

	int identify(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants) const override;
	bool load(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants, floppy_image &image) const override;
	bool save(util::random_read_write &io, const std::vector<uint32_t> &variants, const floppy_image &image) const override;

	const char *name() const noexcept override { return "h8d"; }
	const char *description() const noexcept override { return "Heath H8D disk image"; }
	const char *extensions() const noexcept override { return "h8d"; }
	bool supports_save() const noexcept override { return true; }
};

extern const heath_h8d_format FLOPPY_H8D_FORMAT;

#endif // MAME_FORMATS_H8D_DSK_H
