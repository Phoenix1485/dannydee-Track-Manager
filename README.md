# DannyDee Track Manager

Native Qt-6-Desktopanwendung für Windows und macOS zur Verwaltung einer persönlichen DJ- und
Audiobibliothek.

## Funktionen

- lokale SQLite-Bibliothek mit Titel, Artist, Genre, BPM, Key, Energy, Label und Release-Datum
- Suche sowie Genre- und BPM-Filter
- Import vorhandener FLAC/WAV/AIFF/MP3/M4A/OGG-Dateien
- Massenimport beliebig vieler Links aus einem einzigen Textfeld, inklusive Duplikatfilter
- automatische Erkennung öffentlicher Spotify-, SoundCloud- und anderer unterstützter Playlists;
  Titel, Reihenfolge und einzelne Original-Tracklinks werden als lokale Playlist übernommen
- Link-Erkennung für Spotify, TikTok, YouTube, SoundCloud, Bandcamp, SoundBeaver und viele weitere Seiten
- API-schlüsselfreie Verarbeitung öffentlich zugänglicher Medien-Links über `yt-dlp`
- Spotify-Metadaten und Playlist-Auflösung über `spotDL`; für Audio sucht `spotDL` passende Quellen
  über YouTube Music, YouTube, SoundCloud oder Bandcamp
- SoundCloud-, TikTok-, Bandcamp- und andere Links werden direkt über die eingegebene Adresse verarbeitet
- Warteschlange zum Export aller gespeicherten Internet-Links als FLAC; Fehler stoppen die restliche Liste nicht
- MP3- oder FLAC-Ausgabe mit eingebetteten Metadaten und Cover, soweit die Quelle sie bereitstellt
- direkter Download von Audio-URLs ohne wiederholte Bestätigungsdialoge
- FLAC- und 24-Bit-WAV-Export lokaler Dateien über FFmpeg
- Playlist-/Set-Grundfunktionen und Wiedergabe lokaler Tracks
- transaktionales Kopieren oder Verschieben mehrerer Tracks zwischen lokalen Playlists
- dauerhaft sichtbare Playlist-Navigation mit aktiver Hervorhebung, Trackanzahl und schnellem Wechsel
- automatische Analyse lokaler Tracks auf BPM, musikalischen Key, Energy und vorhandenes Label-Tag
- Analyse der Auswahl oder aller lokalen Tracks direkt aus dem kompakten Analysebereich
- automatische Übernahme vorhandener Audio-Tags über FFprobe
- Rechtsklick-Aktionen zum Umbenennen, Verschieben und Entfernen von Tracks
- sicheres Entfernen eines, mehrerer oder aller sichtbaren Bibliothekseinträge ohne Löschen der Audiodateien
- modernes Desktop-Dashboard mit kompakten Statistiken, nativen Menüs und Tastenkürzeln
- Drag-and-drop-Import aus Windows Explorer und macOS Finder
- identische Bibliothek und Download-Warteschlange auf Windows und macOS
- automatische Update-Prüfung beim Start sowie ein manueller Eintrag unter `Hilfe`
- SHA-256- und Größenprüfung jedes heruntergeladenen Installers vor dem Start

## Kein API-Schlüssel erforderlich

Die Anwendung fragt keine API-Schlüssel, OAuth-Tokens oder Zugangsdaten ab. `yt-dlp` verarbeitet
öffentlich erreichbare Links lokal. Für Spotify übernimmt `spotDL` Metadaten und Playlist-Struktur
und sucht den passenden Audiotreffer über YouTube Music, YouTube, SoundCloud oder Bandcamp.

