async function api(path, options = {}) {
    const { headers, ...restOptions } = options;
    const resp = await fetch(path, {
        credentials: 'same-origin',
        ...restOptions,
        headers: { 'Content-Type': 'application/json', ...headers },
    });
    const text = await resp.text();
    let data;
    try { data = JSON.parse(text); } catch (e) { data = { error: text || 'Unknown error' }; }
    if (!resp.ok) throw new Error(data.error || `HTTP ${resp.status}`);
    return data;
}
