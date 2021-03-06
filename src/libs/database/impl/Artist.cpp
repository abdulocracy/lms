/*
 * Copyright (C) 2015 Emeric Poupon
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
#include "database/Artist.hpp"

#include <Wt/Dbo/WtSqlTraits.h>

#include "database/Cluster.hpp"
#include "database/Release.hpp"
#include "database/Session.hpp"
#include "database/Track.hpp"
#include "database/User.hpp"
#include "utils/Logger.hpp"
#include "SqlQuery.hpp"


namespace Database
{

Artist::Artist(const std::string& name, const std::optional<UUID>& MBID)
: _name {std::string(name, 0 , _maxNameLength)},
_sortName {_name},
_MBID {MBID ? MBID->getAsString() : ""}
{

}

std::vector<Artist::pointer>
Artist::getByName(Session& session, const std::string& name)
{
	session.checkSharedLocked();

	Wt::Dbo::collection<Artist::pointer> res = session.getDboSession().find<Artist>().where("name = ?").bind( std::string{name, 0, _maxNameLength} );
	return std::vector<Artist::pointer>(res.begin(), res.end());
}

Artist::pointer
Artist::getByMBID(Session& session, const UUID& mbid)
{
	session.checkSharedLocked();
	return session.getDboSession().find<Artist>().where("mbid = ?").bind(std::string {mbid.getAsString()});
}

Artist::pointer
Artist::getById(Session& session, IdType id)
{
	session.checkSharedLocked();
	return session.getDboSession().find<Artist>().where("id = ?").bind(id);
}

Artist::pointer
Artist::create(Session& session, const std::string& name, const std::optional<UUID>& MBID)
{
	session.checkUniqueLocked();

	Artist::pointer res {session.getDboSession().add(std::make_unique<Artist>(name, MBID))};
	session.getDboSession().flush();

	return res;
}

static
Wt::Dbo::Query<Artist::pointer>
getQuery(Session& session,
		const std::set<IdType>& clusterIds,
		const std::vector<std::string>& keywords,
		std::optional<TrackArtistLink::Type> linkType,
		Artist::SortMethod sortMethod)
{
	session.checkSharedLocked();

	WhereClause where;

	std::ostringstream oss;
	oss << "SELECT DISTINCT a FROM artist a";

	if (!keywords.empty())
	{
		WhereClause whereKeywordsName;
		WhereClause whereKeywordsSortName;

		for (auto keyword : keywords)
		{
			whereKeywordsName.And(WhereClause("a.name LIKE ?")).bind("%%" + keyword + "%%");
			whereKeywordsSortName.And(WhereClause("a.sort_name LIKE ?")).bind("%%" + keyword + "%%");
		}

		where.And(whereKeywordsName.Or(whereKeywordsSortName));
	}

	if (!clusterIds.empty() || linkType)
	{
		oss << " INNER JOIN track t ON t.id = t_a_l.track_id INNER JOIN track_artist_link t_a_l ON t_a_l.artist_id = a.id";

		if (!clusterIds.empty())
		{
			oss << "  INNER JOIN cluster c ON c.id = t_c.cluster_id INNER JOIN track_cluster t_c ON t_c.track_id = t.id";
			WhereClause clusterClause;

			for (auto id : clusterIds)
				clusterClause.Or(WhereClause("c.id = ?")).bind(std::to_string(id));

			where.And(clusterClause);
		}
		if (linkType)
			where.And(WhereClause {"t_a_l.type = ?"}.bind(std::to_string(static_cast<int>(*linkType))));
	}
	oss << " " << where.get();

	if (!clusterIds.empty())
		oss << " GROUP BY t.id HAVING COUNT(DISTINCT c.id) = " << clusterIds.size();

	switch (sortMethod)
	{
		case Artist::SortMethod::None:
			break;
		case Artist::SortMethod::ByName:
			oss << " ORDER BY a.name COLLATE NOCASE";
			break;
		case Artist::SortMethod::BySortName:
			oss << " ORDER BY a.sort_name COLLATE NOCASE";
			break;
	}

	Wt::Dbo::Query<Artist::pointer> query = session.getDboSession().query<Artist::pointer>( oss.str() );

	for (const std::string& bindArg : where.getBindArgs())
	{
		query.bind(bindArg);
	}

	return query;
}

std::vector<Artist::pointer>
Artist::getAll(Session& session, SortMethod sortMethod, std::optional<std::size_t> offset, std::optional<std::size_t> size)
{
	session.checkSharedLocked();

	Wt::Dbo::collection<Artist::pointer> res = getQuery(session, {}, {}, std::nullopt, sortMethod)
		.limit(size ? static_cast<int>(*size) + 1 : -1)
		.offset(offset ? static_cast<int>(*offset) : -1);

	return std::vector<pointer>(res.begin(), res.end());
}

std::vector<IdType>
Artist::getAllIds(Session& session)
{
	session.checkSharedLocked();

	Wt::Dbo::collection<IdType> res = session.getDboSession().query<IdType>("SELECT id FROM artist");
	return std::vector<IdType>(res.begin(), res.end());
}

std::vector<Artist::pointer>
Artist::getAllOrphans(Session& session)
{
	session.checkSharedLocked();
	Wt::Dbo::collection<Wt::Dbo::ptr<Artist>> res {session.getDboSession().query<Wt::Dbo::ptr<Artist>>("SELECT DISTINCT a FROM artist a WHERE NOT EXISTS(SELECT 1 FROM track t INNER JOIN track_artist_link t_a_l ON t_a_l.artist_id = a.id WHERE t.id = t_a_l.track_id)")};

	return std::vector<pointer>(res.begin(), res.end());
}

std::vector<IdType>
Artist::getAllIdsWithClusters(Session& session, std::optional<std::size_t> limit)
{
	session.checkSharedLocked();

	Wt::Dbo::collection<IdType> res = session.getDboSession().query<IdType>
		("SELECT DISTINCT a.id FROM artist a"
			" INNER JOIN track t ON t.id = t_a_l.track_id INNER JOIN track_artist_link t_a_l ON t_a_l.artist_id = a.id"
			" INNER JOIN track_cluster t_c ON t_c.track_id = t.id")
		.limit(limit ? static_cast<int>(*limit) : -1);

	return std::vector<IdType>(res.begin(), res.end());
}

std::vector<Artist::pointer>
Artist::getByClusters(Session& session, const std::set<IdType>& clusters, SortMethod sortMethod)
{
	assert(!clusters.empty());

	session.checkSharedLocked();
	bool more;
	return getByFilter(session, clusters, {}, {}, sortMethod, {}, {}, more);
}

std::vector<Artist::pointer>
Artist::getByFilter(Session& session,
		const std::set<IdType>& clusters,
		const std::vector<std::string>& keywords,
		std::optional<TrackArtistLink::Type> linkType,
		SortMethod sortMethod,
		std::optional<std::size_t> offset,
		std::optional<std::size_t> size,
		bool& moreResults)
{
	session.checkSharedLocked();
	Wt::Dbo::collection<Artist::pointer> collection = getQuery(session, clusters, keywords, linkType, sortMethod)
		.limit(size ? static_cast<int>(*size) + 1 : -1)
		.offset(offset ? static_cast<int>(*offset) : -1);

	auto res {std::vector<pointer>(collection.begin(), collection.end())};

	if (size && res.size() == static_cast<std::size_t>(*size) + 1)
	{
		moreResults = true;
		res.pop_back();
	}
	else
		moreResults = false;

	return res;
}

std::vector<Artist::pointer>
Artist::getLastAdded(Session& session, Wt::WDateTime after, std::optional<std::size_t> limit)
{
	session.checkSharedLocked();
	Wt::Dbo::collection<Artist::pointer> res = session.getDboSession().query<Artist::pointer>("SELECT a from artist a INNER JOIN track_artist_link t_a_l ON t_a_l.artist_id = a.id INNER JOIN track t ON t.id = t_a_l.track_id")
		.where("t.file_added > ?").bind(after)
		.groupBy("a.id")
		.orderBy("t.file_added DESC")
		.limit(limit ? static_cast<int>(*limit) : -1);

	return std::vector<pointer>(res.begin(), res.end());
}

std::vector<Wt::Dbo::ptr<Release>>
Artist::getReleases(const std::set<IdType>& clusterIds) const
{
	assert(self());
	assert(IdIsValid(self()->id()));
	assert(session());

	WhereClause where;

	std::ostringstream oss;

	oss << "SELECT DISTINCT r FROM release r INNER JOIN artist a ON a.id = t_a_l.artist_id INNER JOIN track_artist_link t_a_l ON t_a_l.track_id = t.id INNER JOIN track t ON t.release_id = r.id";

	if (!clusterIds.empty())
	{
		oss << " INNER JOIN cluster c ON c.id = t_c.cluster_id INNER JOIN track_cluster t_c ON t_c.track_id = t.id";

		WhereClause clusterClause;

		for (auto id : clusterIds)
			clusterClause.Or(WhereClause("c.id = ?")).bind(std::to_string(id));

		where.And(clusterClause);
	}

	where.And(WhereClause("a.id = ?")).bind(std::to_string(id()));

	oss << " " << where.get();

	if (!clusterIds.empty())
		oss << " GROUP BY t.id HAVING COUNT(DISTINCT c.id) = " << clusterIds.size();

	oss << " ORDER BY t.year,r.name";

	Wt::Dbo::Query<Release::pointer> query = session()->query<Release::pointer>( oss.str() );

	for (const std::string& bindArg : where.getBindArgs())
		query.bind(bindArg);

	Wt::Dbo::collection<Wt::Dbo::ptr<Release>> res = query;

	return std::vector<Wt::Dbo::ptr<Release>>(res.begin(), res.end());
}

std::size_t
Artist::getReleaseCount() const
{
	assert(self());
	assert(IdIsValid(self()->id()));
	assert(session());

	int res = session()->query<int>("SELECT COUNT(DISTINCT r.id) FROM release r INNER JOIN artist a ON a.id = t_a_l.artist_id INNER JOIN track_artist_link t_a_l ON t_a_l.track_id = t.id INNER JOIN track t ON t.release_id = r.id")
		.where("a.id = ?").bind(self()->id());
	return res;
}

std::vector<Wt::Dbo::ptr<Track>>
Artist::getTracks(std::optional<TrackArtistLink::Type> linkType) const
{
	assert(self());
	assert(IdIsValid(self()->id()));
	assert(session());

	auto query {session()->query<Wt::Dbo::ptr<Track>>("SELECT DISTINCT t FROM track t INNER JOIN artist a ON a.id = t_a_l.artist_id INNER JOIN track_artist_link t_a_l ON t_a_l.track_id = t.id")
		.where("a.id = ?").bind(self()->id())
		.orderBy("t.year,t.release_id,t.disc_number,t.track_number")};

	if (linkType)
		query.where("t_a_l.type = ?").bind(*linkType);

	Wt::Dbo::collection<Wt::Dbo::ptr<Track>> tracks {query.resultList()};

	return std::vector<Wt::Dbo::ptr<Track>>(tracks.begin(), tracks.end());
}

std::vector<Wt::Dbo::ptr<Track>>
Artist::getTracksWithRelease(std::optional<TrackArtistLink::Type> linkType) const
{
	assert(self());
	assert(IdIsValid(self()->id()));
	assert(session());

	auto query {session()->query<Wt::Dbo::ptr<Track>>("SELECT t FROM track t INNER JOIN artist a ON a.id = t_a_l.artist_id INNER JOIN track_artist_link t_a_l ON t_a_l.track_id = t.id INNER JOIN release r ON r.id = t.release_id")
		.where("a.id = ?").bind(self()->id())
		.orderBy("t.year,r.name,t.disc_number,t.track_number")};

	if (linkType)
		query.where("t_a_l.type = ?").bind(*linkType);

	Wt::Dbo::collection<Wt::Dbo::ptr<Track>> tracks {query.resultList()};

	return std::vector<Wt::Dbo::ptr<Track>>(tracks.begin(), tracks.end());
}


std::vector<Wt::Dbo::ptr<Track>>
Artist::getRandomTracks(std::optional<std::size_t> count) const
{
	assert(self());
	assert(IdIsValid(self()->id()));
	assert(session());

	Wt::Dbo::collection<Wt::Dbo::ptr<Track>> tracks {session()->query<Wt::Dbo::ptr<Track>>("SELECT t from track t INNER JOIN artist a ON a.id = t_a_l.artist_id INNER JOIN track_artist_link t_a_l ON t_a_l.track_id = t.id")
		.where("a.id = ?").bind(self()->id())
		.orderBy("RANDOM()")
		.limit(count ? static_cast<int>(*count) : -1)};

	return std::vector<Wt::Dbo::ptr<Track>>(tracks.begin(), tracks.end());
}

std::vector<Wt::Dbo::ptr<Artist>>
Artist::getSimilarArtists(std::optional<std::size_t> offset, std::optional<std::size_t> count) const
{
	assert(self());
	assert(IdIsValid(self()->id()));
	assert(session());

	Wt::Dbo::Query<pointer> query {session()->query<pointer>(
			"SELECT a FROM artist a"
			" INNER JOIN track_artist_link t_a_l ON t_a_l.artist_id = a.id"
			" INNER JOIN track t ON t.id = t_a_l.track_id"
			" INNER JOIN track_cluster t_c ON t_c.track_id = t.id"
				" WHERE "
					" t_c.cluster_id IN (SELECT c.id from cluster c INNER JOIN track t ON c.id = t_c.cluster_id INNER JOIN track_cluster t_c ON t_c.track_id = t.id INNER JOIN artist a ON a.id = t_a_l.artist_id INNER JOIN track_artist_link t_a_l ON t_a_l.track_id = t.id WHERE a.id = ?)"
					" AND a.id <> ?"
				)
		.bind(self()->id())
		.bind(self()->id())
		.groupBy("a.id")
		.orderBy("COUNT(*) DESC")
		.limit(count ? static_cast<int>(*count) : -1)
		.offset(offset ? static_cast<int>(*offset) : -1)};

	Wt::Dbo::collection<pointer> res = query;
	return std::vector<pointer>(res.begin(), res.end());
}

std::vector<std::vector<Wt::Dbo::ptr<Cluster>>>
Artist::getClusterGroups(std::vector<ClusterType::pointer> clusterTypes, std::size_t size) const
{
	assert(self());
	assert(IdIsValid(self()->id()));
	assert(session());

	WhereClause where;

	std::ostringstream oss;
	oss << "SELECT c FROM cluster c INNER JOIN track t ON c.id = t_c.cluster_id INNER JOIN track_cluster t_c ON t_c.track_id = t.id INNER JOIN cluster_type c_type ON c.cluster_type_id = c_type.id INNER JOIN artist a ON t_a_l.artist_id = a.id INNER JOIN track_artist_link t_a_l ON t_a_l.track_id = t.id";

	where.And(WhereClause("a.id = ?")).bind(std::to_string(self()->id()));
	{
		WhereClause clusterClause;
		for (auto clusterType : clusterTypes)
			clusterClause.Or(WhereClause("c_type.id = ?")).bind(std::to_string(clusterType.id()));

		where.And(clusterClause);
	}
	oss << " " << where.get();
	oss << "GROUP BY c.id ORDER BY COUNT(DISTINCT c.id) DESC";

	Wt::Dbo::Query<Cluster::pointer> query = session()->query<Cluster::pointer>( oss.str() );

	for (const std::string& bindArg : where.getBindArgs())
		query.bind(bindArg);

	Wt::Dbo::collection<Cluster::pointer> queryRes = query;

	std::map<IdType, std::vector<Cluster::pointer>> clusters;
	for (auto cluster : queryRes)
	{
		if (clusters[cluster->getType().id()].size() < size)
			clusters[cluster->getType().id()].push_back(cluster);
	}

	std::vector<std::vector<Cluster::pointer>> res;
	for (auto cluster_list : clusters)
		res.push_back(cluster_list.second);

	return res;
}

void
Artist::setSortName(const std::string& sortName)
{
	_sortName = std::string(sortName, 0 , _maxNameLength);
	LMS_LOG(DB, DEBUG) << "SORT NAME = '" << _sortName << "'";
}

} // namespace Database