Mehrere einzelne Links können gemeinsam importiert und anschließend nacheinander als FLAC exportiert
werden. Öffentliche Spotify-Playlists und -Alben werden als lokale Playlists mit einzelnen Spotify-
Trackreferenzen importiert. Da ein öffentlicher Spotify-Link keine direkte Audiodatei anbietet, wird
beim Download eine passende externe Audioquelle gesucht und anschließend mit Spotify-Metadaten versehen.
Eine FLAC-Ausgabe ist dabei eine verlustfreie Neukodierung der gefundenen Quelle und keine Steigerung
deren ursprünglicher Audioqualität.
Geschützte, private, nicht unterstützte oder DRM-gesicherte Inhalte schlagen fehl und werden in der
Zusammenfassung aufgeführt; die Warteschlange läuft mit den übrigen Links weiter.

## Voraussetzungen

- Qt 6.5 oder neuer mit `Widgets`, `Sql`, `Network` und `Multimedia`
- CMake 3.21 oder neuer
- C++20-Compiler
- FFmpeg und FFprobe im `PATH` oder unter `tools/` neben der Anwendung
- yt-dlp im `PATH` oder unter `tools/` neben der Anwendung
- spotDL im `PATH` oder unter `tools/` für Spotify-Links
- Deno im `PATH` oder unter `tools/` für die zuverlässigere Verarbeitung aktueller YouTube-Links

Der Windows-Installer kann FFmpeg, FFprobe, yt-dlp, spotDL und Deno automatisch aus dem lokalen `PATH`
einbetten. Dafür sind weiterhin keine API-Zugangsdaten erforderlich.

## Automatische Updates über GitHub Pages

Die Workflow-Datei `.github/workflows/publish-updates.yml` baut bei jedem Push auf `main` einen
Windows-x64-Installer sowie getrennte macOS-DMGs für Apple Silicon und Intel. Anschließend
veröffentlicht sie die drei Pakete, eine kleine Downloadseite und `updates.json` auf GitHub Pages.
Die Update-Adresse wird beim CI-Build automatisch
aus Eigentümer und Repository-Name gebildet und direkt in die App kompiliert. Es ist kein eigener
API-Schlüssel nötig; GitHub stellt dem Workflow sein kurzlebiges `GITHUB_TOKEN` selbst bereit.

Einmalige Einrichtung nach dem Erstellen eines leeren GitHub-Repositories:

```powershell
git init -b main
git add .
git commit -m "Initial DannyDee Track Manager release"
git remote add origin https://github.com/DEIN-NAME/DEIN-REPOSITORY.git
git push -u origin main
```

Danach im GitHub-Repository `Settings` > `Pages` öffnen und bei `Build and deployment` als Quelle
`GitHub Actions` auswählen. Der Workflow kann anschließend unter `Actions` beobachtet oder manuell
gestartet werden. Nach der ersten erfolgreichen Ausführung liegt die Seite normalerweise unter
`https://DEIN-NAME.github.io/DEIN-REPOSITORY/`.

Vor einer neuen veröffentlichten Version muss die Versionsnummer ganz oben in `CMakeLists.txt`
erhöht werden, zum Beispiel von `0.11.1` auf `0.11.2`. Ein Push ohne höhere Versionsnummer ersetzt
zwar den Pages-Build, löst in bereits installierten Apps aber absichtlich keine Update-Meldung aus.
Der Workflow behält nur die jeweils aktuellen Pakete auf Pages, damit alte große Installer und DMGs
nicht unnötig Speicherplatz verbrauchen.

Die Version `0.5.1` und ältere Builds kennen noch keine Update-Adresse. Der erste über GitHub
Pages installierte Build ab `0.6.0` kann alle späteren Updates selbst erkennen.

## Bauen

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Windows-Installer bauen

Zusätzlich werden Inno Setup 6 und ein Qt-6-MinGW-Kit benötigt:

```powershell
.\scripts\build-installer.ps1 -QtRoot "C:\Qt\6.8.3\mingw_64" -Clean
```

Bestimmte Binärdateien können ausdrücklich angegeben werden:

