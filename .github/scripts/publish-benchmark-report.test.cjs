const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const report = require('./publish-benchmark-report.cjs');

function fixture({previous, stale = false, branchExists = false} = {}) {
  const calls = [];
  const pr = {number: 977, state: 'open', head: {sha: 'abc'}, base: {repo: {full_name: 'owner/repo'}}};
  const reply = (name, data) => async args => { calls.push({name, args}); return {data}; };
  const github = {rest: {
    repos: {listPullRequestsAssociatedWithCommit: reply('pulls', [pr])},
    pulls: {get: reply('getPull', {...pr, head: {sha: stale ? 'new' : 'abc'}})},
    issues: {
      listComments: reply('comments', previous ? [previous] : []),
      createComment: reply('createComment', {}), updateComment: reply('updateComment', {}),
    },
    actions: {listWorkflowRunArtifacts: reply('artifacts', [{name: 'io-benchmark-linux-epoll'}])},
    git: {
      getRef: branchExists ? reply('ref', {object: {sha: 'old-report'}}) : async () => {
        throw Object.assign(new Error('Not found'), {status: 404});
      },
      getCommit: reply('commit', {tree: {sha: 'old-tree'}}),
      createBlob: reply('blob', {sha: 'image'}), createTree: reply('tree', {sha: 'new-tree'}),
      createCommit: reply('createCommit', {sha: 'new-report'}),
      createRef: reply('createRef', {}), updateRef: reply('updateRef', {}),
    },
  }};
  github.paginate = async (method, args) => (await method(args)).data;
  return {calls, github, context: {repo: {owner: 'owner', repo: 'repo'}, payload: {
    workflow_run: {id: 123, run_attempt: 1, head_sha: 'abc', html_url: 'https://github.com/owner/repo/actions/runs/123'},
  }}, core: {info() {}, setOutput: (key, value) => calls.push({name: key, args: value})}};
}

function images(t) {
  t.mock.method(fs, 'readFileSync', () => Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
}

test('first publication creates only an orphan reports branch and an image-only comment', async t => {
  images(t);
  const f = fixture();
  await report.publish(f);
  assert.deepEqual(f.calls.find(c => c.name === 'createCommit').args.parents, []);
  assert.equal(f.calls.find(c => c.name === 'createRef').args.ref, 'refs/heads/benchmark-reports');
  const body = f.calls.find(c => c.name === 'createComment').args.body;
  assert.equal((body.match(/raw.githubusercontent.com/g) || []).length, 2);
  assert.match(body, /\/new-report\/pr-977\/123-1\/overview.png/);
  assert.equal(f.calls.some(c => c.name === 'updateRef'), false);
});

test('updates only its marked comment and preserves previous image trees', async t => {
  images(t);
  const f = fixture({branchExists: true, previous: {id: 99, user: {login: 'github-actions[bot]'},
    body: '<!-- salts-benchmark-report run=122 attempt=1 head=old -->'}});
  await report.publish(f);
  assert.equal(f.calls.find(c => c.name === 'tree').args.base_tree, 'old-tree');
  assert.equal(f.calls.find(c => c.name === 'updateRef').args.force, false);
  assert.equal(f.calls.find(c => c.name === 'updateComment').args.comment_id, 99);
  assert.equal(f.calls.some(c => c.name === 'createComment'), false);
});

test('a newer or already reported attempt prevents both preparation and publishing', async () => {
  const f = fixture({previous: {user: {login: 'github-actions[bot]'},
    body: '<!-- salts-benchmark-report run=123 attempt=1 head=abc -->'}});
  await report.prepare(f);
  await report.publish(f);
  assert.equal(f.calls.some(c => ['publish', 'blob', 'createComment'].includes(c.name)), false);
});

test('a changed PR head prevents publication', async () => {
  const f = fixture({stale: true});
  await report.publish(f);
  assert.equal(f.calls.some(c => c.name === 'blob'), false);
});

test('concurrent PR publication rebases image paths on the new report tree', async t => {
  images(t);
  const f = fixture({branchExists: true});
  let advanced = false;
  f.github.rest.git.getRef = async () => ({data: {object: {sha: advanced ? 'concurrent-report' : 'old-report'}}});
  f.github.rest.git.updateRef = async args => {
    if (!advanced) {
      advanced = true;
      throw Object.assign(new Error('Non-fast-forward'), {status: 422});
    }
    assert.equal(args.force, false);
  };
  await report.publish(f);
  const commits = f.calls.filter(c => c.name === 'createCommit');
  assert.equal(commits.length, 2);
  assert.deepEqual(commits[1].args.parents, ['concurrent-report']);
  assert.equal(f.calls.filter(c => c.name === 'createComment').length, 1);
});

test('permission failures stop without retrying or creating a comment', async t => {
  images(t);
  const f = fixture({branchExists: true});
  f.github.rest.git.updateRef = async () => {
    throw Object.assign(new Error('Forbidden'), {status: 403});
  };
  await assert.rejects(report.publish(f), /Forbidden/);
  assert.equal(f.calls.filter(c => c.name === 'createCommit').length, 1);
  assert.equal(f.calls.some(c => c.name === 'createComment'), false);
});
