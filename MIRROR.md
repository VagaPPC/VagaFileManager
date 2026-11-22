# GitHub mirror

The canonical repository is:

    https://gitea.vagappc.net/vagappc/VagaFileManager

The suggested GitHub mirror is:

    https://github.com/VagaPPC/VagaFileManager

The local checkout currently has no GitHub credentials:

- the `gh` CLI is not installed;
- `~/.git-credentials` contains only the Gitea credential;
- the SSH key `~/.ssh/github_id_ed25519` is not authorized on GitHub.

So the repository can be prepared, but the first push to GitHub must be done
after adding the public key to GitHub (or using a Personal Access Token).

## Authentication

### SSH (recommended)

1. Add the public key to GitHub: Settings -> SSH and GPG keys -> New SSH key.

       cat ~/.ssh/github_id_ed25519.pub

2. Make sure `~/.ssh/config` is not group/world writable:

       chmod 600 ~/.ssh/config

3. Test:

       ssh -T git@github.com

   GitHub answers `Hi <user>! You've successfully authenticated...`.

### HTTPS

Create a GitHub Personal Access Token with repository write access.  Let
Git use it without storing it in the repository:

    git config --global credential.helper store
    git push https://github.com/VagaPPC/VagaFileManager.git main

Git stores the token in `~/.git-credentials`; do not commit that file.

## Option A - Gitea push mirror (recommended, automatic)

Create an empty GitHub repository named `VagaFileManager` (without README,
license or .gitignore).  Then, in Gitea:

1. open the repository -> Settings -> Repository -> Mirror Settings;
2. add a **Push Mirror**:
   - Remote address: `https://github.com/VagaPPC/VagaFileManager.git`
   - Authentication: GitHub user name and a PAT with write access;
   - enable "Sync when commits are pushed";
3. start the first synchronization.

After that every push to Gitea is mirrored to GitHub automatically.  The
GitHub repository is a read-only mirror; make changes in Gitea.

## Option B - push from this checkout

Once the GitHub repository exists and authentication works:

    cd /Archivio/Amiga/Programmi/Miei/VagaFileManager
    Tools/mirror-github.sh

The script:

- adds the `github` remote if it is missing
  (`https://github.com/VagaPPC/VagaFileManager.git`);
- pushes all local branches and tags.

For a dedicated empty mirror, a true mirror push is also available:

    Tools/mirror-github.sh --mirror

`git push --mirror` deletes remote refs that do not exist locally: use it only
on the dedicated mirror repository, never on a repository that contains
independent work.

## Option C - GitHub Actions pull mirror

`.github/workflows/mirror.yml` is an optional scheduled pull mirror.  It must
exist in the Gitea repository so that it is copied to GitHub by the mirror;
the job is skipped when GitHub Actions is not running on github.com.

If the Gitea repository is private, add a GitHub Actions secret
`GITEA_TOKEN` with read access to Gitea.  The workflow uses `GITHUB_TOKEN`
with `contents: write` to update the GitHub repository.

Choose **either** Option A (Gitea push mirror) **or** Option C (GitHub pull
mirror).  Running both is harmless but redundant.

## Verify

    git remote -v
    git ls-remote github
    # or
    git ls-remote https://github.com/VagaPPC/VagaFileManager.git

The first push must include the current work:

- `main` with the critical fixes and build 10;
- the `fix/` reference patches;
- `README.Aminet`, the VFM drawer rename and the mirror files.

## Notes

- Never commit tokens or `~/.git-credentials`.
- The GitHub repository should be a **mirror**, not a second development
  branch: changes go to Gitea and are copied to GitHub.
- If the GitHub account name is not `VagaPPC`, change the URL in the script,
  in this file and in the workflow.
