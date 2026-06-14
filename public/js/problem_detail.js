async function loadProblem(id) {
    currentProblemId = id;
    try {
        const p = await api('/api/problems/' + id);

        document.getElementById('problemTitle').textContent = p.title;
        document.getElementById('difficultyBadge').innerHTML =
            `<span class="badge badge-${p.difficulty}">${p.difficulty}</span>`;
        document.getElementById('problemDesc').innerHTML = formatText(p.description);
        document.getElementById('inputDesc').innerHTML = formatText(p.input_desc);
        document.getElementById('outputDesc').innerHTML = formatText(p.output_desc);

        document.getElementById('limits').textContent =
            '时间限制: ' + p.time_limit + 's | 内存限制: ' + p.memory_limit + 'MB';

        const samplesDiv = document.getElementById('sampleCases');
        if (!p.sample_cases || p.sample_cases.length === 0) {
            samplesDiv.innerHTML = '<p style="color:var(--text-muted)">暂无示例</p>';
        } else {
            samplesDiv.innerHTML = p.sample_cases.map((tc, i) => `
                <div class="card">
                    <h3>示例 ${i + 1}</h3>
                    <div class="form-group"><label>输入:</label>
                        <pre style="background:var(--bg-input);padding:8px;border-radius:var(--radius);overflow-x:auto">${escapeHtml(tc.input)}</pre>
                    </div>
                    <div class="form-group"><label>输出:</label>
                        <pre style="background:var(--bg-input);padding:8px;border-radius:var(--radius);overflow-x:auto">${escapeHtml(tc.expected)}</pre>
                    </div>
                </div>
            `).join('');
        }

        document.getElementById('submitBtn').disabled = false;
        loadSubmissionHistory(id);
    } catch (e) {
        document.getElementById('result').innerHTML =
            '<p class="error">加载失败: ' + escapeHtml(e.message) + '</p>';
    }
}

function formatText(text) {
    if (!text) return '';
    return text.replace(/\n/g, '<br>');
}
