# Menu and rollback overlay ownership

The custom menu and the rollback messages, badge and ping counter all edit one retained overlay. A visible menu owns the surface between redraws. An empty version panel yields the surface when it has no version text or notice. The session boundary restores displaced network text after the menu closes, including when the match has ended.

The badge host test exercises a competing status draw, a status clear while the menu is visible, and restoration of a static disconnect message after release. It uses the same ownership header as both production draw paths. Native game presentation and timing still need target validation.

The Options builder resets the version and notice state and reconciles the retained surface under the menu mutex. The host callback fixture rebuilds a previously visible version screen, refreshes it with no visible top menu, and checks both network restoration and continued ownership by a custom page, help text or join prompt.
