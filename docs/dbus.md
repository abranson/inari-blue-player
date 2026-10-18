# Player D-Bus interface, version 1

Session bus name and interface: `org.sailfishos.InariBlue1`.
Object: `/org/sailfishos/InariBlue1`.

* `GetState() -> s`: JSON containing present, enabled, blocked (privacy), ready,
  error, playing pattern ID, preview, patterns [{id,name}], assignments.
* `SetEnabled(b) -> b`: atomically save master setting; no service restart.
* `SetPattern(s category, s patternId) -> b`: save selection, apply immediately.
* `Preview(s patternId, u durationMs) -> b`: 100–10000 ms, caller-owned preview.
* `InspectPatternFile(s path) -> s`: validate an absolute local pattern file (or
  `file://` URL) and return JSON `{valid,name,error}` metadata.
* `PreviewFile(s path, u durationMs) -> b`: preview a validated local pattern
  file in memory; it does not add the file to the catalogue or save settings.
* `SetPatternFile(s category, s path) -> b`: import a validated local pattern
  into the user catalogue and save the assignment. A failed settings save
  removes the newly imported file.
* `StopPreview()`: end only the calling connection's preview and resume automatic
  feedback. Preview also ends on timeout or caller disconnect.
* `StartEvent(u cookie, s category) -> b`: register/replace caller's event.
* `StopEvent(u cookie)`: remove only caller's event; unknown cookies are harmless.
* `ReloadPatterns()`: reload catalogue, retaining valid bundled patterns when a
  user override is invalid.
* `StateChanged(s json)` and `FrameChanged(i channel0, i channel1, i channel2)`.

Categories: idle, ringtone, internet-call, message, chat, email, clock, calendar,
other. Idle cannot be supplied to StartEvent. Enabled event categories override
idle in this priority order: ringtone, internet-call, clock, calendar, message,
chat, email, other. Most recent wins ties. Selecting Off disables only that
category; there is no implicit fallback to another category. Preview overrides
feedback temporarily, including while the master setting is disabled; it never
changes that setting. Privacy blocks playback when its Inari option is selected. All requests disappear when
the originating D-Bus connection leaves. No notification content is accepted.

Pattern lifetime comes from its repeat count and the event's Start/Stop lifetime.
A finite pattern ends dark until its event ends. Events are limited to 128
concurrent entries. Clients must stop their events; the ngfd integration handles
cancellation, pause, finite feedback and repeating alarms.

`GetState` also reports `autonomous`, true while the hardware pattern controllers
are playing an eligible loop. Event registrations are retained while disabled
or assigned Off so live settings changes can affect a still-running alarm or
call; they remain bounded by ngfd lifetime and caller disconnect cleanup.

`SetPrivacySwitch(bool)` controls whether privacy mode blocks Inari Blue. The
`privacySwitch` state/config field defaults to true, preserving the original
behavior. Changing it takes effect immediately without altering enabled state or
assignments. An unavailable privacy service still fails closed. The optional
privacy Settings component shows this choice only while the cover is present.
