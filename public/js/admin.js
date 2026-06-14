let currentEditId = 0;
let tcProblemId = 0;

async function loadProblems() {
    const tbody = document.getElementById('problemTableBody');
    if (!tbody) return;
    try {
        const problems = await api('/api/problems');
        if (problems.length === 0) {
            tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;color:var(--text-muted)">暂无题目</td></tr>';
            return;
        }
        tbody.innerHTML = problems.map(p => {
            const rate = p.total_submissions > 0
                ? (p.accepted / p.total_submissions * 100).toFixed(1) + '%'
                : '-';
            return `<tr>
                <td>${p.id}</td>
                <td>${escapeHtml(p.title)}</td>
                <td><span class="badge badge-${p.difficulty}">${p.difficulty}</span></td>
                <td>${rate}</td>
                <td>
                    <button class="small secondary" onclick="editProblem(${p.id})">编辑</button>
                    <button class="small secondary" onclick="manageTestcases(${p.id})">用例</button>
                    <button class="small danger" onclick="deleteProblem(${p.id})">删除</button>
                </td>
            </tr>`;
        }).join('');
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="5" class="error">' + escapeHtml(e.message) + '</td></tr>';
    }
}

async function createProblem() {
    const err = document.getElementById('error');
    const ok = document.getElementById('success');
    err.textContent = '';
    ok.textContent = '';
    try {
        const data = await api('/api/problems', {
            method: 'POST',
            body: JSON.stringify({
                title: document.getElementById('title').value,
                description: document.getElementById('description').value,
                input_desc: document.getElementById('input_desc').value,
                output_desc: document.getElementById('output_desc').value,
                difficulty: document.getElementById('difficulty').value,
                time_limit: parseInt(document.getElementById('time_limit').value),
                memory_limit: parseInt(document.getElementById('memory_limit').value),
            }),
        });
        ok.textContent = '题目创建成功! ID = ' + data.id;
        document.getElementById('problemForm').reset();
        loadProblems();
    } catch (e) {
        err.textContent = e.message;
    }
}

async function editProblem(id) {
    currentEditId = id;
    try {
        const p = await api('/api/problems/' + id);
        document.getElementById('edit_title').value = p.title;
        document.getElementById('edit_description').value = p.description;
        document.getElementById('edit_input_desc').value = p.input_desc;
        document.getElementById('edit_output_desc').value = p.output_desc;
        document.getElementById('edit_difficulty').value = p.difficulty;
        document.getElementById('edit_time_limit').value = p.time_limit;
        document.getElementById('edit_memory_limit').value = p.memory_limit;
        document.getElementById('editModal').classList.add('show');
    } catch (e) {
        document.getElementById('error').textContent = e.message;
    }
}

async function updateProblem() {
    const err = document.getElementById('editError');
    err.textContent = '';
    try {
        await api('/api/problems/' + currentEditId, {
            method: 'PUT',
            body: JSON.stringify({
                title: document.getElementById('edit_title').value,
                description: document.getElementById('edit_description').value,
                input_desc: document.getElementById('edit_input_desc').value,
                output_desc: document.getElementById('edit_output_desc').value,
                difficulty: document.getElementById('edit_difficulty').value,
                time_limit: parseInt(document.getElementById('edit_time_limit').value),
                memory_limit: parseInt(document.getElementById('edit_memory_limit').value),
            }),
        });
        closeEditModal();
        loadProblems();
    } catch (e) {
        err.textContent = e.message;
    }
}

async function deleteProblem(id) {
    if (!confirm('确定删除题目 #' + id + '？此操作也会删除关联的所有测试用例和提交记录。')) return;
    try {
        await api('/api/problems/' + id, { method: 'DELETE' });
        loadProblems();
    } catch (e) {
        document.getElementById('error').textContent = e.message;
    }
}

async function manageTestcases(problemId) {
    tcProblemId = problemId;
    document.getElementById('tcProblemTitle').textContent = '题目 #' + problemId;
    document.getElementById('tcModal').classList.add('show');
    loadTestcases();
}

async function loadTestcases() {
    const tbody = document.getElementById('tcTableBody');
    try {
        const cases = await api('/api/problems/' + tcProblemId + '/testcases');
        if (cases.length === 0) {
            tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;color:var(--text-muted)">暂无测试用例</td></tr>';
            return;
        }
        tbody.innerHTML = cases.map(tc => `
            <tr>
                <td>${tc.id}</td>
                <td><code style="font-size:0.85rem">${escapeHtml(tc.input.substring(0, 60))}${tc.input.length > 60 ? '...' : ''}</code></td>
                <td><code style="font-size:0.85rem">${escapeHtml(tc.expected.substring(0, 60))}${tc.expected.length > 60 ? '...' : ''}</code></td>
                <td>${tc.is_sample ? '示例' : '隐藏'}</td>
                <td><button class="small danger" onclick="deleteTestcase(${tc.id})">删除</button></td>
            </tr>
        `).join('');
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="5" class="error">' + escapeHtml(e.message) + '</td></tr>';
    }
}

async function addTestcase() {
    const err = document.getElementById('tcError');
    const ok = document.getElementById('tcSuccess');
    err.textContent = '';
    ok.textContent = '';
    try {
        await api('/api/problems/' + tcProblemId + '/testcases', {
            method: 'POST',
            body: JSON.stringify({
                input: document.getElementById('tcInput').value,
                expected: document.getElementById('tcExpected').value,
                is_sample: document.getElementById('tcIsSample').checked,
                sort_order: 0,
            }),
        });
        document.getElementById('tcInput').value = '';
        document.getElementById('tcExpected').value = '';
        ok.textContent = '测试用例添加成功';
        loadTestcases();
    } catch (e) {
        err.textContent = e.message;
    }
}

async function deleteTestcase(id) {
    if (!confirm('确定删除测试用例 #' + id + '？')) return;
    try {
        await api('/api/problems/' + tcProblemId + '/testcases/' + id, { method: 'DELETE' });
        loadTestcases();
    } catch (e) {
        document.getElementById('tcError').textContent = e.message;
    }
}

function closeEditModal() {
    document.getElementById('editModal').classList.remove('show');
}

function closeTcModal() {
    document.getElementById('tcModal').classList.remove('show');
}

function escapeHtml(str) {
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}
