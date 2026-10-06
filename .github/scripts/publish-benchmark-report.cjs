const fs = require('node:fs');

const branch = 'benchmark-reports';
const marker = '<!-- salts-benchmark-report ';
const maxImageBytes = 10 * 1024 * 1024;
const maxPublishAttempts = 5;

async function reportHead(github, context) {
  try {
    return (await github.rest.git.getRef({...context.repo, ref: `heads/${branch}`})).data.object.sha;
  } catch (error) {
    if (error.status !== 404) throw error;
    return undefined;
  }
}

async function storeImages(github, context, tree, message) {
  for (let attempt = 0; attempt < maxPublishAttempts; ++attempt) {
    const parent = await reportHead(github, context);
    const baseTree = parent
      ? (await github.rest.git.getCommit({...context.repo, commit_sha: parent})).data.tree.sha
      : undefined;
    const newTree = await github.rest.git.createTree({...context.repo, tree,
      ...(baseTree ? {base_tree: baseTree} : {})});
    const commit = await github.rest.git.createCommit({...context.repo,
      message, tree: newTree.data.sha, parents: parent ? [parent] : []});
    try {
      if (parent) {
        await github.rest.git.updateRef({...context.repo, ref: `heads/${branch}`,
          sha: commit.data.sha, force: false});
      } else {
        await github.rest.git.createRef({...context.repo, ref: `refs/heads/${branch}`, sha: commit.data.sha});
      }
      return commit.data.sha;
    } catch (error) {
      // Another PR may have advanced this branch. Rebase only our image paths;
      // permission/validation failures never trigger a retry or force push.
      if (![409, 422].includes(error.status) ||
          await reportHead(github, context) === parent || attempt + 1 === maxPublishAttempts) throw error;
    }
  }
}

async function currentPull(github, context, run) {
  const {owner, repo} = context.repo;
  const candidates = await github.paginate(github.rest.repos.listPullRequestsAssociatedWithCommit,
    {owner, repo, commit_sha: run.head_sha, per_page: 100});
  const matches = candidates.filter(p => p.state === 'open' &&
    p.head.sha === run.head_sha && p.base.repo.full_name === `${owner}/${repo}`);
  if (matches.length !== 1) return null;
  return (await github.rest.pulls.get({owner, repo, pull_number: matches[0].number})).data;
}

async function previousComment(github, context, number) {
  const comments = await github.paginate(github.rest.issues.listComments,
    {...context.repo, issue_number: number, per_page: 100});
  return comments.find(c => c.user.login === 'github-actions[bot]' && c.body.startsWith(marker));
}

function isNewer(comment, run) {
  const match = comment?.body.match(/^<!-- salts-benchmark-report run=(\d+) attempt=(\d+)/);
  return match && (BigInt(match[1]) > BigInt(run.id) ||
    (BigInt(match[1]) === BigInt(run.id) && Number(match[2]) >= run.run_attempt));
}

exports.prepare = async ({github, context, core}) => {
  const run = context.payload.workflow_run;
  const pr = await currentPull(github, context, run);
  if (!pr || pr.head.sha !== run.head_sha) {
    core.info('No open PR at this exact head; no report will be published.');
    return;
  }
  if (isNewer(await previousComment(github, context, pr.number), run)) {
    core.info('This run attempt has already been reported or superseded.');
    return;
  }
  const artifacts = await github.paginate(github.rest.actions.listWorkflowRunArtifacts,
    {...context.repo, run_id: run.id, per_page: 100});
  if (!artifacts.some(a => a.name.startsWith('io-benchmark-') && !a.expired)) {
    core.info('No benchmark evidence in this run.');
    return;
  }
  core.setOutput('publish', 'true');
};

exports.publish = async ({github, context, core}) => {
  const run = context.payload.workflow_run;
  const pr = await currentPull(github, context, run);
  if (!pr || pr.head.sha !== run.head_sha) {
    core.info('PR changed while rendering; discarding obsolete report.');
    return;
  }
  const previous = await previousComment(github, context, pr.number);
  if (isNewer(previous, run)) return;
  const prefix = `pr-${pr.number}/${run.id}-${run.run_attempt}`;
  const images = ['overview.png', 'performance.png'];
  const tree = [];
  for (const name of images) {
    const content = fs.readFileSync(`benchmark-report/${name}`);
    if (content.length > maxImageBytes ||
        !content.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
      throw new Error(`Invalid generated PNG: ${name}`);
    }
    const blob = await github.rest.git.createBlob({...context.repo,
      content: content.toString('base64'), encoding: 'base64'});
    tree.push({path: `${prefix}/${name}`, mode: '100644', type: 'blob', sha: blob.data.sha});
  }
  const commit = await storeImages(github, context, tree,
    `Benchmark report: PR #${pr.number}, run ${run.id}/${run.run_attempt}`);
  const latest = (await github.rest.pulls.get({...context.repo, pull_number: pr.number})).data;
  if (latest.state !== 'open' || latest.head.sha !== run.head_sha) {
    core.info('PR changed during image publication; leaving its comment untouched.');
    return;
  }
  // Immutable commit URLs keep previous reports stable after later branch updates.
  const root = `https://raw.githubusercontent.com/${context.repo.owner}/${context.repo.repo}/${commit}/${prefix}`;
  const body = `${marker}run=${run.id} attempt=${run.run_attempt} head=${run.head_sha} -->\n\n` +
    images.map(name => `[![Benchmark ${name === 'overview.png' ? 'results' : 'performance'}](${root}/${name})](${run.html_url})`).join('\n\n');
  if (previous) {
    await github.rest.issues.updateComment({...context.repo, comment_id: previous.id, body});
  } else {
    await github.rest.issues.createComment({...context.repo, issue_number: pr.number, body});
  }
};