```powershell
.\scripts\build-installer.ps1 -QtRoot "C:\Qt\6.8.3\mingw_64" `
  -FfmpegPath "C:\Tools\ffmpeg\bin\ffmpeg.exe" `
  -YtDlpPath "C:\Tools\yt-dlp.exe" `
  -SpotDlPath "C:\Tools\spotdl.exe" `
  -DenoPath "C:\Tools\deno.exe"
```

Für einen lokalen Test-Build mit Update-Prüfung kann zusätzlich eine HTTPS-Adresse gesetzt werden:

```powershell
.\scripts\build-installer.ps1 -QtRoot "C:\Qt\6.8.3\mingw_64" `
  -UpdateManifestUrl "https://DEIN-NAME.github.io/DEIN-REPOSITORY/updates.json"
```

Das Skript baut die Anwendung, sammelt die Qt-Laufzeitdateien, bettet gefundene Werkzeuge ein und
erzeugt unter `dist` eine Setup-Datei. Der Installer benötigt keine Administratorrechte und installiert
standardmäßig unter `%LOCALAPPDATA%\Programs`.

## macOS-App und DMG bauen

Benötigt werden macOS 12 oder neuer, Qt 6, CMake, Ninja und die Xcode Command Line Tools. Die fünf
Hilfsprogramme werden dem Skript explizit angegeben, damit ein unvollständiges DMG nicht unbemerkt
veröffentlicht wird:

```bash
chmod +x scripts/build-macos.sh
QT_ROOT="$HOME/Qt/6.8.3/macos" \
FFMPEG_PATH="$(command -v ffmpeg)" \
FFPROBE_PATH="$(command -v ffprobe)" \
YTDLP_PATH="$(command -v yt-dlp)" \
SPOTDL_PATH="$(command -v spotdl)" \
DENO_PATH="$(command -v deno)" \
MACOS_PLATFORM_KEY="macos-arm64" \
./scripts/build-macos.sh
```

Die Update-Adresse kann bei einem macOS-Paket mit
`UPDATE_MANIFEST_URL="https://…/updates.json"` eingebettet werden. Das Manifestformat unterstützt
`macos-arm64` und `macos-x64`; der GitHub-Workflow baut und veröffentlicht beide Varianten automatisch.

`FFMPEG_PATH`, `FFPROBE_PATH`, `YTDLP_PATH`, `SPOTDL_PATH` und `DENO_PATH` müssen auf native
macOS-Programme der gewählten Architektur zeigen. Das Skript legt die Werkzeuge in
`Contents/Resources/tools`, kopiert benötigte Homebrew-Bibliotheken in das App-Bundle, führt
`macdeployqt` aus, signiert die App standardmäßig ad hoc und erzeugt ein DMG unter `dist`.

Zur Installation wird das passende DMG geöffnet und die App in `Applications` gezogen. Die
automatischen Builds sind ad hoc signiert, da im Repository keine Apple-Developer-Zugangsdaten
liegen. macOS kann deshalb beim ersten Start `Rechtsklick` > `Öffnen` verlangen. Eine ohne diesen
Hinweis startende, notarisierte Veröffentlichung benötigt später ein Apple-Developer-ID-Zertifikat
und Notarisierungs-Zugangsdaten als GitHub-Secrets.

## Rechtlicher und technischer Rahmen

Die Anwendung umgeht weder DRM noch Anmelde-, Bezahl- oder Zugriffsschutz. Ein vorhandenes Abonnement
oder die persönliche Nutzung allein verleiht nicht automatisch ein Downloadrecht. Verwende die
Downloadfunktion ausschließlich für eigene, gemeinfreie, frei lizenzierte, gekaufte oder ausdrücklich
zum Download freigegebene Inhalte und beachte die Bedingungen des jeweiligen Dienstes.

Eine FLAC-Ausgabe verbessert die Qualität einer verlustbehafteten Quelle nicht; sie verhindert nur eine
weitere verlustbehaftete Kodierung bei der Ausgabe.
