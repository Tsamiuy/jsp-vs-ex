# Release retries after a failed installer build

Checked against `58594b6` on 2026-11-07, while investigating release preparation in
[issue #10](https://github.com/Tsamiuy/jsp-vs-ex/issues/10). This is a local fix;
no workflow was dispatched and no tag, release, issue or repository setting was changed.

## Reproduction and change

The tag job reserves `vX.Y.Z` before building the installer. Previously, any
existing tag set `build=false`. If the build failed after tagging, rerunning the
whole workflow succeeded without rebuilding or drafting the missing release.
The actual workflow shell step was exercised with a synthetic repository and a
fake `gh` command: `test_retry_after_build_failure` failed on the original step
(`false` instead of `true`). No network or real Git writes are involved in this test.

`tools/ci/release_tag.py` now resumes when the tag resolves to this exact commit
and there is no release. It follows annotated tags, refuses another commit and
never deletes, replaces or moves tags. Existing drafts and published releases
are left alone. Failed reads stop the job before a tag is created; prefix-matched
tags such as `v0.1.10` do not stand in for `v0.1.1`. All release pages are checked.
The workflow's write-capable token can see drafts; a read-only token without that
visibility is unsuitable for deciding whether to create a release.

The helper inspects by default. Only the workflow passes `--execute`; local
read-only use is:

```sh
python3 tools/ci/release_tag.py --repo OWNER/REPO --version X.Y.Z --sha FULL_COMMIT_SHA
```

For a failed release whose tag belongs to an earlier commit, rerun the original
workflow at that commit. Do not move its tag to newer `main`. An existing partial
draft needs maintainer inspection; this change deliberately does not overwrite
its files or claim to repair it.

## Validation

- `python3 tools/ci/test_release_tag.py`: 12 tests pass, including the actual
  workflow shell step, retry, new tag, draft/published releases, later release
  page, other commit, read failure, prefix collision and annotated-tag cycle.
- `python3 tools/test_release.py`: 31 existing cases pass.
- `python3 tools/release.py --check`: passes at version `0.0.1`.
- Ruff passes for the two new scripts.
- This is a Linux host test with a fake API, not a successful Windows installer
  build or a GitHub Actions execution. Issue #10's owner settings and first
  installer/release acceptance remain open.

API behavior is documented in the [GitHub CLI manual](https://cli.github.com/manual/gh_api),
[matching-reference endpoint](https://docs.github.com/en/rest/git/refs#list-matching-references)
and [release-list endpoint](https://docs.github.com/en/rest/releases/releases#list-releases).
