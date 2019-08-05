/*
 * Copyright (C) 2013 Emeric Poupon
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

#include "Session.hpp"

#include "utils/Exception.hpp"
#include "utils/Logger.hpp"

#include "Artist.hpp"
#include "Cluster.hpp"
#include "Release.hpp"
#include "ScanSettings.hpp"
#include "SimilaritySettings.hpp"
#include "Track.hpp"
#include "TrackArtistLink.hpp"
#include "TrackList.hpp"
#include "TrackFeatures.hpp"
#include "User.hpp"

namespace Database {

#define LMS_DATABASE_VERSION	6

using Version = std::size_t;

class VersionInfo
{
	public:
		using pointer = Wt::Dbo::ptr<VersionInfo>;

		static VersionInfo::pointer getOrCreate(Session& session)
		{
			session.checkUniqueLocked();

			pointer versionInfo {session.getDboSession().find<VersionInfo>()};
			if (!versionInfo)
				return session.getDboSession().add(std::make_unique<VersionInfo>());

			return versionInfo;
		}

		static VersionInfo::pointer get(Session& session)
		{
			session.checkSharedLocked();

			return session.getDboSession().find<VersionInfo>();
		}

		Version getVersion() const { return _version; }
		void setVersion(Version version) { _version = static_cast<int>(version); }

		template<class Action>
		void persist(Action& a)
		{
			Wt::Dbo::field(a, _version, "db_version");
		}

	private:
		int _version {LMS_DATABASE_VERSION};
};

void
Session::doDatabaseMigrationIfNeeded()
{
	auto uniqueTransaction {createUniqueTransaction()};

	static const std::string outdatedMsg {"Outdated database, please rebuild it (delete the lms.db file and restart)"};

	Version version;
	try
	{
		version = VersionInfo::getOrCreate(*this)->getVersion();
		LMS_LOG(DB, INFO) << "Database version = " << version;
		if (version == LMS_DATABASE_VERSION)
			return;
	}
	catch (std::exception& e)
	{
		LMS_LOG(DB, ERROR) << "Cannot get database version info: " << e.what();
		throw LmsException {outdatedMsg};
	}

	switch (version)
	{
		case 5:
			_session.execute("ALTER TABLE auth_token ADD usage TEXT NOT NULL DEFAULT \"LmsUI\"");
			_session.execute("ALTER TABLE auth_token DROP CONSTRAINT usage");
			break;

		default:
			LMS_LOG(DB, ERROR) << "Database version " << version << " cannot be handled using migration";
			throw LmsException {outdatedMsg};
	}

	VersionInfo::get(*this).modify()->setVersion(LMS_DATABASE_VERSION);
}

Session::Session(std::shared_timed_mutex& mutex, Wt::Dbo::SqlConnectionPool& connectionPool)
: _mutex {mutex}
{
	_session.setConnectionPool(connectionPool);

	_session.mapClass<VersionInfo>("version_info");
	_session.mapClass<Artist>("artist");
	_session.mapClass<AuthToken>("auth_token");
	_session.mapClass<Cluster>("cluster");
	_session.mapClass<ClusterType>("cluster_type");
	_session.mapClass<Release>("release");
	_session.mapClass<ScanSettings>("scan_settings");
	_session.mapClass<SimilaritySettings>("similarity_settings");
	_session.mapClass<SimilaritySettingsFeature>("similarity_settings_feature");
	_session.mapClass<Track>("track");
	_session.mapClass<TrackArtistLink>("track_artist_link");
	_session.mapClass<TrackFeatures>("track_features");
	_session.mapClass<TrackList>("tracklist");
	_session.mapClass<TrackListEntry>("tracklist_entry");
	_session.mapClass<User>("user");
}


enum class OwnedLock
{
	None,
	Shared,
	Unique,
};

static thread_local std::map<std::shared_timed_mutex*, OwnedLock> lockDebug;

UniqueTransaction::UniqueTransaction(std::shared_timed_mutex& mutex, Wt::Dbo::Session& session)
: _lock {mutex},
 _transaction {session}
{
	assert(lockDebug[_lock.mutex()] == OwnedLock::None);
	lockDebug[_lock.mutex()] = OwnedLock::Unique;
}

UniqueTransaction::~UniqueTransaction()
{
	assert(lockDebug[_lock.mutex()] == OwnedLock::Unique);
	lockDebug[_lock.mutex()] = OwnedLock::None;
}

SharedTransaction::SharedTransaction(std::shared_timed_mutex& mutex, Wt::Dbo::Session& session)
: _lock {mutex},
 _transaction {session}
{
	assert(lockDebug[_lock.mutex()] == OwnedLock::None);
	lockDebug[_lock.mutex()] = OwnedLock::Shared;
}

SharedTransaction::~SharedTransaction()
{
	assert(lockDebug[_lock.mutex()] == OwnedLock::Shared);
	lockDebug[_lock.mutex()] = OwnedLock::None;
}

void
Session::checkUniqueLocked()
{
	assert(lockDebug[&_mutex] == OwnedLock::Unique);
}

void
Session::checkSharedLocked()
{
	assert(lockDebug[&_mutex] != OwnedLock::None);
}

std::unique_ptr<UniqueTransaction>
Session::createUniqueTransaction()
{
	return std::unique_ptr<UniqueTransaction>(new UniqueTransaction{_mutex, _session});
}

std::unique_ptr<SharedTransaction>
Session::createSharedTransaction()
{
	return std::unique_ptr<SharedTransaction>(new SharedTransaction{_mutex, _session});
}

void
Session::prepareTables()
{
	// Creation case
	try {
	        _session.createTables();

		LMS_LOG(DB, INFO) << "Tables created";
	}
	catch (Wt::Dbo::Exception& e)
	{
		LMS_LOG(DB, ERROR) << "Cannot create tables: " << e.what();
	}

	doDatabaseMigrationIfNeeded();

	// Indexes
	{
		auto uniqueTransaction {createUniqueTransaction()};
		_session.execute("CREATE INDEX IF NOT EXISTS artist_name_idx ON artist(name)");
		_session.execute("CREATE INDEX IF NOT EXISTS artist_sort_name_nocase_idx ON artist(sort_name COLLATE NOCASE)");
		_session.execute("CREATE INDEX IF NOT EXISTS artist_mbid_idx ON artist(mbid)");
		_session.execute("CREATE INDEX IF NOT EXISTS auth_token_user_idx ON auth_token(user_id)");
		_session.execute("CREATE INDEX IF NOT EXISTS auth_token_expiry_idx ON auth_token(expiry)");
		_session.execute("CREATE INDEX IF NOT EXISTS auth_token_usage_idx ON auth_token(usage)");
		_session.execute("CREATE INDEX IF NOT EXISTS auth_token_usage_value_idx ON auth_token(usage, value)");
		_session.execute("CREATE INDEX IF NOT EXISTS cluster_name_idx ON cluster(name)");
		_session.execute("CREATE INDEX IF NOT EXISTS cluster_cluster_type_idx ON cluster(cluster_type_id)");
		_session.execute("CREATE INDEX IF NOT EXISTS cluster_type_name_idx ON cluster_type(name)");
		_session.execute("CREATE INDEX IF NOT EXISTS release_name_idx ON release(name)");
		_session.execute("CREATE INDEX IF NOT EXISTS release_name_nocase_idx ON release(name COLLATE NOCASE)");
		_session.execute("CREATE INDEX IF NOT EXISTS release_mbid_idx ON release(mbid)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_path_idx ON track(file_path)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_name_idx ON track(name)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_name_nocase_idx ON track(name COLLATE NOCASE)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_mbid_idx ON track(mbid)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_release_idx ON track(release_id)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_year_idx ON track(year)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_original_year_idx ON track(original_year)");
		_session.execute("CREATE INDEX IF NOT EXISTS tracklist_name_idx ON tracklist(name)");
		_session.execute("CREATE INDEX IF NOT EXISTS tracklist_user_idx ON tracklist(user_id)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_features_track_idx ON track_features(track_id)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_artist_link_artist_idx ON track_artist_link(artist_id)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_artist_link_name_idx ON track_artist_link(name)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_artist_link_track_idx ON track_artist_link(track_id)");
		_session.execute("CREATE INDEX IF NOT EXISTS track_artist_link_type_idx ON track_artist_link(type)");
	}

	// Initial settings tables
	{
		auto uniqueTransaction {createUniqueTransaction()};

		ScanSettings::init(*this);
		SimilaritySettings::init(*this);
	}
}

void
Session::optimize()
{
	auto uniqueTransaction {createUniqueTransaction()};

	_session.execute("ANALYZE");
}

} // namespace Database
