async function submitCode() {
    const resultDiv = document.getElementById('result');
    const btn = document.getElementById('submitBtn');

    if (!currentProblemId) {
        resultDiv.innerHTML = '<p class="error">题目未加载</p>';
        return;
    }

    let code;
    if (window.cmInitialized && window.editorView && window.editorView.state) {
        code = window.editorView.state.doc.toString();
    } else {
        const ta = document.getElementById('codeEditor');
        if (ta) {
            code = ta.value;
        } else {
            resultDiv.innerHTML = '<p class="error">编辑器未初始化，请刷新页面</p>';
            return;
        }
    }

    if (!code.trim()) {
        resultDiv.innerHTML = '<p class="error">代码不能为空</p>';
        return;
    }

    if (code.length > 65536) {
        resultDiv.innerHTML = '<p class="error">代码过长（超过 64KB）</p>';
        return;
    }

    btn.disabled = true;
    btn.textContent = '提交中...';

    try {
        const data = await api('/api/submit', {
            method: 'POST',
            body: JSON.stringify({
                problem_id: currentProblemId,
                code: code,
            }),
        });

        resultDiv.innerHTML = '<p><span class="spinner"></span> 判题中... (' + data.status + ')</p>';
        pollResult(data.submission_id);
    } catch (e) {
        resultDiv.innerHTML = '<p class="error">' + escapeHtml(e.message) + '</p>';
        btn.disabled = false;
        btn.textContent = '提交';
    }
}

async function pollResult(submissionId) {
    const btn = document.getElementById('submitBtn');
    const resultDiv = document.getElementById('result');
    let attempts = 0;
    const maxAttempts = 120;

    const poll = async () => {
        try {
            const data = await api('/api/submissions/' + submissionId);
            if (data.status !== 'PENDING' && data.status !== 'JUDGING') {
                renderResult(data);
                btn.disabled = false;
                btn.textContent = '提交';
                loadSubmissionHistory(currentProblemId);
                return;
            }
            resultDiv.innerHTML = '<p><span class="spinner"></span> 判题中... (' + data.status + ')</p>';
        } catch (e) {
            resultDiv.innerHTML = '<p class="error">查询结果失败: ' + escapeHtml(e.message) + '</p>';
            btn.disabled = false;
            btn.textContent = '提交';
            return;
        }

        if (++attempts < maxAttempts) {
            setTimeout(poll, 500);
        } else {
            resultDiv.innerHTML = '<p class="error">判题超时</p>';
            btn.disabled = false;
            btn.textContent = '提交';
        }
    };

    poll();
}

function renderResult(data) {
    const resultDiv = document.getElementById('result');
    const badgeClass = 'badge-' + data.status.toLowerCase();

    let html = '<div class="card result-card">';
    html += '<h3>判题结果: <span class="badge ' + badgeClass + '">' + data.status + '</span></h3>';

    if (data.status === 'AC') {
        html += '<p class="success">通过！</p>';
    } else if (data.status === 'WA') {
        html += '<p class="error">答案错误</p>';
        if (data.failed_case > 0) {
            html += '<p>第一个未通过测试用例: #' + data.failed_case + '</p>';
        }
    } else if (data.status === 'CE') {
        html += '<p class="error">编译错误</p>';
        if (data.error_msg) {
            html += '<pre style="background:var(--bg-input);padding:8px;border-radius:var(--radius);overflow-x:auto;white-space:pre-wrap;font-size:0.85rem">' + escapeHtml(data.error_msg) + '</pre>';
        }
    } else if (data.status === 'RE') {
        html += '<p class="error">运行时错误</p>';
        if (data.error_msg) {
            html += '<pre style="background:var(--bg-input);padding:8px;border-radius:var(--radius);overflow-x:auto;white-space:pre-wrap;font-size:0.85rem">' + escapeHtml(data.error_msg) + '</pre>';
        }
    } else if (data.status === 'TLE') {
        html += '<p class="error">运行超时</p>';
    } else if (data.status === 'MLE') {
        html += '<p class="error">内存超限</p>';
    }

    if (data.time_used > 0) {
        html += '<p>耗时: ' + data.time_used + 'ms</p>';
    }
    if (data.memory_used > 0) {
        html += '<p>内存: ' + (data.memory_used / 1024).toFixed(1) + 'MB</p>';
    }

    html += '</div>';
    resultDiv.innerHTML = html;
}

async function loadSubmissionHistory(problemId) {
    const div = document.getElementById('submissionHistory');
    try {
        const subs = await api('/api/submissions?problem_id=' + problemId);
        if (subs.length === 0) {
            div.innerHTML = '<p style="color:var(--text-muted)">暂无提交记录</p>';
            return;
        }
        div.innerHTML = subs.map(s => {
            const badgeClass = 'badge-' + s.status.toLowerCase();
            let info = '';
            if (s.failed_case > 0) info = ' | 失败用例: #' + s.failed_case;
            if (s.time_used > 0) info += ' | ' + s.time_used + 'ms';
            if (s.memory_used > 0) info += ' | ' + (s.memory_used / 1024).toFixed(1) + 'MB';
            return `<div class="result-card" style="border-left:3px solid var(--accent)">
                <span class="badge ${badgeClass}">${s.status}</span>
                <span style="color:var(--text-muted);font-size:0.8rem;margin-left:8px">${s.created_at}${info}</span>
            </div>`;
        }).join('');
    } catch (e) {
        div.innerHTML = '<p class="error">加载历史失败</p>';
    }
}

function escapeHtml(str) {
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}
