# Adult town automation fixture

This disposable `Hero1` fixture loads an adult Hero in Bowerstone North. It is
the default save source for `load_fixture` and `appearance_cycle`; the launcher
copies it into a fresh run-specific Documents tree before starting Fable, so
the retained fixture and the player's ordinary saves are never loaded or
modified directly.

Expected baseline:

- region index: `32` (`Bowerstone North`)
- position: `(3260.375488, 4281.842285, 20.5)`
- facing: `0.496185`
- combat health: `130 / 130`
- progression health: `931 / 2000`

The optional server-character snapshot matches the adult Hero's full health and
stable town position. It is not applied by default because the appearance gate
must not mutate an already-valid fixture. Appearance automation must report
`AdultTownFixtureReady` before it creates a visual proxy.
