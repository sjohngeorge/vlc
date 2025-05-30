/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "mlplaylistmedia.hpp"
#include "mlhelper.hpp"

// VLC includes
#include "qt.hpp"
#include "util/vlctick.hpp"

//-------------------------------------------------------------------------------------------------
// Ctor / dtor
//-------------------------------------------------------------------------------------------------

MLPlaylistMedia::MLPlaylistMedia(const vlc_ml_media_t * data)
    : MLMedia(data)
{
    unsigned int width  = 0;
    unsigned int height = 0;

    unsigned int nbChannels = 0;

    for(const vlc_ml_media_track_t & track
        : ml_range_iterate<vlc_ml_media_track_t>(data->p_tracks))
    {
        if (track.i_type == VLC_ML_TRACK_TYPE_VIDEO)
        {
            width  = std::max(width,  track.v.i_width);
            height = std::max(height, track.v.i_height);

            m_video.push_back(
            {
                qfu(track.psz_codec),
                qfu(track.psz_language),
                track.v.i_fpsNum
            });
        }
        else if (track.i_type == VLC_ML_TRACK_TYPE_AUDIO)
        {
            nbChannels = std::max(nbChannels, track.a.i_nbChannels);

            m_audio.push_back(
            {
                qfu(track.psz_codec),
                qfu(track.psz_language),
                track.a.i_nbChannels, track.a.i_sampleRate
            });
        }
    }

    if (nbChannels >= 8)
        m_channel = "7.1";
    else if (nbChannels >= 6)
        m_channel = "5.1";
    else
        m_channel = "";

    if (width >= 7680 && height >= 4320)
        m_resolution = "8K";
    else if (width >= 3840 && height >= 2160)
        m_resolution = "4K";
    else if (width >= 1440 && height >= 1080)
        m_resolution = "HD";
    else if (width >= 720 && height >= 1280)
        m_resolution = "720p";
    else
        m_resolution = "";
}

//-------------------------------------------------------------------------------------------------
// Interface
//-------------------------------------------------------------------------------------------------

bool MLPlaylistMedia::isNew() const
{
    return (m_playCount == 1 && m_progress <= 0);
}

void MLPlaylistMedia::setSmallCover(const QString& thumbnail, vlc_ml_thumbnail_status_t status)
{
    m_smallThumbnail = { thumbnail, status };
}

//-------------------------------------------------------------------------------------------------

QString MLPlaylistMedia::getResolutionName() const
{
    return m_resolution;
}

//-------------------------------------------------------------------------------------------------

QString MLPlaylistMedia::getChannel() const
{
    return m_channel;
}

//-------------------------------------------------------------------------------------------------

QString MLPlaylistMedia::getMRLDisplay() const
{
    return m_mrl.toString(QUrl::PrettyDecoded | QUrl::RemoveUserInfo | QUrl::PreferLocalFile |
                          QUrl::NormalizePathSegments);
}

//-------------------------------------------------------------------------------------------------

QList<VideoDescription> MLPlaylistMedia::getVideo() const
{
    return m_video;
}

QList<AudioDescription> MLPlaylistMedia::getAudio() const
{
    return m_audio;
}
