# Running your own fork

A fork can publish its own firmware, OTA updates and web companion without
touching the upstream project. Most of it is automatic; a few GitHub settings
are one-time switches.

## What follows the fork automatically

- **Firmware owner.** `tools/pio_set_version.py` sets `RSVP_REPOSITORY_OWNER`
  from `RSVP_REPOSITORY_OWNER`, then `GITHUB_REPOSITORY_OWNER` in Actions, then
  the `origin` remote of a local checkout. It falls back to `ionutdecebal`.
  That owner is the reader's default for OTA releases and catalogs. A value
  entered under Settings > Network & updates still wins.
- **Companion access.** The reader's HTTP API accepts browser requests from
  `https://<owner>.github.io` as well as the upstream site and localhost.
- **Companion defaults.** `DefaultFirmwareRepositoryOwner` in
  `companion/shared/.../updates/FirmwareUpdates.kt` names this fork, so update
  checks and font, theme and language catalogs come from it. Change it if the
  repository moves.

## One-time GitHub settings

1. **Actions:** open the repository's Actions tab and enable workflows. Forks
   start with them disabled.
2. **Pages:** Settings > Pages > Build and deployment > Source: **GitHub
   Actions**.
3. **Variable:** Settings > Secrets and variables > Actions > Variables > New
   repository variable: `DEPLOY_PAGES` = `true`. Without it the Pages workflow
   only builds and tests the site.

## Try a build before releasing

Run **Preview firmware** from the Actions tab on any branch with a channel such
as `preview-ui`. It creates a prerelease with every board's full image and OTA
file. Prereleases are never offered as OTA updates.

Flash the full image (for example
`rsvp-nano-esp32-s3-touch-lcd-3.49.bin`, or the `-rev2` file for rev2 boards)
from the web companion's manual `.bin` option in Chrome or Edge.

## Publish a release

1. Add `docs/releases/vX.Y.Z.md`. The release workflow refuses to run without it.
2. Merge to `main`, then tag the merge commit and push the tag:

   ```sh
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```

3. **Release firmware** builds all boards and publishes the release. When it
   succeeds, **Pages** redeploys `https://<owner>.github.io/rsvpnano/` with the
   new firmware in its installer.

Readers running this fork's firmware then find the release through their normal
OTA check. A reader moving over from upstream firmware needs one USB install of
the fork's image first. If that reader set a repository owner by hand, clear
it, or set it to the fork's owner.

## Building locally

`pio run -e waveshare_esp32s3_touch_lcd_349_rev1` in a clone of the fork picks up
the owner from `origin`. Set `RSVP_REPOSITORY_OWNER` to override it, for example
when the remote uses SSH aliases.
