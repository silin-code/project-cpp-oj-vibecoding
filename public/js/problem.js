async function loadProblemList() {
    const tbody = document.getElementById('problemList');
    if (!tbody) return;
    try {
        const problems = await api('/api/problems');
        if (problems.length === 0) {
            tbody.innerHTML = '<tr><td colspan="4" style="text-align:center;color:var(--text-muted)">暂无题目</td></tr>';
            return;
        }
        tbody.innerHTML = problems.map(p => {
            const rate = p.total_submissions > 0
                ? (p.accepted / p.total_submissions * 100).toFixed(1) + '%'
                : '-';
            return `<tr>
                <td><a href="/problem.html?id=${p.id}">${escapeHtml(p.title)}</a></td>
                <td><span class="badge badge-${p.difficulty}">${p.difficulty}</span></td>
                <td>${rate}</td>
                <td>${p.total_submissions}</td>
            </tr>`;
        }).join('');
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="4" style="color:var(--danger)">加载失败: ' + escapeHtml(e.message) + '</td></tr>';
    }
}

function escapeHtml(str) {
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}
