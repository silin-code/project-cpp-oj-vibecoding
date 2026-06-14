async function checkAuth() {
    try {
        const data = await api('/api/me');
        return { ok: true, user: data };
    } catch (e) {
        return { ok: false, error: e.message };
    }
}

async function requireAuth(redirect = '/login.html') {
    const result = await checkAuth();
    if (!result.ok) {
        window.location.href = redirect;
        return null;
    }
    return result.user;
}
