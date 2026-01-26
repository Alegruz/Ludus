# Branching and Release Flow

Status: draft
Owner: maintainers
Last updated: 2026-01-26


This repository uses a lightweight, release-friendly branching model.

## Branches

- **main**: always release-ready. Only accepts merges from `release/*` or `hotfix/*`.
- **develop**: integration branch for active work. Default PR target.
- **feature/*`**: short-lived branches for most changes.
- **hotfix/*`**: urgent fixes branched from `main`.
- **release/*`**: optional stabilization branch cut from `develop`.

## Typical flows

### Feature work
1. Branch from `develop` ??`feature/short-description`
2. Open PR ??`develop`
3. Squash-merge after review

### Release stabilization (optional)
1. Cut `release/1.2.0` from `develop`
2. Only accept fixes on the release branch
3. Merge `release/1.2.0` ??`main` and tag `v1.2.0`
4. Merge `release/1.2.0` ??`develop`

### Hotfix
1. Branch from `main` ??`hotfix/fix-name`
2. PR to `main`
3. Tag the release on `main`
4. Merge `hotfix/*` ??`develop`

## CI alignment

- PRs target `develop` and run the fast PR CI.
- Pushes to `main`/`develop` run the main CI.
- Nightly runs on a schedule (and can be triggered manually).
