from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def test_typed_audio_api_and_sources_are_declared():
    header = read("components/x_feature_manager/src/features/music/esp32_music.h")
    visualizer = read("components/x_feature_manager/src/features/music/music_visualizer.h")

    assert "enum class AudioContentType" in header
    assert "AudioPlaybackRequest" in header
    assert "PlayAudio(const AudioPlaybackRequest& request)" in header
    assert "AUDIO_STORY" in visualizer


def test_unified_mcp_tool_is_registered_without_legacy_wrappers():
    tools = read("components/x_feature_manager/src/core/mcp_server_features.cc")

    assert '"self.audio.play_url"' in tools
    assert '"self.audio_story.play_url"' in tools
    assert '"self.music.play_url"' not in tools
    assert '"self.music.play_song"' not in tools
    assert '"self.radio.play_station"' not in tools
    assert '"self.radio.play_url"' not in tools
    assert '"self.radio.get_stations"' not in tools
    assert "mimi_music_play" in tools


def test_local_music_server_default_is_removed():
    header = read("components/x_feature_manager/src/features/music/esp32_music.h")
    source = read("components/x_feature_manager/src/features/music/esp32_music.cc")

    assert "192.168.1.77" not in header
    assert "192.168.1.77" not in source
    assert "DEFAULT_MUSIC_URL" not in header


def test_audio_stream_player_follows_http_redirects():
    header = read("components/x_feature_manager/src/features/music/audio_stream_player.h")
    source = read("components/x_feature_manager/src/features/music/audio_stream_player.cc")

    assert "AUDIO_HTTP_MAX_REDIRECTS" in header
    assert "IsRedirectStatus" in source
    assert 'GetResponseHeader("Location")' in source
    assert "ResolveRedirectUrl" in source


def test_audio_stream_player_supports_audio_hls_playlists():
    header = read("components/x_feature_manager/src/features/music/audio_stream_player.h")
    source = read("components/x_feature_manager/src/features/music/audio_stream_player.cc")

    assert "AUDIO_HLS_PLAYLIST_REFRESH_MS" in header
    assert "StreamHlsPlaylist" in header
    assert "ParseM3u8Uris" in source
    assert "IsM3u8Url" in source
    assert "StreamHttpSegment" in source


def test_legacy_esp32_radio_feature_is_removed():
    feature_manager = read("components/x_feature_manager/src/x_feature_manager.cc")
    tools = read("components/x_feature_manager/src/core/mcp_server_features.cc")
    cmake = read("components/x_feature_manager/CMakeLists.txt")

    assert "esp32_radio.cc" not in cmake
    assert "radio_feature.cc" not in cmake
    assert "RadioFeature" not in feature_manager
    assert "Esp32Radio" not in tools
    assert "AudioContentType::RADIO" in tools


def test_esp_ssl_disconnect_does_not_destroy_tls_under_live_receive_task():
    source = read("managed_components/78__esp-ml307/src/esp/esp_ssl.cc")

    assert "shutdown(sockfd, SHUT_RDWR)" in source
    assert "ESP_ERROR_CHECK(esp_tls_get_conn_sockfd" not in source
    assert "vTaskDelete(receive_task_handle_)" in source
    assert "receive_task_handle_ = nullptr" in source
