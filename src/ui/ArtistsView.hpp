/*
 * Copyright (C) 2018 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <Wt/WContainerWidget>
#include <Wt/WLineEdit>
#include <Wt/WSignal>

namespace UserInterface {

class Filters;

class Artists : public Wt::WContainerWidget
{
	public:
		Artists(Filters* filters, Wt::WContainerWidget* parent = 0);

		Wt::Signal<Database::id_type> artistAdd;
		Wt::Signal<Database::id_type> artistPlay;

	private:
		void refresh();
		void add_some();

		Filters* _filters;
		Wt::WTemplate* _showMore;
		Wt::WLineEdit* _search;
		Wt::WContainerWidget* _artistsContainer;
};

} // namespace UserInterface

